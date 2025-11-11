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
    string target = "127.0.0.1:50051";
    
    NodeClient client(grpc::CreateChannel(target, grpc::InsecureChannelCredentials()));

    string rid = "Req-123";
    string intro = client.getLeader();
    string response = client.getHello(rid);

    cout << response << endl;

    cout<<endl;

    cout<< "Getting Average Population" << endl;
    int tCnt = 2;
    string response2 = client.getPopulation(tCnt);



    return 0;
}