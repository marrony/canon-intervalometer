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

    pub fn execute(cmd: Command) anyerror!void {
        switch (cmd) {
            .GetEvent => camera.getEvents(),
            .Initialize => try camera.initializeCamera(),
            .Deinitialize => try camera.deinitializeCamera(),
            .Connect => try camera.connect(),
            .Disconnect => try camera.disconnect(),
            .TakePicture => try camera.takePicture(),
            .StartShooting => try camera.startShooting(),
            .StopShooting => try camera.stopShooting(),
            .Terminate => try camera.terminate(),
        }
    }
};
