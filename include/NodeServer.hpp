#include <iostream>
#include <memory>
#include <string>

#include "omp.h"
#include "ipcLoop.hpp"
#include "jobs.hpp"
#include "config.hpp"

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
        // string server_addr;
        NodeId nodeInfo;

    public:
        explicit NodeServer(const NodeId& info):nodeInfo(info){}

        // string get_server_addr(){
        //     return server_addr;
        // }

        // void set_server_addr(const string& addr){
        //     server_addr = addr;
        // }

        void runServer(){
            ipcLoop service;
            jobLoop jobs;

            grpc::ServerBuilder builder;
            builder.AddListeningPort(nodeInfo.addr, grpc::InsecureServerCredentials());
            builder.RegisterService(&service);
            builder.RegisterService(&jobs);
            unique_ptr<grpc::Server> server(builder.BuildAndStart());
            #if defined(USE_OPENMP)
            std::cout << "[Server] Using openmp" << std::endl;
            #else
            std::cout << "[Server] Running Single Threaded" << std::endl;
            #endif

            cout << "[Node " << nodeInfo.id << "] listening on " << nodeInfo.addr << endl;
            
            cout << "[Node " << nodeInfo.id << "] Peers: ";
            for(auto& p : nodeInfo.peer_id){
                cout<< p << " ";
            }
            cout << endl;

            server->Wait();
        }

};