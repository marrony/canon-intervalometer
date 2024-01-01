const std = @import("std");
const camera = @import("camera.zig");

const http = std.http;

const log = std.log.scoped(.server);

const server_addr = "0.0.0.0";
const server_port = 8001;

/// Order of operations
//             [/ <----------------------------------- \]
// accept -> wait -> send  [ -> write -> finish][ -> reset /]
//              \ -> read /
fn runServer(server: *http.Server) !void {
    //outer:
    while (true) {
        // Accept incoming connection.
        var response = try server.accept(.{ .allocator = server.allocator });
        defer {
            log.info("Deinit response", .{});
            response.deinit();
        }

        while (true) {
            log.info("gonna wait", .{});
            try response.wait();

            // Handle errors during request processing.
            // response.wait() catch |err| switch (err) {
            //     error.HttpHeadersInvalid => {
            //         log.info("invalid headers", .{});
            //         continue :outer;
            //     },
            //     error.EndOfStream => {
            //         log.info("end of stream", .{});
            //         continue;
            //     },
            //     else => return err,
            // };

            // Process the request.
            try handleRequest(&response);

            if (response.reset() == .closing)
                break;
        }

        log.info("Handle Request", .{});
    }
}

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

        try std.fmt.format(writer,
            \\<input type="number" name="{0s}" value="{1d}" class="input-{0s}" required hx-validate="true"
            \\ min="0" inputmode="numeric" hx-post="/api/camera/state/{0s}" hx-swap="outerHTML" {2s}/>
        , .{
            @tagName(self.kind),
            self.value,
            if (self.enabled) "" else "disabled ",
        });
    }
};

fn handle_input_delay(response: *http.Server.Response) anyerror!void {
    var body: [32]u8 = undefined;
    const size = try response.readAll(&body);

    if (std.mem.startsWith(u8, body[0..size], "delay=")) {
        if (std.fmt.parseUnsigned(u32, body["delay=".len..], 10)) |delay| {
            camera.g_camera.delay_us = delay;
        } else |_| {
            return try write(response, .bad_request, "Invalid delay input");
        }
    }

    var buf: [1024]u8 = undefined;

    const input = Input{
        .kind = .delay,
        .value = camera.g_camera.delay_us,
        .enabled = camera.g_camera.inputs_enabled(),
    };

    const formatted = std.fmt.bufPrint(&buf, "{}", .{input}) catch return try write(response, .bad_request, "Cannot format delay");

    try write(response, .ok, formatted);
}

fn handle_input_iso(response: *http.Server.Response) !void {
    var body: [32]u8 = undefined;
    const size = try response.readAll(&body);

    if (std.mem.startsWith(u8, body[0..size], "iso=")) {
        if (std.fmt.parseUnsigned(u32, body["iso=".len..], 10)) |iso_param| {
            camera.g_camera.iso_param = iso_param;
        } else |_| {
            return try write(response, .bad_request, "Invalid ISO input");
        }
    }

    var buf: [1024]u8 = undefined;

    const iso = IsoContent{
        .camera = &camera.g_camera,
    };

    const formatted = std.fmt.bufPrint(&buf, "{}", .{iso}) catch return try write(response, .bad_request, "Cannot format ISO");

    try write(response, .ok, formatted);
}

fn handle_input_exposure(response: *http.Server.Response) !void {
    var body: [32]u8 = undefined;
    const size = try response.readAll(&body);

    if (std.mem.startsWith(u8, body[0..size], "exposure=")) {
        if (std.fmt.parseUnsigned(u32, body["exposure=".len..], 10)) |exposure_param| {
            camera.g_camera.exposure_param = exposure_param;
            camera.g_camera.exposure_us = 0;
        } else |_| {
            return try write(response, .bad_request, "Invalid exposure input");
        }
    }

    if (std.mem.startsWith(u8, body[0..size], "exposure-custom=")) {
        if (std.fmt.parseUnsigned(u32, body["exposure-custom=".len..], 10)) |exposure| {
            camera.g_camera.exposure_us = exposure;
            camera.g_camera.exposure_param = 0xff;
        } else |_| {
            return try write(response, .bad_request, "Invalid exposure input");
        }
    }

    var buf: [1024]u8 = undefined;

    const exposure = ExposureContent{
        .camera = &camera.g_camera,
    };

    const formatted = std.fmt.bufPrint(&buf, "{}", .{exposure}) catch return try write(response, .bad_request, "Cannot format exposure");

    try write(response, .ok, formatted);
}

fn handle_input_interval(response: *http.Server.Response) !void {
    var body: [32]u8 = undefined;
    const size = try response.readAll(&body);

    if (std.mem.startsWith(u8, body[0..size], "interval=")) {
        if (std.fmt.parseUnsigned(u32, body["interval=".len..], 10)) |interval| {
            camera.g_camera.interval_us = interval;
        } else |_| {
            return try write(response, .bad_request, "Invalid interval input");
        }
    }

    var buf: [1024]u8 = undefined;

    const input = Input{
        .kind = .interval,
        .value = camera.g_camera.interval_us,
        .enabled = camera.g_camera.inputs_enabled(),
    };

    const formatted = std.fmt.bufPrint(&buf, "{}", .{input}) catch return try write(response, .bad_request, "Cannot format interval");

    try write(response, .ok, formatted);
}

fn handle_input_frames(response: *http.Server.Response) !void {
    var body: [32]u8 = undefined;
    const size = try response.readAll(&body);

    if (std.mem.startsWith(u8, body[0..size], "frames=")) {
        if (std.fmt.parseUnsigned(u32, body["frames=".len..], 10)) |frames| {
            camera.g_camera.frames = frames;
        } else |_| {
            return try write(response, .bad_request, "Invalid frames input");
        }
    }

    var buf: [1024]u8 = undefined;

    const input = Input{
        .kind = .frames,
        .value = camera.g_camera.frames,
        .enabled = camera.g_camera.inputs_enabled(),
    };

    const formatted = std.fmt.bufPrint(&buf, "{}", .{input}) catch return try write(response, .bad_request, "Cannot format frames");

    try write(response, .ok, formatted);
}

fn render_content(response: *http.Server.Response, no_content: bool) !void {
    if (no_content and camera.g_camera.shooting) {
        try write(response, .no_content, "No Content");
    } else {
        var buf: [1024 * 8]u8 = undefined;

        const content = Content{
            .camera = &camera.g_camera,
        };

        const formatted = std.fmt.bufPrint(&buf, "{}", .{content}) catch return try write(response, .bad_request, "Cannot format frames");

        try write(response, .ok, formatted);
    }
}

fn handle_get_state(response: *http.Server.Response) !void {
    try render_content(response, true);
}

fn handle_get_camera(response: *http.Server.Response) !void {
    camera.g_queue.putAsync(.Initialize);

    var buf: [1024 * 8]u8 = undefined;

    const content = CameraContent{
        .camera = &camera.g_camera,
    };

    const formatted = std.fmt.bufPrint(&buf, "{}", .{content}) catch return try write(response, .bad_request, "Cannot format camera");

    try write(response, .ok, formatted);
}

fn handle_camera_connect(response: *http.Server.Response) !void {
    camera.g_queue.putAsync(.Connect);
    try render_content(response, false);
}

fn handle_camera_disconnect(response: *http.Server.Response) !void {
    camera.g_queue.putAsync(.Disconnect);
    try render_content(response, false);
}

fn handle_camera_start_shoot(response: *http.Server.Response) !void {
    camera.g_queue.putSync(.StartShooting);
    try render_content(response, false);
}

fn handle_camera_stop_shoot(response: *http.Server.Response) !void {
    camera.g_queue.putAsync(.StopShooting);
    try render_content(response, false);
}

fn handle_camera_take_picture(response: *http.Server.Response) !void {
    camera.g_queue.putSync(.TakePicture);
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

    log.info("Web root = {any}", .{web_root});

    const file = web_root.openFile(response.request.target[1..], .{ .mode = .read_only }) catch return try handle_500(response);
    defer file.close();

    log.info("File = {any}", .{file});

    const stat = file.stat() catch return try handle_500(response);

    log.info("Stat = {any}", .{stat});

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

    log.info("Done wrting file", .{});

    try response.finish();

    log.info("Done finishing reponse", .{});
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

            try std.fmt.format(writer,
                \\<option value="{1d}" {2s}>{0s}</option>
            , .{ custom.description, custom.eds_param, if (is_custom) "selected" else "" });
        }

        for (self.options) |option| {
            const is_selected = self.selected == option.eds_param;

            try std.fmt.format(writer,
                \\<option value="{1d}" {2s}>{0s}</option>
            , .{ option.description, option.eds_param, if (is_selected) "selected" else "" });
        }
    }
};

const CameraContent = struct {
    camera: *const camera.Camera,

    pub fn format(self: *const CameraContent, comptime fmt: []const u8, options: std.fmt.FormatOptions, writer: anytype) !void {
        _ = fmt;
        _ = options;

        try std.fmt.format(writer,
            \\<div class="content camera">
        , .{});

        try std.fmt.format(writer,
            \\<fieldset>
            \\  <legent>Camera</legend>
            \\  <input name="camera" type="text" disabled value="{s}" />
            \\</fieldset>
        , .{if (self.camera.initialized) &self.camera.description else "No cameras detected"});

        if (self.camera.initialized) {
            if (self.camera.connected) {
                try std.fmt.format(writer,
                    \\<button hx-get="/api/camera/disconnect" hx-target=".content"
                    \\  hx-swap="outerHTML">Disconnect</button>
                , .{});
            } else {
                try std.fmt.format(writer,
                    \\<button hx-get="/api/camera/connect" hx-target=".content"
                    \\  hx-swap="outerHTML">Connect</button>
                , .{});
            }
        } else {
            try std.fmt.format(writer,
                \\<button hx-get="/api/camera" hx-target=".content .camera"
                \\  hx-swap="outerHTML">Refresh</button>
            , .{});
        }

        try std.fmt.format(writer, "</div>", .{});
    }
};

const ExposureContent = struct {
    camera: *const camera.Camera,

    pub fn format(self: *const ExposureContent, comptime fmt: []const u8, options: std.fmt.FormatOptions, writer: anytype) !void {
        _ = fmt;
        _ = options;

        try std.fmt.format(writer,
            \\<div class="input-exposure">
        , .{});

        try std.fmt.format(writer,
            \\<select name="exposure" hx-post="/api/camera/state/exposure"
            \\  hx-swap="outerHTML" hx-target=".input-exposure" {s}>
        , .{if (self.camera.inputs_enabled()) "" else "disabled"});

        const optionsContent: OptionsContent = .{
            .custom = .{
                .description = "Custom",
                .eds_param = 0xff,
            },
            .options = camera.getExposures(),
            .selected = self.camera.exposure_param,
        };

        try std.fmt.format(writer, "{}", .{optionsContent});
        try std.fmt.format(writer, "</select>", .{});

        if (self.camera.exposure_param == 0xff) {
            try std.fmt.format(writer,
                \\<input type="text" name="exposure-custom" value="{0d}" required
                \\  hx-validate="true" min="0" inputmode="numeric"
                \\  hx-post="/api/camera/state/exposure"
                \\  hx-swap="outerHTML" hx-target=".input-exposure" {1s} />
            , .{ camera.g_camera.exposure_us, if (camera.g_camera.inputs_enabled()) "" else "disabled" });
        }

        try std.fmt.format(writer, "</div>", .{});
    }
};

const IsoContent = struct {
    camera: *const camera.Camera,

    pub fn format(self: *const IsoContent, comptime fmt: []const u8, options: std.fmt.FormatOptions, writer: anytype) !void {
        _ = fmt;
        _ = options;

        try std.fmt.format(writer,
            \\<select class="input-iso" name="iso"
            \\  hx-post="/api/camera/state/iso"
            \\  hx-swap="outerHTML" hx-target=".input-iso" {s}>
        , .{if (self.camera.inputs_enabled()) "" else "disabled"});

        const optionsContent: OptionsContent = .{
            .options = camera.getIsos(),
            .selected = self.camera.iso_param,
        };

        try std.fmt.format(writer, "{}", .{optionsContent});
        try std.fmt.format(writer, "</select>", .{});
    }
};

const InputsContent = struct {
    camera: *const camera.Camera,

    pub fn format(self: *const InputsContent, comptime fmt: []const u8, options: std.fmt.FormatOptions, writer: anytype) !void {
        _ = fmt;
        _ = options;

        const delay = Input{
            .kind = .delay,
            .value = self.camera.delay_us,
            .enabled = self.camera.inputs_enabled(),
        };

        const exposure = ExposureContent{
            .camera = self.camera,
        };

        const interval = Input{
            .kind = .interval,
            .value = self.camera.interval_us,
            .enabled = self.camera.inputs_enabled(),
        };

        const frames = Input{
            .kind = .frames,
            .value = self.camera.frames,
            .enabled = self.camera.inputs_enabled(),
        };

        const iso = IsoContent{
            .camera = self.camera,
        };

        try std.fmt.format(writer,
            \\<div class="content inputs">
            \\  <fieldset>
            \\    <legend>Delay (seconds)</legend>
            \\    <div>{0}</div>
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
    camera: *const camera.Camera,

    pub fn format(self: *const ActionsContent, comptime fmt: []const u8, options: std.fmt.FormatOptions, writer: anytype) !void {
        _ = fmt;
        _ = options;

        try std.fmt.format(writer,
            \\<div class="content actions">
        , .{});

        {
            const enabled = self.camera.initialized and self.camera.connected and !self.camera.shooting;

            try std.fmt.format(writer,
                \\<button hx-post="/api/camera/start-shoot"
                \\ hx-target=".content" hx-swap="outerHTML" {s}>Start</button>
            , .{if (!enabled) "disabled" else ""});
        }

        {
            const enabled = self.camera.initialized and self.camera.connected and self.camera.shooting;

            try std.fmt.format(writer,
                \\<button hx-post="/api/camera/stop-shoot"
                \\ hx-target=".content" hx-swap="outerHTML" {s}>Stop</button>
            , .{if (!enabled) "disabled" else ""});
        }

        {
            const enabled = self.camera.initialized and self.camera.connected and !self.camera.shooting;

            try std.fmt.format(writer,
                \\<button hx-post="/api/camera/take-picture"
                \\ hx-target=".content" hx-swap="outerHTML" {s}>Take Picture</button>
            , .{if (!enabled) "disabled" else ""});
        }

        try std.fmt.format(writer,
            \\</div>
        , .{});
    }
};

const Content = struct {
    camera: *camera.Camera,

    pub fn format(self: *const Content, comptime fmt: []const u8, options: std.fmt.FormatOptions, writer: anytype) !void {
        _ = fmt;
        _ = options;

        const refresh = if (self.camera.shooting)
            \\hx-get="/api/camera/state" hx-swap="outerHTML" hx-trigger="every 2s"
        else
            "";

        const cameraContent = CameraContent{ .camera = self.camera };
        const inputContent = InputsContent{ .camera = self.camera };
        const actionsContent = ActionsContent{ .camera = self.camera };

        try std.fmt.format(writer,
            \\<div class="content" {0s}>{1}{2}{3}</div>
        , .{ refresh, cameraContent, inputContent, actionsContent });
    }
};

const IndexContent = struct {
    camera: *camera.Camera,

    pub fn format(self: *const IndexContent, comptime fmt: []const u8, options: std.fmt.FormatOptions, writer: anytype) !void {
        _ = fmt;
        _ = options;

        const content = Content{
            .camera = self.camera,
        };

        try std.fmt.format(writer,
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

    const content = IndexContent{
        .camera = &camera.g_camera,
    };

    const formatted = std.fmt.bufPrint(&buf,
        \\{}
    , .{content}) catch "Error formatting index.html";

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

pub fn runHttpServer() !void {
    var gpa = std.heap.GeneralPurposeAllocator(.{}){};
    defer std.debug.assert(gpa.deinit() == .ok);
    const allocator = gpa.allocator();

    var server = http.Server.init(allocator, .{ .reuse_address = true });
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
}
