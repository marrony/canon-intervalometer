
bin/main-mongoose: bin src/main-mongoose.c src/mongoose.c src/mongoose.h
	gcc -Wall -Werror src/main-mongoose.c src/mongoose.c  -o bin/main-mongoose

bin:
	mkdir -p bin
