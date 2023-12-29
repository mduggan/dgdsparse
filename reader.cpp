#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <vector>
#include "types.h"
#include "read.h"

struct Resource {
    int volNum;
    uint32 checksum;
    uint32 pos;
    uint32 size;
    char name[20];
};

static void readidx(vector<Resource> &resources) {
    char pathbuf[128];
    char *databuf = (char *)malloc(1024 * 1024);
    FILE *f = fopen("VOLUME.VGA", "rb");
    if (f == NULL) {
        printf("ERROR: Couldn't open VOLUME.VGA\n");
        exit(1);
    }

    dword crc = readuint32(f);
    word nvols = readuint16(f);

    mkdir("extracted", 0777);

    for (int i = 0; i < nvols; i++) {
        char volname[13];
        memset(volname, 0, sizeof(volname));
        fread(volname, 12, 1, f);
        fskip(f, 1);
        word entries = readuint16(f);
        printf("Volume %s (%d entries):\n", volname, entries);

        printf("  Name   \tChecksum\tPos\tSize\n");
        FILE *v = fopen(volname, "rb");
        if (v == NULL) {
            printf("ERROR: Couldn't open %s\n", volname);
            exit(2);
        }

        sprintf(pathbuf, "extracted/%s", volname);
        mkdir(pathbuf, 0777);

        for (int j = 0; j < entries; j++) {
            Resource r;
            memset(&r, 0, sizeof(r));
            r.volNum = i;
            r.checksum = readuint32(f);
            r.pos = readuint32(f);
            fseek(v, r.pos, SEEK_SET);
            r.pos += 12 + 5;
            fread(r.name, 12, 1, v);
            fskip(v, 1);
            r.size = readuint32(v);
            printf("  %12s\t%08X\t%d\t%d\n", r.name, r.checksum, r.pos, r.size);
            resources.push_back(r);
            sprintf(pathbuf, "extracted/%s/%s", volname, r.name);
            FILE *resfile = fopen(pathbuf, "wb");
            long nextOffset = ftell(v);
            fseek(v, r.pos, SEEK_SET);
            fread(databuf, r.size, 1, v);
            fwrite(databuf, r.size, 1, resfile);
            fclose(resfile);
            fseek(v, nextOffset, SEEK_SET);
        }
    }

    free(databuf);
}


int main(int argc, char **argv) {
    vector<Resource> resources;
    readidx(resources);

    return 0;
}
