#include <iostream>
#include <memory>
#include <string>

#include "omp.h"
#include "ipcLoop.hpp"

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

using namespace std;

class NodeServer{
    private:
        string server_addr;

    public:
        NodeServer(){}

        string get_server_addr(){
            return server_addr;
        }

        void set_server_addr(const string& addr){
            server_addr = addr;
        }

        void runServer(){
            ipcLoop service;

            grpc::ServerBuilder builder;
            builder.AddListeningPort(server_addr, grpc::InsecureServerCredentials());
            builder.RegisterService(&service);
            unique_ptr<grpc::Server> server(builder.BuildAndStart());
            #if defined(USE_OPENMP)
            std::cout << "Server using openmp" << std::endl;
            #else
            std::cout << "Server is not threaded" << std::endl;
            #endif

            cout << "Server listening on " << server_addr << endl;
            server->Wait();
        }

};