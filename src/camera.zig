const std = @import("std");
const queue = @import("queue.zig");
const log = std.log.scoped(.camera);

const edsdk = @cImport({
    @cDefine("__MACOS__", "");
    @cDefine("__APPLE__", "");
    //@cDefine("TARGET_OS_LINUX", "");
    @cInclude("stdbool.h");
    @cInclude("EDSDK.h");
});

const CommandErr = error{
    InitErr,
    CameraErr,
};

const Iso = struct {
    description: []const u8,
    eds_param: edsdk.EdsUInt32,
};

const Exposure = struct {
    description: []const u8,
    eds_param: edsdk.EdsUInt32,
};

const Command = union(enum) {
    Null: void,
    Initialize: void,
    Deinitialize: void,
    Connect: void,
    Disconnect: void,
    TakePicture: void,
    StartShooting: void,
    StopShooting: void,
    Terminate: void,

    pub fn execute(cmd: Command) CommandErr!void {
        switch (cmd) {
            .Null => log.info("Null", .{}),
            .Initialize => try g_camera.initializeCamera(),
            .Deinitialize => try g_camera.deinitializeCamera(),
            .Connect => try g_camera.connect(),
            .Disconnect => try g_camera.disconnect(),
            .TakePicture => try g_camera.takePicture(),
            .StartShooting => try g_camera.startShooting(),
            .StopShooting => try g_camera.stopShooting(),
            .Terminate => try g_camera.terminate(),
            //else => @panic("What?"),
        }
    }
};

pub const Camera = struct {
    camera: edsdk.EdsCameraRef = undefined,
    running: bool,
    iso_index: usize,
    exposure_index: usize,
    delay_us: i32,
    exposure_us: i32,
    interval_us: i32,
    frames: i32,
    frames_taken: i32,
    initialized: bool,
    connected: bool,
    shooting: bool,
    description: [256]u8 = undefined,

    pub fn inputs_enabled(self: *const Camera) bool {
        return self.initialized and self.connected and !self.shooting;
    }

    pub fn initializeCamera(self: *Camera) !void {
        if (self.initialized)
            return;

        if (edsdk.EdsInitializeSDK() != edsdk.EDS_ERR_OK)
            return CommandErr.InitErr;

        self.initialized = true;

        self.detectConnectedCamera() catch try self.deinitializeCamera();
    }

    pub fn deinitializeCamera(self: *Camera) !void {
        self.initialized = false;

        if (edsdk.EdsTerminateSDK() != edsdk.EDS_ERR_OK)
            return CommandErr.InitErr;
    }

    pub fn connect(self: *Camera) !void {
        if (self.connected)
            return;

        errdefer {
            self.deinitializeCamera() catch {};
        }

        if (edsdk.EdsOpenSession(self.camera) != edsdk.EDS_ERR_OK) {
            try self.deinitializeCamera();
            return CommandErr.CameraErr;
        }

        self.connected = true;
        try self.filterExposures();
        try self.filterIsos();
        //self.lockUI();
        try self.updateExposure();
        try self.updateIsoSpeed();
    }

    pub fn disconnect(self: *Camera) !void {
        if (!self.connected)
            return;

        //self.unlockUI();

        if (edsdk.EdsCloseSession(self.camera) != edsdk.EDS_ERR_OK) {
            try self.deinitializeCamera();
            return CommandErr.CameraErr;
        }

        self.connected = false;
    }

    pub fn terminate(self: *Camera) !void {
        self.running = false;
    }

    pub fn takePicture(self: *Camera) !void {
        if (!self.initialized or !self.connected)
            return CommandErr.InitErr;

        if (self.exposure_index < exposures.len) {
            try self.pressShutter();
            try self.releaseShutter();
        } else {
            try self.pressShutter();
            std.time.sleep(@as(u64, @intCast(self.exposure_us * std.time.us_per_s)));
            try self.releaseShutter();
        }

        self.frames_taken += 1;
        if (self.shooting and self.frames_taken < self.frames) {
            std.time.sleep(@as(u64, @intCast(self.interval_us * std.time.us_per_s)));

            g_queue.putAsync(.TakePicture);
        } else {
            self.shooting = false;
        }
    }

    pub fn startShooting(self: *Camera) !void {
        if (!self.initialized or !self.connected)
            return CommandErr.InitErr;

        self.frames_taken = 0;
        self.shooting = true;

        try self.updateExposure();
        try self.updateIsoSpeed();

        if (self.delay_us > 0) {
            std.time.sleep(@as(u64, @intCast(self.delay_us * std.time.us_per_s)));
        }

        g_queue.putAsync(.TakePicture);
    }

    pub fn stopShooting(self: *Camera) !void {
        self.shooting = false;
    }

    fn updateExposure(self: *Camera) !void {
        if (!self.initialized or !self.connected)
            return CommandErr.InitErr;

        const eds_param = if (self.exposure_index < exposures.len) exposures[self.exposure_index].eds_param else 0x0c;

        try self.setExposure(eds_param);
    }

    fn updateIsoSpeed(self: *Camera) !void {
        if (!self.initialized or !self.connected)
            return CommandErr.InitErr;

        const eds_param = if (self.iso_index < isos.len) isos[self.iso_index].eds_param else 0x0;

        try self.setIsoSpeed(eds_param);
    }

    fn pressShutter(self: *Camera) !void {
        const err = edsdk.EdsSendCommand(
            self.camera,
            edsdk.kEdsCameraCommand_PressShutterButton,
            edsdk.kEdsCameraCommand_ShutterButton_Completely_NonAF,
        );

        if (err != edsdk.EDS_ERR_OK)
            return CommandErr.CameraErr;
    }

    fn releaseShutter(self: *Camera) !void {
        const err = edsdk.EdsSendCommand(
            self.camera,
            edsdk.kEdsCameraCommand_PressShutterButton,
            edsdk.kEdsCameraCommand_ShutterButton_OFF,
        );

        if (err != edsdk.EDS_ERR_OK)
            return CommandErr.CameraErr;
    }

    fn setExposure(self: *Camera, exposure: edsdk.EdsUInt32) !void {
        const err = edsdk.EdsSetPropertyData(
            self.camera,
            edsdk.kEdsPropID_Tv,
            0,
            @sizeOf(edsdk.EdsUInt32),
            &exposure,
        );

        if (err != edsdk.EDS_ERR_OK)
            return CommandErr.CameraErr;
    }

    fn setIsoSpeed(self: *Camera, isoSpeed: edsdk.EdsUInt32) !void {
        const err = edsdk.EdsSetPropertyData(
            self.camera,
            edsdk.kEdsPropID_ISOSpeed,
            0,
            @sizeOf(edsdk.EdsUInt32),
            &isoSpeed,
        );

        if (err != edsdk.EDS_ERR_OK)
            return CommandErr.CameraErr;
    }

    fn filterExposures(self: *Camera) !void {
        exposures_len = 0;

        var property_desc: edsdk.EdsPropertyDesc = undefined;
        if (edsdk.EdsGetPropertyDesc(
            self.camera,
            edsdk.kEdsPropID_Tv,
            &property_desc,
        ) != edsdk.EDS_ERR_OK)
            return CommandErr.CameraErr;

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

    fn filterIsos(self: *Camera) !void {
        isos_len = 0;

        var property_desc: edsdk.EdsPropertyDesc = undefined;
        if (edsdk.EdsGetPropertyDesc(
            self.camera,
            edsdk.kEdsPropID_ISOSpeed,
            &property_desc,
        ) != edsdk.EDS_ERR_OK)
            return CommandErr.CameraErr;

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

    fn detectConnectedCamera(self: *Camera) !void {
        var camera_list: edsdk.EdsCameraListRef = null;

        defer {
            if (camera_list != null)
                _ = edsdk.EdsRelease(camera_list);
        }

        if (edsdk.EdsGetCameraList(&camera_list) != edsdk.EDS_ERR_OK) {
            return CommandErr.CameraErr;
        }

        var count: edsdk.EdsUInt32 = 0;

        if (edsdk.EdsGetChildCount(camera_list, &count) != edsdk.EDS_ERR_OK) {
            return CommandErr.CameraErr;
        }

        if (count != 1) {
            return CommandErr.CameraErr;
        }

        if (self.camera != null) {
            _ = edsdk.EdsRelease(self.camera);
            self.camera = null;
        }

        var camera_ref: edsdk.EdsCameraRef = null;
        if (edsdk.EdsGetChildAtIndex(camera_list, 0, &camera_ref) != edsdk.EDS_ERR_OK) {
            return CommandErr.CameraErr;
        }

        if (camera_ref == null) {
            return CommandErr.CameraErr;
        }

        var device_info: edsdk.EdsDeviceInfo = undefined;
        if (edsdk.EdsGetDeviceInfo(camera_ref, &device_info) != edsdk.EDS_ERR_OK) {
            _ = edsdk.EdsRelease(camera_ref);
            return CommandErr.CameraErr;
        }

        self.camera = camera_ref;
        self.description = device_info.szDeviceDescription;
        // std.mem.copyForwards(u8, self.description[0..], &device_info.szDeviceDescription);
    }
};

pub var g_camera = Camera{
    .running = false,
    .iso_index = 0,
    .exposure_index = 0,
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

pub const CommandQueue = queue.Queue(Command, 8);

pub var g_queue: CommandQueue = .{};

pub fn process_commands() void {
    while (true) {
        if (g_queue.get(500 * std.time.us_per_s, Command.execute)) {
            log.info("Consumed", .{});
        } else |err| switch (err) {
            error.Timeout => _ = edsdk.EdsGetEvent(),
            else => {
                log.err("command error: {}\n", .{err});
                if (@errorReturnTrace()) |trace| {
                    std.debug.dumpStackTrace(trace.*);
                }
            },
        }
    }
}

const all_exposures = [_]Exposure{
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

const all_isos = [_]Iso{
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

var exposures: [all_exposures.len]Exposure = undefined;
var exposures_len: usize = 0;

var isos: [all_isos.len]Iso = undefined;
var isos_len: usize = 0;

pub fn getExposures() []const Exposure {
    return exposures[0..exposures_len];
}

pub fn getIsos() []const Iso {
    return isos[0..isos_len];
}
