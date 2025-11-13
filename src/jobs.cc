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
    string src = msg->src();
    string dest = msg->dest();
    string pyld = msg->payload();
    
    cout << "[Node " << nodeInfo.id << "] Msg recelived from [Node " << src << "]: " << pyld << endl;

    if(dest == nodeInfo.id){
        response->set_rspid("Msg delivered to " + nodeInfo.id);
        return Status::OK;
    }else{
     //forward to other peers from list
        return forwardToPeer(msg, response);
    }
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
