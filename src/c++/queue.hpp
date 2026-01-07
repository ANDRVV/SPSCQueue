// MIT License
//
// Copyright (c) Andrea Vaccaro
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//
// Source: https://github.com/ANDRVV/SPSCQueue
// A single-producer, single-consumer lock-free queue using a ring buffer.

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
    struct alignas(CACHE_LINE) AtomicCursor {
        std::atomic<size_t> cursor{0};
        char _padding[CACHE_LINE - sizeof(std::atomic<size_t>)];
    };

    struct alignas(CACHE_LINE) Cursor {
        size_t cursor = 0;
        char _padding[CACHE_LINE - sizeof(size_t)];
    };

    std::vector<T> items;
    /* producer and consumer are aligned to cache line
     * size in order to avoid false sharing */
    AtomicCursor producer;
    AtomicCursor consumer;

    Cursor push_cursor_cache;
    Cursor pop_cursor_cache;

    inline size_t
    nextIndex(size_t i) const noexcept {
        return (i + 1) & (items.size() - 1);
    }

public:
    explicit SPSCQueue(size_t slots_) : items(slots_) {
        assert((slots_ & (slots_ - 1)) == 0);
    }

    inline void
    push(const T& value) {
        size_t const index = producer.cursor.load(std::memory_order_relaxed);
        size_t const next = nextIndex(index);

        while (next == push_cursor_cache.cursor) {
            /* in this line, asm pause is commented out assuming the consumer is more hot
             * than the producer. uncomment this line if the producer have more throughput.
             * _busy_wait(); */
            push_cursor_cache.cursor = consumer.cursor.load(std::memory_order_acquire);
        }

        items[index] = value;
        producer.cursor.store(next, std::memory_order_release);
    }

    [[nodiscard]] inline bool
    tryPush(const T& value) {
        size_t const index = producer.cursor.load(std::memory_order_relaxed);
        size_t const next = nextIndex(index);

        if (next == push_cursor_cache.cursor) {
            push_cursor_cache.cursor = consumer.cursor.load(std::memory_order_acquire);
            if (next == push_cursor_cache.cursor) return false;
        }

        items[index] = value;
        producer.cursor.store(next, std::memory_order_release);
        return true;
    }

    [[nodiscard]] inline T
    pop() {
        size_t const index = consumer.cursor.load(std::memory_order_relaxed);
        while (index == pop_cursor_cache.cursor) {
            _mm_pause();
            pop_cursor_cache.cursor = producer.cursor.load(std::memory_order_acquire);
        }

        T const value = items[index];
        consumer.cursor.store(nextIndex(index), std::memory_order_release);
        return value;
    }

    [[nodiscard]] inline bool
    tryPop(T& out) {
        size_t const index = consumer.cursor.load(std::memory_order_relaxed);
        if (index == pop_cursor_cache.cursor) {
            pop_cursor_cache.cursor = producer.cursor.load(std::memory_order_acquire);
            if (index == pop_cursor_cache.cursor) return false;
        }

        out = items[index];
        consumer.cursor.store(nextIndex(index), std::memory_order_release);
        return true;
    }

    [[nodiscard]] inline size_t
    size() const noexcept {
        size_t const write_index = producer.cursor.load(std::memory_order_acquire);
        size_t const read_index = consumer.cursor.load(std::memory_order_acquire);
        return (write_index - read_index) & (items.size() - 1);
    }

    [[nodiscard]] inline bool
    isEmpty() const noexcept {
        size_t const write_index = producer.cursor.load(std::memory_order_acquire);
        size_t const read_index = consumer.cursor.load(std::memory_order_acquire);
        return write_index == read_index;
    }
};
