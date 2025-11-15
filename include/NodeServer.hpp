#include <iostream>
#include <memory>
#include <string>
#include <map>

#include "omp.h"
#include "ipcLoop.hpp"
#include "jobs.hpp"
#include "config.hpp"

#include <grpc/grpc.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>
#include <grpcpp/security/server_credentials.h>

// allow servers to talk with other servers
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>

// generated code
#include "route.grpc.pb.h"

using grpc::Server;
using grpc::ServerBuilder; 
using grpc::ServerContext;
using grpc::Status;

using loop::RouteService;
using loop::Request;
using loop::Response;

using namespace std;

class NodeServer{
    private:
        NodeId nodeInfo;
        map<string, unique_ptr<RouteService::Stub>> peerStubs_;

    public:
        explicit NodeServer(const NodeId& info):nodeInfo(info){}

        void runServer(){
            ipcLoop service;
            jobLoop jobs(nodeInfo);

            grpc::ServerBuilder builder;
            builder.AddListeningPort(nodeInfo.addr, grpc::InsecureServerCredentials());
            builder.RegisterService(&service);
            builder.RegisterService(&jobs);
            unique_ptr<grpc::Server> server(builder.BuildAndStart());
            if (!server) {
                std::cerr << "[Node " << nodeInfo.id << "] ERROR: Failed to start gRPC server on "
                          << nodeInfo.addr << std::endl;
                return;
            }

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

            //sample way for servers to talk to server peers at run time
            // peerStubs();
            // sendToPeer();

            // jobs.peerStubs();
            server->Wait();
        }

};