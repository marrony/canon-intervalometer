UNAME_S := $(shell uname -s)

LDFLAGS := -lpthread
CFLAGS := -Icanon-sdk/EDSDK/Header
DEPS :=

ifeq ($(UNAME_S),Darwin)
LDFLAGS += -Fcanon-sdk/EDSDK/Framework -framework EDSDK -rpath @executable_path/Framework
CFLAGS += -D__MACOS__
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

SRCS := src/main.c src/mongoose.c
OBJS := $(patsubst src/%.c, bin/%.o, $(SRCS))

all: bin bin/web_root bin/main

.PHONY: build_dist

build_dist:
	mkdir -p web-ui/dist
	rm -rf web-ui/dist/*
	cd web-ui && VITE_BACKEND_URL= VITE_POOLING_TIME=2000 npm run build

bin/main: $(OBJS)
	gcc $(LDFLAGS) -o $@ $^

bin/%.o: src/%.c Makefile
	gcc -Wall $(CFLAGS) -o $@ -c $<

web_root_deps :=

# it's very time consuming build web-ui on raspberry pi
ifeq ($(UNAME_S),Darwin)
web_root_deps += build_dist
endif

web_root_deps += $(wildcard web-ui/dist/*)

bin/web_root: $(web_root_deps)
	mkdir -p bin/web_root
	rm -rf bin/web_root/*
	cp -r web-ui/dist/* bin/web_root/

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
