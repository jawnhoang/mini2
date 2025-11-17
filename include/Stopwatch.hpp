#pragma once

#include <iostream>
#include <chrono>

using namespace std;

class Stopwatch {
    private:
        chrono::high_resolution_clock::time_point start;
        chrono::high_resolution_clock::time_point stop;


    public:
        void startTimer(){start = chrono::high_resolution_clock::now();}
        void stopTimer(){stop = chrono::high_resolution_clock::now();}
        void printTimeSummary(string& msg){
            auto executionDuration = chrono::duration_cast<chrono::milliseconds>(stop-start).count();
            if(executionDuration < 2){
                auto executionDurationNS = chrono::duration_cast<chrono::nanoseconds>(stop-start).count();
                cout<< msg<< ": " << executionDurationNS << "ns" << endl;
            }else{
                cout<< msg<< ": " << executionDuration << "ms" << endl;
            }
            cout<<"------------------" << endl;
        }
};