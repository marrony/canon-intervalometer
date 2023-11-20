all: bin
	cd web-ui && npm run build
	mkdir -p bin/web_root
	rm -rf bin/web_root
	mkdir -p bin/web_root
	cp -r web-ui/dist/* bin/web_root/
	make bin/main

bin/main: bin bin/Framework/EDSDK.framework src/main.c src/mongoose.c src/mongoose.h
	gcc -Wall -Werror -D__MACOS__ src/main.c src/mongoose.c -Icanon-sdk/EDSDK/Header -Fcanon-sdk/EDSDK/Framework -framework EDSDK -rpath @executable_path/Framework -o bin/main

bin/Framework/EDSDK.framework:
	mkdir -p bin/Framework/
	cp -r canon-sdk/EDSDK/Framework/EDSDK.framework bin/Framework

bin:
	mkdir -p bin
