const std = @import("std");
const http = @import("http.zig");
const camera = @import("camera.zig");
const command = @import("command.zig");

const log = std.log.scoped(.main);

fn signalHandler(sig: c_int) callconv(.C) void {
    _ = sig;

    camera.stopCamera();
    http.stopHttpServer();
}

fn installSignalHandler() !void {
    const sa: std.os.Sigaction = .{
        .handler = .{ .handler = signalHandler },
        .flags = std.os.SA.RESTART,
        .mask = std.os.empty_sigset,
    };

    try std.os.sigaction(std.os.SIG.INT, &sa, null);
    try std.os.sigaction(std.os.SIG.TERM, &sa, null);
}

pub fn main() !void {
    try installSignalHandler();

    var gpa = std.heap.GeneralPurposeAllocator(.{}){};
    defer std.debug.assert(gpa.deinit() == .ok);
    const allocator = gpa.allocator();

    const httpThread = try std.Thread.spawn(.{}, http.runHttpServer, .{allocator});
    const eventsThread = try std.Thread.spawn(.{}, camera.getEventsThread, .{});

    camera.processCommands();

    httpThread.join();
    eventsThread.join();
}
