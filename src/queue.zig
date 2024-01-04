const std = @import("std");

pub fn DispatchQueue(comptime T: type, comptime Size: usize) type {
    const FnType = fn (data: T) anyerror!void;

    const Item = struct {
        func: *const FnType,
        data: T,
    };

    return struct {
        items: [Size]Item = undefined,
        size: usize = 0,
        quit: bool = false,
        mutex: std.Thread.Mutex = .{},
        cond: std.Thread.Condition = .{},

        const Self = @This();

        pub fn quitDispatcher(self: *Self) void {
            self.mutex.lock();
            defer self.mutex.unlock();

            self.quit = true;
            self.cond.signal();
        }

        pub fn dispatch(self: *Self, comptime func: FnType, data: T) bool {
            self.mutex.lock();

            const size = self.size;

            defer {
                self.mutex.unlock();

                if (size != self.size)
                    self.cond.signal();
            }

            if (self.size < Size) {
                self.items[self.size] = .{
                    .func = func,
                    .data = data,
                };
                self.size += 1;
                return true;
            }

            return false;
        }

        pub fn handler(self: *Self) void {
            self.mutex.lock();
            defer self.mutex.unlock();

            while (!self.quit) {
                if (self.size > 0 and !self.quit) {
                    const item = self.items[0];

                    for (1..self.size) |idx| {
                        self.items[idx - 1] = self.items[idx];
                    }

                    self.size -= 1;

                    self.mutex.unlock();

                    item.func(item.data) catch unreachable;

                    self.mutex.lock();
                } else if (!self.quit) {
                    while (self.size == 0 and !self.quit)
                        self.cond.wait(&self.mutex);
                }
            }
        }
    };
}

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
