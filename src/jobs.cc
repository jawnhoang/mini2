#include "jobs.hpp"
#include <iostream>
#include <cstdio>
#include <sstream>
#include <iomanip>


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

grpc::Status jobLoop::sendMsg(::grpc::ServerContext* context, const ::loop::Msg* msg, ::loop::MsgResponse* response)
{
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

        jobStub_[pid] = executeJob::NewStub(
            grpc::CreateChannel(paddr, grpc::InsecureChannelCredentials()));
        cout << "[Node] Stub for Peer " << pid << " created at " << paddr << endl;
    }
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

    // 1. Prefer a direct edge to the destination if it exists
    auto directIt = jobStub_.find(dest);
    if (directIt != jobStub_.end()) {
        cout << "[Node " << nodeInfo.id
             << "] Forwarding Msg directly to dest Peer " << dest << endl;

        grpc::ClientContext context;
        loop::MsgResponse peerResp;
        grpc::Status status = directIt->second->sendMsg(&context, *msg, &peerResp);

        if (status.ok()) {
            cout << "[Node " << nodeInfo.id << "] reply from direct Peer " << dest
                 << ": " << peerResp.rspid() << endl;
            response->set_rspid(peerResp.rspid());
            return Status::OK;
        } else {
            cerr << "[Node " << nodeInfo.id
                 << "] Unable to forward msg directly to Peer "
                 << dest << " : " << status.error_message() << endl;
            // fall through to try other peers as backup
        }
    }

    // 2. Otherwise, or if direct failed, try other peers as next hops
    for (const auto& [pid, stub] : jobStub_) {
        // avoid immediately bouncing back to where it came from
        if (pid == origSrc) {
            continue;
        }
        // we already tried sending directly to dest above
        if (pid == dest) {
            continue;
        }

        cout << "[Node " << nodeInfo.id << "] Forwarding Msg to Peer Node " << pid << endl;

        grpc::ClientContext context;
        loop::MsgResponse peerResp;
        grpc::Status status = stub->sendMsg(&context, *msg, &peerResp);

        if (status.ok()) {
            cout << "[Node " << nodeInfo.id << "] reply from Peer " << pid
                 << ": " << peerResp.rspid() << endl;
            response->set_rspid(peerResp.rspid());
            return Status::OK;
        } else {
            cerr << "[Node " << nodeInfo.id << "] Unable to forward msg to Peer Node "
                 << pid << " : " << status.error_message() << endl;
        }
    }

    // If we got here, no peer could be used to reach dest
    cerr << "[Node " << nodeInfo.id << "] No Peer available to reach dest "
         << dest << endl;
    response->set_rspid("Path to " + dest + " not found.");
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

        cout << "[Node " << nodeInfo.id << "][Worker " << workerId
             << "] Msg recelived from [Node " << src << "]: " << pyld << endl;

        string rspid;

        const string helloPrefix = "__HELLO__";
        const string getPopPref = "getAvgPop";
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
                cout << "[Worker " << workerId << "]Read in "<< csvData.size() << " rows" << endl;

                
                vector<int> columnIdx = {0, 5, 68};// cols: country name, 1960, 1968
                int rowStart = 5; // skip header rows

                parser.calculateAvgPop1930_1968(csvData, columnIdx, rowStart);

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
            }
        }else if (dest == nodeInfo.id) {
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
            loop::Msg forwardMsg;
            forwardMsg.set_src(src);
            forwardMsg.set_dest(dest);
            forwardMsg.set_payload(pyld);

            loop::MsgResponse peerResp;
            grpc::Status status = forwardToPeer(&forwardMsg, &peerResp);
            if (status.ok()) {
                ostringstream oss;
                oss << peerResp.rspid() << " (via " << nodeInfo.id
                    << ", worker " << workerId << ")";
                rspid = oss.str();
            } else {
                ostringstream oss;
                oss << "Forwarding failed at node " << nodeInfo.id
                    << " (worker " << workerId << ")";
                rspid = oss.str();
            }
        }
        {
            std::lock_guard<std::mutex> lk(job->mtx);
            job->resultRspid = rspid;
            job->done = true;
        }
        job->cv.notify_one();
    }
}
