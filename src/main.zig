const std = @import("std");
const http = @import("http.zig");
const camera = @import("camera.zig");
const command = @import("command.zig");

pub fn main() !void {
    if (false) {
        //to enable signal handling I need to find a way
        //to abort server.accept() on http thread, nonblocking?

        const sa: std.os.Sigaction = .{
            .handler = .{ .handler = camera.signalHandler },
            .flags = std.os.SA.RESTART,
            .mask = std.os.empty_sigset,
        };

        try std.os.sigaction(std.os.SIG.INT, &sa, null);
        try std.os.sigaction(std.os.SIG.TERM, &sa, null);
    }

    const httpThread = try std.Thread.spawn(.{}, http.runHttpServer, .{});
    const eventsThread = try std.Thread.spawn(.{}, camera.getEventsThread, .{});

    camera.processCommands();

    httpThread.join();
    eventsThread.join();
}
