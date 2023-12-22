UNAME_S := $(shell uname -s)

#CC = zig cc
CC = gcc

common_cflags := -isystem ./canon-sdk/EDSDK/Header

LDFLAGS := -lpthread
CFLAGS := $(common_cflags) -Wall -Werror -pedantic
# CFLAGS += -Wsystem-headers
DEPS :=

ifeq ($(UNAME_S),Darwin)
LDFLAGS += -Fcanon-sdk/EDSDK/Framework -framework EDSDK
LDFLAGS += -Wl,-rpath -Wl,@executable_path/Framework
CFLAGS += --std=c17 -D__MACOS__ -D__APPLE__
DEPS += bin/Framework/EDSDK.framework
endif

ifeq ($(UNAME_S),Linux)
ifeq ($(shell uname -m),armv7l)
LIB_DIR = canon-sdk/EDSDK/Library/ARM32
DEPS += bin/web_root
endif
ifeq ($(shell uname -m),armv8)
LIB_DIR = canon-sdk/EDSDK/Library/ARM64
endif
LDFLAGS += -lEDSDK -L$(LIB_DIR)
LDFLAGS += -Wl,-rpath -Wl,\$$ORIGIN
CFLAGS += -DTARGET_OS_LINUX
DEPS += bin/libEDSDK.so
endif

HDRS := src/camera.h src/http.h src/queue.h src/timer.h src/mongoose.h
SRCS := src/main.c src/camera.c src/http.c src/queue.c src/timer.c src/mongoose.c
OBJS := $(patsubst src/%.c, bin/%.o, $(SRCS))

.PHONY: all sync cppcheck update-mongoose defs

all: bin bin/web_root/assets bin/run-canon.sh bin/canon-intervalometer $(DEPS)

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

