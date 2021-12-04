#include <algorithm>
#include <atomic>
#include <iostream>
#include <thread>
#include <vector>
#include <cstdint>
#include <pthread.h>
#include <unistd.h>

constexpr int g_samples = 1000000;
int g_writer_cpu = 1;
int g_reader_cpu = 3;
int g_tsc_khz = 2207999;
int g_rdtsc_lat = 0;
bool g_rdtsc_lat_adjust = true;

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

struct Line
{
    std::atomic_uint64_t ts{0};
    char pad[56];
};

struct State
{
    Line lines[g_samples];
    std::atomic_bool next{true};
    std::atomic_bool stop{false};
} g_state;

void writer()
{
    set_affinity(1);

    for (size_t i=0; i<g_samples; ++i)
    {
        g_state.next = false;
        g_state.lines[i].ts = rdtsc();
        while (! g_state.next) ;
    }
}

void measure_rdstc()
{
    uint64_t prev = 0;
    int i = 10000000;
    std::vector<int> res;
    while (--i)
    {
        auto now = rdtsc();
        res.push_back(now - prev);
        prev = now;
    }
    std::sort(begin(res), end(res));
    std::cout << "rdstc latency: min=" << res.front()
              << " median=" << res[res.size()/2]
              << " max=" << res.back() << '\n';
    g_rdtsc_lat = res[res.size()/2];
    std::cout << "rdtsc latency = " << g_rdtsc_lat << '\n';
}

void reader()
{
    set_affinity(3);

    if (g_rdtsc_lat_adjust)
        measure_rdstc();

    std::vector<int64_t> results;

    for (size_t i=0; i<g_samples; ++i)
    {
        uint64_t ts;
        while ((ts = g_state.lines[i].ts) == 0) continue;

        const auto now = rdtsc();
        results.push_back(now - ts);
        g_state.next = true;
    }

    std::sort(begin(results), end(results));
    auto report = [](const char* name, uint64_t cycles){
        std::cout << name << ": " << cycles_to_ns(cycles) << " ns (" << cycles << ")\n";
    };
    report("min", results.front() - g_rdtsc_lat);
    report("median", results[results.size()/2] - g_rdtsc_lat);
    report("max", results.back() - g_rdtsc_lat);
}

int main(int argc, char** argv)
{
    int opt;
    while ((opt = getopt(argc, argv, "r:w:f:a")) != -1)
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
        case 'a':
            g_rdtsc_lat_adjust = false;
            break;
        default:
            std::cerr << "Usage: " <<  argv[0] << " [-r cpu] [-w cpu] [-f size] -a\n"
                      << "\t-w cpu affinity for read thread, default = 1\n"
                      << "\t-r cpu affinity for read thread, default = 3\n"
                      << "\t-f tsc frequency obtained from dmesg, default value only makes sense on my machine\n"
                      << "\t-a do not substract estimated rdtsc latency from results\n";
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