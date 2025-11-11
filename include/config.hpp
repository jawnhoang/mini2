#pragma once

#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <iostream>

//read in ConfigFile
#include <nlohmann/json.hpp>
using nlohmann::json;

using namespace std;

struct NodeId{
    string id;
    string addr;
    vector<string> peer_id;
    map<string, string> peer_addr;
};

class ConfigFileLoader {
    public:
        static NodeId loadConfig(const string& pathToFile, const string& nodeId){
            ifstream file(pathToFile);
            if(!file.is_open()){
                cerr << "Cannot Open ConfigFile: " << pathToFile << endl;
                
            }
            

            //read file to json data obj provided by library
            json data;
            file >> data;
            

            //create mapping of ALL Nodes and addresses following given overlay
            // AB, BC, BD, AE, EF, and ED
            // {Id : Addr}
            map<string, string> addressMap;
            for (auto& nodes : data["nodes"]){
                string node_id = nodes["id"].get<string>();
                addressMap[node_id] = nodes["address"].get<string>();
            }
            
                //create node object to contain current node data
            NodeId node;      
            for(auto& nodes: data["nodes"]){
                if(nodes["id"].get<string>() == nodeId){
                    node.id = nodes["id"].get<string>();
                    node.addr = nodes["address"].get<string>();

                    for(auto& p : nodes["peers"]){
                        node.peer_id.push_back(p.get<string>());
                    }
                    //exit loop if node data found
                    break;
                }
            }
            
            //get peer addr
            for(auto& pid : node.peer_id){
                if (addressMap.count(pid)){ //if pid exist
                    node.peer_addr[pid] = addressMap[pid];
                }else{
                    cerr << "Peer " << pid << " not found." << endl;
                }
            }

            return node;

        }


};