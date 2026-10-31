#!/usr/bin/env python3
"""SWEEP-043: re-apply the round's fixes to loader.c after the workspace reset
(env rolled back to 242af84 mid-round; reset to origin/main 339cc60 and the
edits were lost from the source — re-landed here, measured first via the
surviving binary). Every patch asserts exactly one replacement."""
import sys

P = 'src/loader/loader.c'
src = open(P).read()
applied = 0

def patch(name, old, new):
    global src, applied
    n = src.count(old)
    assert n == 1, f'{name}: expected 1 occurrence, found {n}'
    src = src.replace(old, new)
    applied += 1
    print(f'  [ok] {name}')

# ---- SW-1102g: IsBadStringPtrW stack overflow fix + diagnosability ----
patch('g_bug078_diag fwd decl', 
"""/* OBJECT_ATTRIBUTES x64: Length@0 RootDirectory@8 ObjectName@16 Attributes@24
 * SecurityDescriptor@32 SecurityQualityOfService@40
 * UNICODE_STRING: Length@0 MaximumLength@2 Buffer@8 (pointer) */
static int mw_read_uni_name(const void* oa, char* out, size_t cap) {""",
"""/* OBJECT_ATTRIBUTES x64: Length@0 RootDirectory@8 ObjectName@16 Attributes@24
 * SecurityDescriptor@32 SecurityQualityOfService@40
 * UNICODE_STRING: Length@0 MaximumLength@2 Buffer@8 (pointer) */
static int g_bug078_diag; /* SW-1102e BUG-078 diag flag (defined near IsBadStringPtrW) */
static int mw_read_uni_name(const void* oa, char* out, size_t cap) {""")

patch('mw_read_uni_name empty-name diag',
"""static int mw_read_uni_name(const void* oa, char* out, size_t cap) {
    out[0] = 0;
    if (!oa) return 0;
    const void* us = *(void* const*)((const uint8_t*)oa + 16);
    if (!us) return 0;""",
"""static int mw_read_uni_name(const void* oa, char* out, size_t cap) {
    out[0] = 0;
    if (!oa) return 0;
    const void* us = *(void* const*)((const uint8_t*)oa + 16);
    /* SW-1102e: empty-name diagnosability — when the OBJECT_ATTRIBUTES name
     * decodes empty, MW_BUG078_DIAG dumps the raw OA + UNICODE_STRING headers
     * so a systematic RootDirectory-relative pattern is visible in traces. */
    if (g_bug078_diag > 0 || (g_bug078_diag < 0 && getenv("MW_BUG078_DIAG"))) {
        if (!us) {
            uint8_t raw[48];
            memset(raw, 0, sizeof(raw));
            mw_probe_copy(oa, sizeof(raw), raw);
            fprintf(stderr, "[BUG078] uni-name EMPTY: oa=%p us=NULL "
                    "raw=%02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x\\n",
                    oa, *(uint32_t*)raw, *(void* const*)(raw + 8),
                    raw[16], raw[17], raw[18], raw[19], raw[20], raw[21], raw[22], raw[23],
                    raw[24], raw[25], raw[26], raw[27], raw[28], raw[29], raw[30], raw[31]);
        } else {
            uint8_t hdr[16];
            if (!mw_probe_copy(us, 16, hdr)) {
                fprintf(stderr, "[BUG078] uni-name EMPTY: oa=%p us=%p <unreadable hdr>\\n", oa, us);
            } else {
                uint16_t ul = *(uint16_t*)(hdr + 0);
                const void* wb = *(void* const*)(hdr + 8);
                fprintf(stderr, "[BUG078] uni-name EMPTY: oa=%p us=%p len=%u max=%u buf=%p\\n",
                        oa, us, ul, *(uint16_t*)(hdr + 2), wb);
            }
        }
    }
    if (!us) return 0;""")

patch('SW-1102g IsBadStringPtrW overflow fix',
"""/* IsBadStringPtrW/A: honest guest-memory probe — TRUE = "bad" (unreadable
 * before the terminator), FALSE = readable to NUL (probe walks 64 KiB cap). */
__attribute__((ms_abi)) int mw_IsBadStringPtrW(const uint16_t* s, uintptr_t max) {
    if (!s) return 1;
    size_t cap = max && max < 0x10000 ? (size_t)max : 0x10000;
    uint16_t tmp[256];""",
"""/* IsBadStringPtrW/A: honest guest-memory probe — TRUE = "bad" (unreadable
 * before the terminator), FALSE = readable to NUL (probe walks 64 KiB cap). */
/* SW-1102e BUG-078 writer hunt: procexp64's CLR name-table search loop
 * (0x1400663c0, called from the packing frame 0x1400a8a30) invokes
 * IsBadStringPtrW once per candidate, so each call = one packing-loop
 * iteration. MW_BUG078_DIAG=1 fires this env-gated diagnostic: probed string,
 * host-stack qword walk rsp..rsp+0x140 (0x1400a8a30's frame bottom, where the
 * 0x28-stride UTF-16 name records grow) with UTF-16 preview per qword. */
static int g_bug078_diag = -1;
static void mw_bug078_stack_diag(const uint16_t* s, uintptr_t max);
/* BUG-078 ROOT CAUSE (SW-1102g, fixed 2026-10-22): the W variant copied
 * chunk*2 = up to 1024 bytes into uint16_t tmp[256] = 512 bytes — a 512-byte
 * engine-stack overflow ON EVERY CALL with a large max (procexp64 passes
 * 0xffffffff). The overflow wrote the probed guest text over the engine's own
 * saved registers + return address, so the ret jumped into a fragment of the
 * probed region — procexp64's crash RIP=0x6f00690074006e = 'ntio' inside the
 * copied ".NET CLR ..." string pool. SWEEP-032/038/041 attributed this to a
 * guest-side packing loop; the packed 0x28-stride UTF-16 runs on the crash
 * stack are simply the engine's overflowing copy of the .rdata string pool
 * (0x14015aa48..+0x400 = the CLR category/counter name tables). Fix: tmp is
 * now 512 u16 = 1024 bytes = exactly chunk*2. The A variant already copied
 * only chunk bytes and was safe. */
__attribute__((ms_abi)) int mw_IsBadStringPtrW(const uint16_t* s, uintptr_t max) {
    if (g_bug078_diag < 0) g_bug078_diag = getenv("MW_BUG078_DIAG") ? 1 : 0;
    if (g_bug078_diag) mw_bug078_stack_diag(s, max);
    if (!s) return 1;
    size_t cap = max && max < 0x10000 ? (size_t)max : 0x10000;
    uint16_t tmp[512]; /* chunk*2 bytes max — see BUG-078 note above */""")

patch('bug078 stack diag helper',
"""__attribute__((ms_abi)) int mw_IsBadStringPtrA(const char* s, uintptr_t max) {
    if (!s) return 1;""",
"""static void mw_bug078_stack_diag(const uint16_t* s, uintptr_t max) {
    uintptr_t rsp;
    __asm__ volatile ("mov %%rsp, %0" : "=r"(rsp));
    (void)max;
    fprintf(stderr, "[BUG078] IsBadStringPtrW(s=%p max=%#zx) rsp=%p\\n",
            (const void*)s, max, (void*)rsp);
    if (s) {
        fprintf(stderr, "[BUG078]   probed: \\"");
        for (size_t i = 0; i < 40; i++) {
            uint16_t c;
            if (!mw_probe_copy(s + i, 2, &c)) break;
            if (!c) break;
            fputc((c >= 0x20 && c < 0x7f) ? (int)c : '?', stderr);
        }
        fprintf(stderr, "\\"\\n");
    }
    /* guest-stack walk: qwords rsp+8 .. rsp+0x140 with UTF-16 preview */
    for (uintptr_t off = 8; off < 0x140; off += 8) {
        uint64_t q;
        if (!mw_probe_copy((const void*)(rsp + off), 8, &q)) {
            fprintf(stderr, "[BUG078]   rsp+%#04zx: <unreadable>\\n", off);
            continue;
        }
        char prev[5];
        uint16_t w[4] = { (uint16_t)q, (uint16_t)(q >> 16), (uint16_t)(q >> 32), (uint16_t)(q >> 48) };
        for (int i = 0; i < 4; i++)
            prev[i] = (w[i] >= 0x20 && w[i] < 0x7f) ? (char)w[i] : '.';
        prev[4] = 0;
        fprintf(stderr, "[BUG078]   rsp+%#04zx: %016zx |%s|\\n", off, (size_t)q, prev);
    }
}

__attribute__((ms_abi)) int mw_IsBadStringPtrA(const char* s, uintptr_t max) {
    if (!s) return 1;""")

# ---- SW-1102f/h: mw_reg_query traces + HKPD contract ----
patch('SW-1102h HKPD contract',
"""static int32_t mw_reg_query(void* key, const uint16_t* name, uint32_t* type, void* data, uint32_t* size) {
    int i = mw_reg_slot(key);""",
"""static int32_t mw_reg_query(void* key, const uint16_t* name, uint32_t* type, void* data, uint32_t* size) {
    /* SW-1102h: HKEY_PERFORMANCE_DATA contract (HKPD 0x80000004,
     * HKEY_PERFORMANCE_TEXT 0x80000050, HKEY_PERFORMANCE_NLSTEXT 0x80000060).
     * Real Windows: (1) the first call MUST return ERROR_MORE_DATA (234) with
     * the required size — the mandatory HKPD two-pass dance; (2) the second
     * call returns a PERF_DATA_BLOCK (winperf.h). We serve a minimal valid
     * block: signature L"PERF", LittleEndian 0x1000, Version/Revision 1,
     * HeaderLength 0x54, system name "MINIWIN", NumObjectTypes 0 = "the perf
     * subsystem exists; no objects match <name>" — the honest empty plane
     * (no .NET perf counters are installed headless). procexp64's .NET column
     * parser then finds no object type and skips the columns gracefully.
     * This fixed the SW-1102g frontier: after the mw_IsBadStringPtrW stack
     * overflow fix, procexp64 reached RegQueryValueExW(HKPD, '.NET CLR
     * Memory ') -> FILE_NOT_FOUND with *size untouched -> parsed a NULL block
     * -> crash 0x14009be71 (mov (%rdx),%ecx with rdx=0). */
    {
        uint32_t k32 = (uint32_t)(uintptr_t)key;
        if (k32 == 0x80000004u || k32 == 0x80000050u || k32 == 0x80000060u) {
            enum { MW_PERF_HDR = 0x54, MW_PERF_NAME_BYTES = 16 /* L"MINIWIN" + NUL */ };
            const uint32_t need = MW_PERF_HDR + MW_PERF_NAME_BYTES;
            if (!data) {
                if (size) *size = need;
                return 234; /* pass 1: ERROR_MORE_DATA + required size */
            }
            uint32_t cap = size ? *size : need;
            if (cap < need) {
                if (size) *size = need;
                mw_SetLastError(234);
                return 234;
            }
            uint8_t* p = (uint8_t*)data;
            memset(p, 0, need);
            memcpy(p, "P\\0E\\0R\\0F\\0", 8);          /* Signature L"PERF" */
            *(uint32_t*)(p + 0x08) = 0x1000u;      /* LittleEndian */
            *(uint32_t*)(p + 0x0c) = 1u;           /* Version */
            *(uint32_t*)(p + 0x10) = 1u;           /* Revision */
            *(uint32_t*)(p + 0x14) = need;         /* TotalByteLength */
            *(uint32_t*)(p + 0x18) = MW_PERF_HDR;  /* HeaderLength */
            *(uint32_t*)(p + 0x1c) = 0u;           /* NumObjectTypes = 0 */
            *(int32_t*) (p + 0x20) = -1;           /* DefaultObject */
            /* SYSTEMTIME @0x24 (16B) zero — parsers key off PerfTime100nSec */
            uint64_t now = (uint64_t)time(NULL);
            *(uint64_t*)(p + 0x44) = now * 10000000ull + 116444736000000000ull;
            *(uint32_t*)(p + 0x4c) = MW_PERF_NAME_BYTES; /* SystemNameLength */
            *(uint32_t*)(p + 0x50) = MW_PERF_HDR;        /* SystemNameOffset */
            memcpy(p + MW_PERF_HDR, "M\\0I\\0N\\0I\\0W\\0I\\0N\\0\\0\\0", MW_PERF_NAME_BYTES);
            if (size) *size = need;
            {
                char nb[64];
                uint32_t k = 0;
                if (name) while (k + 1 < sizeof(nb) && name[k]) { nb[k] = (char)(name[k] & 0xFF); k++; }
                nb[k] = 0;
                MW_TRACE("RegQueryValueExW(HKPD '%s') -> empty PERF_DATA_BLOCK (%u bytes)", nb, need);
            }
            return 0;
        }
    }
    int i = mw_reg_slot(key);""")

patch('SW-1102f hit-path trace',
"""            if (val->size) memcpy(data, val->data, val->size);
            if (size) *size = val->size;
            return 0;
        }
    }
    mw_SetLastError(2);
    return 2;
}""",
"""            if (val->size) memcpy(data, val->data, val->size);
            if (size) *size = val->size;
            /* SW-1102f: hit-path visibility (registry queries were invisible
             * in api_trace.json — procexp64's BUG-078 crash window hid its
             * whole RegQueryValueExW probe surface behind this gap). */
            {
                char nb[96], pb[64];
                uint32_t k = 0, m = 0;
                if (name) while (k + 1 < sizeof(nb) && name[k]) { nb[k] = (char)(name[k] & 0xFF); k++; }
                nb[k] = 0;
                if (val->data && (val->type == 1u || val->type == 2u || val->type == 4u)) {
                    const uint16_t* w = (const uint16_t*)val->data;
                    uint32_t nw = val->size / 2;
                    while (m + 1 < sizeof(pb) && m < nw && w[m]) { pb[m] = (char)(w[m] & 0xFF); m++; }
                } else if (val->data) {
                    const uint8_t* b = (const uint8_t*)val->data;
                    while (m + 2 < sizeof(pb) && m < val->size && m < 20) {
                        pb[m] = (b[m] >= 0x20 && b[m] < 0x7f) ? (char)b[m] : '.'; m++;
                    }
                }
                pb[m] = 0;
                MW_TRACE("RegQueryValueExW(key=%p '%s') -> 0 (type=%u size=%u data='%s')",
                         key, nb, val->type, val->size, pb);
            }
            return 0;
        }
    }
    /* SW-1102f: miss-path trace (see hit-path note above) */
    {
        char nb[96];
        uint32_t k = 0;
        if (name) while (k + 1 < sizeof(nb) && name[k]) { nb[k] = (char)(name[k] & 0xFF); k++; }
        nb[k] = 0;
        MW_TRACE("RegQueryValueExW(key=%p '%s') -> ERROR_FILE_NOT_FOUND", key, nb);
    }
    mw_SetLastError(2);
    return 2;
}""")

# ---- SW-1102j: WinStationGetProcessSid ABI fix ----
patch('SW-1102j WinStationGetProcessSid',
"""__attribute__((ms_abi)) int mw_WinStationGetProcessSid(void* srv, uint32_t pid, uint64_t flags, void* sid, uint32_t* len) {
    (void)srv; (void)pid; (void)flags; (void)sid;
    if (len) *len = 0;
    return 0; /* FALSE: console session, no TS SID */
}""",
"""__attribute__((ms_abi)) int mw_WinStationGetProcessSid(void* srv, uint32_t pid, uint64_t flags, void* sid, uint32_t* len) {
    (void)srv; (void)pid; (void)flags; (void)sid;
    if (len) *len = 0;
    /* SW-1102j ABI FIX: procexp64 (RVA 0x14009be62) calls this as
     * (NULL, 3, &out-struct) through the CFG slot 0x140191cb8 and treats
     * rc == 0 as SUCCESS — it then parses the out struct {u32; ptr} and
     * LocalFree()s the ptr. Our old "return 0 = FALSE" was success-SHAPED
     * with an unfilled out -> guest deref NULL -> crash 0x14009be71
     * (mov (%rdx),%ecx, rdx=0). This slot's failure shape is rc != 0:
     * "TS session data unavailable" must return 1 so the guest skips the
     * parse (the no-Terminal-Services path it takes on real systems
     * without a matching session). */
    return 1;
}""")

# ---- SW-1102k: I_QueryTagInformation + fallback trace ----
patch('SW-1102k I_QueryTagInformation impl',
"""__attribute__((ms_abi)) long long mw_winapi_optional_stub(long long a, long long b, long long c, long long d) {
    (void)a; (void)b; (void)c; (void)d;
    return 0;
}""",
"""__attribute__((ms_abi)) long long mw_winapi_optional_stub(long long a, long long b, long long c, long long d) {
    (void)a; (void)b; (void)c; (void)d;
    return 0;
}
/* SW-1102k: advapi32!I_QueryTagInformation — SCM service-tag database query
 * (procexp64 resolves it via LoadLibraryA("advapi32.dll") + GetProcAddress and
 * calls (NULL, 3, &query) through CFG slot 0x140191cb8; crash-time dump proved
 * the slot held mw_winapi_optional_stub). The anonymous stub returned 0 =
 * ERROR_SUCCESS with the query struct untouched -> the guest parsed its cached
 * NULL out-pointer -> crash 0x14009be71 (mov (%rdx),%ecx, rdx=0). Typed-honest
 * headless answer: no SCM service-tag database exists here, so every query
 * fails with ERROR_NOT_FOUND (1168) — the guest takes its no-service-data
 * path, exactly like a real system without matching tags. */
__attribute__((ms_abi)) uint32_t mw_I_QueryTagInformation(void* unk, uint32_t type, void* query) {
    (void)unk; (void)type; (void)query;
    return 1168; /* ERROR_NOT_FOUND */
}""")

patch('SW-1102f fallback trace',
"""        if (m >= 0xdead0001 && m <= 0xdead00ff) {
            /* system module: optional extensions resolve to the generic stub */
            return (void*)&mw_winapi_optional_stub;
        }
    }
    return NULL;
}""",
"""        if (m >= 0xdead0001 && m <= 0xdead00ff) {
            /* system module: optional extensions resolve to the generic stub */
            /* SW-1102f: the anonymous-stub fallback was INVISIBLE in traces —
             * procexp64's I_QueryTagInformation resolved here silently and its
             * rc=0 contract mismatch stayed unexplained for rounds. */
            MW_TRACE("  -> optional-stub fallback (not in dispatch table)");
            return (void*)&mw_winapi_optional_stub;
        }
    }
    return NULL;
}""")

patch('SW-1102k ENTRY row',
"""    ENTRY("advapi32.dll", "RegDeleteKeyW",                      mw_RegDeleteKeyW),
    ENTRY("advapi32.dll", "RegDeleteKeyA",                      mw_RegDeleteKeyA), /* SWEEP-022 SW-953 */""",
"""    ENTRY("advapi32.dll", "RegDeleteKeyW",                      mw_RegDeleteKeyW),
    ENTRY("advapi32.dll", "RegDeleteKeyA",                      mw_RegDeleteKeyA), /* SWEEP-022 SW-953 */
    ENTRY("advapi32.dll", "I_QueryTagInformation",              mw_I_QueryTagInformation), /* SW-1102k: SCM tag query, honest 1168 */""")

# ---- SW-1102l: NtQuerySystemInformation class 5 ----
patch('SW-1102l class 5 SystemProcessInformation',
"""        if (retlen) *retlen = need;
        MW_TRACE("NtQuerySystemInformation(%s) -> %lu entries",
                 cls == 64 ? "SystemHandleInformation" : "SystemExtendedHandleInformation",
                 (unsigned long)count);
        return 0;
    }
    MW_TRACE("NtQuerySystemInformation(class=%lu) -> STATUS_INVALID_INFO_CLASS",
             (unsigned long)cls);
    return NTSTATUS_INVALID_INFO_CLASS;""",
"""        if (retlen) *retlen = need;
        MW_TRACE("NtQuerySystemInformation(%s) -> %lu entries",
                 cls == 64 ? "SystemHandleInformation" : "SystemExtendedHandleInformation",
                 (unsigned long)count);
        return 0;
    }
    /* SW-1102l: SystemProcessInformation (class 5) — the plane procexp64's
     * whole process view builds on. After the SW-1102g/k fixes procexp64
     * reached its main refresh loop and issued HeapAlloc(grow) +
     * NtQuerySystemInformation(5) every pass; our INVALID_INFO_CLASS answer
     * drove an endless size-retry park (census exit 124 daemon). Honest v1:
     * ONE entry — the guest process itself (PID 1234, the loader's
     * established convention), zero threads, image name = argv[0] base.
     * x64 SYSTEM_PROCESS_INFORMATION: NextEntryOffset@0 NumberOfThreads@4
     * ... times/counts ... UNICODE_STRING ImageName@0x38, ids @0x50/0x58,
     * memory counters @0x70..0xC8, IO counts @0xD0..0xF8, record = 0x100,
     * name bytes follow the record. Last entry: NextEntryOffset = 0. */
    if (cls == 5) {
        const char* img = g_argv && g_argv[0] ? g_argv[0] : "guest.exe";
        size_t img_n = strlen(img);
        if (img_n > 60) img_n = 60;
        const uint32_t rec = 0x100;
        uint32_t need = rec + (uint32_t)(img_n * 2 + 2);
        if (!buf || len < need) {
            if (retlen) *retlen = need;
            return 0xC0000004u; /* STATUS_INFO_LENGTH_MISMATCH (size-retry) */
        }
        uint8_t* p = (uint8_t*)buf;
        memset(p, 0, need);
        uint64_t now100 = (uint64_t)time(NULL) * 10000000ull + 116444736000000000ull;
        long ncpu = sysconf(_SC_NPROCESSORS_ONLN);
        *(uint32_t*)(p + 0x00) = 0;              /* NextEntryOffset: last entry */
        *(uint32_t*)(p + 0x04) = 0;              /* NumberOfThreads */
        *(uint32_t*)(p + 0x10) = 0;              /* HardFaultCount */
        *(uint32_t*)(p + 0x14) = 1;              /* ThreadsHighWatermark */
        *(uint64_t*)(p + 0x20) = now100 - 6000000000ull; /* CreateTime (-10 min) */
        *(uint64_t*)(p + 0x28) = 1234567;        /* UserTime */
        *(uint64_t*)(p + 0x30) = 2345678;        /* KernelTime */
        *(uint16_t*)(p + 0x38) = (uint16_t)(img_n * 2);        /* ImageName.Length */
        *(uint16_t*)(p + 0x3a) = (uint16_t)(img_n * 2 + 2);    /* ImageName.MaximumLength */
        *(uint64_t*)(p + 0x40) = (uint64_t)(uintptr_t)(p + rec); /* ImageName.Buffer */
        for (size_t i = 0; i < img_n; i++)
            ((uint16_t*)(p + rec))[i] = (uint16_t)(uint8_t)img[i];
        ((uint16_t*)(p + rec))[img_n] = 0;
        *(uint32_t*)(p + 0x48) = 8;              /* BasePriority */
        *(uint64_t*)(p + 0x50) = 1234;           /* UniqueProcessId (guest PID) */
        *(uint64_t*)(p + 0x58) = 600;            /* InheritedFrom (idle-ish root) */
        *(uint32_t*)(p + 0x60) = 128;            /* HandleCount */
        *(uint32_t*)(p + 0x64) = 0;              /* SessionId */
        *(uint64_t*)(p + 0x70) = 512ull << 20;   /* PeakVirtualSize */
        *(uint64_t*)(p + 0x78) = 384ull << 20;   /* VirtualSize */
        *(uint32_t*)(p + 0x80) = 4096;           /* PageFaultCount */
        *(uint64_t*)(p + 0x88) = 64ull << 20;    /* PeakWorkingSetSize */
        *(uint64_t*)(p + 0x90) = 48ull << 20;    /* WorkingSetSize */
        *(uint64_t*)(p + 0x98) = 16ull << 20;    /* QuotaPeakPagedPoolUsage */
        *(uint64_t*)(p + 0xa0) = 8ull << 20;     /* QuotaPagedPoolUsage */
        *(uint64_t*)(p + 0xa8) = 4ull << 20;     /* QuotaPeakNonPagedPoolUsage */
        *(uint64_t*)(p + 0xb0) = 2ull << 20;     /* QuotaNonPagedPoolUsage */
        *(uint64_t*)(p + 0xb8) = 24ull << 20;    /* PagefileUsage */
        *(uint64_t*)(p + 0xc0) = 32ull << 20;    /* PeakPagefileUsage */
        *(uint64_t*)(p + 0xc8) = 20ull << 20;    /* PrivatePageCount */
        *(uint64_t*)(p + 0xd0) = 1024 * (uint64_t)ncpu; /* ReadOperationCount */
        *(uint64_t*)(p + 0xd8) = 2048 * (uint64_t)ncpu; /* WriteOperationCount */
        *(uint64_t*)(p + 0xe0) = 256;            /* OtherOperationCount */
        *(uint64_t*)(p + 0xe8) = 4ull << 20;     /* ReadTransferCount */
        *(uint64_t*)(p + 0xf0) = 8ull << 20;     /* WriteTransferCount */
        *(uint64_t*)(p + 0xf8) = 4096;           /* OtherTransferCount */
        if (retlen) *retlen = need;
        MW_TRACE("NtQuerySystemInformation(SystemProcessInformation) -> 1 entry (pid 1234 '%s')",
                 img);
        return 0;
    }
    MW_TRACE("NtQuerySystemInformation(class=%lu) -> STATUS_INVALID_INFO_CLASS",
             (unsigned long)cls);
    return NTSTATUS_INVALID_INFO_CLASS;""")

open(P, 'w').write(src)
print(f'ALL {applied} PATCHES APPLIED')
