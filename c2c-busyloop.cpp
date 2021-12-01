#include <algorithm>
#include <atomic>
#include <iostream>
#include <thread>
#include <vector>
#include <cstdint>
#include <pthread.h>
#include <unistd.h>

int g_writer_cpu = 1;
int g_reader_cpu = 3;
int g_tsc_khz = 2207999;

int set_affinity(int cpu)
{
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu, &cpuset);
    pthread_t current_thread = pthread_self();    
    return pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset);
}

uint64_t rdtsc(){
    unsigned int lo,hi;
    __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
    return ((uint64_t)hi << 32) | lo;
}

uint64_t cycles_to_ns(uint64_t cycles) {
    return cycles * 1000000 / g_tsc_khz;
}

struct State
{
    std::atomic_uint64_t ts{0};
    char pad[56];
    std::atomic_bool next{true};
    std::atomic_bool stop{false};
} g_state;

void writer()
{
    set_affinity(1);

    int i = 1000000;
    while (--i) {        
        g_state.next = false;
        g_state.ts = rdtsc();
        while (! g_state.next) ;
    }
    g_state.ts = -1;
}

void reader()
{
    set_affinity(3);

    std::vector<int64_t> results;
    while (true)
    {
        while (! g_state.ts) continue;
        if (g_state.ts == -1) break;

        results.push_back(rdtsc() - g_state.ts);
        g_state.ts = 0;
        g_state.next = true;
    }

    std::sort(begin(results), end(results));
    auto report = [](const char* name, uint64_t cycles){
        std::cout << name << ": " << cycles_to_ns(cycles) << " ns (" << cycles << ")\n";
    };
    report("min", results.front());
    report("median", results[results.size()/2]);
    report("max", results.back());
}

int main(int argc, char** argv)
{
    int opt;
    while ((opt = getopt(argc, argv, "r:w:f:")) != -1)
    {
        switch (opt)
        {
        case 'r':
            g_reader_cpu = atoi(optarg);
            break;
        case 'w':
            g_writer_cpu = atoi(optarg);
            break;
        case 'f':
            g_tsc_khz = atoi(optarg);
            break;
        default:
            std::cerr << "Usage: " <<  argv[0] << " [-r cpu] [-w cpu] [-f size]\n"
                      << "\t-w cpu affinity for read thread, default = 1\n"
                      << "\t-r cpu affinity for read thread, default = 3\n"
                      << "\t-f tsc frequency obtained from dmesg, default value only makes sense on my machine\n";
            exit(EXIT_FAILURE);
        }
    }

    std::cerr << "Reader cpu: " << g_reader_cpu << "\n"
              << "Writer cpu: " << g_writer_cpu << "\n";

    std::thread w(writer);
    std::thread r(reader);
    w.join();
    r.join();
};