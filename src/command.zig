const std = @import("std");
const camera = @import("camera.zig");
const log = std.log.scoped(.command);

pub const Command = union(enum) {
    GetEvent: void,
    Initialize: Synced,
    Deinitialize: void,
    Connect: void,
    Disconnect: void,
    TakePicture: Synced,
    StartShooting: Synced,
    StopShooting: void,
    Terminate: void,

    const Synced = struct {
        done: ?*std.atomic.Value(u32) = null,

        pub fn syncDone(self: *const @This()) void {
            if (self.done) |done| {
                done.store(1, .Release);
                std.Thread.Futex.wake(done, 1);
            }
        }
    };

    pub fn execute(cmd: Command) anyerror!void {
        switch (cmd) {
            .GetEvent => camera.getEvents(),
            .Initialize => |sync| {
                defer sync.syncDone();
                try camera.initializeCamera();
            },
            .Deinitialize => try camera.deinitializeCamera(),
            .Connect => try camera.connect(),
            .Disconnect => try camera.disconnect(),
            .TakePicture => |sync| {
                defer sync.syncDone();
                try camera.takePicture();
            },
            .StartShooting => |sync| {
                defer sync.syncDone();
                try camera.startShooting();
            },
            .StopShooting => try camera.stopShooting(),
            .Terminate => try camera.terminate(),
        }
    }
};
