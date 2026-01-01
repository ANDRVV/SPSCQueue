const std = @import("std");

const SPSCQueue = @import("src/queue.zig").SPSCQueue;
const recommendedSlots = @import("src/queue.zig").recommendedSlots;

const slots: u64 = recommendedSlots(u64);
const iterations: u64 = 10_000_0000;

const core1: usize = 1;
const core2: usize = 2;

fn pinToCore(core_id: usize) void {
    const CPUSet = std.bit_set.ArrayBitSet(usize, std.os.linux.CPU_SETSIZE * @sizeOf(usize));
    var set: CPUSet = .initEmpty();
    set.set(core_id);
    std.os.linux.sched_setaffinity(0, @ptrCast(&set.masks)) catch @panic("err");
}

fn producerThroughput(queue: *SPSCQueue(u64), core: usize) void {
    pinToCore(core);

    var i: u64 = 0;
    while (i < iterations) : (i += 1) {
        queue.push(i);
    }
}

fn consumerThroughput(queue: *SPSCQueue(u64), core: usize) void {
    pinToCore(core);

    var i: u64 = 0;
    while (i < iterations) : (i += 1) {
        const value = queue.pop();
        std.mem.doNotOptimizeAway(value);
    }
}

fn producerRTT(q1: *SPSCQueue(u64), q2: *SPSCQueue(u64), core: usize) void {
    pinToCore(core);

    var i: u64 = 0;
    while (i < iterations) : (i += 1) {
        q1.push(i);
        _ = q2.pop();
    }
}

fn consumerRTT(q1: *SPSCQueue(u64), q2: *SPSCQueue(u64), core: usize) void {
    pinToCore(core);

    var i: u64 = 0;
    while (i < iterations) : (i += 1) {
        const value = q1.pop();
        q2.push(value);
    }
}

pub fn main() !void {
    const allocator = std.heap.page_allocator;

    {
        var queue: SPSCQueue(u64) = try .initCapacity(allocator, slots);
        defer queue.deinit(allocator);

        const th = try std.Thread.spawn(.{}, consumerThroughput, .{ &queue, core1 });

        var timer = try std.time.Timer.start();
        producerThroughput(&queue, core2);
        th.join();
        const elapsed = timer.read();

        const ops_per_ms = @divFloor(iterations * std.time.ns_per_ms, elapsed);
        std.debug.print("{d} ops/ms\n", .{ops_per_ms});
    }

    {
        var q1: SPSCQueue(u64) = try .initCapacity(allocator, slots);
        defer q1.deinit(allocator);
        var q2: SPSCQueue(u64) = try .initCapacity(allocator, slots);
        defer q2.deinit(allocator);

        const th = try std.Thread.spawn(.{}, consumerRTT, .{ &q1, &q2, core1 });

        var timer = try std.time.Timer.start();
        producerRTT(&q1, &q2, core2);
        const elapsed = timer.read();

        th.join();

        std.debug.print("{d} ns RTT\n", .{@divFloor(elapsed, iterations)});
    }
}
