UNAME_S := $(shell uname -s)

LDFLAGS := -lpthread
CFLAGS := -Icanon-sdk/EDSDK/Header
DEPS :=

ifeq ($(UNAME_S),Darwin)
LDFLAGS += -Fcanon-sdk/EDSDK/Framework -framework EDSDK -rpath @executable_path/Framework
CFLAGS += -D__MACOS__ -D__APPLE__
DEPS += bin/Framework/EDSDK.framework
endif

ifeq ($(UNAME_S),Linux)
ifeq ($(shell uname -m),armv7l)
LIB_DIR += canon-sdk/EDSDK/Library/ARM32
DEPS += bin/web_root
endif
ifeq ($(shell uname -m),armv8)
LIB_DIR = canon-sdk/EDSDK/Library/ARM64
endif
LDFLAGS += -lEDSDK -L$(LIB_DIR) -Wl,-rpath -Wl,\$$ORIGIN
CFLAGS += -DTARGET_OS_LINUX
DEPS += bin/libEDSDK.so
endif

HDRS := src/queue.h src/timer.h src/mongoose.h
SRCS := src/main.c src/queue.c src/timer.c src/mongoose.c
OBJS := $(patsubst src/%.c, bin/%.o, $(SRCS))

all: bin bin/web_root bin/main

bin/main: $(OBJS)
	gcc $(LDFLAGS) -o $@ $^

bin/%.o: src/%.c $(HDRS) Makefile
	gcc -Wall $(CFLAGS) -o $@ -c $<

web_root_deps := web-ui/index.css web-ui/index.js web-ui/htmx.min.js

bin/web_root: $(web_root_deps)
	mkdir -p bin/web_root
	rm -rf bin/web_root/*
	cp -r web-ui/* bin/web_root/

bin/Framework/EDSDK.framework:
	mkdir -p bin/Framework/
	cp -r canon-sdk/EDSDK/Framework/EDSDK.framework bin/Framework

bin/libEDSDK.so:
	cp $(LIB_DIR)/libEDSDK.so bin/

bin:
	mkdir -p bin

sync:
	git submodule sync
	git submodule update --init --recursive --remote

cppcheck:
	cppcheck --force --enable=all --suppress=missingIncludeSystem --std=c99 $(CFLAGS) src/main.c

