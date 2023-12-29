#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <vector>
#include "types.h"
#include "read.h"

struct Img {
    uint16 w;
    uint16 h;
    uint16 off;
};

void parsevqt(FILE *f, vector<Img> &imgs) {
    uint32 vsize = readuint32(f);
    bool morechunks = vsize & 0x80000000;
    if (morechunks) {
        printf("ERROR: expect VQT should not contain other chunks\n");
        exit(1);
    }

    uint32 vqtstart = ftell(f);

    fskip(f, vsize);
    char fourcc[4];
    fread(fourcc, 4, 1, f);
    if (strncmp(fourcc, "OFF:", 4)) {
        printf("ERROR: expect VQT should be followed by OFF\n");
        exit(1);
    }
    uint32 osize = readuint32(f);
    if (osize == 2) {
        uint16 x = readuint16(f);
        printf("WARN: expect %d bytes of offsets for %d imgs, got single short 0x%X\n", (int)imgs.size() * 4, (int)imgs.size(), x);
    } else if (osize != imgs.size() * 4) {
        printf("ERROR: expect %d bytes of offsets for %d imgs, got %d\n", (int)imgs.size() * 4, (int)imgs.size(), osize);
        exit(1);
    } else {
        for (int i = 0; i < imgs.size(); i++) {
            imgs[i].off = readuint32(f);
        }
    }

    printf("  VQT format BMP with %d sub-imgs\n", (int)imgs.size());
    printf("  Width\tHeight\tOffset\n");
    for (int i = 0; i < imgs.size(); i++) {
        printf("  %d\t%d\t%d\n", imgs[i].w, imgs[i].h, imgs[i].off);
        fseek(f, vqtstart + imgs[i].off, SEEK_SET);
        byte *buf = (byte *)malloc(128 * 1024);
        memset(buf, 0, 128*1024);
        if (i < imgs.size() - 1) {
            fread(buf, 1, imgs[i + 1].off - imgs[i].off, f);
        } else {
            fread(buf, 1, vsize - imgs[i].off, f);
        }
    }
}

void parsebin(FILE *f, vector<Img> &imgs) {
    uint32 csize = readuint32(f);
    bool morechunks = csize & 0x80000000;
    if (morechunks) {
        printf("ERROR: expect BIN should not contain other chunks\n");
        exit(1);
    }
    printf("  BIN format BMP with %d sub-imgs\n", (int)imgs.size());
}

void readbmp(FILE *f) {
    char fourcc[4];
    fread(fourcc, 4, 1, f);
    if (strncmp(fourcc, "BMP:", 4)) {
        printf("ERROR: not a bmp file? expected 'BMP:' got '%s'\n", fourcc);
        exit(1);
    }
    uint32 csize = readuint32(f);
    bool morechunks = csize & 0x80000000;
    if (!morechunks) {
        printf("ERROR: expect more chunks after 'BMP:'\n");
        exit(1);
    }
    uint32 len = csize | 0x7fffffff;
    fread(fourcc, 4, 1, f);
    if (strncmp(fourcc, "INF:", 4)) {
        printf("ERROR: don't support chunk type '%s' here yet\n", fourcc);
        exit(2);
    }

    csize = readuint32(f);
    morechunks = csize & 0x80000000;
    if (morechunks) {
        printf("ERROR: expect INF should not contain other chunks\n");
        exit(1);
    }
    int nimgs = readuint16(f);
    vector<Img> imgs;
    imgs.resize(nimgs);
    for (int i = 0; i < nimgs; i++)
        imgs[i].w = readuint16(f);
    for (int i = 0; i < nimgs; i++)
        imgs[i].h = readuint16(f);

    fread(fourcc, 4, 1, f);
    if (!strncmp(fourcc, "VQT:", 4)) {
        parsevqt(f, imgs);
    } else if (!strncmp(fourcc, "BIN:", 4)) {
        parsebin(f, imgs);
    } else {
        printf("ERROR: don't support chunk type '%s' here yet\n", fourcc);
        exit(2);
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("usage: bmpparse FILE.BMP\n");
        exit(1);
    }
    FILE *f = fopen(argv[1], "rb");
    if (f == NULL) {
        printf("ERROR: couldn't open %s\n", argv[1]);
        exit(1);
    }
    readbmp(f);

    return 0;
}
