# SPSCQueue
A single-producer single-consumer wait-free and lock-free fixed size queue, inspired by [rigtorp's](https://github.com/rigtorp/SPSCQueue/tree/master) implementation and faster than it.
Designed to minimize latency and maximize processing speed without overengineering.

## Optimizations

* **Branchless index wrap**:
Using `(idx + 1) & (size - 1)` instead of `if (idx == capacity) idx = 0` eliminates
a branch in the hot path, removing a potential pipeline stall on every push and pop.
This comes with a trade-off: capacity must be a power of two. In practice this is a
reasonable constraint: use `recommendedSlots<T>()` to get a sensible default.

* **`pause` in the spin loop**:
When the queue is empty, the consumer spins with the `pause` instruction. This signals the CPU that the thread is in a spin-wait loop, reducing memory bus contention and improving throughput. The substantial difference is the correct positioning of the pause, see the `pop()` implementation

* **No slot padding**:
Extra dummy elements at the start and end of the internal array waste memory and reduce cache density.
Slots are kept tightly packed in a plain `std::vector`, making them more likely to remain in cache under load.

* **No slack slot**:
Keeping a sentinel slot always empty wastes capacity. The distinction between full and
empty is implicit in the difference between the two cursors, with no wasted slot.

**See the resulting GCC x86-64 assembly on https://godbolt.org/z/r7qTK5qPY.**

# Benchmarks

To run local benchmark:
* For **Zig** implementation: `zig run src/zig/benchmark.zig -O ReleaseFast -fomit-frame-pointer`
* For **C++** implementation: `g++ src/cpp/benchmark.cpp -o benchmark -O3; ./benchmark`

Tested benchmark on `Intel i7-12700H` with WSL2:

| Queue (C++ version)        | Throughput (ops/ms) | Latency RTT (ns) |
| -------------------------- | ------------------: | ---------------: |
| SPSCQueue (Andrea Vaccaro) |       (p50)  943597 |        (p50) 142 |
| SPSCQueue (Andrea Vaccaro) |       (p90) 1049140 |        (p90) 147 |
| SPSCQueue (Andrea Vaccaro) |       (p99) 1068306 |        (p99) 152 |

| Queue (Zig version)        | Throughput (ops/ms) | Latency RTT (ns) |
| -------------------------- | ------------------: | ---------------: |
| SPSCQueue (Andrea Vaccaro) |       (p50) 1008548 |        (p50) 179 |
| SPSCQueue (Andrea Vaccaro) |       (p90) 1350722 |        (p90) 189 |
| SPSCQueue (Andrea Vaccaro) |       (p99) 1357763 |        (p99) 190 |

Other queues:

| Queue                 | Throughput (ops/ms) | Latency RTT (ns) |
| --------------------- | ------------------: | ---------------: |
| SPSCQueue (rigtorp)   |        (avg) 166279 |        (avg) 206 |
| boost::lockfree::spsc |        (avg) 258024 |        (avg) 224 |

# Zig
**Example**
```zig
const slots: u64 = queue.recommendedSlots(u64);

var q: queue.SPSCQueue(u64) = try .initCapacity(allocator, slots);
defer q.deinit(allocator);

var i: u64 = 0;
while (i < 1000) : (i += 1) {
    q.push(i);
    _ = q.pop();
}
```

**API**
```zig
pub fn initBuffer(buf: []T) Self
pub fn initCapacity(allocator: std.mem.Allocator, slots: usize) error{OutOfMemory}!Self
pub fn deinit(self: *Self, allocator: std.mem.Allocator) void

pub fn push(self: *Self, value: T) void // blocking push
pub fn pop(self: *Self) T // blocking pop
pub fn tryPush(self: *Self, value: T) bool // non-blocking push
pub fn tryPop(self: *Self) ?T // non-blocking pop

pub inline fn count(self: *Self) usize
pub inline fn isEmpty(self: *Self) bool
```

# C++
**Example**
```cpp
size_t slots = recommendedSlots<uint64_t>();

SPSCQueue<uint64_t> q(slots);

for (uint64_t i = 0; i < 1000; ++i) {
    q.push(i);
    auto value = q.pop();
    (void)value;
}
```

**API**
```cpp
template<typename T>
static constexpr size_t recommendedSlots();
template<typename T>
explicit SPSCQueue(size_t slots);

void skip(); /* pop() without returning value */
void push(const T& value); /* blocking push */
T pop(); /* blocking pop */
bool tryPush(const T& value); /* non-blocking push */
bool tryPop(T& out); /* non-blocking pop */

size_t count() const;
bool isEmpty() const;
```

## About

This project was created by Andrea Vaccaro <[vaccaro.andrea45@gmail.com](mailto:vaccaro.andrea45@gmail.com)>.
