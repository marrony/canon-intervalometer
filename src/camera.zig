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

const Command = union(enum) {
    Null: void,
    Initialize: void,
    Finalize: void,
    Connect: void,
    Disconnect: void,

    pub fn execute(cmd: Command) CommandErr!void {
        switch (cmd) {
            .Null => log.info("Null", .{}),
            .Initialize => try g_state.initializeCamera(),
            .Finalize => try g_state.deinitializeCamera(),
            .Connect => log.info("Connect", .{}),
            .Disconnect => log.info("Disconnect", .{}),
            //else => @panic("What?"),
        }
    }
};

pub const CameraState = struct {
    camera: edsdk.EdsCameraRef = undefined,
    running: bool,
    iso_index: i32,
    exposure_index: i32,
    delay_us: i32,
    exposure_us: i32,
    interval_us: i32,
    frames: i32,
    frames_taken: i32,
    initialized: bool,
    connected: bool,
    shooting: bool,
    description: [256]u8 = undefined,

    pub fn inputs_enabled(self: *const CameraState) bool {
        return self.initialized and self.connected and !self.shooting;
    }

    pub fn initializeCamera(self: *CameraState) !void {
        if (self.initialized)
            return;

        if (edsdk.EdsInitializeSDK() != edsdk.EDS_ERR_OK)
            return CommandErr.InitErr;

        self.initialized = true;

        self.detectConnectedCamera() catch try self.deinitializeCamera();
    }

    pub fn deinitializeCamera(self: *CameraState) !void {
        self.initialized = false;

        if (edsdk.EdsTerminateSDK() != edsdk.EDS_ERR_OK)
            return CommandErr.InitErr;
    }

    fn detectConnectedCamera(self: *CameraState) !void {
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

pub var g_state = CameraState{
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
