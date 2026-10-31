/* SWEEP-051: extract all files from LibreOffice payload CAB via libmspack */
#include <mspack.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

static const char *outdir = "/home/z/my-project/workspace/miniwin-engineering/artifacts/sweep051/lo_extracted";

static int ensure_dir_for(const char *path) {
    char tmp[4096];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + strlen(outdir) + 1; *p; p++) {
        if (*p == '/') { *p = 0; mkdir(tmp, 0755); *p = '/'; }
    }
    return 0;
}

static int file_count = 0, err_count = 0;

static struct mscab_decompressor *g_d = NULL;

static void extract_one(struct mscabd_file *file) {
    char dest[4096];
    const char *nm = file->filename;
    snprintf(dest, sizeof(dest), "%s/%s", outdir, nm);
    ensure_dir_for(dest);
    if (g_d->extract(g_d, file, dest) != MSPACK_ERR_OK) {
        fprintf(stderr, "EXTRACT-FAIL %s\n", nm);
        err_count++;
    } else {
        file_count++;
    }
}

int main(int argc, char **argv) {
    const char *cab = argv[1];
    struct mscab_decompressor *d;
    struct mscabd_cabinet *cb, *next;
    d = mspack_create_cab_decompressor(NULL);
    g_d = d;
    if (!d) { fprintf(stderr, "no decompressor\n"); return 1; }
    cb = d->open(d, cab);
    if (!cb) { fprintf(stderr, "open fail\n"); return 1; }
    mkdir(outdir, 0755);
    for (struct mscabd_cabinet *c = cb; c; c = c->next) {
        for (struct mscabd_file *f = c->files; f; f = f->next) {
            extract_one(f);
        }
    }
    printf("extracted=%d failed=%d\n", file_count, err_count);
    d->close(d, cb);
    mspack_destroy_cab_decompressor(d);
    return err_count ? 2 : 0;
}
