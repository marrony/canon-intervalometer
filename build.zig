const std = @import("std");

// Although this function looks imperative, note that its job is to
// declaratively construct a build graph that will be executed by an external
// runner.
pub fn build(b: *std.Build) void {
    b.exe_dir = "bin";

    // Standard target options allows the person running `zig build` to choose
    // what target to build for. Here we do not override the defaults, which
    // means any target is allowed, and the default is native. Other options
    // for restricting supported target set are available.
    const target = b.standardTargetOptions(.{});

    // Standard optimization options allow the person running `zig build` to select
    // between Debug, ReleaseSafe, ReleaseFast, and ReleaseSmall. Here we do not
    // set a preferred release mode, allowing the user to decide how to optimize.
    const optimize = b.standardOptimizeOption(.{});

    const exe = b.addExecutable(.{
        .name = "canon-intervalometer",
        .target = target,
        .optimize = optimize,
    });

    exe.addIncludePath(.{ .path = "src" });
    exe.addIncludePath(.{ .path = "canon-sdk/EDSDK/Header" });

    const sources = [_][]const u8{ "src/camera.c", "src/http.c", "src/main.c", "src/mongoose.c", "src/queue.c", "src/timer.c" };
    const flags = [_][]const u8{"-std=gnu17"};

    exe.addCSourceFiles(&sources, &flags);

    if (target.isDarwin()) {
        exe.defineCMacroRaw("__MACOS__");
        exe.defineCMacroRaw("__APPLE__");
        exe.addFrameworkPath(.{ .path = "canon-sdk/EDSDK/Framework" });
        exe.linkFramework("EDSDK");
        exe.addRPath(.{ .path = "@executable_path/Framework" });
        //todo: copy Framework to /bin
    } else if (target.isLinux()) {
        exe.defineCMacroRaw("TARGET_OS_LINUX");

        if (target.getCpu().arch == .arm) {
            exe.addLibraryPath(.{ .path = "canon-sdk/EDSDK/Library/ARM32" });
        }

        if (target.getCpu().arch == .aarch64) {
            exe.addLibraryPath(.{ .path = "canon-sdk/EDSDK/Library/ARM64" });
        }

        exe.linkSystemLibrary("EDSDK");
        exe.addRPath(.{ .path = "$ORIGIN" });
        exe.addRPath(.{ .path = "." });
    } else {
        @panic("OS not supported");
    }

    // This declares intent for the executable to be installed into the
    // standard location when the user invokes the "install" step (the default
    // step when running `zig build`).
    b.installArtifact(exe);

    // This *creates* a Run step in the build graph, to be executed when another
    // step is evaluated that depends on it. The next line below will establish
    // such a dependency.
    const run_cmd = b.addRunArtifact(exe);

    // By making the run step depend on the install step, it will be run from the
    // installation directory rather than directly from within the cache directory.
    // This is not necessary, however, if the application depends on other installed
    // files, this ensures they will be present and in the expected location.
    run_cmd.step.dependOn(b.getInstallStep());

    // This allows the user to pass arguments to the application in the build
    // command itself, like this: `zig build run -- arg1 arg2 etc`
    if (b.args) |args| {
        run_cmd.addArgs(args);
    }

    // This creates a build step. It will be visible in the `zig build --help` menu,
    // and can be selected like this: `zig build run`
    // This will evaluate the `run` step rather than the default, which is "install".
    const run_step = b.step("run", "Run the app");
    run_step.dependOn(&run_cmd.step);
}
