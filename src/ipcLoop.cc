#include "ipcLoop.hpp"
#include <iostream>
#include <cstdio>

//basecamp
grpc::Status ipcLoop::Hello(grpc::ServerContext* context, const loop::Request* request, loop::Response* response)
{
 //Test: Intro for all Nodes to arrive at memory point
        string sharedVariable = "Hello, Welcome to the shared memory space!";
        
        //individual threads will perform work here
        #if defined(USE_OPENMP) //Without the ifdef, why does this execute if i launch the no threaded server?
        #pragma omp parallel 
        {
            int tId = omp_get_thread_num();
            int tCount = omp_get_num_threads();

            #pragma omp single // only need 1 thread to print header info
            printf("---> %d) %d threads available\n", tId, tCount);

            //each thread to read sharedVariable
            printf("Thread %d : %s\n", tId, sharedVariable.c_str());
        }
        #else
        printf("Single Thread : %s\n", sharedVariable.c_str());
        #endif

        response->set_rspid(request->rid());
        // response->set_collisions(0); // placeholder

        return Status::OK;
}

grpc::Status ipcLoop::DetermineLeader(grpc::ServerContext* context, const loop::Request* request, loop::Response* response){
    #if defined(USE_OPENMP)
    #pragma omp parallel 
    {
        int tId = omp_get_thread_num();

        if(tId == 0){
            printf("\nThread %d is the leader\n", tId);
        }
    }
    #else
    printf("Single Threaded. No leader.");
    #endif
    return Status::OK;
    
}