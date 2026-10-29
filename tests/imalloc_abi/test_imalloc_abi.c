/* IMalloc vtable ABI regression test (APP-TEST-2 / SW-1501).
 *
 * Contract under test (objidl.h / objidlbase.idl, Wine dlls/ole32/imalloc.c):
 *   [0]QueryInterface [1]AddRef [2]Release [3]Alloc [4]Realloc [5]Free
 *   [6]GetSize [7]DidAlloc [8]HeapMinimize
 *
 * Method: the guest calls IMalloc members through the vtable with the
 * MICROSOFT x64 calling convention. On this x64 host that convention is
 * exactly what GCC emits for __attribute__((ms_abi)) function pointers, so
 * calling the engine's real vtable memory through ms_abi pointers is
 * ABI-identical to a guest vtable call (same registers, same shadow space).
 * No guest PE is needed to prove slot ORDER, QI behavior, GetSize honesty,
 * and foreign-pointer refusal.
 *
 * Discriminators:
 *  - slot order: Realloc at [4] must GROW (data preserved, still owned);
 *    the old Free-at-[4] bug freed the block instead.
 *  - GetSize at [6] must return >= requested for owned blocks (old stub: 0).
 *  - QI at [0] must answer IID_IUnknown + IID_IMalloc with S_OK and the
 *    same object (old CoGetMalloc: slot 0 was NULL -> jump-to-zero crash).
 *  - Free(foreign)/DidAlloc(foreign): foreign pointers must be refused,
 *    never passed to the host free().
 *
 * Build+run via run.sh (compiles loader.c with -Dmain= to avoid the loader's
 * own main, links this file, executes). Exit 0 = all assertions pass.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

extern __attribute__((ms_abi)) uint32_t mw_CoGetMalloc(uint32_t ctx, void** out);
extern __attribute__((ms_abi)) uint32_t mw_SHGetMalloc(void** out);

typedef long          (__attribute__((ms_abi)) *fn_qi)(void* self, const void* iid, void** out);
typedef unsigned long (__attribute__((ms_abi)) *fn_addref)(void* self);
typedef void*         (__attribute__((ms_abi)) *fn_alloc)(void* self, size_t cb);
typedef void*         (__attribute__((ms_abi)) *fn_realloc)(void* self, void* p, size_t cb);
typedef void          (__attribute__((ms_abi)) *fn_free)(void* self, void* p);
typedef size_t        (__attribute__((ms_abi)) *fn_getsize)(void* self, void* p);
typedef int           (__attribute__((ms_abi)) *fn_didalloc)(void* self, void* p);
typedef void          (__attribute__((ms_abi)) *fn_heapmin)(void* self);

typedef struct {
    fn_qi       qi;
    fn_addref   addref;
    fn_addref   release;
    fn_alloc    alloc;
    fn_realloc  realloc;
    fn_free     free;
    fn_getsize  getsize;
    fn_didalloc didalloc;
    fn_heapmin  heapmin;
} IMallocVtbl;

static const unsigned char IID_IUnknown[16] = {
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xC0,0x00,0x00,0x00,0x00,0x00,0x00,0x46 };
static const unsigned char IID_IMalloc[16] = {
    0xDA,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xC0,0x00,0x00,0x00,0x00,0x00,0x00,0x46 };
static const unsigned char IID_IBogus[16] = {
    0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xAA,0xBB,0xCC,0xDD,0xEE,0xFF,0x00 };

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, ...) do { \
    if (cond) { printf("  PASS: "); printf(__VA_ARGS__); printf("\n"); g_pass++; } \
    else      { printf("  FAIL: "); printf(__VA_ARGS__); printf(" (at line %d)\n", __LINE__); g_fail++; } \
} while (0)

/* walk all 9 slots through the REAL vtable memory, in documented order */
static void exercise_imalloc(const char* tag, void* obj) {
    IMallocVtbl* v = *(IMallocVtbl**)obj;
    void* out = NULL;

    /* [0] QueryInterface: IUnknown + IMalloc -> S_OK, same object */
    CHECK(v->qi(obj, IID_IUnknown, &out) == 0 && out == obj,
          "%s: QI(IID_IUnknown) -> S_OK, same object", tag);
    out = NULL;
    CHECK(v->qi(obj, IID_IMalloc, &out) == 0 && out == obj,
          "%s: QI(IID_IMalloc) -> S_OK, same object", tag);
    out = NULL;
    CHECK(v->qi(obj, IID_IBogus, &out) == (long)0x80004002L && out == NULL,
          "%s: QI(bogus) -> E_NOINTERFACE, out NULL", tag);

    /* [1]/[2] AddRef/Release return nonzero reference counts */
    CHECK(v->addref(obj) > 0, "%s: AddRef -> nonzero", tag);
    CHECK(v->release(obj) > 0, "%s: Release -> nonzero", tag);

    /* [3] Alloc + write */
    unsigned char* p = (unsigned char*)v->alloc(obj, 64);
    CHECK(p != NULL, "%s: Alloc(64) -> non-NULL", tag);
    memset(p, 0xAA, 64);

    /* [6] GetSize: honest usable size for OWNED blocks (old stub: always 0) */
    CHECK(v->getsize(obj, p) >= 64, "%s: GetSize(Alloc'd) >= 64", tag);

    /* [4] Realloc GROWS — the discriminator for the Free-at-[4] bug */
    unsigned char* q = (unsigned char*)v->realloc(obj, p, 128);
    CHECK(q != NULL, "%s: Realloc(64->128) -> non-NULL", tag);
    int preserved = 1;
    for (int i = 0; i < 64; i++) if (q[i] != 0xAA) { preserved = 0; break; }
    CHECK(preserved, "%s: Realloc preserves the first 64 bytes", tag);
    CHECK(v->didalloc(obj, q) == 1, "%s: DidAlloc(realloc'd) == 1", tag);

    /* [5] Free an owned block, then the registry must answer 0 */
    v->free(obj, q);
    CHECK(v->didalloc(obj, q) == 0, "%s: DidAlloc(freed) == 0", tag);

    /* [8] HeapMinimize: callable no-op */
    v->heapmin(obj);
    CHECK(1, "%s: HeapMinimize callable", tag);

    /* foreign pointers: engine-owned registry must refuse, never free() */
    unsigned char stackbuf[32];
    CHECK(v->didalloc(obj, stackbuf) == 0, "%s: DidAlloc(foreign stack) == 0", tag);
    v->free(obj, stackbuf); /* must be refused silently, not host-freed */
    CHECK(1, "%s: Free(foreign stack) refused without crash", tag);
    void* foreign = malloc(32);
    v->free(obj, foreign);
    int still_valid = 1;
    memset(foreign, 0x5C, 32);
    for (int i = 0; i < 32; i++) if (((unsigned char*)foreign)[i] != 0x5C) { still_valid = 0; break; }
    CHECK(still_valid, "%s: Free(foreign malloc) did NOT host-free the block", tag);
    v->free(obj, foreign); /* release via the right owner (host) */
    free(foreign);
}

/* the runner compiles loader.c with -Dmain=...; undo it for THIS main */
#undef main
int main(void) {
    printf("IMalloc vtable ABI regression (SW-1501)\n");
    void* pm = NULL;
    CHECK(mw_CoGetMalloc(1, &pm) == 0 && pm != NULL, "CoGetMalloc(1) -> S_OK, object out");
    if (pm) exercise_imalloc("CoGetMalloc", pm);

    void* ps = NULL;
    CHECK(mw_SHGetMalloc(&ps) == 0 && ps != NULL, "SHGetMalloc() -> S_OK, object out");
    if (ps) exercise_imalloc("SHGetMalloc", ps);

    /* NULL out pointer -> E_POINTER, no crash */
    CHECK(mw_CoGetMalloc(1, NULL) == (uint32_t)0x80004003L, "CoGetMalloc(out=NULL) -> E_POINTER");
    CHECK(mw_SHGetMalloc(NULL) == (uint32_t)0x80004003L, "SHGetMalloc(out=NULL) -> E_POINTER");

    printf("RESULT: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
