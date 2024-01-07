const std = @import("std");
const edsk = @import("edsdk.zig");
const c = edsk.c;
const queue = @import("queue.zig");
const command = @import("command.zig");
const log = std.log.scoped(.camera);

pub const Option = struct {
    description: []const u8,
    eds_param: u32,
};

const CameraErr = error{
    InitErr,
    CameraErr,
};

pub const Camera = struct {
    camera: c.EdsCameraRef = undefined,
    running: bool,
    iso_param: u32,
    exposure_param: u32,
    delay_us: u32,
    exposure_us: u32,
    interval_us: u32,
    frames: u32,
    frames_taken: u32,
    initialized: bool,
    connected: bool,
    shooting: bool,
    description: [256]u8 = undefined,
};

pub const DispatchQueue = queue.DispatchQueue(command.Command, 8);

pub var g_dispatch: DispatchQueue = .{};
var g_camera: Camera = .{
    .running = true,
    .iso_param = 0xff,
    .exposure_param = 0xff,
    .delay_us = 0,
    .exposure_us = 0,
    .interval_us = 0,
    .frames = 0,
    .frames_taken = 0,
    .initialized = false,
    .connected = false,
    .shooting = false,
    .description = [1]u8{' '} ** 256,
};

const all_exposures = [_]Option{
    .{ .description = "30\"", .eds_param = 0x10 },
    .{ .description = "25\"", .eds_param = 0x13 },
    .{ .description = "20\"", .eds_param = 0x14 },
    .{ .description = "15\"", .eds_param = 0x18 },
    .{ .description = "13\"", .eds_param = 0x1B },
    .{ .description = "10\"", .eds_param = 0x1C },
    .{ .description = "8\"", .eds_param = 0x20 },
    .{ .description = "6\"", .eds_param = 0x24 },
    .{ .description = "5\"", .eds_param = 0x25 },
    .{ .description = "4\"", .eds_param = 0x28 },
    .{ .description = "3\"", .eds_param = 0x2B },
    .{ .description = "3\"", .eds_param = 0x2C },
    .{ .description = "2.5\"", .eds_param = 0x2D },
    .{ .description = "2.0\"", .eds_param = 0x30 },
    .{ .description = "1.6\"", .eds_param = 0x33 },
    .{ .description = "1.5\"", .eds_param = 0x34 },
    .{ .description = "1.3\"", .eds_param = 0x35 },
    .{ .description = "1.0\"", .eds_param = 0x38 },
    .{ .description = "0.8\"", .eds_param = 0x3B },
    .{ .description = "0.7\"", .eds_param = 0x3C },
    .{ .description = "0.6\"", .eds_param = 0x3D },
    .{ .description = "0.5\"", .eds_param = 0x40 },
    .{ .description = "0.4\"", .eds_param = 0x43 },
    .{ .description = "0.3\"", .eds_param = 0x44 },
    .{ .description = "1/4\"", .eds_param = 0x48 },
    .{ .description = "1/5\"", .eds_param = 0x4B },
    .{ .description = "1/6\"", .eds_param = 0x4C },
    .{ .description = "1/8\"", .eds_param = 0x50 },
    .{ .description = "1/10\"", .eds_param = 0x54 },
    .{ .description = "1/13\"", .eds_param = 0x55 },
    .{ .description = "1/15\"", .eds_param = 0x58 },
    .{ .description = "1/20\"", .eds_param = 0x5C },
    .{ .description = "1/25\"", .eds_param = 0x5D },
    .{ .description = "1/30\"", .eds_param = 0x60 },
    .{ .description = "1/40\"", .eds_param = 0x63 },
    .{ .description = "1/45\"", .eds_param = 0x64 },
    .{ .description = "1/50\"", .eds_param = 0x65 },
    .{ .description = "1/60\"", .eds_param = 0x68 },
    .{ .description = "1/80\"", .eds_param = 0x6B },
    .{ .description = "1/90\"", .eds_param = 0x6C },
    .{ .description = "1/100\"", .eds_param = 0x6D },
    .{ .description = "1/125\"", .eds_param = 0x70 },
    .{ .description = "1/160\"", .eds_param = 0x73 },
    .{ .description = "1/180\"", .eds_param = 0x74 },
    .{ .description = "1/200\"", .eds_param = 0x75 },
    .{ .description = "1/250\"", .eds_param = 0x78 },
    .{ .description = "1/320\"", .eds_param = 0x7B },
    .{ .description = "1/350\"", .eds_param = 0x7C },
    .{ .description = "1/400\"", .eds_param = 0x7D },
    .{ .description = "1/500\"", .eds_param = 0x80 },
    .{ .description = "1/640\"", .eds_param = 0x83 },
    .{ .description = "1/750\"", .eds_param = 0x84 },
    .{ .description = "1/800\"", .eds_param = 0x85 },
    .{ .description = "1/1000\"", .eds_param = 0x88 },
    .{ .description = "1/1250\"", .eds_param = 0x8B },
    .{ .description = "1/1500\"", .eds_param = 0x8C },
    .{ .description = "1/1600\"", .eds_param = 0x8D },
    .{ .description = "1/2000\"", .eds_param = 0x90 },
    .{ .description = "1/2500\"", .eds_param = 0x93 },
    .{ .description = "1/3000\"", .eds_param = 0x94 },
    .{ .description = "1/3200\"", .eds_param = 0x95 },
    .{ .description = "1/4000\"", .eds_param = 0x98 },
    .{ .description = "1/5000\"", .eds_param = 0x9B },
    .{ .description = "1/6000\"", .eds_param = 0x9C },
    .{ .description = "1/6400\"", .eds_param = 0x9D },
    .{ .description = "1/8000\"", .eds_param = 0xA0 },
    .{ .description = "1/10000\"", .eds_param = 0xA3 },
    .{ .description = "1/12800\"", .eds_param = 0xA5 },
    .{ .description = "1/16000\"", .eds_param = 0xA8 },
};

const all_isos = [_]Option{
    .{ .description = "Auto", .eds_param = 0x0 },
    .{ .description = "ISO 6", .eds_param = 0x28 },
    .{ .description = "ISO 12", .eds_param = 0x30 },
    .{ .description = "ISO 25", .eds_param = 0x38 },
    .{ .description = "ISO 50", .eds_param = 0x40 },
    .{ .description = "ISO 100", .eds_param = 0x48 },
    .{ .description = "ISO 125", .eds_param = 0x4b },
    .{ .description = "ISO 160", .eds_param = 0x4d },
    .{ .description = "ISO 200", .eds_param = 0x50 },
    .{ .description = "ISO 250", .eds_param = 0x53 },
    .{ .description = "ISO 320", .eds_param = 0x55 },
    .{ .description = "ISO 400", .eds_param = 0x56 },
    .{ .description = "ISO 500", .eds_param = 0x5b },
    .{ .description = "ISO 640", .eds_param = 0x5d },
    .{ .description = "ISO 800", .eds_param = 0x60 },
    .{ .description = "ISO 1000", .eds_param = 0x63 },
    .{ .description = "ISO 1250", .eds_param = 0x65 },
    .{ .description = "ISO 1600", .eds_param = 0x68 },
    .{ .description = "ISO 2000", .eds_param = 0x6b },
    .{ .description = "ISO 2500", .eds_param = 0x6d },
    .{ .description = "ISO 3200", .eds_param = 0x70 },
    .{ .description = "ISO 4000", .eds_param = 0x73 },
    .{ .description = "ISO 5000", .eds_param = 0x75 },
    .{ .description = "ISO 6400", .eds_param = 0x78 },
    .{ .description = "ISO 8000", .eds_param = 0x07b },
    .{ .description = "ISO 10000", .eds_param = 0x7d },
    .{ .description = "ISO 12800", .eds_param = 0x80 },
    .{ .description = "ISO 16000", .eds_param = 0x83 },
    .{ .description = "ISO 20000", .eds_param = 0x85 },
    .{ .description = "ISO 25600", .eds_param = 0x88 },
    .{ .description = "ISO 32000", .eds_param = 0x8b },
    .{ .description = "ISO 40000", .eds_param = 0x8d },
    .{ .description = "ISO 51200", .eds_param = 0x90 },
    .{ .description = "ISO 64000", .eds_param = 0x3 },
    .{ .description = "ISO 80000", .eds_param = 0x95 },
    .{ .description = "ISO 102400", .eds_param = 0x98 },
    .{ .description = "ISO 204800", .eds_param = 0xa0 },
    .{ .description = "ISO 409600", .eds_param = 0xa8 },
    .{ .description = "ISO 819200", .eds_param = 0xb0 },
};

var exposures: [all_exposures.len]Option = undefined;
var exposures_len: usize = 0;

var isos: [all_isos.len]Option = undefined;
var isos_len: usize = 0;

pub fn dispatchAsync(cmd: command.Command) bool {
    return g_dispatch.dispatch(command.Command.execute, cmd, null);
}

pub fn dispatchBlocking(cmd: command.Command) bool {
    var token: queue.SyncToken = .{};
    defer token.wait();

    return g_dispatch.dispatch(command.Command.execute, cmd, &token);
}

pub fn getExposures() []const Option {
    return exposures[0..exposures_len];
}

pub fn getIsos() []const Option {
    return isos[0..isos_len];
}

pub fn inputsEnabled() bool {
    return g_camera.initialized and g_camera.connected and !g_camera.shooting;
}

pub fn isRunning() bool {
    return g_camera.running;
}

pub fn isDetected() bool {
    return g_camera.initialized and g_camera.camera != null;
}

pub fn description() []const u8 {
    return std.mem.sliceTo(&g_camera.description, 0);
}

pub fn isConnected() bool {
    return g_camera.initialized and g_camera.connected;
}

pub fn isShooting() bool {
    return g_camera.shooting;
}

pub fn getDelay() u32 {
    return g_camera.delay_us;
}

pub fn setDelayTime(delay: u32) void {
    g_camera.delay_us = delay;
}

pub fn getInterval() u32 {
    return g_camera.interval_us;
}

pub fn setIntervalTime(interval: u32) void {
    g_camera.interval_us = interval;
}

pub fn getExposure() u32 {
    return g_camera.exposure_us;
}

pub fn setExposureTime(exposure: u32) void {
    g_camera.exposure_us = exposure;
    g_camera.exposure_param = 0xff;
}

pub fn setExposureParam(exposure: u32) void {
    g_camera.exposure_us = 0;
    g_camera.exposure_param = exposure;

    updateExposure() catch {};
}

pub fn getExposureParam() u32 {
    return g_camera.exposure_param;
}

pub fn getFrames() u32 {
    return g_camera.frames;
}

pub fn setFrames(frames: u32) void {
    g_camera.frames = frames;
}

pub fn getIsoParam() u32 {
    return g_camera.iso_param;
}

pub fn setIsoParam(iso_param: u32) void {
    g_camera.iso_param = iso_param;

    updateIsoSpeed() catch {};
}

pub fn getEvents() !void {
    if (g_camera.initialized)
        return;

    if (c.EdsGetEvent() != c.EDS_ERR_OK)
        return CameraErr.CameraErr;
}

pub fn initializeCamera() !void {
    if (g_camera.initialized)
        return;

    if (c.EdsInitializeSDK() != c.EDS_ERR_OK)
        return CameraErr.InitErr;

    g_camera.initialized = true;

    detectConnectedCamera() catch {
        log.warn("Failure detecting camera", .{});
    };

    connect() catch {
        log.warn("Failure connecting to camera", .{});
    };
}

// just call this function at the end of the program
// a weird bug in EDSDK is closing fd 0 causing "close failed: Bad file descriptor"
pub fn deinitializeCamera() !void {
    if (g_camera.camera) |camera| {
        _ = c.EdsRelease(camera);
    }

    g_camera.camera = null;
    g_camera.connected = false;

    if (g_camera.initialized) {
        if (c.EdsTerminateSDK() != c.EDS_ERR_OK)
            return CameraErr.InitErr;
        g_camera.initialized = false;
    }
}

fn detectConnectedCamera() !void {
    log.info("detectConnectedCamera", .{});

    var camera_list: c.EdsCameraListRef = null;

    defer {
        if (camera_list) |list| {
            _ = c.EdsRelease(list);
        }
    }

    if (c.EdsGetCameraList(&camera_list) != c.EDS_ERR_OK) {
        return CameraErr.CameraErr;
    }

    var count: c.EdsUInt32 = 0;

    if (c.EdsGetChildCount(camera_list, &count) != c.EDS_ERR_OK) {
        return CameraErr.CameraErr;
    }

    if (count != 1) {
        return CameraErr.CameraErr;
    }

    if (g_camera.camera) |camera| {
        _ = c.EdsRelease(camera);
    }

    g_camera.camera = null;

    var camera_ref: c.EdsCameraRef = null;
    if (c.EdsGetChildAtIndex(camera_list, 0, &camera_ref) != c.EDS_ERR_OK) {
        return CameraErr.CameraErr;
    }

    if (camera_ref == null) {
        return CameraErr.CameraErr;
    }

    var device_info: c.EdsDeviceInfo = undefined;
    if (c.EdsGetDeviceInfo(camera_ref, &device_info) != c.EDS_ERR_OK) {
        _ = c.EdsRelease(camera_ref);
        return CameraErr.CameraErr;
    }

    log.info("camera detected", .{});
    g_camera.camera = camera_ref;
    g_camera.description = device_info.szDeviceDescription;
}

pub fn connect() !void {
    if (g_camera.connected)
        return;

    if (c.EdsOpenSession(g_camera.camera) != c.EDS_ERR_OK) {
        return CameraErr.CameraErr;
    }

    g_camera.connected = true;
    try filterExposures();
    try filterIsos();
    //lockUI();
    try updateExposure();
    try updateIsoSpeed();
}

pub fn disconnect() !void {
    if (!g_camera.connected)
        return;

    //g_camera.unlockUI();

    if (c.EdsCloseSession(g_camera.camera) != c.EDS_ERR_OK) {
        return CameraErr.CameraErr;
    }

    g_camera.connected = false;
}

pub fn terminate() !void {
    g_camera.running = false;

    log.info("quiting dispatcher", .{});
    g_dispatch.quitDispatcher();
    log.info("quiting dispatcher", .{});
}

pub fn takePicture() !void {
    if (!g_camera.initialized or !g_camera.connected)
        return CameraErr.InitErr;

    if (g_camera.exposure_param == 0xff) {
        try pressShutter();
        std.time.sleep(@intCast(g_camera.exposure_us * std.time.us_per_s));
        try releaseShutter();
    } else {
        try pressShutter();
        try releaseShutter();
    }

    g_camera.frames_taken += 1;
    if (g_camera.shooting and g_camera.frames_taken < g_camera.frames) {
        std.time.sleep(@intCast(g_camera.interval_us * std.time.us_per_s));

        _ = dispatchAsync(.TakePicture);
    } else {
        g_camera.shooting = false;
    }
}

pub fn startShooting() !void {
    if (!g_camera.initialized or !g_camera.connected)
        return CameraErr.InitErr;

    g_camera.frames_taken = 0;
    g_camera.shooting = true;

    try updateExposure();
    try updateIsoSpeed();

    if (g_camera.delay_us > 0) {
        std.time.sleep(@intCast(g_camera.delay_us * std.time.us_per_s));
    }

    _ = dispatchAsync(.TakePicture);
}

pub fn stopShooting() !void {
    g_camera.shooting = false;
}

fn updateExposure() !void {
    if (!g_camera.initialized or !g_camera.connected)
        return CameraErr.InitErr;

    if (g_camera.exposure_param != 0xff) {
        try setExposure(g_camera.exposure_param);
    } else {
        try setExposure(0x0c);
    }
}

fn updateIsoSpeed() !void {
    if (!g_camera.initialized or !g_camera.connected)
        return CameraErr.InitErr;

    if (g_camera.iso_param != 0xff) {
        try setIsoSpeed(g_camera.iso_param);
    } else {
        try setIsoSpeed(0x0);
    }
}

fn pressShutter() !void {
    const err = c.EdsSendCommand(
        g_camera.camera,
        c.kEdsCameraCommand_PressShutterButton,
        c.kEdsCameraCommand_ShutterButton_Completely_NonAF,
    );

    if (err != c.EDS_ERR_OK)
        return CameraErr.CameraErr;
}

fn releaseShutter() !void {
    const err = c.EdsSendCommand(
        g_camera.camera,
        c.kEdsCameraCommand_PressShutterButton,
        c.kEdsCameraCommand_ShutterButton_OFF,
    );

    if (err != c.EDS_ERR_OK)
        return CameraErr.CameraErr;
}

fn setExposure(exposure: u32) !void {
    const param: c.EdsUInt32 = exposure;

    const err = c.EdsSetPropertyData(
        g_camera.camera,
        c.kEdsPropID_Tv,
        0,
        @sizeOf(c.EdsUInt32),
        &param,
    );

    if (err != c.EDS_ERR_OK)
        return CameraErr.CameraErr;
}

fn setIsoSpeed(isoSpeed: u32) !void {
    const param: c.EdsUInt32 = isoSpeed;

    const err = c.EdsSetPropertyData(
        g_camera.camera,
        c.kEdsPropID_ISOSpeed,
        0,
        @sizeOf(c.EdsUInt32),
        &param,
    );

    if (err != c.EDS_ERR_OK)
        return CameraErr.CameraErr;
}

fn filterExposures() !void {
    exposures_len = 0;

    var property_desc: c.EdsPropertyDesc = undefined;
    if (c.EdsGetPropertyDesc(
        g_camera.camera,
        c.kEdsPropID_Tv,
        &property_desc,
    ) != c.EDS_ERR_OK)
        return CameraErr.CameraErr;

    const numElements: usize = @intCast(property_desc.numElements);
    for (0..numElements) |i| {
        const key = property_desc.propDesc[i];

        for (all_exposures) |exp| {
            if (exp.eds_param == key) {
                exposures[exposures_len] = exp;
                exposures_len += 1;
                break;
            }
        }
    }
}

fn filterIsos() !void {
    isos_len = 0;

    var property_desc: c.EdsPropertyDesc = undefined;
    if (c.EdsGetPropertyDesc(
        g_camera.camera,
        c.kEdsPropID_ISOSpeed,
        &property_desc,
    ) != c.EDS_ERR_OK)
        return CameraErr.CameraErr;

    const numElements: usize = @intCast(property_desc.numElements);
    for (0..numElements) |i| {
        const key = property_desc.propDesc[i];

        for (all_isos) |iso| {
            if (iso.eds_param == key) {
                isos[isos_len] = iso;
                isos_len += 1;
                break;
            }
        }
    }
}

pub fn signalHandler(sig: c_int) callconv(.C) void {
    //_ = sig;
    log.info("Signal Handler {}", .{sig});
    disconnect() catch {};
    terminate() catch {};
}

pub fn getEventsThread() void {
    while (g_camera.running) {
        _ = dispatchAsync(.GetEvent);
        std.time.sleep(500 * std.time.us_per_s);
    }
    log.info("Stoping events thread", .{});
}

pub fn processCommands() void {
    g_dispatch.handler();
    log.info("Stoping commands thread", .{});
}
