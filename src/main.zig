const std = @import("std");
const http = @import("http.zig");
const camera = @import("camera.zig");
const command = @import("command.zig");

fn getEvents() void {
    while (!camera.g_dispatch.quit) {
        _ = camera.g_dispatch.dispatch(command.Command.execute, .GetEvent);
        std.time.sleep(500 * std.time.us_per_s);
    }
}

pub fn main() !void {
    const httpThread = try std.Thread.spawn(.{}, http.runHttpServer, .{});
    const eventsThread = try std.Thread.spawn(.{}, getEvents, .{});

    camera.g_dispatch.handler();

    httpThread.join();
    eventsThread.join();
}
