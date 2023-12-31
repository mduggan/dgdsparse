#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <vector>
#include <assert.h>
#include "types.h"
#include "read.h"


struct SubImgInfo {
    byte *outbuf;
    uint16 off;
    uint16 w;
    uint16 h;
};

struct DecoderState {
    uint32 offset;
    byte *inputPtr;
    byte *dstPtr;
    uint16 rowStarts[200];
};


static inline uint16 getBits(struct DecoderState *decoder, int nbits) {
    const uint32 offset = decoder->offset;
   const uint32 index = offset >> 3;
   const uint32 shift = offset & 7;
   decoder->offset += nbits;
   return (*(uint16 *)(decoder->inputPtr + index) >> (shift)) & (byte)(0xff00 >> (16 - nbits));
}

void doVqtDecode2(struct DecoderState *state, const word x, const word y, const word w, const word h) {
    // Empty region -> nothing to do
    if (h == 0 || w == 0)
        return;

    // 1x1 region -> put the byte directly
    if (w == 1 && h == 1) {
        state->dstPtr[state->rowStarts[y] + x] = getBits(state, 8);
        return;
    }

    const uint losize = (w & 0xff) * (h & 0xff);
    uint bitcount1 = 8;
    if (losize < 256) {
        bitcount1 = 0;
        byte b = (byte)(losize - 1);
        do {
            bitcount1++;
            b >>= 1;
        } while (b != 0);
    }

    uint16 firstval = getBits(state, bitcount1);

    uint16 bitcount2 = 0;
    byte bval = (byte)firstval;
    while (firstval != 0) {
        bitcount2++;
        firstval >>= 1;
    }

    bval++;

    if (losize * 8 <= losize * bitcount2 + bval * 8) {
        for (uint xx = x; xx < x + w; xx++) {
            for (uint yy = y; yy < y + h; yy++) {
                state->dstPtr[state->rowStarts[yy] + xx] = getBits(state, 8);
            }
        }
        return;
    }

    if (bval == 1) {
        const uint16 val = getBits(state, 8);
        for (uint yy = y; yy < y + h; yy++) {
            for (uint xx = x; xx < x + w; xx++) {
                state->dstPtr[state->rowStarts[yy] + xx] = val;
            }
        }
        return;
    }

    byte tmpbuf [262];
    byte *ptmpbuf = tmpbuf;
    for (; bval != 0; bval--) {
        *ptmpbuf = getBits(state, 8);
        ptmpbuf++;
    }

    for (uint xx = x; xx < x + w; xx++) {
        for (uint yy = y; yy < y + h; yy++) {
            state->dstPtr[state->rowStarts[yy] + xx] = tmpbuf[getBits(state, bitcount2)];
        }
    }
}


////////////////////////////////////////////////////////////////////

void doVqtDecode(struct DecoderState *state, word x,word y,word w,word h) {
    if (!w && !h)
        return;

    const uint16 mask = getBits(state, 4);

    // Top left quadrant
    if (mask & 8)
        doVqtDecode(state, x, y, w / 2, h / 2);
    else
        doVqtDecode2(state, x, y, w / 2, h / 2);

    // Top right quadrant
    if (mask & 4)
        doVqtDecode(state, x + (w / 2), y, (w + 1) >> 1, h >> 1);
    else
        doVqtDecode2(state, x + (w / 2), y, (w + 1) >> 1, h >> 1);

    // Bottom left quadrant
    if (mask & 2)
        doVqtDecode(state, x, y + (h / 2), w / 2, (h + 1) / 2);
    else
        doVqtDecode2(state, x, y + (h / 2), w / 2, (h + 1) / 2);

    // Bottom right quadrant
    if (mask & 1)
        doVqtDecode(state, x + (w / 2), y + (h / 2), (w + 1) / 2, (h + 1) / 2);
    else
        doVqtDecode2(state, x + (w / 2), y + (h / 2), (w + 1) / 2, (h + 1) / 2);
}


////////////////////////////////////////////////////////////////////

void parsevqt(FILE *f, vector<SubImgInfo> &imgs, const char *fname) {
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
    bool gotoffsets = false;
    if (osize == 2) {
        uint16 x = readuint16(f);
        printf("WARN: expect %d bytes of offsets for %d imgs, got single short 0x%X\n", (int)imgs.size() * 4, (int)imgs.size(), x);
        for (int i = 0; i < imgs.size(); i++)
            imgs[i].off = 0;
    } else if (osize != imgs.size() * 4) {
        printf("ERROR: expect %d bytes of offsets for %d imgs, got %d\n", (int)imgs.size() * 4, (int)imgs.size(), osize);
        exit(1);
    } else {
        for (int i = 0; i < imgs.size(); i++) {
            imgs[i].off = readuint32(f);
        }
        gotoffsets = true;
    }

    printf("  VQT format BMP with %d sub-imgs (%d bytes)\n", (int)imgs.size(), vsize);
    printf("  Width\tHeight\tOffset\n");
    byte *buf = (byte *)malloc(64 * 1024);
    memset(buf, 0, 64*1024);
    if (gotoffsets) {
        for (int i = 0; i < imgs.size(); i++) {
            uint16 w = imgs[i].w;
            uint16 h = imgs[i].h;
            printf("  %d\t%d\t%d\n", w, h, imgs[i].off);
            byte *outbuf = imgs[i].outbuf;
            if (!outbuf)
                continue;
            fseek(f, vqtstart + imgs[i].off, SEEK_SET);

            if (i < imgs.size() - 1)
                fread(buf, 1, imgs[i + 1].off - imgs[i].off, f);
            else
                fread(buf, 1, vsize - imgs[i].off, f);

            // TODO: Do something to decode the data in buf - decode into img[i].outbuf
            DecoderState state;
            state.offset = 0;
            state.inputPtr = buf;
            state.dstPtr = outbuf;

            for (int r = 0; r < h; r++)
                state.rowStarts[r] = w * r;

            doVqtDecode(&state, 0, 0, w, h);

            char tmpbuf[128];
            snprintf(tmpbuf, 128, "%s-img-%02d-%d-%d.pgm", fname, i, w, h);
            FILE *dmpfile = fopen(tmpbuf, "wb");
            fprintf(dmpfile, "P5 %d %d 255\n", w, h);
            fwrite(outbuf, w * h, 1, dmpfile);

            fclose(dmpfile);
        }
    } else {
        fseek(f, vqtstart, SEEK_SET);
        fread(buf, 1, vsize, f);
        uint32 lastoffset = 0;
        for (int i = 0; i < imgs.size(); i++) {
            uint16 w = imgs[i].w;
            uint16 h = imgs[i].h;
            printf("  %d\t%d\t%d\n", w, h, lastoffset / 8);
            byte *outbuf = imgs[i].outbuf;
            if (!outbuf)
                continue;

            DecoderState state;
            state.offset = ((lastoffset + 7) / 8) * 8;
            state.inputPtr = buf;
            state.dstPtr = outbuf;

            for (int r = 0; r < h; r++)
                state.rowStarts[r] = w * r;

            doVqtDecode(&state, 0, 0, w, h);

            char tmpbuf[128];
            snprintf(tmpbuf, 128, "%s-img-%02d-%d-%d.pgm", fname, i, w, h);
            FILE *dmpfile = fopen(tmpbuf, "wb");
            fprintf(dmpfile, "P5 %d %d 255\n", w, h);
            fwrite(outbuf, w * h, 1, dmpfile);

            fclose(dmpfile);
            lastoffset = state.offset;
        }
    }

}

void parsebin(FILE *f, vector<SubImgInfo> &imgs) {
    uint32 csize = readuint32(f);
    bool morechunks = csize & 0x80000000;
    if (morechunks) {
        printf("ERROR: expect BIN should not contain other chunks\n");
        exit(1);
    }
    printf("  BIN format BMP with %d sub-imgs\n", (int)imgs.size());
}

void readbmp(FILE *f, const char *fname) {
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
    vector<SubImgInfo> imgs;
    imgs.resize(nimgs);
    for (int i = 0; i < nimgs; i++)
        imgs[i].w = readuint16(f);
    for (int i = 0; i < nimgs; i++)
        imgs[i].h = readuint16(f);
    for (int i = 0; i < nimgs; i++) {
        imgs[i].off = 0;
        if (imgs[i].w * imgs[i].h > 0) {
            int nbytes = imgs[i].w * ((imgs[i].h + 4) / 4) * 4;
            imgs[i].outbuf = (byte *)malloc(nbytes);
            memset(imgs[i].outbuf, 0, nbytes);
        }
    }

    fread(fourcc, 4, 1, f);
    if (!strncmp(fourcc, "VQT:", 4)) {
        parsevqt(f, imgs, fname);
    } else if (!strncmp(fourcc, "BIN:", 4)) {
        parsebin(f, imgs);
    } else {
        printf("ERROR: don't support chunk type '%s' here yet\n", fourcc);
        exit(2);
    }
}

int main(int argc, char **argv) {
    const char *fname;
    if (argc < 2) {
        printf("usage: bmpparse FILE.BMP\n");
        fname = "extracted/VOLUME.002/CLMUTSLO.BMP";
        printf("default to file %s\n", fname);
    } else {
        fname = argv[1];
    }

    FILE *f = fopen(fname, "rb");
    if (f == NULL) {
        printf("ERROR: couldn't open %s\n", fname);
        exit(1);
    }
    readbmp(f, fname);

    return 0;
}
