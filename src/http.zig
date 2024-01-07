const std = @import("std");
const camera = @import("camera.zig");
const command = @import("command.zig");

const http = std.http;

const log = std.log.scoped(.server);

const server_addr = "0.0.0.0";
const server_port = 8001;

const HandlerFn = *const fn (response: *http.Server.Response) anyerror!void;

const Handler = struct {
    endpoint: []const u8,
    handler: HandlerFn,
};

const post_handlers = [_]Handler{
    .{ .endpoint = "/api/camera/state/delay", .handler = handle_input_delay },
    .{ .endpoint = "/api/camera/state/iso", .handler = handle_input_iso },
    .{ .endpoint = "/api/camera/state/exposure", .handler = handle_input_exposure },
    .{ .endpoint = "/api/camera/state/interval", .handler = handle_input_interval },
    .{ .endpoint = "/api/camera/state/frames", .handler = handle_input_frames },
    .{ .endpoint = "/api/camera/connect", .handler = handle_camera_connect },
    .{ .endpoint = "/api/camera/disconnect", .handler = handle_camera_disconnect },
    .{ .endpoint = "/api/camera/start-shoot", .handler = handle_camera_start_shoot },
    .{ .endpoint = "/api/camera/stop-shoot", .handler = handle_camera_stop_shoot },
    .{ .endpoint = "/api/camera/take-picture", .handler = handle_camera_take_picture },
};

const get_handlers = [_]Handler{
    .{ .endpoint = "/api/camera/state", .handler = handle_get_state },
    .{ .endpoint = "/api/camera", .handler = handle_get_camera },
    .{ .endpoint = "/assets/*", .handler = handle_get_assets },
    .{ .endpoint = "/", .handler = handle_get_index_html },
};

const InputKind = enum {
    delay,
    interval,
    frames,
};

const Input = struct {
    kind: InputKind,
    value: u32,
    enabled: bool,

    pub fn format(self: *const Input, comptime fmt: []const u8, options: std.fmt.FormatOptions, writer: anytype) !void {
        _ = fmt;
        _ = options;

        try writer.print(
            \\<div class="input-{s}">
        , .{@tagName(self.kind)});

        try writer.print(
            \\<input type="number" name="{0s}" value="{1d}" required hx-validate="true"
            \\ min="0" inputmode="numeric" hx-post="/api/camera/state/{0s}" hx-swap="outerHTML"
            \\ hx-target=".input-{0s}" {2s}/>
        , .{
            @tagName(self.kind),
            self.value,
            if (self.enabled) "" else "disabled ",
        });

        try writer.print("</div>", .{});
    }
};

fn handle_input_delay(response: *http.Server.Response) anyerror!void {
    var body: [32]u8 = undefined;
    const size = try response.readAll(&body);

    if (std.mem.startsWith(u8, body[0..size], "delay=")) {
        if (std.fmt.parseInt(u32, body["delay=".len..size], 10)) |delay| {
            camera.setDelayTime(delay);
        } else |err| {
            return try writeError(response, "Invalid delay input", err);
        }
    }

    var buf: [1024 * 2]u8 = undefined;

    const input = Input{
        .kind = .delay,
        .value = camera.getDelay(),
        .enabled = camera.inputsEnabled(),
    };

    const formatted = std.fmt.bufPrint(&buf, "{}", .{input}) catch |err| {
        return try writeError(response, "Cannot format delay", err);
    };

    try write(response, .ok, formatted);
}

fn handle_input_iso(response: *http.Server.Response) !void {
    var body: [32]u8 = undefined;
    const size = try response.readAll(&body);

    if (std.mem.startsWith(u8, body[0..size], "iso=")) {
        if (std.fmt.parseInt(u32, body["iso=".len..size], 10)) |iso_param| {
            camera.setIsoParam(iso_param);
        } else |err| {
            return try writeError(response, "Invalid ISO input", err);
        }
    }

    var buf: [1024 * 2]u8 = undefined;

    const iso: IsoContent = .{};

    const formatted = std.fmt.bufPrint(&buf, "{}", .{iso}) catch |err| {
        return try writeError(response, "Cannot format ISO", err);
    };

    try write(response, .ok, formatted);
}

fn handle_input_exposure(response: *http.Server.Response) !void {
    var body: [32]u8 = undefined;
    const size = try response.readAll(&body);

    if (std.mem.startsWith(u8, body[0..size], "exposure=")) {
        if (std.fmt.parseInt(u32, body["exposure=".len..size], 10)) |exposure_param| {
            camera.setExposureParam(exposure_param);
        } else |err| {
            return try writeError(response, "Invalid exposure input", err);
        }
    }

    if (std.mem.startsWith(u8, body[0..size], "exposure-custom=")) {
        if (std.fmt.parseInt(u32, body["exposure-custom=".len..size], 10)) |exposure| {
            camera.setExposureTime(exposure);
        } else |err| {
            return try writeError(response, "Invalid exposure input", err);
        }
    }

    var buf: [1024 * 2]u8 = undefined;

    const exposure: ExposureContent = .{};

    const formatted = std.fmt.bufPrint(&buf, "{}", .{exposure}) catch |err| {
        return try writeError(response, "Cannot format exposure", err);
    };

    try write(response, .ok, formatted);
}

fn handle_input_interval(response: *http.Server.Response) !void {
    var body: [32]u8 = undefined;
    const size = try response.readAll(&body);

    if (std.mem.startsWith(u8, body[0..size], "interval=")) {
        if (std.fmt.parseInt(u32, body["interval=".len..size], 10)) |interval| {
            camera.setIntervalTime(interval);
        } else |err| {
            return try writeError(response, "Invalid interval input", err);
        }
    }

    var buf: [1024 * 2]u8 = undefined;

    const input = Input{
        .kind = .interval,
        .value = camera.getInterval(),
        .enabled = camera.inputsEnabled(),
    };

    const formatted = std.fmt.bufPrint(&buf, "{}", .{input}) catch |err| {
        return try writeError(response, "Cannot format interval", err);
    };

    try write(response, .ok, formatted);
}

fn handle_input_frames(response: *http.Server.Response) !void {
    var body: [32]u8 = undefined;
    const size = try response.readAll(&body);

    if (std.mem.startsWith(u8, body[0..size], "frames=")) {
        if (std.fmt.parseInt(u32, body["frames=".len..size], 10)) |frames| {
            camera.setFrames(frames);
        } else |err| {
            return try writeError(response, "Invalid frames input", err);
        }
    }

    var buf: [1024 * 2]u8 = undefined;

    const input = Input{
        .kind = .frames,
        .value = camera.getFrames(),
        .enabled = camera.inputsEnabled(),
    };

    const formatted = std.fmt.bufPrint(&buf, "{}", .{input}) catch |err| {
        return try writeError(response, "Cannot format frames", err);
    };

    try write(response, .ok, formatted);
}

fn render_content(response: *http.Server.Response, no_content: bool) !void {
    if (no_content and camera.isShooting()) {
        try write(response, .no_content, "No Content");
    } else {
        var buf: [1024 * 8]u8 = undefined;

        const content: Content = .{};

        const formatted = std.fmt.bufPrint(&buf, "{}", .{content}) catch return try write(response, .bad_request, "Cannot render content");

        try write(response, .ok, formatted);
    }
}

fn handle_get_state(response: *http.Server.Response) !void {
    try render_content(response, true);
}

fn handle_get_camera(response: *http.Server.Response) !void {
    if (!camera.dispatchBlocking(.Initialize))
        return try write(response, .bad_request, "Cannot initialize camera");

    try render_content(response, false);
}

fn handle_camera_connect(response: *http.Server.Response) !void {
    if (!camera.dispatchBlocking(.Connect))
        return try write(response, .bad_request, "Cannot connect to camera");

    try render_content(response, false);
}

fn handle_camera_disconnect(response: *http.Server.Response) !void {
    if (!camera.dispatchBlocking(.Disconnect))
        return try write(response, .bad_request, "Cannot disconnect from camera");

    try render_content(response, false);
}

fn handle_camera_start_shoot(response: *http.Server.Response) !void {
    if (!camera.dispatchBlocking(.StartShooting))
        return try write(response, .bad_request, "Cannot start shooting");

    try render_content(response, false);
}

fn handle_camera_stop_shoot(response: *http.Server.Response) !void {
    if (!camera.dispatchAsync(.StopShooting))
        return try write(response, .bad_request, "Cannot stop shooting");

    try render_content(response, false);
}

fn handle_camera_take_picture(response: *http.Server.Response) !void {
    if (!camera.dispatchBlocking(.TakePicture))
        return try write(response, .bad_request, "Cannot take picture");

    try render_content(response, false);
}

fn handle_500(response: *http.Server.Response) !void {
    response.status = .internal_server_error;
    try response.send();
    try response.finish();

    log.info("Internal error", .{});
}

fn handle_get_assets(response: *http.Server.Response) !void {
    var web_root_str: ?[]const u8 = null;

    var args = std.process.args();

    while (args.next()) |argc| {
        if (std.mem.eql(u8, argc, "--web-root")) {
            web_root_str = args.next();
        }
    }

    if (web_root_str == null) {
        return try handle_500(response);
    }

    var web_root = std.fs.openDirAbsolute(web_root_str.?, .{}) catch return try handle_500(response);
    defer web_root.close();

    const file = web_root.openFile(response.request.target[1..], .{ .mode = .read_only }) catch return try handle_500(response);
    defer file.close();

    const stat = file.stat() catch return try handle_500(response);

    response.transfer_encoding = .{ .content_length = stat.size };
    try response.send();

    var buffer: [1024]u8 = undefined;

    while (file.readAll(&buffer)) |bytes| {
        log.info("Writing = {}", .{bytes});
        if (bytes == 0)
            break;
        try response.writeAll(buffer[0..bytes]);
    } else |err| {
        log.info("some error {}", .{err});
        return try handle_500(response);
    }

    try response.finish();
}

const OptionsContent = struct {
    custom: ?camera.Option = null,
    options: []const camera.Option,
    selected: u32,

    pub fn format(self: *const OptionsContent, comptime fmt: []const u8, options: std.fmt.FormatOptions, writer: anytype) !void {
        _ = fmt;
        _ = options;

        if (self.custom) |custom| {
            const is_custom = self.selected == custom.eds_param;

            try writer.print(
                \\<option value="{1d}" {2s}>{0s}</option>
            , .{ custom.description, custom.eds_param, if (is_custom) "selected" else "" });
        }

        for (self.options) |option| {
            const is_selected = self.selected == option.eds_param;

            try writer.print(
                \\<option value="{1d}" {2s}>{0s}</option>
            , .{ option.description, option.eds_param, if (is_selected) "selected" else "" });
        }
    }
};

const CameraContent = struct {
    pub fn format(self: *const CameraContent, comptime fmt: []const u8, options: std.fmt.FormatOptions, writer: anytype) !void {
        _ = self;
        _ = fmt;
        _ = options;

        try writer.print(
            \\<div class="content camera">
        , .{});

        try writer.print(
            \\<fieldset>
            \\  <legent>Camera</legend>
            \\  <input name="camera" type="text" disabled value="{s}" />
            \\</fieldset>
        , .{if (camera.isDetected()) camera.description() else "No cameras detected"});

        if (camera.isDetected()) {
            if (camera.isConnected()) {
                try writer.print(
                    \\<button hx-post="/api/camera/disconnect" hx-target=".content"
                    \\  hx-swap="outerHTML">Disconnect</button>
                , .{});
            } else {
                try writer.print(
                    \\<button hx-post="/api/camera/connect" hx-target=".content"
                    \\  hx-swap="outerHTML">Connect</button>
                , .{});
            }
        } else {
            try writer.print(
                \\<button hx-get="/api/camera" hx-target=".content"
                \\  hx-swap="outerHTML">Refresh</button>
            , .{});
        }

        try writer.print("</div>", .{});
    }
};

const ExposureContent = struct {
    pub fn format(self: *const ExposureContent, comptime fmt: []const u8, options: std.fmt.FormatOptions, writer: anytype) !void {
        _ = self;
        _ = fmt;
        _ = options;

        try writer.print(
            \\<div class="input-exposure">
        , .{});

        try writer.print(
            \\<select name="exposure" hx-post="/api/camera/state/exposure"
            \\  hx-swap="outerHTML" hx-target=".input-exposure" {s}>
        , .{if (camera.inputsEnabled()) "" else "disabled"});

        const optionsContent: OptionsContent = .{
            .custom = .{
                .description = "Custom",
                .eds_param = 0xff,
            },
            .options = camera.getExposures(),
            .selected = camera.getExposureParam(),
        };

        try writer.print("{}", .{optionsContent});
        try writer.print("</select>", .{});

        if (camera.getExposureParam() == 0xff) {
            try writer.print(
                \\<input type="text" name="exposure-custom" value="{0d}" required
                \\  hx-validate="true" min="0" inputmode="numeric"
                \\  hx-post="/api/camera/state/exposure"
                \\  hx-swap="outerHTML" hx-target=".input-exposure" {1s} />
            , .{ camera.getExposure(), if (camera.inputsEnabled()) "" else "disabled" });
        }

        try writer.print("</div>", .{});
    }
};

const IsoContent = struct {
    pub fn format(self: *const IsoContent, comptime fmt: []const u8, options: std.fmt.FormatOptions, writer: anytype) !void {
        _ = self;
        _ = fmt;
        _ = options;

        try writer.print(
            \\<div class="input-iso">
        , .{});

        try writer.print(
            \\<select name="iso"
            \\  hx-post="/api/camera/state/iso"
            \\  hx-swap="outerHTML" hx-target=".input-iso" {s}>
        , .{if (camera.inputsEnabled()) "" else "disabled"});

        const optionsContent: OptionsContent = .{
            .options = camera.getIsos(),
            .selected = camera.getIsoParam(),
        };

        try writer.print("{}", .{optionsContent});
        try writer.print("</select>", .{});

        try writer.print("</div>", .{});
    }
};

const InputsContent = struct {
    pub fn format(self: *const InputsContent, comptime fmt: []const u8, options: std.fmt.FormatOptions, writer: anytype) !void {
        _ = self;
        _ = fmt;
        _ = options;

        const delay = Input{
            .kind = .delay,
            .value = camera.getDelay(),
            .enabled = camera.inputsEnabled(),
        };

        const exposure: ExposureContent = .{};

        const interval = Input{
            .kind = .interval,
            .value = camera.getInterval(),
            .enabled = camera.inputsEnabled(),
        };

        const frames = Input{
            .kind = .frames,
            .value = camera.getFrames(),
            .enabled = camera.inputsEnabled(),
        };

        const iso: IsoContent = .{};

        try writer.print(
            \\<div class="content inputs">
            \\  <fieldset>
            \\    <legend>Delay (seconds)</legend>
            \\    <div class="delay">{0}</div>
            \\  </fieldset>
            \\  <fieldset>
            \\    <legend>Exposure (seconds)</legend>
            \\    <div class="exposure">{1}</div>
            \\  </fieldset>
            \\  <fieldset>
            \\    <legend>Interval (seconds)</legend>
            \\    <div class="interval">{2}</div>
            \\  </fieldset>
            \\  <fieldset>
            \\    <legend>Frames</legend>
            \\    <div class="frames">{3}</div>
            \\  </fieldset>
            \\  <fieldset>
            \\    <legend>ISO</legend>
            \\    <div class="iso">{4}</div>
            \\  </fieldset>
            \\</div>
        , .{ delay, exposure, interval, frames, iso });
    }
};

const ActionsContent = struct {
    pub fn format(self: *const ActionsContent, comptime fmt: []const u8, options: std.fmt.FormatOptions, writer: anytype) !void {
        _ = self;
        _ = fmt;
        _ = options;

        try writer.print(
            \\<div class="content actions">
        , .{});

        {
            const enabled = camera.isConnected() and !camera.isShooting();

            try writer.print(
                \\<button hx-post="/api/camera/start-shoot"
                \\ hx-target=".content" hx-swap="outerHTML" {s}>Start</button>
            , .{if (!enabled) "disabled" else ""});
        }

        {
            const enabled = camera.isConnected() and camera.isShooting();

            try writer.print(
                \\<button hx-post="/api/camera/stop-shoot"
                \\ hx-target=".content" hx-swap="outerHTML" {s}>Stop</button>
            , .{if (!enabled) "disabled" else ""});
        }

        {
            const enabled = camera.isConnected() and !camera.isShooting();

            try writer.print(
                \\<button hx-post="/api/camera/take-picture"
                \\ hx-target=".content" hx-swap="outerHTML" {s}>Take Picture</button>
            , .{if (!enabled) "disabled" else ""});
        }

        try writer.print(
            \\</div>
        , .{});
    }
};

const Content = struct {
    pub fn format(self: *const Content, comptime fmt: []const u8, options: std.fmt.FormatOptions, writer: anytype) !void {
        _ = self;
        _ = fmt;
        _ = options;

        const refresh = if (camera.isShooting())
            \\hx-get="/api/camera/state" hx-swap="outerHTML" hx-trigger="every 2s"
        else
            "";

        const cameraContent: CameraContent = .{};
        const inputContent: InputsContent = .{};
        const actionsContent: ActionsContent = .{};

        try writer.print(
            \\<div class="content" {0s}>{1}{2}{3}</div>
        , .{ refresh, cameraContent, inputContent, actionsContent });
    }
};

const IndexContent = struct {
    pub fn format(self: *const IndexContent, comptime fmt: []const u8, options: std.fmt.FormatOptions, writer: anytype) !void {
        _ = self;
        _ = fmt;
        _ = options;

        const content: Content = .{};

        try writer.print(
            \\<!doctype html>
            \\<html lang="en">
            \\<head>
            \\  <meta meta="viewport" content="width=device-width, initial-scale=1.0" />
            \\  <link rel="stylesheet" href="assets/index.css">
            \\  <script src="assets/htmx.min.js"></script>
            \\  <script src="assets/index.js"></script>
            \\</head>
            \\<body>
            \\  {}
            \\</body>
            \\</html>
        , .{content});
    }
};

fn handle_get_index_html(response: *http.Server.Response) !void {
    var buf: [1024 * 8]u8 = undefined;

    const content: IndexContent = .{};

    const formatted = std.fmt.bufPrint(&buf, "{}", .{content}) catch |err| {
        return try writeError(response, "Error formatting index.html", err);
    };

    try write(response, .ok, formatted);
}

fn write(response: *http.Server.Response, status: http.Status, content: []const u8) !void {
    response.transfer_encoding = .{ .content_length = content.len };
    //try response.headers.append("Content-Type", "text/html");
    response.status = status;
    try response.send();
    try response.writeAll(content);
    try response.finish();
}

fn writeError(response: *http.Server.Response, msg: []const u8, err: anyerror) !void {
    var buffer: [128]u8 = undefined;
    const content = try std.fmt.bufPrint(buffer[0..], "{s}: {s}", .{ msg, @errorName(err) });
    try write(response, .bad_request, content);
}

fn match_url(s: []const u8, p: []const u8, caps: [][]const u8) bool {
    // comptime check
    // var cap_count: usize = 0;
    // for (p) |ch| {
    //     if (ch == '?' or ch == '*')
    //         cap_count += 1;
    // }
    //
    // if (cap_count != caps.len) {
    //     return false;
    // }

    var is: usize = 0;
    var ip: usize = 0;
    var init_cap: usize = 0;
    var captures: usize = 0;

    while (ip < p.len or is < s.len) {
        if (ip < p.len and is < s.len and p[ip] == s[is]) {
            if (init_cap != 0 and captures < caps.len) {
                caps[captures] = s[init_cap..is];
                captures += 1;
                init_cap = 0;
            }

            ip += 1;
            is += 1;
        } else if (ip < p.len and (p[ip] == '*' or p[ip] == '?')) {
            init_cap = is;
            ip += 1;
        } else {
            if (init_cap == 0)
                return false;

            is += 1;
        }
    }

    if (init_cap != 0 and captures < caps.len) {
        caps[captures] = s[init_cap..];
    }

    return true;
}

// Handle an individual request.
fn handleRequest(response: *http.Server.Response) !void {
    // Log the request details.
    log.info("{s} {s} {s}", .{ @tagName(response.request.method), @tagName(response.request.version), response.request.target });

    // Set "connection" header to "keep-alive" if present in request headers.
    // if (response.request.headers.contains("connection")) {
    //     try response.headers.append("connection", "keep-alive");
    // } else {
    //     try response.headers.append("connection", "close");
    // }

    try response.headers.append("connection", "close");

    const handlers = switch (response.request.method) {
        .GET => &get_handlers,
        .POST => &post_handlers,
        else => &[0]Handler{},
    };

    for (handlers) |handler| {
        var caps: [0][]const u8 = undefined;
        if (match_url(response.request.target, handler.endpoint, &caps)) {
            try handler.handler(response);
            return;
        }
    }

    log.info("Not found {s}", .{response.request.target});

    response.status = .not_found;
    try response.send();
    try response.finish();
}

/// Order of operations
//             [/ <----------------------------------- \]
// accept -> wait -> send  [ -> write -> finish][ -> reset /]
//              \ -> read /
fn runServer(server: *http.Server) !void {
    outer: while (camera.isRunning()) {
        // Accept incoming connection.
        var response = try server.accept(.{
            .allocator = server.allocator,
        });
        defer response.deinit();

        while (camera.isRunning()) {
            // Handle errors during request processing.
            response.wait() catch |err| switch (err) {
                error.HttpHeadersInvalid => {
                    continue :outer;
                },
                error.EndOfStream => {
                    continue;
                },
                else => return err,
            };

            // Process the request.
            try handleRequest(&response);

            if (response.reset() == .closing)
                break;
        }
    }
}

pub fn runHttpServer() !void {
    var gpa = std.heap.GeneralPurposeAllocator(.{}){};
    defer std.debug.assert(gpa.deinit() == .ok);
    const allocator = gpa.allocator();

    var server = http.Server.init(allocator, .{
        .reuse_address = true,
    });
    defer server.deinit();

    const address = std.net.Address.parseIp(server_addr, server_port) catch unreachable;
    try server.listen(address);

    runServer(&server) catch |err| {
        // Handle server errors.
        log.err("server error: {}\n", .{err});
        if (@errorReturnTrace()) |trace| {
            std.debug.dumpStackTrace(trace.*);
        }
        std.os.exit(1);
    };

    log.info("Stoping http thread", .{});
}
