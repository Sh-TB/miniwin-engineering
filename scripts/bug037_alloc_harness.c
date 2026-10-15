/* BUG-037 verification harness: replicate mw_RtlUnwindEx's target-alloc loop
 * byte-for-byte against the real t08 UNWIND_INFO file bytes. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

/* opcode constants as now defined in include/pe.h */
#define UWOP_PUSH_NONVOL       0
#define UWOP_ALLOC_LARGE       1
#define UWOP_ALLOC_SMALL       2
#define UWOP_SET_FPREG         3
#define UWOP_SAVE_NONVOL       4
#define UWOP_SAVE_NONVOL_FAR   5
#define UWOP_EPILOG            6
#define UWOP_SPARE_CODE        7
#define UWOP_SAVE_XMM128       8
#define UWOP_SAVE_XMM128_FAR   9
#define UWOP_PUSH_MACHFRAME    10

static uint8_t ui[64]; /* UNWIND_INFO bytes read from file */

int main(void) {
    FILE* f = fopen("tests/appcompat/t08_big_cpp_stl.exe", "rb");
    if (!f) { perror("open"); return 1; }
    fseek(f, 0x4ea58, SEEK_SET); /* UI @ RVA 0x50458 -> file 0x4ea58 */
    if (fread(ui, 1, 64, f) != 64) { perror("read"); return 1; }
    fclose(f);

    printf("UI header: %02x %02x %02x %02x\n", ui[0], ui[1], ui[2], ui[3]);
    uint8_t tgt_count = ui[2];
    uint8_t* tgt_codes = ui + 4;
    int tgt_slot = 0;
    uint64_t alloc_total = 0;
    for (int i = 0; i < tgt_count; i++) {
        if (tgt_slot * 2 + 1 >= (int)(tgt_count * 2 + 16)) break;
        uint16_t cw = *(uint16_t*)(tgt_codes + tgt_slot * 2);
        uint8_t op = (cw >> 8) & 0x0f;
        uint8_t info = (cw >> 12) & 0x0f;
        printf("i=%2d slot=%2d cw=0x%04x op=%2d info=%x", i, tgt_slot, cw, op, info);
        switch (op) {
        case UWOP_ALLOC_LARGE:
            tgt_slot++;
            if (info == 0) {
                uint64_t v = (uint64_t)(*(uint16_t*)(tgt_codes + tgt_slot * 2)) * 8;
                printf(" ALLOC_LARGE(u16*8) += 0x%lx", (unsigned long)v);
                alloc_total += v;
            } else {
                tgt_slot++;
                uint64_t v = (uint64_t)(*(uint32_t*)(tgt_codes + tgt_slot * 2 - 2));
                printf(" ALLOC_LARGE(u32) += 0x%lx", (unsigned long)v);
                alloc_total += v;
            }
            break;
        case UWOP_ALLOC_SMALL:
            alloc_total += (uint64_t)(info + 1) * 8;
            printf(" ALLOC_SMALL += 0x%lx", (unsigned long)((info + 1) * 8));
            break;
        case UWOP_PUSH_NONVOL:
            alloc_total += 8;
            printf(" PUSH +8");
            break;
        case UWOP_SAVE_NONVOL:
            tgt_slot++; printf(" SAVE_NONVOL"); break;
        case UWOP_SAVE_NONVOL_FAR:
            tgt_slot++; tgt_slot++; printf(" SAVE_NONVOL_FAR"); break;
        case UWOP_SAVE_XMM128:
            tgt_slot++; printf(" SAVE_XMM128"); break;
        case UWOP_SAVE_XMM128_FAR:
            tgt_slot++; tgt_slot++; printf(" SAVE_XMM128_FAR"); break;
        case UWOP_EPILOG:
            tgt_slot++; printf(" EPILOG"); break;
        case UWOP_SPARE_CODE:
            tgt_slot++; tgt_slot++; printf(" SPARE"); break;
        case UWOP_PUSH_MACHFRAME:
            alloc_total += 40; printf(" MACHFRAME +40"); break;
        default:
            if (op >= 11) tgt_slot++;
            printf(" reserved/default");
            break;
        }
        tgt_slot++;
        printf("  -> slot=%d total=0x%lx\n", tgt_slot, (unsigned long)alloc_total);
    }
    printf("FINAL alloc_total=0x%lx\n", (unsigned long)alloc_total);
    return 0;
}
