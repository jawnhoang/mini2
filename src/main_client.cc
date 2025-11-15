#include <grpc/grpc.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>

#include <chrono>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "NodeClient.hpp"

// generated code
#include "route.grpc.pb.h"


using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReader;
using grpc::ClientReaderWriter;
using grpc::ClientWriter;
using grpc::Status;

using loop::RouteService;
using namespace std;


int main(int argc, char** argv){
    // string target = "127.0.0.1:50051";
    
    // NodeClient client(grpc::CreateChannel(target, grpc::InsecureChannelCredentials()));

    // string rid = "Req-123";
    // string intro = client.getLeader();
    // string response = client.getHello(rid);

    // cout << response << endl;

    // cout<<endl;
    // string root = "169.254.223.138:";
    //string root = "127.0.0.1:";   
   
    // Expecting: ./loop-client <host> <src_id> <dest_id> <'payload'>
    if (argc != 5){
        cerr << "Tip: " << argv[0]
             << " <host> <src_id> <dest_id> <'payload'>" << endl;
        return 1;
    }
    // NodeClient client(grpc::CreateChannel(target, grpc::InsecureChannelCredentials()));


    // cout<< "Getting Average Population" << endl;
    // int tCnt = 2;
    // string response2 = client.getPopulation(tCnt);

    
    // External clients always send to the local entry node.
    // The overlay and leader/worker servers decide how to route it.
    // std::string host = "127.0.0.1"
    // std::string port = "50051"; 
    
    std::string host = argv[1];        // entry node host (no hardcoded IP)
    std::string src = argv[2];         // logical source node id, e.g. "A"
    std::string dest = argv[3];        // logical destination node id, e.g. "F"
    std::string payload = argv[4];     // message body

    // Auto-try ports 50051–50055 on the given host.
    std::vector<std::string> ports = {"50051", "50052", "50053", "50054", "50055"};

    std::string rsp = "RPC_ERR";

    std::cout << "[Client] Ports: ";

    for (const auto& p : ports) {
        std::cout << p << " ";
        std::string target = host + ":" + p;
        NodeClient client(grpc::CreateChannel(target, grpc::InsecureChannelCredentials()));
        rsp = client.sendMsg(src, dest, payload);
        if (rsp != "RPC_ERR") {
            std::cout << "(ok)" << std::endl;
            std::cout << "[Client] Response: " << rsp << std::endl;
            return 0; // done on first successful port
        }
    }

    // If here, no port worked.
    std::cout << "(all failed)" << std::endl;
    std::cout << "[Client] Response: " << rsp << std::endl;
    return 1;
}