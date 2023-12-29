all: bmpparse reader

bmpparse.o: bmpparse.cpp
	clang -c bmpparse.cpp

read.o: read.cpp
	clang -c read.cpp

reader.o: reader.cpp
	clang -c reader.cpp

bmpparse: bmpparse.o read.o
	clang -lc++ -o bmpparse bmpparse.o read.o

reader: reader.o read.o
	clang -lc++ -o reader reader.o read.o
