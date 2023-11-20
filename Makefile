UNAME_S := $(shell uname -s)

LDFLAGS :=
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
endif
ifeq ($(shell uname -m),armv8)
LIB_DIR = canon-sdk/EDSDK/Library/ARM64
endif
LDFLAGS += -lEDSDK -L$(LIB_DIR) -Wl,-rpath -Wl,\$$ORIGIN
CFLAGS += -DTARGET_OS_LINUX
DEPS += bin/libEDSDK.so
endif

all: bin
	cd web-ui && npm run build
	mkdir -p bin/web_root
	rm -rf bin/web_root
	mkdir -p bin/web_root
	cp -r web-ui/dist/* bin/web_root/
	make bin/main

bin/main: bin $(DEPS) src/main.c src/mongoose.c src/mongoose.h
	gcc -Wall -Werror src/main.c src/mongoose.c $(CFLAGS) $(LDFLAGS) -o bin/main

bin/Framework/EDSDK.framework:
	mkdir -p bin/Framework/
	cp -r canon-sdk/EDSDK/Framework/EDSDK.framework bin/Framework

bin/libEDSDK.so:
	cp $(LIB_DIR)/libEDSDK.so bin/

bin:
	mkdir -p bin
