#include <cmath>
#include <iostream>
#include <memory>
#include <string>

#include "omp.h"
#include "NodeServer.hpp"

#include <grpc/grpc.h>
#include <grpcpp/server.h> // omit 
#include <grpcpp/server_builder.h> //async dont use - omit
#include <grpcpp/server_context.h>
#include <grpcpp/security/server_credentials.h>

// generated code
#include "route.grpc.pb.h"

using grpc::Server; //async - dont use
using grpc::ServerBuilder; //async - dont use
using grpc::ServerContext;
using grpc::ServerReader;
using grpc::ServerReaderWriter;
using grpc::ServerWriter;
using grpc::Status;

using loop::RouteService;
using namespace std;

#define SHOWRESULTS

int main(int arg, char** argv){
    NodeServer server;
    string addr = "127.0.0.1:50051";
    server.set_server_addr(addr);
    server.runServer();

    return 0;
}