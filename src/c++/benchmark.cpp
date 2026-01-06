#include <iostream>
#include <chrono>
#include <thread>

#include "queue.cpp"

void pinToCore(size_t core_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    if (sched_setaffinity(0, sizeof(cpuset), &cpuset) != 0) {
        perror("sched_setaffinity");
        std::terminate();
    }
}

constexpr size_t iterations = 10'000'000;
size_t slots = recommendedSlots<uint64_t>();

inline void doNotOptimizeAway(uint64_t& val) {
    asm volatile("" : "+r,m"(val) : : "memory");
}

void producerThroughput(SPSCQueue<uint64_t>* q, size_t core) {
    pinToCore(core);
    for (uint64_t i = 0; i < iterations; ++i) {
        q->push(i);
    }
}

void consumerThroughput(SPSCQueue<uint64_t>* q, size_t core) {
    pinToCore(core);
    for (uint64_t i = 0; i < iterations; ++i) {
        uint64_t val = q->pop();
        doNotOptimizeAway(val);
    }
}

void producerRTT(SPSCQueue<uint64_t>* q1, SPSCQueue<uint64_t>* q2, size_t core) {
    pinToCore(core);
    for (uint64_t i = 0; i < iterations; ++i) {
        q1->push(i);
        uint64_t x = q2->pop();
        doNotOptimizeAway(x);
    }
}

void consumerRTT(SPSCQueue<uint64_t>* q1, SPSCQueue<uint64_t>* q2, size_t core) {
    pinToCore(core);
    for (uint64_t i = 0; i < iterations; ++i) {
        uint64_t val = q1->pop();
        q2->push(val);
    }
}

int main() {
    size_t const core1 = 0, core2 = 1;

    {
        SPSCQueue<uint64_t> queue(slots);

        std::thread consumer_thread(consumerThroughput, &queue, core1);
        auto start = std::chrono::high_resolution_clock::now();
        producerThroughput(&queue, core2);
        consumer_thread.join();
        auto end = std::chrono::high_resolution_clock::now();

        uint64_t elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        uint64_t ops_per_ms = (iterations * 1'000'000) / elapsed_ns;

        std::cout << ops_per_ms << " ops/ms\n";
    }
    {
        SPSCQueue<uint64_t> q1(slots);
        SPSCQueue<uint64_t> q2(slots);

        std::thread consumer_thread(consumerRTT, &q1, &q2, core1);
        auto start = std::chrono::high_resolution_clock::now();
        producerRTT(&q1, &q2, core2);
        auto end = std::chrono::high_resolution_clock::now();
        consumer_thread.join();

        uint64_t elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        std::cout << (elapsed_ns / iterations) << " ns RTT\n";
    }
    return 0;
}