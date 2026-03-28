//! MIT License
//!
//! Copyright (c) Andrea Vaccaro
//!
//! Permission is hereby granted, free of charge, to any person obtaining a copy
//! of this software and associated documentation files (the "Software"), to deal
//! in the Software without restriction, including without limitation the rights
//! to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//! copies of the Software, and to permit persons to whom the Software is
//! furnished to do so, subject to the following conditions:
//!
//! The above copyright notice and this permission notice shall be included in all
//! copies or substantial portions of the Software.
//!
//! THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//! IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//! FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//! AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//! LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//! OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
//! SOFTWARE.
//!
//! Source: https://github.com/ANDRVV/SPSCQueue
//! A single-producer, single-consumer lock-free queue using a ring buffer.

const std = @import("std");

const Atomic = std.atomic.Value;
const cache_line = std.atomic.cache_line;

/// Returns recommended slots with alignment size of L2 cache.
pub fn recommendedSlots(comptime T: type) usize {
    const sweet_spot = 4096 * std.atomic.cache_line;
    const slots = sweet_spot / @sizeOf(T);
    std.debug.assert(std.math.isPowerOfTwo(slots));
    return slots;
}

pub fn SPSCQueue(comptime T: type) type {
    return struct {
        const Self = @This();

        items: []T,
        mask: usize,
        // producer and consumer are aligned to cache line
        // size in order to avoid false sharing
        producer: Atomic(usize) align(cache_line) = .init(0),
        consumer: Atomic(usize) align(cache_line) = .init(0),

        push_cursor_cache: usize align(cache_line) = 0,
        pop_cursor_cache: usize align(cache_line) = 0,

        pub fn initBuffer(buf: []T) Self {
            std.debug.assert(std.math.isPowerOfTwo(buf.len));
            return .{ .items = buf, .mask = buf.len - 1 };
        }

        pub fn initCapacity(allocator: std.mem.Allocator, slots: usize) error{OutOfMemory}!Self {
            return .initBuffer(try allocator.alloc(T, slots));
        }

        pub fn deinit(self: *Self, allocator: std.mem.Allocator) void {
            allocator.free(self.items);
        }

        pub fn push(self: *Self, value: T) void {
            const index = self.producer.load(.monotonic);

            const next = self.nextIndex(index);
            while (next == self.push_cursor_cache) {
                // in this line, spinLoopHint is commented out assuming the consumer is more hot
                // than the producer. Uncomment this line if the producer has more throughput.
                // std.atomic.spinLoopHint();
                self.push_cursor_cache = self.consumer.load(.acquire);
            }

            self.items[index] = value;
            self.producer.store(next, .release);
        }

        pub fn tryPush(self: *Self, value: T) bool {
            const index = self.producer.load(.monotonic);

            const next = self.nextIndex(index);
            if (next == self.push_cursor_cache) {
                self.push_cursor_cache = self.consumer.load(.acquire);
                if (next == self.push_cursor_cache) return false;
            }

            self.items[index] = value;
            self.producer.store(next, .release);
            return true;
        }

        pub fn pop(self: *Self) T {
            const index = self.consumer.load(.monotonic);
            while (index == self.pop_cursor_cache) {
                std.atomic.spinLoopHint();
                self.pop_cursor_cache = self.producer.load(.acquire);
            }

            self.consumer.store(self.nextIndex(index), .release);
            return self.items[index];
        }

        pub fn tryPop(self: *Self) ?T {
            const index = self.consumer.load(.monotonic);
            if (index == self.pop_cursor_cache) {
                self.pop_cursor_cache = self.producer.load(.acquire);
                if (index == self.pop_cursor_cache) return null;
            }

            self.consumer.store(self.nextIndex(index), .release);
            return self.items[index];
        }

        pub inline fn size(self: *Self) usize {
            const write_index = self.producer.load(.acquire);
            const read_index = self.consumer.load(.acquire);
            const n = self.items.len;
            return (write_index + n - read_index) % n;
        }

        pub inline fn isEmpty(self: *Self) bool {
            const write_index = self.producer.load(.acquire);
            const read_index = self.consumer.load(.acquire);
            return write_index == read_index;
        }

        inline fn nextIndex(self: *const Self, i: usize) usize {
            return (i + 1) & self.mask;
        }
    };
}

const TestQueue = SPSCQueue(u64);

test "spsc queue single-threaded" {
    var queue: TestQueue = try .initCapacity(std.heap.page_allocator, 1024);

    for (0..1000) |i| {
        queue.push(i);
        try std.testing.expect(queue.pop() == i);
    }
}

fn producerTest(queue: *TestQueue, comptime iterations: comptime_int) void {
    for (0..iterations) |i| queue.push(i);
}

test "spsc queue multi-threaded" {
    const iterations = 1000;
    var queue: TestQueue = try .initCapacity(std.heap.page_allocator, 1024);
    const producer: std.Thread = try .spawn(.{}, producerTest, .{ &queue, iterations });
    for (0..iterations) |i| try std.testing.expect(queue.pop() == i);
    producer.join();
}
