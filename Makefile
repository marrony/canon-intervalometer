.PHONY: rpi

mac:
	zig build --verbose-link

rpi:
	zig build -Dtarget=arm-linux-gnueabihf --verbose-link
