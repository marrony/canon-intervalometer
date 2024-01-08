const std = @import("std");

pub fn build(b: *std.Build) void {
    const use_link_hack = b.option(bool, "link-hack", "Use stub link hack") orelse false;

    b.exe_dir = "bin";

    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const exe = b.addExecutable(.{
        .name = "canon-intervalometer",
        .root_source_file = .{ .path = "src/main.zig" },
        .target = target,
        .optimize = optimize,
    });

    exe.addIncludePath(.{ .path = "src" });
    exe.addIncludePath(.{ .path = "canon-sdk/EDSDK/Header" });

    exe.linkLibC();

    if (target.result.os.tag == .macos) {
        exe.addFrameworkPath(.{ .path = "canon-sdk/EDSDK/Framework" });
        exe.linkFramework("EDSDK");
        exe.addRPathSpecial("@executable_path/Framework");
        //todo: copy Framework folder to /bin preserving symlinks
        b.installDirectory(.{
            .source_dir = .{ .path = "canon-sdk/EDSDK/Framework" },
            .install_dir = .bin,
            .install_subdir = "Framework",
        });
    } else if (target.result.os.tag == .linux) {
        const libDir = switch (target.result.cpu.arch) {
            .arm => "canon-sdk/EDSDK/Library/ARM32",
            .aarch64 => "canon-sdk/EDSDK/Library/ARM64",
            else => @panic("Unsupported CPU"),
        };

        exe.addLibraryPath(.{ .path = libDir });
        b.installBinFile(libDir ++ "/libEDSDK.so", "libEDSDK.so");

        if (use_link_hack) {
            linkLibraryHack(b, exe, target, optimize);
            exe.addRPath(.{ .path = ":." });
        } else {
            exe.linkSystemLibrary(":libEDSDK.so");
            exe.addRPathSpecial("$ORIGIN");
        }
    } else {
        @panic("OS not supported");
    }

    b.installDirectory(.{
        .source_dir = .{ .path = "web-ui" },
        .install_dir = .bin,
        .install_subdir = "web_root/assets",
    });

    b.installArtifact(exe);

    const run_cmd = b.addRunArtifact(exe);
    run_cmd.setCwd(.{ .path = "bin" });

    if (b.args) |args| {
        run_cmd.addArgs(args);
    }

    const run_step = b.step("run", "Run the app");
    run_step.dependOn(&run_cmd.step);

    const unit_tests = b.addTest(.{
        .root_source_file = .{ .path = "src/main.zig" },
        .target = target,
        .optimize = optimize,
    });

    const run_unit_tests = b.addRunArtifact(unit_tests);

    const test_step = b.step("test", "Run unit tests");
    test_step.dependOn(&run_unit_tests.step);
}

// create a stub .so with the symbols used
// in the program just to satisfy the linker
fn linkLibraryHack(b: *std.Build, exe: *std.Build.Step.Compile, target: std.Build.ResolvedTarget, optimize: std.builtin.Mode) void {
    const lib_EDSDK = b.addSharedLibrary(.{
        .name = "EDSDK",
        .target = target,
        .optimize = optimize,
    });

    exe.linkLibrary(lib_EDSDK);

    const writeFile = b.addWriteFiles();
    lib_EDSDK.step.dependOn(&writeFile.step);

    const symbols = [_][]const u8{
        "EdsSetPropertyData",
        "EdsSendCommand",
        "EdsGetPropertyDesc",
        "EdsGetDeviceInfo",
        "EdsGetChildAtIndex",
        "EdsGetChildCount",
        "EdsGetCameraList",
        "EdsOpenSession",
        "EdsInitializeSDK",
        "EdsTerminateSDK",
        "EdsRelease",
        "EdsSendStatusCommand",
        "EdsCloseSession",
        "EdsGetEvent",
    };

    inline for (symbols) |sym| {
        lib_EDSDK.addAssemblyFile(genStub(writeFile, sym));
    }
}

fn genStub(writeFile: *std.Build.Step.WriteFile, comptime sym: []const u8) std.Build.LazyPath {
    var buf: [100]u8 = undefined;

    const formatted = std.fmt.bufPrint(&buf,
        \\.global {0s}
        \\.align 4
        \\{0s}:
        \\  .byte 0
        \\
    , .{sym}) catch @panic("Fail to format");

    return writeFile.add(sym ++ ".S", formatted);
}
