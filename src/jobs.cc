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

    response->set_rspid(job->resultRspid);
    return Status::OK;
}

//communicate with Peers
void jobLoop::peerStubs(){
    for(const auto& [pid, paddr] : nodeInfo.peer_addr){
        if(pid == nodeInfo.id){
            continue;
        }
        jobStub_[pid] = executeJob::NewStub(grpc::CreateChannel(paddr, grpc::InsecureChannelCredentials()));
        cout << "[Node] Stub for Peer " << pid << " created at " << paddr << endl;
    }
}

//in a way, server becomes a client if its forwarding msgs
grpc::Status jobLoop::forwardToPeer(const ::loop::Msg* msg, ::loop::MsgResponse* response){
    for(const auto& [pid, stub] : jobStub_){ //cycles through list of mapped Ids and addresses
        if(pid == msg->src()){
            continue;
        }
        cout << "[Node " << nodeInfo.id << "] Forwarding Msg to Peer Node " << pid << endl;

        grpc::ClientContext context;
        loop::MsgResponse peerResp;

        grpc::Status status = stub->sendMsg(&context, *msg, &peerResp);

        if(status.ok()){
            cout << "[Node " << nodeInfo.id << "] reply from Peer " << pid << ": " << peerResp.rspid() << endl;
            response->set_rspid(peerResp.rspid());
            return Status::OK;
        }else{
            cerr << "[Node " << nodeInfo.id << "] Unable to forward msg to Peer Node " << pid << " : " << status.error_message() << endl;
        }
    }

    //if loop looking for available peers completes
    // then no peers exist
    cerr << "[Node " << nodeInfo.id << "] No Peer available to reach" << endl;
    response->set_rspid("Path to " + msg->dest() + " not found.");
    return Status::OK;

}

void jobLoop::workerLoop(int workerId)
{
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

        cout << "[Node " << nodeInfo.id << "][Worker " << workerId << "] Msg recelived from [Node " << src << "]: " << pyld << endl;

        string rspid;

        if (dest == nodeInfo.id) {
            std::ostringstream oss;
            oss << "Msg delivered to " << nodeInfo.id << " (worker " << workerId << ")";
            rspid = oss.str();
        } else {
            loop::Msg forwardMsg;
            forwardMsg.set_src(src);
            forwardMsg.set_dest(dest);
            forwardMsg.set_payload(pyld);

            loop::MsgResponse peerResp;
            grpc::Status status = forwardToPeer(&forwardMsg, &peerResp);
            if (status.ok()) {
                std::ostringstream oss;
                oss << peerResp.rspid() << " (via " << nodeInfo.id << ", worker " << workerId << ")";
                rspid = oss.str();
            } else {
                rspid = "Forwarding failed at node " + nodeInfo.id;
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
