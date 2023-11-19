all: bin
	cd web-ui && npm run build
	mkdir -p bin/web_root
	rm -rf bin/web_root
	mkdir -p bin/web_root
	cp -r web-ui/dist/* bin/web_root/
	make bin/main

bin/main: bin src/main.c src/mongoose.c src/mongoose.h
	gcc -Wall -Werror src/main.c src/mongoose.c  -o bin/main

bin:
	mkdir -p bin
