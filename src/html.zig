const std = @import("std");
const camera = @import("camera.zig");

pub const InputKind = enum {
    delay,
    interval,
    frames,
};

pub const Input = struct {
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

const Options = struct {
    custom: ?camera.Option = null,
    options: []const camera.Option,
    selected: u32,

    pub fn format(self: *const Options, comptime fmt: []const u8, options: std.fmt.FormatOptions, writer: anytype) !void {
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

const Camera = struct {
    pub fn format(self: *const Camera, comptime fmt: []const u8, options: std.fmt.FormatOptions, writer: anytype) !void {
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

pub const Exposure = struct {
    pub fn format(self: *const Exposure, comptime fmt: []const u8, options: std.fmt.FormatOptions, writer: anytype) !void {
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

        const optionsContent: Options = .{
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

pub const Iso = struct {
    pub fn format(self: *const Iso, comptime fmt: []const u8, options: std.fmt.FormatOptions, writer: anytype) !void {
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

        const optionsContent: Options = .{
            .options = camera.getIsos(),
            .selected = camera.getIsoParam(),
        };

        try writer.print("{}", .{optionsContent});
        try writer.print("</select>", .{});

        try writer.print("</div>", .{});
    }
};

pub const Inputs = struct {
    pub fn format(self: *const Inputs, comptime fmt: []const u8, options: std.fmt.FormatOptions, writer: anytype) !void {
        _ = self;
        _ = fmt;
        _ = options;

        const delay = Input{
            .kind = .delay,
            .value = camera.getDelay(),
            .enabled = camera.inputsEnabled(),
        };

        const exposure: Exposure = .{};

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

        const iso: Iso = .{};

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

const Actions = struct {
    pub fn format(self: *const Actions, comptime fmt: []const u8, options: std.fmt.FormatOptions, writer: anytype) !void {
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

        try writer.print("</div>", .{});
    }
};

pub const Body = struct {
    pub fn format(self: *const Body, comptime fmt: []const u8, options: std.fmt.FormatOptions, writer: anytype) !void {
        _ = self;
        _ = fmt;
        _ = options;

        const refresh = if (camera.isShooting())
            \\hx-get="/api/camera/state" hx-swap="outerHTML" hx-trigger="every 2s"
        else
            "";

        const cameraContent: Camera = .{};
        const inputContent: Inputs = .{};
        const actionsContent: Actions = .{};

        try writer.print(
            \\<body class="content" {0s}>{1}{2}{3}</body>
        , .{ refresh, cameraContent, inputContent, actionsContent });
    }
};

pub const Index = struct {
    pub fn format(self: *const Index, comptime fmt: []const u8, options: std.fmt.FormatOptions, writer: anytype) !void {
        _ = self;
        _ = fmt;
        _ = options;

        const body: Body = .{};

        try writer.print(
            \\<!doctype html>
            \\<html lang="en">
            \\<head>
            \\  <meta meta="viewport" content="width=device-width, initial-scale=1.0" />
            \\  <link rel="stylesheet" href="assets/index.css">
            \\  <script src="assets/htmx.min.js"></script>
            \\  <script src="assets/index.js"></script>
            \\</head>
            \\{}
            \\</html>
        , .{body});
    }
};
