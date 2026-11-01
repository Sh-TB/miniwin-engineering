/* t13_imalloc — IMalloc ABI regression PE (wine-l0 wagon 1).
 *
 * Discriminates the wine-source-audit IMalloc contract on the merged
 * origin/main engine:
 *   W1a  QueryInterface IID images — IID_IMalloc is {00000018-0000-0000-
 *        C000-000000000046}; the engine's static byte image carried 0xDA
 *        as Data1's first byte, so every legitimate QI(IID_IMalloc) got
 *        E_NOINTERFACE (both providers).
 *   W1b  Alloc(0) must return non-NULL (Wine combase/malloc.c L114-140
 *        HeapAlloc parity; glibc malloc(0) is a unique non-NULL).
 *   W1c  GetSize(NULL)/(foreign) -> (SIZE_T)-1 (Wine malloc.c L218-239);
 *        owned -> usable size >= requested (Wine HeapSize semantics).
 *   W1d  CoGetMalloc(context != MEMCTX_TASK) -> E_INVALIDARG, *out=NULL
 *        (Wine malloc.c L293-302); the old body ignored ctx.
 *   W1e  SHGetMalloc and CoGetMalloc hand out the SAME allocator instance
 *        (Wine: one static `allocator`; shell32's SHGetMalloc is a
 *        CoGetMalloc wrapper).
 *   Base SW-1501 coverage re-pinned: 9-slot vtable, ownership registry,
 *   foreign Free/Realloc refused, ms_abi slots.
 *
 * House style: no stdio.h, wsprintfA output, exit 0 = all pass. Deterministic.
 */
#include <windows.h>
#include <objidl.h>
#include <stdint.h>
#include <string.h>

static int g_fail = 0;
static int g_pass = 0;
static char g_log[8192];
static int g_log_n = 0;

static void check(int cond, const char* what) {
    if (cond) g_pass++;
    else g_fail++;
    if (g_log_n < (int)sizeof(g_log) - 160) {
        g_log_n += wsprintfA(g_log + g_log_n, "%s %s\r\n",
                             cond ? "[PASS]" : "[FAIL]", what);
    }
}

static const GUID kIID_IUnknown =
    {0x00000000,0x0000,0x0000,{0xC0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}};
static const GUID kIID_IMalloc =
    {0x00000018,0x0000,0x0000,{0xC0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}};
static const GUID kIID_Bogus =
    {0xBEEFCAFE,0x1234,0x5678,{0x9A,0xBC,0xDE,0xF0,0x12,0x34,0x56,0x78}};

__attribute__((dllimport)) HRESULT __stdcall SHGetMalloc(IMalloc** out);

int WINAPI WinMain(HINSTANCE a, HINSTANCE b, LPSTR c, int d) {
    (void)a; (void)b; (void)c; (void)d;

    IMalloc* mShell = NULL;
    IMalloc* mCo = NULL;

    /* P1: obtain through the actual exposed APIs */
    check(SHGetMalloc(&mShell) == 0 && mShell != NULL, "SHGetMalloc -> S_OK non-NULL");
    check(CoGetMalloc(1 /*MEMCTX_TASK*/, &mCo) == 0 && mCo != NULL, "CoGetMalloc(MEMCTX_TASK) -> S_OK non-NULL");
    check(mShell == mCo, "W1e: both providers return the SAME allocator");

    /* P2: CoGetMalloc context contract */
    {
        IMalloc* px = (IMalloc*)0x1;
        check(CoGetMalloc(0, &px) == (HRESULT)0x80070057u && px == NULL,
              "W1d: CoGetMalloc(0) -> E_INVALIDARG, *out=NULL");
        px = (IMalloc*)0x1;
        check(CoGetMalloc(2, &px) == (HRESULT)0x80070057u && px == NULL,
              "W1d: CoGetMalloc(2) -> E_INVALIDARG, *out=NULL");
    }

    IMalloc* m = mShell;
    const IMallocVtbl* v = m->lpVtbl;

    check(v->AddRef(m) == 2, "slot1 AddRef -> 2 (Wine static-allocator parity)");
    check(v->Release(m) == 1, "slot2 Release -> 1 (Wine parity)");

    /* P4: QueryInterface identities (W1a) */
    {
        void* q = (void*)0x1;
        check(v->QueryInterface(m, &kIID_IMalloc, &q) == 0 && q == (void*)m,
              "W1a: QI(IID_IMalloc) -> S_OK same object");
        q = (void*)0x1;
        check(v->QueryInterface(m, &kIID_IUnknown, &q) == 0 && q == (void*)m,
              "QI(IID_IUnknown) -> S_OK same object");
        q = (void*)0x1;
        check(v->QueryInterface(m, &kIID_Bogus, &q) == (HRESULT)0x80004002u && q == NULL,
              "QI(bogus) -> E_NOINTERFACE, *out=NULL");
        IUnknown* unk = NULL;
        if (v->QueryInterface(m, &kIID_IUnknown, (void**)&unk) == 0 && unk) {
            IMalloc* m2 = NULL;
            check(unk->lpVtbl->QueryInterface(unk, &kIID_IMalloc, (void**)&m2) == 0 && m2 == m,
                  "IUnknown QI round-trip -> same IMalloc");
        }
    }

    /* P5: Alloc + GetSize + DidAlloc */
    void* p = v->Alloc(m, 64);
    check(p != NULL, "Alloc(64) -> non-NULL");
    {
        unsigned char* bb = (unsigned char*)p;
        int ok = 1;
        for (int i = 0; i < 64; i++) bb[i] = (unsigned char)i;
        for (int i = 0; i < 64; i++) if (bb[i] != (unsigned char)i) { ok = 0; break; }
        check(ok, "Alloc(64) block writable/readable");
    }
    {
        size_t sz = v->GetSize(m, p);
        check(sz != (size_t)-1 && sz >= 64,
              "W1c: GetSize(owned) -> usable size >= requested");
    }
    check(v->DidAlloc(m, p) == 1, "DidAlloc(own) -> 1");

    /* P6: Realloc grow + preserve */
    void* p2 = v->Realloc(m, p, 4096);
    check(p2 != NULL, "Realloc(p,4096) -> non-NULL");
    {
        unsigned char* bb = (unsigned char*)p2;
        int ok = 1;
        for (int i = 0; i < 64; i++) if (bb[i] != (unsigned char)i) { ok = 0; break; }
        check(ok, "Realloc preserves first 64 bytes");
        memset(bb + 64, 0x5A, 4096 - 64);
        check(bb[4095] == 0x5A, "Realloc(4096) block fully writable");
    }
    check(v->DidAlloc(m, p2) == 1, "DidAlloc(realloc'd) -> 1");
    check(p == p2 ? v->DidAlloc(m, p) == 1 : v->DidAlloc(m, p) == 0,
          "old pointer after Realloc: same-block owned, or moved+released");

    /* P7: Realloc(NULL,cb)=Alloc; Realloc(p,0)=Free+NULL */
    void* q = v->Realloc(m, NULL, 32);
    check(q != NULL, "Realloc(NULL,32) -> Alloc path non-NULL");
    check(v->DidAlloc(m, q) == 1, "Realloc(NULL,32) block owned");
    void* q2 = v->Realloc(m, q, 0);
    check(q2 == NULL, "Realloc(q,0) -> NULL");
    check(v->DidAlloc(m, q) == 0, "Realloc(q,0) freed the block");

    /* P8: W1b Alloc(0) non-NULL */
    {
        void* z = v->Alloc(m, 0);
        check(z != NULL, "W1b: Alloc(0) -> non-NULL (Wine HeapAlloc parity)");
        if (z) v->Free(m, z);
    }

    /* P9: W1c unknown-pointer truthfulness */
    check(v->GetSize(m, NULL) == (size_t)-1, "W1c: GetSize(NULL) -> (SIZE_T)-1");
    check(v->DidAlloc(m, NULL) == -1, "DidAlloc(NULL) -> -1");

    /* P10: foreign pointer safety */
    {
        volatile unsigned char stackbuf[64];
        void* foreign = (void*)stackbuf;
        stackbuf[0] = 1;
        check(v->DidAlloc(m, foreign) == 0, "DidAlloc(foreign stack) -> 0");
        check(v->GetSize(m, foreign) == (size_t)-1, "W1c: GetSize(foreign) -> (SIZE_T)-1");
        v->Free(m, foreign);
        check(stackbuf[0] == 1, "Free(foreign) is a no-op — stack intact");
        check(v->Realloc(m, foreign, 16) == NULL, "Realloc(foreign) -> NULL");
        v->HeapMinimize(m);
        check(1, "HeapMinimize callable");
    }

    /* P11: Free owned */
    v->Free(m, p2);
    check(v->DidAlloc(m, p2) == 0, "Free(owned) -> DidAlloc 0 after");

    g_log_n += wsprintfA(g_log + g_log_n,
                         "---- t13_imalloc: %d passed, %d failed ----\r\n",
                         g_pass, g_fail);
    OutputDebugStringA(g_log);
    return g_fail == 0 ? 0 : 1;
}
