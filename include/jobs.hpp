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


#include "config.hpp"

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

    public:
        explicit jobLoop(const NodeId& nodeData): nodeInfo(nodeData){peerStubs();}

        Status sendMsg(::grpc::ServerContext* context, const ::loop::Msg* msg, ::loop::MsgResponse* response) override;

        void peerStubs();

        Status forwardToPeer(const ::loop::Msg* msg, ::loop::MsgResponse* response);
};


