
#include <iostream>
#include <memory>
#include <string>

#include "omp.h"

#include <grpc/grpc.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>
#include <grpcpp/security/server_credentials.h>

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
    public:
    explicit jobLoop(){}

    Status getAvgPopulation(::grpc::ServerContext* context, const ::loop::threadCnt* threadcnt, ::loop::Response* response) override;


};


