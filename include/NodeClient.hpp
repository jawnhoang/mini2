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

#include "route.grpc.pb.h"


using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReader;
using grpc::ClientReaderWriter;
using grpc::ClientWriter;
using grpc::Status;

using loop::RouteService;
using loop::executeJob;
using namespace std;

class NodeClient{
    private:
        unique_ptr<RouteService::Stub> stub_;
        unique_ptr<executeJob::Stub> jobStub_;

    public:
        explicit NodeClient(std::shared_ptr<grpc::Channel> channel): 
                            stub_(RouteService::NewStub(channel)),
                            jobStub_(executeJob::NewStub(channel))
                            {}

        string getHello(const string& rid){
            loop::Request request;
            request.set_rid(rid);

            loop::Response response;

            grpc::ClientContext context;

            grpc::Status status = stub_->Hello(&context, request, &response);

            if(status.ok()){
                return response.rspid();
            }else{
                cerr << "RPC failed: " << status.error_code() << " - " << status.error_message() << endl;
                return "RPC_ERR";
            }
        }

        string getLeader(){
            loop::Request request;
            loop::Response response;
            grpc::ClientContext context;
            
            grpc::Status status = stub_->DetermineLeader(&context, request, &response);

            if(status.ok()){
                return "end";
            }else{
                cerr << "RPC failed " << status.error_code() << " - " << status.error_message() << endl;
                return "RPC_ERR";
            }

        }


        string sendMsg(const string& src, const string& dest, const string& payload){
            loop::Msg msg;
            loop::MsgResponse response;
            grpc::ClientContext context;


            msg.set_src(src);
            msg.set_dest(dest);
            msg.set_payload(payload);



            grpc::Status status = jobStub_->sendMsg(&context, msg, &response);
            
            if(status.ok()){
                return response.rspid();
            }else{
                cerr << "RPC failed " << status.error_code() << " - " << status.error_message() << endl;
                return "RPC_ERR";
            }

        }


};