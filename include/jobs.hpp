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
        Stopwatch timer;

        struct Job {
            string src;
            string dest;
            string payload;
            // Mark whether this came from an external client
            bool isExternal = false;

            // For later chunking / tracking
            string uid;

            string resultRspid;
            bool done = false;
            bool needsForward = false;
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
        explicit jobLoop(const NodeId& nodeData): nodeInfo(nodeData){
            peerStubs();
        }

        Status sendMsg(::grpc::ServerContext* context, const ::loop::Msg* msg, ::loop::MsgResponse* response) override;

        void peerStubs();

        // NEW:
        void runHandshake();

        Status sendWithOptionalRetry(
            const std::string& pid,
            std::unique_ptr<executeJob::Stub>& stub,
            const ::loop::Msg* msg,
            ::loop::MsgResponse* peerResp);
        Status forwardToPeer(const ::loop::Msg* msg, ::loop::MsgResponse* response);

    private:
        void workerLoop(int workerId);
};
