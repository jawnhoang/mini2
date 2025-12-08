#include "jobs.hpp"
#include <iostream>
#include <cstdio>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <atomic>
#include <thread>



// simple FNV-1a 32-bit checksum so we avoid extra libs
static uint32_t fnv1a32(const std::string& s) {
    const uint32_t FNV_OFFSET = 2166136261u;
    const uint32_t FNV_PRIME  = 16777619u;
    uint32_t hash = FNV_OFFSET;
    for (unsigned char c : s) {
        hash ^= c;
        hash *= FNV_PRIME;
    }
    return hash;
}


using namespace std;

int jobLoop::streamPopRowsReceived = 0;
std::mutex jobLoop::streamPopRowsReceivedMutex;

grpc::Status jobLoop::sendMsg(::grpc::ServerContext* context, const ::loop::Msg* msg, ::loop::MsgResponse* response)
{
    auto start = chrono::high_resolution_clock::now();
    // LEADER: gRPC entrypoint. This thread only:
    //  1) Enqueues the incoming message as a Job into the internal queue.
    //  2) Waits for a worker thread to process it.
    //  3) Returns the result set by the worker (no forwarding logic here).
    std::call_once(workerInitFlag, [this]() {
        for (int i = 0; i < 2; i++) {
            workers.emplace_back([this, i]() { this->workerLoop(i); });
        }
    });

    auto job = std::make_shared<Job>();
    job->src = msg->src();
    job->dest = msg->dest();
    job->payload = msg->payload();
    job->originalMsg = msg;
    job->state = JobState::PENDING;

    {
        std::lock_guard<std::mutex> lock(queueMutex);
        jobQueue.push(job);
    }
    queueCv.notify_one();

    std::unique_lock<std::mutex> lk(job->mtx);
    job->cv.wait(lk, [job](){ return job->done; });

    // Leader: jobLoop::sendMsg only waits for a worker to finish and returns
    // whatever result the worker computed. All routing/forwarding decisions
    // are made inside workerLoop() on the server side.
    response->set_rspid(job->resultRspid);
    
    auto stop = chrono::high_resolution_clock::now();
    string timerMsg = "Execution time:";
    auto executionDuration = chrono::duration_cast<chrono::milliseconds>(stop-start).count();
    cout<<"------------------" << endl;
    if(executionDuration < 2){
        auto executionDurationNS = chrono::duration_cast<chrono::nanoseconds>(stop-start).count();
        cout<< timerMsg<< ": " << executionDurationNS << "ns" << endl;
    }else{
        cout<< timerMsg<< ": " << executionDuration << "ms" << endl;
    }
    cout<<"------------------" << endl;
    return Status::OK;
}

//communicate with Peers
void jobLoop::peerStubs(){
    for (const auto& [pid, paddr] : nodeInfo.peer_addr) {
        if (pid == nodeInfo.id) {
            continue;   // don't create a stub to self
        }
        if (jobStub_.count(pid)) {
            // stub already created earlier, skip so we don't double-print
            continue;
        }

        shared_ptr<grpc::Channel> channel = grpc::CreateChannel(paddr, grpc::InsecureChannelCredentials());
        peerChannels_[pid] = channel;
        jobStub_[pid] = executeJob::NewStub(channel);
        
        // Check connection state like Hook does
        grpc_connectivity_state state = channel->GetState(true);
        if (state == GRPC_CHANNEL_READY) {
            peerHealth_[pid] = true;
            cout << "[Node] Stub for Peer " << pid << " created at " << paddr << " (channel ready)" << endl;
        } else {
            peerHealth_[pid] = false;
            cout << "[Node] Stub for Peer " << pid << " created at " << paddr << " (channel state: " << state << ")" << endl;
        }
    }
}

bool jobLoop::isPeerReady(const std::string& peerId) {
    auto channelIt = peerChannels_.find(peerId);
    if (channelIt == peerChannels_.end()) {
        cout << "[Health] Peer " << peerId << " has no channel" << endl;
        return false;
    }
    
    grpc_connectivity_state state = channelIt->second->GetState(true);
    bool ready = (state == GRPC_CHANNEL_READY);
    
    // Check if channel is completely dead (SHUTDOWN or TRANSIENT_FAILURE that persists)
    if (state == GRPC_CHANNEL_SHUTDOWN) {
        cout << "[Health] Peer " << peerId << " channel is SHUTDOWN (dead)" << endl;
        updatePeerHealth(peerId, false);
        return false;
    }
    
    // Don't log health status constantly - only log when it changes
    // This reduces the "healthy/unhealthy" spam in logs
    static std::map<std::string, bool> lastLoggedHealth;
    bool shouldLog = !lastLoggedHealth.count(peerId) || lastLoggedHealth[peerId] != ready;
    if (!ready && shouldLog) {
        cout << "[Health] Peer " << peerId << " channel state: " << state << " (not ready)" << endl;
        lastLoggedHealth[peerId] = ready;
    } else if (ready && shouldLog && lastLoggedHealth.count(peerId) && !lastLoggedHealth[peerId]) {
        cout << "[Health] Peer " << peerId << " recovered (channel ready)" << endl;
        lastLoggedHealth[peerId] = ready;
    }
    
    updatePeerHealth(peerId, ready);
    return ready;
}

void jobLoop::updatePeerHealth(const std::string& peerId, bool healthy) {
    // Only log if health status changed (to reduce spam)
    if (peerHealth_.count(peerId) && peerHealth_[peerId] != healthy) {
        // Only log significant changes (unhealthy -> healthy, or healthy -> unhealthy)
        if (!healthy) {
            cout << "[Health] Peer " << peerId << " marked UNHEALTHY" << endl;
        } else {
            cout << "[Health] Peer " << peerId << " recovered (HEALTHY)" << endl;
        }
    }
    // Don't log initial health status to reduce noise
    peerHealth_[peerId] = healthy;
}

void jobLoop::runHandshake(){
    for (const auto& [pid, stub] : jobStub_) {
        loop::Msg msg;
        msg.set_src(nodeInfo.id);
        msg.set_dest(pid);
        msg.set_payload("__HELLO__" + nodeInfo.addr);

        grpc::ClientContext ctx;
        loop::MsgResponse resp;

        cout << "[Node " << nodeInfo.id << "] Sending HELLO to peer "
             << pid << " with addr " << nodeInfo.addr << endl;

        grpc::Status status = stub->sendMsg(&ctx, msg, &resp);
        if (status.ok()) {
            cout << "[Node " << nodeInfo.id << "] HELLO ack from peer "
                 << pid << ": " << resp.rspid() << endl;
        } else {
            cerr << "[Node " << nodeInfo.id << "] HELLO to peer " << pid
                 << " failed: " << status.error_message() << endl;
        }
    }
}

//in a way, server becomes a client if its forwarding msgs
grpc::Status jobLoop::forwardToPeer(const ::loop::Msg* msg, ::loop::MsgResponse* response){
    const std::string& dest    = msg->dest();
    const std::string& origSrc = msg->src();
    const std::string& prevSrc = msg->prev();
    
    // CRITICAL FIX: Check if we're the destination - if so, we should have already processed this
    // This prevents duplicate processing when message reaches destination through multiple paths
    if (dest == nodeInfo.id) {
        // We're the destination, but forwardToPeer was called - this shouldn't happen
        // Return success immediately to prevent loops
        response->set_rspid("Msg delivered to " + nodeInfo.id);
        return Status::OK;
    }
    
    // Check if destination is already confirmed as dead (via peerHealth_)
    if (peerHealth_.count(dest) && !peerHealth_[dest]) {
        // Double-check channel state to see if it's really dead
        auto destChannelIt = peerChannels_.find(dest);
        if (destChannelIt != peerChannels_.end()) {
            grpc_connectivity_state destState = destChannelIt->second->GetState(true);
            if (destState == GRPC_CHANNEL_SHUTDOWN) {
                cerr << "[Node " << nodeInfo.id << "] Destination " << dest 
                     << " is confirmed dead (SHUTDOWN), giving up" << endl;
                response->set_rspid("Destination " + dest + " is unreachable (dead)");
                return Status::OK;
            }
        }
    }
    
    // Check if destination channel is completely dead before trying
    auto destChannelIt = peerChannels_.find(dest);
    if (destChannelIt != peerChannels_.end()) {
        grpc_connectivity_state destState = destChannelIt->second->GetState(true);
        if (destState == GRPC_CHANNEL_SHUTDOWN) {
            cout << "[Health] Destination " << dest << " channel is SHUTDOWN, marking as dead" << endl;
            peerHealth_[dest] = false; // Mark as dead
            response->set_rspid("Destination " + dest + " is unreachable (channel shutdown)");
            return Status::OK;
        }
    }

    // Helper lambda for retry logic with exponential backoff
    // CRITICAL: Only retry ONCE to prevent duplicate messages
    auto tryForward = [this, msg, response](const std::string& peerId, const std::unique_ptr<executeJob::Stub>& stub) -> grpc::Status {
        const int maxRetries = 1; // Only 1 retry to prevent duplicates
        grpc::Status lastStatus;
        
        for (int attempt = 0; attempt < maxRetries; attempt++) {
            // Check connection state before EVERY attempt (Hook pattern)
            if (attempt > 0) {
                cout << "[Retry] Attempt " << (attempt + 1) << "/" << maxRetries << " for peer " << peerId << endl;
            }
            
            // Check health before attempting, but don't spam logs
            bool ready = isPeerReady(peerId);
            if (!ready && attempt > 0) {
                // Wait a bit for channel to recover on retries
                int backoffMs = 100 * (1 << attempt); // 100ms, 200ms, 400ms
                // Only log if peer was previously healthy (to reduce noise)
                if (peerHealth_.count(peerId) && peerHealth_[peerId]) {
                    cout << "[Retry] Peer " << peerId << " not ready, waiting " << backoffMs << "ms before retry" << endl;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(backoffMs));
            }
            // Don't log health status on first attempt to reduce noise
            
            grpc::ClientContext context;
            auto deadline = std::chrono::system_clock::now() + std::chrono::milliseconds(800);
            context.set_deadline(deadline);
            loop::MsgResponse peerResp;
            lastStatus = stub->sendMsg(&context, *msg, &peerResp);
            
            if (lastStatus.ok()) {
                response->set_rspid(peerResp.rspid());
                updatePeerHealth(peerId, true);
                if (attempt > 0) {
                    cout << "[Node " << nodeInfo.id << "] Forward to " << peerId 
                         << " succeeded on retry " << attempt << endl;
                }
                // Successfully forwarded - stop immediately to prevent infinite loops
                // Response will propagate back through the chain
                return Status::OK;
            } else {
                cout << "[Retry] Forward to " << peerId << " failed (attempt " << (attempt + 1) 
                     << "/" << maxRetries << "): " << lastStatus.error_message() << endl;
                updatePeerHealth(peerId, false);
                
                // Check if this is the destination and it's completely dead
                if (peerId == msg->dest()) {
                    auto channelIt = peerChannels_.find(peerId);
                    if (channelIt != peerChannels_.end()) {
                        grpc_connectivity_state state = channelIt->second->GetState(true);
                        if (state == GRPC_CHANNEL_SHUTDOWN) {
                            cout << "[Health] Destination " << peerId << " confirmed dead (SHUTDOWN)" << endl;
                            peerHealth_[peerId] = false; // Mark as dead
                        }
                    }
                }
                
                if (attempt < maxRetries - 1) {
                    int backoffMs = 100 * (1 << attempt);
                    cout << "[Retry] Backing off " << backoffMs << "ms before next attempt" << endl;
                    std::this_thread::sleep_for(std::chrono::milliseconds(backoffMs));
                }
            }
        }
        return lastStatus;
    };

    // 1. Prefer a direct edge to the destination if it exists
    auto directIt = jobStub_.find(dest);
    if (directIt != jobStub_.end() && dest != prevSrc && std::find(msg->visited().begin(), msg->visited().end(), dest) == msg->visited().end()) {
        // Always check health before attempting direct forward
        bool directReady = isPeerReady(dest);
        if (!directReady && peerHealth_.count(dest) && !peerHealth_[dest]) {
            cout << "[Node " << nodeInfo.id << "] Direct peer " << dest << " marked unhealthy, but will still try" << endl;
        }
        
        cout << "[Node " << nodeInfo.id
             << "] Forwarding Msg directly to dest Peer " << dest << endl;
        
        grpc::Status status = tryForward(dest, directIt->second);
        if (status.ok()) {
            cout << "[Node " << nodeInfo.id << "] reply from direct Peer " << dest
                 << ": " << response->rspid() << endl;
            return Status::OK;
        } else {
            cerr << "[Node " << nodeInfo.id
                 << "] Unable to forward msg directly to Peer "
                 << dest << " : " << status.error_message() << endl;
        }
    }

    // 2. Otherwise, or if direct failed, try other peers as next hops
    // CRITICAL FIX: Only try ONE peer at a time. If it succeeds, STOP immediately.
    // This prevents duplicate messages from being sent through multiple paths.
    int pathAttempts = 0;
    const int maxPathAttempts = 3; // Limit to prevent infinite loops - try at most 3 different peers
    
    for (const auto& [pid, stub] : jobStub_) {
        // Prevent infinite loops - limit total path attempts
        if (pathAttempts >= maxPathAttempts) {
            cerr << "[Node " << nodeInfo.id << "] Max path attempts (" << maxPathAttempts 
                 << ") reached, giving up on dest " << dest << endl;
            break;
        }
        
        if(pid == prevSrc){
            continue;
        }
        // avoid immediately bouncing back to where it came from
        if (pid == origSrc) {
            continue;
        }
        // we already tried sending directly to dest above
        if (pid == dest) {
            continue;
        }

        if (std::find(msg->visited().begin(), msg->visited().end(), pid) != msg->visited().end()){
            continue;
        }
        
        // Skip if destination is confirmed dead (check peerHealth_)
        if (peerHealth_.count(dest) && !peerHealth_[dest]) {
            auto destChannelIt = peerChannels_.find(dest);
            if (destChannelIt != peerChannels_.end()) {
                grpc_connectivity_state destState = destChannelIt->second->GetState(true);
                if (destState == GRPC_CHANNEL_SHUTDOWN) {
                    cerr << "[Node " << nodeInfo.id << "] Destination " << dest 
                         << " is confirmed dead, stopping path search" << endl;
                    break;
                }
            }
        }

        // Skip peers that are confirmed dead/unhealthy to reduce noise
        if (peerHealth_.count(pid) && !peerHealth_[pid]) {
            auto channelIt = peerChannels_.find(pid);
            if (channelIt != peerChannels_.end()) {
                grpc_connectivity_state state = channelIt->second->GetState(false); // Don't wait for state
                if (state == GRPC_CHANNEL_SHUTDOWN) {
                    continue; // Skip dead peers
                }
            }
        }

        cout << "[Node " << nodeInfo.id << "] Trying Peer Node " << pid << " (attempt " << (pathAttempts + 1) << ")" << endl;
        pathAttempts++;

        grpc::Status status = tryForward(pid, stub);
        if (status.ok()) {
            cout << "[Node " << nodeInfo.id << "] *** SUCCESS: Forwarded to Peer " << pid
                 << ", response: " << response->rspid() << " *** STOPPING - no more peers will be tried" << endl;
            // CRITICAL: Successfully forwarded - MUST stop immediately to prevent duplicates
            // Do NOT try any other peers - the response will propagate back through the chain
            return Status::OK;
        } else {
            cerr << "[Node " << nodeInfo.id << "] Forward to " << pid
                 << " failed: " << status.error_message() << ", trying next peer" << endl;
            // Mark peer as unhealthy if it failed
            if (status.error_code() == grpc::StatusCode::DEADLINE_EXCEEDED || 
                status.error_code() == grpc::StatusCode::UNAVAILABLE) {
                peerHealth_[pid] = false;
            }
            continue; // try next peer
        }
    }

    // If we got here, no peer could be used to reach dest
    // Mark destination as dead if we've exhausted all paths
    if (pathAttempts >= maxPathAttempts) {
        peerHealth_[dest] = false; // Mark as dead
        cerr << "[Node " << nodeInfo.id << "] Marking destination " << dest 
             << " as unreachable after exhausting all paths (" << pathAttempts << " attempts)" << endl;
        response->set_rspid("Destination " + dest + " is unreachable (all paths exhausted)");
    } else {
        cerr << "[Node " << nodeInfo.id << "] No Peer available to reach dest "
             << dest << endl;
        response->set_rspid("Path to " + dest + " not found.");
    }
    return Status::OK;
}

void jobLoop::workerLoop(int workerId)
{
    // WORKERS: background threads that pull Jobs from the internal queue.
    // Each worker:
    //  1) Waits on queueCv for new jobs.
    //  2) Processes local messages or handshake messages.
    //  3) If the destination is another node, forwards the message using
    //     forwardToPeer() and records the result back into the Job.
    {
        std::lock_guard<std::mutex> lock(jobLoop::streamPopRowsReceivedMutex);
        jobLoop::streamPopRowsReceived = 0;
    }
    for (;;) {
        std::shared_ptr<Job> job;

        {
            std::unique_lock<std::mutex> lock(queueMutex);
            queueCv.wait(lock, [this](){ return stopping || !jobQueue.empty(); });
            if (stopping && jobQueue.empty()) {
                return;
            }
            job = jobQueue.front();
            jobQueue.pop();
        }

        string src = job->src;
        string dest = job->dest;
        string pyld = job->payload;
        
        // Update job state to PROCESSING
        {
            std::lock_guard<std::mutex> lk(job->mtx);
            job->state = JobState::PROCESSING;
        }
        // if (dest == nodeInfo.id){
        //     cout << "[Node " << nodeInfo.id << "][Worker " << workerId
        //         << "] Msg recelived from [Node " << src << "]: " << pyld << endl;
        //     // streamPopRowsReceived.fetch_add(1, std::memory_order_relaxed);
        //     // cout<< "Total Msgs: " << streamPopRowsReceived.load();
        //     // streamPopRowsReceived.store(0, std::memory_order_relaxed);
        //     // {
        //     //     std::lock_guard<std::mutex> lock(streamPopRowsReceivedMutex);
        //     //     streamPopRowsReceived++;
        //     // }
        //     // cout<< "Total Msgs: " << jobLoop::streamPopRowsReceived << endl;


        // }
        string rspid;

        const string helloPrefix = "__HELLO__";
        const string getPopPref = "getAvgPop";
        const string streamPopRef = "streamPop";
        if (pyld.rfind("streamRow:", 0) == 0 && dest == nodeInfo.id) {
            job->needsForward = false;
            string row = pyld.substr(strlen("streamRow:"));
            // cout << "[Node " << nodeInfo.id << "] Received row: " << row << endl;
            {
                std::lock_guard<std::mutex> lock(jobLoop::streamPopRowsReceivedMutex);
                jobLoop::streamPopRowsReceived++;
                cout << "[Node " << nodeInfo.id << "] Received row: " << row << endl;
                cout << "Total Msgs: " << jobLoop::streamPopRowsReceived << endl;
            }
            job->resultRspid = "row received";
            continue;
        }
        if (pyld.rfind(helloPrefix, 0) == 0) {
            // Handshake message: "__HELLO__<ip:port>"
            string peerAddr = pyld.substr(helloPrefix.size());
            nodeInfo.peer_addr[src] = peerAddr;
            cout << "[Node " << nodeInfo.id
                << "] Connected to peer " << src
                << " at " << peerAddr << endl;

            job->needsForward = false;

            ostringstream oss;
            oss << "HELLO-ACK-from-" << nodeInfo.id
                << " (worker " << workerId << ")";
            rspid = oss.str();
        }else if (pyld.rfind(getPopPref,0) == 0 && dest == nodeInfo.id) {
             // Only do this for local delivery
            job->needsForward = false;
    
            try {
                cout << "[Worker " << workerId << "] Attempting to read..." << endl;

                WorldDataParser parser;
                string filePath = "../dataset/world/populations.csv";
                auto csvData = parser.read(filePath);
                cout << "[Worker " << workerId << "]Read in "<< csvData.size() - 5 << " rows" << endl;

                
                vector<int> columnIdx = {0, 5, 68};// cols: country name, 1960, 1968
                int rowStart = 5; // skip header rows
                
                cout << "[Worker " << workerId << "] Performing calculations" << endl;
                parser.calculateAvgPop1930_1968(csvData, columnIdx, rowStart);
                cout << "[Worker " << workerId << "] Completed. Returning results to Node "<< src << endl;
                // convert to a single string
                ostringstream oss;
                for (const auto& e : parser.getCountryToAvgPop()) {
                    oss << e.first << ":" << fixed << setprecision(2) << e.second << "\n";
                }
                string rsp = oss.str();
                if (!rsp.empty()) rsp.pop_back(); // remove last comma

                job->resultRspid = rsp;
                rspid = rsp;
            } catch (const exception& e) {
                job->resultRspid = string("Error reading CSV: ") + e.what();
                {
                    std::lock_guard<std::mutex> lk(job->mtx);
                    job->state = JobState::FAILED;
                }
            }
        }else if(pyld.rfind(streamPopRef, 0) == 0 && src == nodeInfo.id){
            // Update state to ROUTING for streamPop
            {
                std::lock_guard<std::mutex> lk(job->mtx);
                job->state = JobState::ROUTING;
            }
            
            WorldDataParser parser;
            auto csvData = parser.read("../dataset/world/populations.csv");
            cout << "CSV read in. Preparing to send to Node "<< dest << " " << csvData.size()-5 << " messages." << endl;
            
            int failedRows = 0;
            for (size_t i = 5; i < csvData.size(); ++i) {
                string row = parser.rowToString(csvData[i]);

                loop::Msg forwardMsg;
                forwardMsg.set_src(src);
                forwardMsg.set_prev(nodeInfo.id);
                forwardMsg.set_dest(dest);
                forwardMsg.set_payload("Row: " + row);

                for(const auto& v : job->originalMsg->visited()){
                    forwardMsg.add_visited(v);
                }

                forwardMsg.add_visited(nodeInfo.id);

                // Check if destination is dead before trying (avoid infinite retries)
                if (peerHealth_.count(dest) && !peerHealth_[dest]) {
                    auto destChannelIt = peerChannels_.find(dest);
                    if (destChannelIt != peerChannels_.end()) {
                        grpc_connectivity_state destState = destChannelIt->second->GetState(true);
                        if (destState == GRPC_CHANNEL_SHUTDOWN) {
                            cerr << "[streamPop] Destination " << dest << " is dead, skipping remaining rows" << endl;
                            failedRows = csvData.size() - 5 - (i - 5); // Count remaining rows
                            break; // Stop streaming, destination is dead
                        }
                    }
                }
                
                // Single attempt per row (forwardToPeer() already has 3 retries inside)
                loop::MsgResponse peerResp;
                grpc::Status status = forwardToPeer(&forwardMsg, &peerResp);
                
                if (!status.ok()) {
                    failedRows++;
                    cerr << "[streamPop] Row " << (i-4) << " failed: " << status.error_message() << endl;
                    
                    // If destination is now confirmed dead, stop streaming
                    if (peerHealth_.count(dest) && !peerHealth_[dest]) {
                        auto destChannelIt = peerChannels_.find(dest);
                        if (destChannelIt != peerChannels_.end()) {
                            grpc_connectivity_state destState = destChannelIt->second->GetState(true);
                            if (destState == GRPC_CHANNEL_SHUTDOWN) {
                                cerr << "[streamPop] Destination " << dest << " confirmed dead, stopping stream" << endl;
                                failedRows = csvData.size() - 5 - (i - 5); // Count remaining rows
                                break;
                            }
                        }
                    }
                }
                
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            }

            ostringstream oss;
            oss << "streamPop complete";
            if (failedRows > 0) {
                oss << " (" << failedRows << " rows failed)";
            }
            job->resultRspid = oss.str();
            rspid = job->resultRspid;        
        }else if (dest == nodeInfo.id) {
            cout << "[Node " << nodeInfo.id << "][Worker " << workerId
                << "] Msg recelived from [Node " << src << "]: " << pyld << endl;
            {
                std::lock_guard<std::mutex> lock(jobLoop::streamPopRowsReceivedMutex);
                jobLoop::streamPopRowsReceived++;
                // cout << "[Node " << nodeInfo.id << "] Received row: " << row << endl;
                cout << "Total Msgs: " << jobLoop::streamPopRowsReceived << endl;
            }
            // Local delivery only
            job->needsForward = false;

            ostringstream oss;
            oss << "Msg delivered to " << nodeInfo.id
                << " (worker " << workerId << ")";
            rspid = oss.str();
        } else {
            // Worker role: this node is not the final destination, so the worker
            // is responsible for forwarding the message along the overlay using
            // forwardToPeer(). The leader never calls forwardToPeer directly.
            // Update state to ROUTING and mark as forwarding to prevent duplicates
            {
                std::lock_guard<std::mutex> lk(job->mtx);
                if (job->isForwarding) {
                    // Already forwarding this job - skip to prevent duplicates
                    cout << "[Node " << nodeInfo.id << "][Worker " << workerId 
                         << "] Job already being forwarded, skipping duplicate" << endl;
                    rspid = "Duplicate forward attempt prevented";
                    job->state = JobState::COMPLETED;
                    job->resultRspid = rspid;
                    job->done = true;
                    job->cv.notify_one();
                    continue; // Skip to next job
                }
                job->state = JobState::ROUTING;
                job->isForwarding = true;  // Mark as forwarding
            }
            
            cout << "Current Node: " << nodeInfo.id << endl;
            loop::Msg forwardMsg;
            forwardMsg.set_src(src);
            forwardMsg.set_prev(nodeInfo.id);
            forwardMsg.set_dest(dest);
            forwardMsg.set_payload(pyld);

            for(const auto& v : job->originalMsg->visited()){
                forwardMsg.add_visited(v);
            }

            forwardMsg.add_visited(nodeInfo.id);

            loop::MsgResponse peerResp;
            grpc::Status status = forwardToPeer(&forwardMsg, &peerResp);
            if (status.ok()) {
                ostringstream oss;
                oss << peerResp.rspid() << " (via " << nodeInfo.id
                    << ", worker " << workerId << ")";
                rspid = oss.str();
                // CRITICAL: Forwarding succeeded - mark job as done IMMEDIATELY
                // This prevents the same message from being processed again
                {
                    std::lock_guard<std::mutex> lk(job->mtx);
                    job->state = JobState::COMPLETED;
                    job->resultRspid = rspid;
                    job->done = true;
                }
                job->cv.notify_one();
                // Exit worker loop for this job - don't continue processing
                continue; // Go to next job in queue
            } else {
                ostringstream oss;
                oss << "Forwarding failed at node " << nodeInfo.id
                    << " (worker " << workerId << ")";
                rspid = oss.str();
                // Update state to FAILED on permanent forwarding failure
                {
                    std::lock_guard<std::mutex> lk(job->mtx);
                    job->state = JobState::FAILED;
                }
            }
        }
        // CRITICAL: Set job as done BEFORE notifying to prevent race conditions
        // This ensures the job is marked complete and won't be processed again
        {
            std::lock_guard<std::mutex> lk(job->mtx);
            if (job->state != JobState::FAILED) {
                job->state = JobState::COMPLETED;
            }
            job->resultRspid = rspid;
            job->done = true;  // Mark as done FIRST
        }
        job->cv.notify_one();  // Then notify - this prevents the infinite loop
    }
}