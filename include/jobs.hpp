#pragma once

#include <iostream>
#include <memory>
#include <string>

#include "omp.h"

#include <grpc/grpc.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/channel.h> 

#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <vector>

#include "config.hpp"
#include "WorldDataParser.hpp"
#include "Stopwatch.hpp"
// generated code
#include "route.grpc.pb.h"

using grpc::Server;
using grpc::ServerBuilder; 
using grpc::ServerContext;
using grpc::Status;

using loop::executeJob;
using namespace std;

//Extract server creation from loopImpl.cc
class jobLoop final : public executeJob::Service{
    private:
        NodeId nodeInfo;
        map<string, unique_ptr<executeJob::Stub>> jobStub_;
        map<string, shared_ptr<grpc::Channel>> peerChannels_; // For connection state checking
        map<string, bool> peerHealth_; // Track peer health status (false = dead/unhealthy)
        Stopwatch timer;

        enum class JobState {
            PENDING,
            ROUTING,
            PROCESSING,
            COMPLETED,
            FAILED
        };

        struct Job {
            string src;
            string dest;
            string payload;
            string resultRspid;
            const loop::Msg* originalMsg = nullptr;
            bool done = false;
            bool needsForward = false;
            JobState state = JobState::PENDING;
            std::mutex mtx;
            std::condition_variable cv;
        };

        std::queue<std::shared_ptr<Job>> jobQueue;
        std::mutex queueMutex;
        std::condition_variable queueCv;
        std::vector<std::thread> workers;
        bool stopping = false;
        std::once_flag workerInitFlag;

    public:
        static int streamPopRowsReceived;
        static std::mutex streamPopRowsReceivedMutex;

        explicit jobLoop(const NodeId& nodeData): nodeInfo(nodeData){
            peerStubs();
        }

        Status sendMsg(::grpc::ServerContext* context, const ::loop::Msg* msg, ::loop::MsgResponse* response) override;

        void peerStubs();

        // NEW:
        void runHandshake();

        Status forwardToPeer(const ::loop::Msg* msg, ::loop::MsgResponse* response);

    private:
        bool isPeerReady(const std::string& peerId);
        void updatePeerHealth(const std::string& peerId, bool healthy);
        void workerLoop(int workerId);
};
