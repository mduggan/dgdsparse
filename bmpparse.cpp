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
    byte *dstPtrs[4];
    uint16 rowStarts[200];
};

struct DecoderState *g_currentDecode = nullptr;


void printbits16(uint16 x) {
    for(int i=sizeof(x)<<3; i; i--)
        putchar('0'+((x>>(i-1))&1));
}

void printbits32(uint32 x) {
    for(int i=sizeof(x)<<3; i; i--)
        putchar('0'+((x>>(i-1))&1));
}

void doVqtDecode2(const word x, const word y, const word w, const word h) {
    if (h == 0 || w == 0)
        return;

    if (w == 1 && h == 1) {
        const uint32 offset = g_currentDecode->offset;
        const uint16 index = offset >> 3;
        g_currentDecode->dstPtrs[x & 3][(g_currentDecode->rowStarts[y] + x) >> 2] =
            (byte)(*(uint *)(g_currentDecode->inputPtr + index) >> (offset & 7));
        g_currentDecode->offset += 8;
        return;
    }

    const uint losize = (w & 0xff) * (h & 0xff);
    uint bitcount1 = 8;
    if ((losize >> 8) == 0) {
        bitcount1 = 0;
        byte b = (byte)(losize - 1);
        do {
            bitcount1++;
            b >>= 1;
        } while (b != 0);
    }

    uint16 firstval;
    {
        const uint32 offset = g_currentDecode->offset;
        const uint16 index = offset >> 3;
        firstval = *(uint *)(g_currentDecode->inputPtr + index) >> (offset & 7) & (byte)(0xff00 >> (0x10 - bitcount1));
        g_currentDecode->offset += bitcount1;
    }

    uint16 bitcount2 = 0;
    byte bval = (byte)firstval;
    while (firstval != 0) {
        bitcount2++;
        firstval = (byte)firstval >> 1;
    }

    bval++;

    if (losize * 8 <= losize * bitcount2 + bval * 8) {
        for (uint xx = x; xx < x + w; xx++) {
            for (uint yy = y; yy < (y + h); yy++) {
                const uint32 offset = g_currentDecode->offset;
                const uint16 index = offset >> 3;
                g_currentDecode->dstPtrs[xx & 3][(g_currentDecode->rowStarts[yy] + xx) >> 2] =
                    (byte)(*(uint *)(g_currentDecode->inputPtr + index) >> (offset & 7));
                g_currentDecode->offset += 8;
            }
        }
        return;
    }

    if (bval == 1) {
        const uint32 offset = g_currentDecode->offset;
        const uint16 index = offset >> 3;
        const uint16 val = *(uint *)(g_currentDecode->inputPtr + index) >> (offset & 7);
        for (uint yy = y; yy < y + h; yy++) {
            for (uint xx = x; xx < x + w; xx++) {
                g_currentDecode->dstPtrs[xx & 3][(g_currentDecode->rowStarts[yy] + xx) >> 2] = val;
            }
        }
        g_currentDecode->offset += 8;
        return;
    }

    byte tmpbuf [262];
    byte *ptmpbuf = tmpbuf;
    for (; bval != 0; bval--) {
        const uint32 offset = g_currentDecode->offset;
        const uint16 index = offset >> 3;
        *ptmpbuf = (byte)(*(uint *)(g_currentDecode->inputPtr + index) >> (offset & 7));
        ptmpbuf++;
        g_currentDecode->offset += 8;
    }

    for (uint xx = x; xx < x + w; xx++) {
        for (uint yy = y; yy < y + h; yy++) {
            const uint32 offset = g_currentDecode->offset;
            const uint16 index = offset >> 3;
            g_currentDecode->dstPtrs[xx & 3][(g_currentDecode->rowStarts[yy] + xx) >> 2] =
                tmpbuf[*(uint *)(g_currentDecode->inputPtr + index) >> (offset & 7) & (byte)(0xff00 >> (0x10 - bitcount2))];
            g_currentDecode->offset += bitcount2;
        }
    }
}


////////////////////////////////////////////////////////////////////

void doVqtDecode(word x,word y,word w,word h) {
    if (!w && !h)
        return;

    const uint32 offset = g_currentDecode->offset;
    const uint16 index = (offset >> 3);
    g_currentDecode->offset += 4;
    const uint16 bits = *(uint *)(g_currentDecode->inputPtr + index);
    const uint16 mask = bits >> (offset & 7);


    // Top left quadrant
    if (mask & 8)
        doVqtDecode(x, y, w / 2, h / 2);
    else
        doVqtDecode2(x, y, w / 2, h / 2);

    // Top right quadrant
    if (mask & 4)
        doVqtDecode(x + (w / 2), y, (w + 1) >> 1, h >> 1);
    else
        doVqtDecode2(x + (w / 2), y, (w + 1) >> 1, h >> 1);

    // Bottom left quadrant
    if (mask & 2)
        doVqtDecode(x, y + (h / 2), w / 2, (h + 1) / 2);
    else
        doVqtDecode2(x, y + (h / 2), w / 2, (h + 1) / 2);

    // Bottom right quadrant
    if (mask & 1)
        doVqtDecode(x + (w / 2), y + (h / 2), (w + 1) / 2, (h + 1) / 2);
    else
        doVqtDecode2(x + (w / 2), y + (h / 2), (w + 1) / 2, (h + 1) / 2);
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
    } else if (osize != imgs.size() * 4) {
        printf("ERROR: expect %d bytes of offsets for %d imgs, got %d\n", (int)imgs.size() * 4, (int)imgs.size(), osize);
        exit(1);
    } else {
        for (int i = 0; i < imgs.size(); i++) {
            imgs[i].off = readuint32(f);
        }
        gotoffsets = true;
    }

    printf("  VQT format BMP with %d sub-imgs\n", (int)imgs.size());
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

            g_currentDecode = &state;

            int npx = w * h;
            for (int j = 0; j < 4; j++) {
                g_currentDecode->dstPtrs[j] = outbuf + j * (npx / 4);
            }

            memset(state.rowStarts, 0, sizeof(state.rowStarts));
            for (int r = 0; r < h; r++)
                state.rowStarts[r] = w * r;

            doVqtDecode(0, 0, w, h);

            char tmpbuf[128];
            snprintf(tmpbuf, 128, "%s-img-%02d-%d-%d.pgm", fname, i, w, h);
            FILE *dmpfile = fopen(tmpbuf, "wb");
            fprintf(dmpfile, "P5 %d %d 255\n", w, h);
            for (int b = 0; b < npx / 4; b++) {
                fwrite(g_currentDecode->dstPtrs[0] + b, 1, 1, dmpfile);
                fwrite(g_currentDecode->dstPtrs[1] + b, 1, 1, dmpfile);
                fwrite(g_currentDecode->dstPtrs[2] + b, 1, 1, dmpfile);
                fwrite(g_currentDecode->dstPtrs[3] + b, 1, 1, dmpfile);
            }
            //fwrite(outbuf, npx, 1, dmpfile);

            fclose(dmpfile);
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
            printf("malloc %d bytes\n", nbytes);
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
