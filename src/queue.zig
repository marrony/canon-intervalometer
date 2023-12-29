const std = @import("std");

/// A thread safe queue
pub fn Queue(comptime T: type, comptime Size: usize) type {
    const FnType = fn (arg: T) anyerror!void;

    const SlotType = enum(u1) {
        Free,
        Used,
    };

    return struct {
        buffer: [Size]T = undefined,
        slots: [Size]SlotType = [1]SlotType{.Free} ** Size,
        size: usize = 0,
        nextin: usize = 0,
        nextout: usize = 0,
        mutex: std.Thread.Mutex = .{},
        produced: std.Thread.Condition = .{},
        consumed: std.Thread.Condition = .{},
        sync: std.Thread.Condition = .{},

        const Self = @This();

        pub fn full(self: *Self) bool {
            self.mutex.lock();
            defer self.mutex.unlock();

            return self.size == Size;
        }

        pub fn empty(self: *Self) bool {
            self.mutex.lock();
            defer self.mutex.unlock();

            return self.size == 0;
        }

        pub fn putAsync(self: *Self, value: T) void {
            self.mutex.lock();
            defer self.mutex.unlock();

            _ = self._put(value);
        }

        pub fn putSync(self: *Self, value: T) void {
            self.mutex.lock();
            defer self.mutex.unlock();

            const nextin = self._put(value);

            self.slots[nextin] = .Used;
            while (self.slots[nextin] == .Used)
                self.sync.wait(&self.mutex);
        }

        fn _put(self: *Self, value: T) usize {
            while (self.size >= Size) {
                self.consumed.wait(&self.mutex);
            }

            const nextin = self.nextin;
            self.nextin = (nextin + 1) % Size;
            self.size += 1;

            self.buffer[nextin] = value;

            self.produced.signal();

            return nextin;
        }

        pub fn get(self: *Self, timeout_ns: u64, comptime callback: FnType) !void {
            self.mutex.lock();
            defer self.mutex.unlock();

            const nextout = try self._get(timeout_ns);
            defer {
                self.slots[nextout] = .Free;
                self.sync.signal();
            }

            try callback(self.buffer[nextout]);
        }

        fn _get(self: *Self, timeout_ns: u64) !usize {
            while (self.size == 0) {
                try self.produced.timedWait(&self.mutex, timeout_ns);
            }

            const nextout = self.nextout;
            self.nextout = (nextout + 1) % Size;
            self.size -= 1;

            self.consumed.signal();
            return nextout;
        }
    };
}

// pub fn AsyncQueue(comptime T: type, comptime Size: usize) type {
//     const FnType = fn (arg: T) void;
//
//     const SlotType = enum(u1) {
//         Free,
//         Used,
//     };
//
//     return struct {
//         queue: Queue(T, Size) = .{},
//         slots: [Size]SlotType = [1]SlotType{.Free} ** Size,
//         mutex: std.Thread.Mutex = .{},
//         cond: std.Thread.Condition = .{},
//
//         const Self = @This();
//
//         pub fn consume(self: *Self, timeout_ns: u64, comptime callback: FnType) !void {
//             // if (!self.mutex.tryLock()) return;
//             self.mutex.lock();
//             defer self.mutex.unlock();
//
//             const nextout = self.queue.nextout;
//             callback(try self.queue.get(timeout_ns));
//
//             self.slots[nextout] = .Free;
//             self.cond.signal();
//         }
//
//         pub fn postSync(self: *Self, value: T) void {
//             self.mutex.lock();
//             defer self.mutex.unlock();
//
//             const nextin = self.queue.nextin;
//             self.queue.put(value);
//
//             self.slots[nextin] = .Used;
//             while (self.slots[nextin] == .Used)
//                 self.cond.wait(&self.mutex);
//         }
//
//         pub fn postAsync(self: *Self, value: T) void {
//             self.mutex.lock();
//             defer self.mutex.unlock();
//
//             self.queue.put(value);
//         }
//     };
// }
