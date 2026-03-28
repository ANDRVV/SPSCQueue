/* 
 * MIT License
 *
 * Copyright (c) Andrea Vaccaro
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Source: https://github.com/ANDRVV/SPSCQueue
 * A single-producer, single-consumer lock-free queue using a ring buffer.
 */

#include <atomic>
#include <new>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

#if defined(_MSC_VER)
    #include <intrin.h>
#elif defined(__GNUC__) || defined(__clang__)
    #include <xmmintrin.h>
#endif

#ifdef __cpp_lib_hardware_interference_size
    #define CACHE_LINE std::hardware_destructive_interference_size
#else
    #define CACHE_LINE 64
#endif

inline __attribute__((always_inline)) void
spinLoopHint() noexcept {
#if defined(__x86_64__) || defined(_M_X64)
    _mm_pause();
#elif defined(__aarch64__)
    asm volatile("yield" ::: "memory");
#else
    std::this_thread::yield();
#endif
}

/* Returns recommended slots with alignment size of L2 cache. */
template<typename T> [[nodiscard]] constexpr size_t
recommendedSlots() {
    constexpr size_t sweet_spot = 4096 * CACHE_LINE;
    size_t slots = sweet_spot / sizeof(T);
    assert((slots & (slots - 1)) == 0 && slots >= 2);
    return slots;
}

template<typename T>
class alignas(CACHE_LINE) SPSCQueue {
    static_assert(std::is_nothrow_destructible<T>::value,
                  "T must be nothrow destructible");
    static_assert(
        std::is_nothrow_move_constructible<T>::value ||
        std::is_nothrow_copy_constructible<T>::value,
        "T must be nothrow movable or copyable");

private:
    std::vector<T> items;
    size_t mask;

    /* producer and consumer are aligned to cache line
     * size in order to avoid false sharing */
    alignas(CACHE_LINE) std::atomic<size_t> producer{0};
    alignas(CACHE_LINE) std::atomic<size_t> consumer{0};

    /* cursors as cache is used to reduce MESI protocol traffic
     * between shared caches, this improve throughput */
    alignas(CACHE_LINE) size_t push_cursor_cache = 0;
    alignas(CACHE_LINE) size_t pop_cursor_cache = 0;

    inline __attribute__((always_inline)) size_t
    nextIndex(size_t i) const noexcept {
        return (i + 1) & mask;
    }

public:
    explicit SPSCQueue(size_t slots_) : items(slots_), mask(slots_ - 1) {
        assert((slots_ & (slots_ - 1)) == 0);
    }

    inline void
    push(const T& value) {
        size_t const index = producer.load(std::memory_order_relaxed);
        size_t const next = nextIndex(index);

        while (next == push_cursor_cache) {
            /* In this line, the `asm pause` function is commented out,
             * assuming the consumer is hotter than the producer.
             * Uncomment this line below if the producer has a higher throughput.
             * spinLoopHint(); */
            push_cursor_cache = consumer.load(std::memory_order_acquire);
        }

        items[index] = value;
        producer.store(next, std::memory_order_release);
    }

    [[nodiscard]] inline bool
    tryPush(const T& value) {
        size_t const index = producer.load(std::memory_order_relaxed);
        size_t const next = nextIndex(index);

        if (next == push_cursor_cache) {
            push_cursor_cache = consumer.load(std::memory_order_acquire);
            if (next == push_cursor_cache) return false;
        }

        items[index] = value;
        producer.store(next, std::memory_order_release);
        return true;
    }

    [[nodiscard]] inline T
    pop() {
        size_t const index = consumer.load(std::memory_order_relaxed);
        while (index == pop_cursor_cache) {
            spinLoopHint();
            pop_cursor_cache = producer.load(std::memory_order_acquire);
        }

        T const value = items[index];
        consumer.store(nextIndex(index), std::memory_order_release);
        return value;
    }

    inline void
    skip() {
        (void)pop();
    }

    [[nodiscard]] inline bool
    tryPop(T& out) {
        size_t const index = consumer.load(std::memory_order_relaxed);
        if (index == pop_cursor_cache) {
            pop_cursor_cache = producer.load(std::memory_order_acquire);
            if (index == pop_cursor_cache) return false;
        }

        out = items[index];
        consumer.store(nextIndex(index), std::memory_order_release);
        return true;
    }

    [[nodiscard]] inline size_t
    count() const noexcept {
        size_t const write_index = producer.load(std::memory_order_acquire);
        size_t const read_index = consumer.load(std::memory_order_acquire);
        return (write_index - read_index) & mask;
    }

    [[nodiscard]] inline bool
    isEmpty() const noexcept {
        size_t const write_index = producer.load(std::memory_order_acquire);
        size_t const read_index = consumer.load(std::memory_order_acquire);
        return write_index == read_index;
    }
};
