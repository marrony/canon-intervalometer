const std = @import("std");
const camera = @import("camera.zig");
const log = std.log.scoped(.command);

pub const Command = union(enum) {
    GetEvent: void,
    Initialize: void,
    Deinitialize: void,
    Connect: void,
    Disconnect: void,
    TakePicture: void,
    StartShooting: void,
    StopShooting: void,
    Terminate: void,

    pub fn execute(cmd: Command) void {
        const ret = switch (cmd) {
            .GetEvent => camera.getEvents(),
            .Initialize => camera.initializeCamera(),
            .Deinitialize => camera.deinitializeCamera(),
            .Connect => camera.connect(),
            .Disconnect => camera.disconnect(),
            .TakePicture => camera.takePicture(),
            .StartShooting => camera.startShooting(),
            .StopShooting => camera.stopShooting(),
            .Terminate => camera.terminate(),
        };

        ret catch |err| {
            log.err("command error: {}\n", .{err});
            if (@errorReturnTrace()) |trace| {
                std.debug.dumpStackTrace(trace.*);
            }
        };
    }
};
