#include <algorithm>
#include <array>
#include <iostream>
#include <mutex>
#include <condition_variable>
#include <random>
#include <thread>
#include <pthread.h>
#include <unistd.h>

int g_writer_cpu = 1;
int g_reader_cpu = 3;
int g_cache_size_bytes = 64*1024;
int g_memory_size_bytes = 1024*1024*1024;

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

struct Block
{
    Block* next = nullptr;
    char pad[56];
};
Block* head = 0;

std::mutex g_lock;
std::condition_variable g_cond;
enum State {
    READ,
    WRITE,
    DONE
};
State g_state = State::WRITE;

void init(int bytes)
{
    int count = bytes / sizeof(Block);
    if (! count) count = 1;
    Block* chain = new Block[count];

    std::vector<size_t> indexes(count);
    std::iota(std::begin(indexes), std::end(indexes), 0);
    std::shuffle(std::begin(indexes), std::end(indexes), std::mt19937{std::random_device{}()});    
    for (int i=0; i<count-1; ++i)
    {
        chain[indexes[i]].next = &chain[indexes[i+1]];
    }
    
    head = &chain[indexes[0]];
    //std::cout << "created chain of " << count << " blocks at " << std::hex << head << "\n";
}

static void writer()
{
    set_affinity(g_writer_cpu);

    int bytes_processed = 0;
    while (bytes_processed < g_memory_size_bytes)
    {
        init(g_cache_size_bytes);
        bytes_processed += g_cache_size_bytes;

        {
            std::unique_lock<std::mutex> lock(g_lock);
            g_state = READ;
            g_cond.notify_all();
            g_cond.wait(lock, [](){ return g_state == WRITE; });
        }
    }

    std::unique_lock<std::mutex> lock(g_lock);
    g_state = DONE;
    g_cond.notify_all();
}

static void reader()
{
    set_affinity(g_reader_cpu);

    int64_t total_cycles = 0;
    int64_t total_events = 0;

    while (true)
    {
        {
            std::unique_lock<std::mutex> lock(g_lock);
            g_cond.wait(lock, [](){ return g_state == READ || g_state == DONE; });
            if (g_state == DONE) break;
        }

        //std::cout << "reading chain at " << std::hex << head << "\n";

        Block* cur = head;
        auto start = rdtsc();        
        while (cur)
        {
            cur = cur->next;
            ++total_events;
        }
        auto end = rdtsc();
        total_cycles += end - start;

        {
            std::unique_lock<std::mutex> lock(g_lock);
            g_state = WRITE;
            g_cond.notify_all();
        }
    }

    std::cout << total_cycles / total_events << "\n";
    //std::cerr << total_cycles << " cycles and " << total_events << " events\n"
    //          << total_cycles / total_events << " cycles / element \n";
}

int main(int argc, char** argv)
{
    int opt;
    while ( (opt = getopt(argc, argv, "r:w:c:")) != -1)
    {
        switch (opt)
        {
        case 'r':
            g_reader_cpu = atoi(optarg);
            break;
        case 'w':
            g_writer_cpu = atoi(optarg);
            break;
        case 'c':
            g_cache_size_bytes = atoi(optarg);
            break;
        default:
            std::cerr << "Unknown option " << optopt 
                      << "\nUsage: " <<  argv[0] << " -r cpu -w cpu -s size\n";
            exit(EXIT_FAILURE);
        }
    }

    std::cerr << "Reader cpu: " << g_reader_cpu << "\n"
              << "Writer cpu: " << g_writer_cpu << "\n"
              << "Cache size: " << g_cache_size_bytes << " bytes\n";

    std::thread w(writer);
    std::thread r(reader);
    w.join();
    r.join();
};