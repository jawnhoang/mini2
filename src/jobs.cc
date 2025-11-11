#include "jobs.hpp"
#include <iostream>
#include <cstdio>
#include <sstream>
#include <iomanip>

// simple FNV-1a 32-bit checksum so we avoid extra libs
static uint32_t fnv1a32(const std::string& s) {
    const uint32_t FNV_OFFSET = 2166136261u;
    const uint32_t FNV_PRIME  = 16777619u;
    uint32_t hash = FNV_OFFSET;
    for (unsigned char c : s) {
        hash ^= c;
        hash *= FNV_PRIME;
    }
    return hash;
}


using namespace std;

grpc::Status jobLoop::getAvgPopulation(::grpc::ServerContext* context, const ::loop::threadCnt* threadcnt, ::loop::Response* response)
{
    int numthreads = threadcnt->tcnt();
    // cout<< "Num Threads: " << numthreads << endl;
    #if defined(USE_OPENMP)
    #pragma omp parallel num_threads(numthreads)
    {
        int tId = omp_get_thread_num();

        #pragma omp single
        cout << "Reading CSV using << numthreads << threads" << endl;


        response->set_rspid("CSV Proccessed");

    }
    #endif
    return Status::OK;
}
