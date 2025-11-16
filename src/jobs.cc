#include "jobs.hpp"
#include <iostream>
#include <cstdio>
#include <sstream>
#include <iomanip>
#include <chrono>

namespace {
    std::map<std::string, bool> peerAlive;
    std::mutex peerAliveMutex;
}

using namespace std;

Status jobLoop::sendMsg(::grpc::ServerContext* context,
                        const ::loop::Msg* msg,
                        ::loop::MsgResponse* response)
{
    std::call_once(workerInitFlag, [this]() {
        for (int i = 0; i < 2; i++) {
            workers.emplace_back([this, i]() { this->workerLoop(i); });
        }
    });

    auto job = std::make_shared<Job>();
    job->src = msg->src();
    job->dest = msg->dest();
    job->payload = msg->payload();

    // External if it came from the real client
    job->isExternal = (job->src == "CLIENT");

    // Give every job a simple uid
    static std::atomic<uint64_t> uidCounter{0};
    job->uid = nodeInfo.id + "-" + std::to_string(uidCounter++);

    {
        std::lock_guard<std::mutex> lock(queueMutex);
        jobQueue.push(job);
    }
    queueCv.notify_one();

    if (job->isExternal) {
        // Leader behavior for external jobs:
        // enqueue and immediately return the uid to the client
        response->set_rspid(job->uid);
        return Status::OK;
    } else {
        // Internal server-to-server call:
        // keep the current behavior, wait for worker result
        std::unique_lock<std::mutex> lk(job->mtx);
        job->cv.wait(lk, [job](){ return job->done; });

        response->set_rspid(job->resultRspid);
        return Status::OK;
    }
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
        {
            std::lock_guard<std::mutex> lk(peerAliveMutex);
            peerAlive[pid] = true;
        }
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

// Helper: send a message to a peer stub, and if it fails, retry once using
// the latest address from nodeInfo.peer_addr (if available).
grpc::Status jobLoop::sendWithOptionalRetry(
    const std::string& pid,
    std::unique_ptr<executeJob::Stub>& stub,
    const ::loop::Msg* msg,
    ::loop::MsgResponse* peerResp)
{
    // First attempt with the existing stub
    grpc::ClientContext ctx;
    auto deadline = std::chrono::system_clock::now() + std::chrono::milliseconds(300);
    ctx.set_deadline(deadline);
    grpc::Status status = stub->sendMsg(&ctx, *msg, peerResp);
    if (status.ok()) {
        return status;
    }

    {
        std::lock_guard<std::mutex> lk(peerAliveMutex);
        peerAlive[pid] = false;
    }

    // Look up the latest known address for this peer
    auto addrIt = nodeInfo.peer_addr.find(pid);
    if (addrIt == nodeInfo.peer_addr.end()) {
        // No newer address stored; return original failure
        return status;
    }

    const std::string& peerAddr = addrIt->second;
    cout << "[Node " << nodeInfo.id << "] Retrying forward to Peer Node "
         << pid << " using addr " << peerAddr << endl;

    auto retryStub = executeJob::NewStub(
        grpc::CreateChannel(peerAddr, grpc::InsecureChannelCredentials()));

    grpc::ClientContext retryCtx;
    auto retryDeadline = std::chrono::system_clock::now() + std::chrono::milliseconds(300);
    retryCtx.set_deadline(retryDeadline);
    loop::MsgResponse retryResp;
    grpc::Status retryStatus = retryStub->sendMsg(&retryCtx, *msg, &retryResp);

    if (retryStatus.ok()) {
        {
            std::lock_guard<std::mutex> lk(peerAliveMutex);
            peerAlive[pid] = true;
        }
        // On success, copy the retried response into the caller's response
        *peerResp = retryResp;
        return retryStatus;
    }

    cerr << "[Node " << nodeInfo.id << "] retry to Peer " << pid
         << " failed: " << retryStatus.error_message() << endl;
    return retryStatus;
}

//in a way, server becomes a client if its forwarding msgs
grpc::Status jobLoop::forwardToPeer(const ::loop::Msg* msg, ::loop::MsgResponse* response){
    const std::string& dest    = msg->dest();
    const std::string& origSrc = msg->src();

    bool directAlive = true;
    {
        std::lock_guard<std::mutex> lk(peerAliveMutex);
        auto it = peerAlive.find(dest);
        if (it != peerAlive.end()) {
            directAlive = it->second;
        }
    }

    // 1. Prefer a direct edge to the destination if it exists and is alive
    auto directIt = jobStub_.find(dest);
    if (directIt != jobStub_.end() && directAlive) {
        cout << "[Node " << nodeInfo.id
             << "] Forwarding Msg directly to dest Peer " << dest << endl;

        loop::MsgResponse peerResp;
        grpc::Status status = sendWithOptionalRetry(dest, directIt->second, msg, &peerResp);

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
        {
            std::lock_guard<std::mutex> lk(peerAliveMutex);
            auto it = peerAlive.find(pid);
            if (it != peerAlive.end() && !it->second) {
                continue;
            }
        }

        cout << "[Node " << nodeInfo.id << "] Forwarding Msg to Peer Node " << pid << endl;

        loop::MsgResponse peerResp;
        grpc::Status status = sendWithOptionalRetry(pid, const_cast<std::unique_ptr<executeJob::Stub>&>(stub), msg, &peerResp);

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
        } else if (dest == nodeInfo.id) {
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
            //forwardMsg.set_src(src);
            forwardMsg.set_src(nodeInfo.id);
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
