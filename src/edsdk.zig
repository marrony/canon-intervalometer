const builtin = @import("builtin");

pub const c = @cImport({
    if (builtin.os.tag == .macos) {
        @cDefine("__MACOS__", "");
        @cDefine("__APPLE__", "");
    } else if (builtin.os.tag == .linux) {
        @cDefine("TARGET_OS_LINUX", "1");
    }
    @cInclude("stdbool.h");
    @cInclude("EDSDK.h");
});
