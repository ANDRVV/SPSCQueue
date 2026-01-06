# SPSCQueue
A single producer single consumer wait-free and lock-free fixed size queue written in Zig, inspired by [rigtorp's](https://github.com/rigtorp/SPSCQueue/tree/master) implementation in C++ and faster than it.

## Implementation

The implementation is intuitive, aiming for clarity and maximum performance. It also features `recommendedSlots()` for selecting the "sweet spot" for slots.

# Zig

## Example

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

## API

```zig
pub fn initBuffer(buf: []T) Self
pub fn initCapacity(allocator: std.mem.Allocator, slots: usize) error{OutOfMemory}!Self
pub fn deinit(self: *Self, allocator: std.mem.Allocator) void

pub fn push(self: *Self, value: T) void
pub fn pop(self: *Self) T
pub fn tryPush(self: *Self, value: T) bool
pub fn tryPop(self: *Self) ?T

pub inline fn size(self: *Self) usize
pub inline fn isEmpty(self: *Self) bool
```

# C++

## Example

```cpp
size_t slots = recommendedSlots<uint64_t>();

SPSCQueue<uint64_t> q(slots);

for (uint64_t i = 0; i < 1000; ++i) {
    q.push(i);
    auto value = q.pop();
    (void)value;
}
```

## API

```cpp
template<typename T>
static constexpr size_t recommendedSlots();
template<typename T>
explicit SPSCQueue(size_t slots);

void push(const T& value);
T pop();
bool tryPush(const T& value);
bool tryPop(T& out);

size_t size() const;
bool isEmpty() const;
```

## Benchmarks

(see benchmark files on src/zig and src/c++)
Benchmark results from 12th Gen Intel(R) Core(TM) i7-12700H with `-O3` and CPU affinity settings on every thread:

| Queue                           | Throughput (ops/ms) | Latency RTT (ns) |
| ------------------------------- | ------------------: | ---------------: |
| SPSCQueue Zig (Andrea Vaccaro)  | (best-case) 1107122 |  (best-case) 168 |
| SPSCQueue C++ (Andrea Vaccaro)  | (best-case) 1361409 |  (best-case) 165 |
| SPSCQueue (rigtorp)             |              166279 |              206 |
| boost::lockfree::spsc           |              258024 |              224 |

## About

This project was created by Andrea Vaccaro <[vaccaro.andrea45@gmail.com](mailto:vaccaro.andrea45@gmail.com)>.
