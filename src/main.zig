const std = @import("std");
const http = @import("http.zig");
const camera = @import("camera.zig");
const log = std.log.scoped(.server);

pub fn main() !void {
    const httpThread = try std.Thread.spawn(.{}, http.runHttpServer, .{});

    camera.process_commands();

    httpThread.join();
}
