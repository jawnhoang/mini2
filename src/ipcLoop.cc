#include "ipcLoop.hpp"
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

        // Treat the incoming rid as the payload for data validation
        const std::string payload = request->rid();

        // Log a concise server-side trace
        uint32_t h = fnv1a32(payload);
        std::cout << "------------------------------------------------------------\n";
        std::cout << "[Server] Received payload\n";
        std::cout << "  Length:   " << payload.size() << " bytes\n";
        std::cout << "  Head:     " << payload.substr(0, std::min<size_t>(payload.size(), 16)) << "\n";
        std::cout << "  Checksum: 0x" << std::hex << std::setw(8) << std::setfill('0') << h << std::dec << "\n";
        #if defined(USE_OPENMP)
        std::cout << "[Server] Threads used: " << omp_get_max_threads() << " (OpenMP)\n";
        #else
        std::cout << "[Server] Threads used: 1 (Single-threaded)\n";
        #endif
        std::cout << "------------------------------------------------------------\n";

        // Build an integrity tag the client can verify: length + FNV-1a checksum
        std::ostringstream os;
        os << "len=" << payload.size()
           << "|fnv1a=0x" << std::hex << std::setw(8) << std::setfill('0') << h;

        response->set_rspid(os.str());

        return Status::OK;
}
