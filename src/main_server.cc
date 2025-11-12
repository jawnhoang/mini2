#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <iomanip>

#include "omp.h"
#include "NodeServer.hpp"
#include "config.hpp"

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
    string nodeId;
    string Path = "../config/configFile.json";


    if (arg < 2){
        cerr << "Tip: " << argv[0] << " <node_id> <dest> <payload>";
        return 1;
    }
    
    char nodeArg = toupper(argv[1][0]);

    if(nodeArg>= 'A' && nodeArg <= 'F'){
        nodeId = string(1, nodeArg);
    }else
    {
        cerr << "Valid Args ['A', 'B', 'C', 'D', 'E', 'F']" << endl;
        return 1;
    }

    NodeId nodeInfo = ConfigFileLoader::loadConfig(Path, nodeId);
    NodeServer server(nodeInfo);
    // string addr = "127.0.0.1:50051";
    // server.set_server_addr(addr);
    server.runServer();

    return 0;
}