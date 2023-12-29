#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <vector>
#include "types.h"

uint32 readuint32(FILE *f) {
    uint32 x;
    fread(&x, 4, 1, f);
    return x;
}

uint16 readuint16(FILE *f) {
    uint16 x;
    fread(&x, 2, 1, f);
    return x;
}

byte readbyte(FILE *f) {
    byte x;
    fread(&x, 1, 1, f);
    return x;
}

int fskip(FILE *f, int len) {
    return fseek(f, len, SEEK_CUR);
}
