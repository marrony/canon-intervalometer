OS := $(shell uname -s)
ARCH := $(shell uname -m)

CC = zig cc

common_cflags := -isystem ./canon-sdk/EDSDK/Header

LDFLAGS := -lpthread
CFLAGS := $(common_cflags) -std=gnu17 -Wall -Werror -pedantic
# CFLAGS += -Wsystem-headers
DEPS :=

ifeq ($(shell uname -s)-$(shell uname -m),$(OS)-$(ARCH))
TARGET := --target=native
else
#arch-os-abi
ifeq ($(OS)-$(ARCH),Darwin-arm64)
TARGET := --target=arm-macos-macabi
endif

ifeq ($(OS)-$(ARCH),Linux-armv7l)
TARGET := --target=arm-linux-gnueabihf
endif

ifeq ($(OS)-$(ARCH),Linux-armv8)
TARGET := --target=aarch64-linux-gnueabihf
endif
endif

#disable cross compilation on gcc
ifeq ($(CC),gcc)
TARGET :=
endif

#disable cross compilation on clang
ifeq ($(CC),clang)
TARGET :=
endif

ifeq ($(OS),Darwin)
LDFLAGS += -F canon-sdk/EDSDK/Framework -framework EDSDK
LDFLAGS += -Wl,-rpath -Wl,@executable_path/Framework
CFLAGS += -D__MACOS__ -D__APPLE__
DEPS += bin/Framework/EDSDK.framework
endif

ifeq ($(OS),Linux)
ifeq ($(ARCH),armv7l)
LIB_DIR = canon-sdk/EDSDK/Library/ARM32
endif

ifeq ($(ARCH),armv8)
LIB_DIR = canon-sdk/EDSDK/Library/ARM64
endif

LDFLAGS += '-Wl,-rpath,\$$ORIGIN'
LDFLAGS += '-Wl,-rpath,.'
LDFLAGS += -L$(LIB_DIR) -l :libEDSDK.so
CFLAGS += -DTARGET_OS_LINUX
DEPS += bin/libEDSDK.so
endif

LDFLAGS += $(TARGET)
CFLAGS += $(TARGET)
DEPS += bin/web_root/assets

HDRS := src/camera.h src/http.h src/queue.h src/timer.h src/mongoose.h
SRCS := src/main.c src/camera.c src/http.c src/queue.c src/timer.c src/mongoose.c
OBJS := $(patsubst src/%.c, bin/%.o, $(SRCS))

.PHONY: all sync scp cppcheck update-mongoose defs

all: bin $(DEPS) bin/run-canon.sh bin/canon-intervalometer

ifeq ($(SCP_DEST),)
scp:
	@echo "SCP_DEST not provided"
else
scp: bin/canon-intervalometer bin/libEDSDK.so bin/run-canon.sh bin/web_root/assets
	scp bin/canon-intervalometer $(SCP_DEST)/bin/
	scp bin/libEDSDK.so $(SCP_DEST)/bin/
	scp bin/run-canon.sh $(SCP_DEST)/bin/
	scp -r bin/web_root $(SCP_DEST)/bin/
endif

bin/run-canon.sh: run-canon.sh bin/canon-intervalometer Makefile
	cp run-canon.sh bin/run-canon.sh

bin/canon-intervalometer: $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

bin/%.o: src/%.c $(HDRS) Makefile
	$(CC) $(CFLAGS) -o $@ -c $<

web_root_deps := web-ui/index.css web-ui/index.js web-ui/htmx.min.js

bin/web_root/assets: $(web_root_deps)
	mkdir -p bin/web_root/assets
	rm -rf bin/web_root/assets/*
	cp -R web-ui/* bin/web_root/assets/

bin/Framework/EDSDK.framework:
	mkdir -p bin/Framework/
	cp -R canon-sdk/EDSDK/Framework/EDSDK.framework bin/Framework

bin/libEDSDK.so:
	cp $(LIB_DIR)/libEDSDK.so bin/

bin:
	mkdir -p bin

sync:
	git submodule sync
	git submodule update --init --recursive --remote

update-mongoose:
	make sync
	cp -f mongoose/mongoose.c src/mongoose.c
	cp -f mongoose/mongoose.h src/mongoose.h

to_check := $(filter-out src/mongoose.c, $(SRCS))

cppcheck:
	cppcheck --force --enable=all --suppress=missingIncludeSystem $(common_cflags) $(to_check)

defs:
	$(CC) $(CFLAGS) -dM -E -x c /dev/null

