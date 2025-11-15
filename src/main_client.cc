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


int main(int arg, char** argv){
    // string target = "127.0.0.1:50051";
    
    // NodeClient client(grpc::CreateChannel(target, grpc::InsecureChannelCredentials()));

    // string rid = "Req-123";
    // string intro = client.getLeader();
    // string response = client.getHello(rid);

    // cout << response << endl;

    // cout<<endl;
    // string root = "169.254.223.138:";
    //string root = "127.0.0.1:";
    if (arg != 5){
        cerr << "Tip: " << argv[0] << " <host> <port|auto> <dest> <'payload'>" << endl;
        return 1;
    }
        // NodeClient client(grpc::CreateChannel(target, grpc::InsecureChannelCredentials()));


    // cout<< "Getting Average Population" << endl;
    // int tCnt = 2;
    // string response2 = client.getPopulation(tCnt);
    // string rid2 = "client:50051";
    // string src = "A";
    string host = argv[1];
    string portArg = argv[2];
    string dest = argv[3];
    string payload = argv[4];

    string src = portArg;
    string rsp;
    if (portArg == string("auto")) {
        const string candidatePorts[] = {"50051", "50052", "50053", "50054", "50055"};
        bool delivered = false;
        for (const auto& p : candidatePorts) {
            string target = host + string(":") + p;
            cout << "[Client] Trying " << target << endl;
            //try catch - can this be sped up if the servers are not up? ie, only B, C
            NodeClient client(grpc::CreateChannel(target, grpc::InsecureChannelCredentials()));
            rsp = client.sendMsg(src, dest, payload);
            if (rsp != "RPC_ERR") {
                delivered = true;
                break;
            }
        }
        if (!delivered) {
            cout << "[Client] No reachable server found on candidate ports" << endl;
            return 1;
        }
    } else {
        string target = host + string(":") + portArg;
        cout << "[Client] Trying " << target << endl;
        NodeClient client(grpc::CreateChannel(target, grpc::InsecureChannelCredentials()));
        rsp = client.sendMsg(src, dest, payload);
    }

    cout << "[Client] Response: " << rsp << endl;
    return 0;
}