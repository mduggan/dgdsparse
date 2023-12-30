all: bmpparse reader

CLANG=clang -c -g -fsanitize=address -fno-omit-frame-pointer -fPIE
LINK=clang -lc++ -fsanitize=address

bmpparse.o: bmpparse.cpp
	$(CLANG) bmpparse.cpp

read.o: read.cpp
	$(CLANG) read.cpp

reader.o: reader.cpp
	$(CLANG) reader.cpp

bmpparse: bmpparse.o read.o
	$(LINK) -o bmpparse bmpparse.o read.o

reader: reader.o read.o
	$(LINK) -o reader reader.o read.o

clean:
	rm -f bmpparse reader reader.o bmpparse.o read.o

test: bmpparse
	for x in extracted/VOLUME.001/INTRO1*.BMP; do ./bmpparse $x; done && mv extracted/VOLUME.001/*.pgm . && echo "Checking for differences.."; for x in INTRO*.pgm; do diff $x best/$x; done
