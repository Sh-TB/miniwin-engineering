/* Dump UNWIND_INFO + LSDA from UPX PE */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static uint8_t* g_data;
static uint32_t g_size;
static uint64_t g_img_base = 0x400000;

/* Section table: name, VMA, VSize, RawOff, RawSize */
static struct { const char* name; uint32_t vma, vs, rawoff, rsz; } secs[] = {
    {".text",  0x1000, 0xdfbb0, 0x400, 0},
    {".rdata", 0x4e2000, 0x115800, 0xe0c00, 0},
    {".pdata",  0x5f8000, 0x8e08, 0x1f6400, 0},
    {".xdata", 0x601000, 0x937c,   0x1ff400, 0},
};
#define NSECS (sizeof(secs)/sizeof(secs[0]))

static uint32_t rva_to_off(uint32_t rva) {
    for (int i = 0; i < NSECS; i++) {
        if (secs[i].vma <= rva && rva < secs[i].vma + secs[i].vs)
            return secs[i].rawoff + (rva - secs[i].vma);
    }
    return (uint32_t)-1;
}

static uint8_t* ui_data(uint32_t ui_rva, const char* label) {
    uint32_t off = rva_to_off(ui_rva);
    if (off == (uint32_t)-1) { printf("%s: RVA not found\n", label); return NULL; }
    uint8_t* ui = g_data + off;
    uint8_t ver = ui[0] & 0x07;
    uint8_t flags = (ui[0] >> 3) & 0x03;
    uint8_t prolog = ui[1];
    uint8_t count = ui[2];
    printf("%s: ver=%u flags=0x%x prolog=%u codes=%u\n", label, ver, flags, prolog, count);
    int slot = 0;
    for (int i = 0; i < count; i++) {
        uint16_t cw = *(uint16_t*)(ui + 4 + slot*2);
        uint8_t op = (cw >> 8) & 0x0f;
        uint8_t info = (cw >> 12) & 0x0f;
        uint8_t off = cw & 0xff;
        const char* names[] = {"PUSH_NONVOL","ALLOC_LARGE","ALLOC_SMALL","SET_FPREG","SAVE_NONVOL","SAVE_NONVOL_FAR","SAVE_XMM128","SAVE_XMM128_FAR","PUSH_MACHFRAME"};
        const char* n = (op < 9) ? names[op] : "?";
        printf("  Code[%d]: %s(info=%u, off=%u)\n", i, n, info, off);
        if (op == 1) { slot++; if (info == 0) { slot++; } }
        else if (op == 5 || op == 7) { slot++; }
        slot++;
    }
    return ui;
}

static void dump_ui_and_lsda(uint32_t ui_rva, const char* label) {
    uint8_t* ui = ui_data(ui_rva, label);
    if (!ui) return;
    uint8_t ver = ui[0] & 0x07;
    uint8_t flags = (ui[0] >> 3) & 0x03;
    uint8_t count = ui[2];
    int slot = 0;
    for (int i = 0; i < count; i++) {
        uint16_t cw = *(uint16_t*)(ui + 4 + slot*2);
        if ((cw >> 8) & 0x0f == 1) slot++; /* ALLOC_LARGE */
        if (((cw >> 8) & 0x0f) == 5 || ((cw >> 8) & 0x0f) == 7) slot++; /* SAVE_NONVOL_FAR or SAVE_XMM128_FAR */
        slot++;
    }
    uint32_t h_off = 4 + slot * 2;
    if (h_off % 4) h_off += 2;
    if (flags & 0x01) {
        uint32_t hr = *(uint32_t*)(ui + h_off);
        uint32_t lsda_off = h_off + 4;
        printf("  Handler RVA: 0x%x\n", hr);
        printf("  LSDA at offset 0x%x from UI start\n", lsda_off);
        uint32_t lsda_len = 32;
        printf("  LSDA hex:\n");
        for (int j = 0; j < lsda_len; j++) {
            uint8_t b = g_data[off + lsda_off + j];
            printf("  %04x: %02x", j, b);
        }
    } else {
        printf("  No EHANDLER\n");
    }
}
int main(int argc, char** argv) {
    if (argc < 2) { printf("Usage: %s <ui_rva> [<ui_rva>...]\n", argv[0]); return 1; }
    FILE* f = fopen(argv[1], "rb");
    if (!f) { printf("Cannot open %s\n", argv[1]); return 1; }
    fseek(f, 0, SEEK_END);
    g_size = ftell(f);
    g_data = (uint8_t*)malloc(g_size);
    fread(g_data, 1, g_size, f);
    fclose(f);
    g_img_base = 0x400000;
    printf("Loaded %u bytes\n", g_size);
    for (int i = 2; i < argc; i++) dump_ui_and_lsda((uint32_t)strtoul(argv[i], NULL, 16), argv[i]);
    return 0;
}
