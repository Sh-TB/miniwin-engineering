
/* ---------------- TEB32 / PEB32 / stack ---------------- */
static void mw32_setup_world(uint32_t image_base, uint32_t cmdline_host) {
    mw32_world_init();
    /* TEB32 (classic NT_TIB + Win32 fields) */
    uint8_t* teb = g_arena32 + (MW32_TEB32_VA - MW32_ARENA_BASE);
    *(uint32_t*)(teb + 0x00) = 0xFFFFFFFFu;                       /* ExceptionList: empty chain */
    *(uint32_t*)(teb + 0x04) = MW32_STACK_TOP;                    /* StackBase */
    *(uint32_t*)(teb + 0x08) = MW32_STACK_TOP - MW32_STACK_BYTES; /* StackLimit */
    *(uint32_t*)(teb + 0x18) = MW32_TEB32_VA;                     /* Self */
    *(uint32_t*)(teb + 0x20) = 0x00001DB0u;                       /* ClientId.UniqueProcess (honest constant) */
    *(uint32_t*)(teb + 0x24) = 0x00001DB8u;                       /* ClientId.UniqueThread */
    *(uint32_t*)(teb + 0x2C) = MW32_TLSARR_VA;                    /* ThreadLocalStoragePointer */
    *(uint32_t*)(teb + 0x30) = MW32_PEB32_VA;                     /* ProcessEnvironmentBlock */
    *(uint32_t*)(teb + 0xF14) = 0x00000400u;                      /* GDI batch/handle hint (honest small) */
    /* PEB32 */
    uint8_t* peb = g_arena32 + (MW32_PEB32_VA - MW32_ARENA_BASE);
    *(uint32_t*)(peb + 0x0C) = image_base;                        /* ImageBaseAddress */
    *(uint32_t*)(peb + 0x64) = 5;                                 /* OSMajorVersion (NT family; env-tunable later) */
    *(uint32_t*)(peb + 0x68) = 1;                                 /* OSMinorVersion */
    *(uint32_t*)(peb + 0x6C) = 2600;                              /* OSBuildNumber */
    *(uint32_t*)(peb + 0xA4) = 4;                                 /* NumberOfProcessors */
    *(uint32_t*)(peb + 0xB8) = 0x00010000u;                       /* NtGlobalFlag (honest zero-ish) */
    /* ANSI cmdline copy into low arena (guests read it as a 32-bit ptr) */
    uint32_t strva = MW32_STRA_VA;
    if (cmdline_host) {
        size_t cl = strlen((const char*)(uintptr_t)cmdline_host) + 1;
        if (cl < 0x10000) { memcpy(g_arena32 + (strva - MW32_ARENA_BASE), (const void*)(uintptr_t)cmdline_host, cl); }
    }
    t_cpu32.fsbase = MW32_TEB32_VA;
    t_cpu32.gsbase = 0;
    t_cpu32.fcw = MW32_FCW_INIT;
    t_cpu32.eflags = 0x202;
    xmm_zero_all();
    MW_TRACE("[PE32] TEB32 @0x%x PEB32 @0x%x stack 0x%x..0x%x cmdline@0x%x",
             MW32_TEB32_VA, MW32_PEB32_VA, MW32_STACK_TOP - MW32_STACK_BYTES,
             MW32_STACK_TOP, strva);
}

/* ---------------- wndproc / callback shims ---------------- */
static uint32_t g_main_class_proc32 = 0; /* x86 wndproc EIP */
__attribute__((ms_abi)) static long long mw_wndproc32_shim(void* hwnd, uint32_t msg, uintptr_t wp, long long lp) {
    if (!g_main_class_proc32) return 0;
    uint32_t args[4] = { (uint32_t)(uintptr_t)hwnd, msg, (uint32_t)wp, (uint32_t)lp };
    uint32_t r = cpu32_call(g_main_class_proc32, 4, args);
    if (g_cpu32_exit_req) mw_gui_set_quit((int)g_cpu32_exit_req);
    return (long long)(int32_t)r;
}

/* CreateThread shim: x86 thread start runs on its own host thread with a
 * thread-local MwCpu32 (t_cpu32) — the interpreter is thread-safe by TLS.
 * Mw32ThreadArg typedef lives in the SW-1281 fwd block (needed earlier). */
static void* mw32_thread_trampoline(void* p) {
    Mw32ThreadArg a = *(Mw32ThreadArg*)p; free(p);
    t_cpu32.fsbase = MW32_TEB32_VA; t_cpu32.gsbase = 0;
    t_cpu32.fcw = MW32_FCW_INIT; t_cpu32.eflags = 0x202;
    uint32_t args[1] = { a.param };
    cpu32_call(a.start_eip, 1, args);
    return NULL;
}

/* ---------------- x86 mode hooks for host allocators ---------------- */
static int mw32_mode(void) { return g_cpu32_active; }

/* ---------------- load_pe32: the PE32 loader plane ---------------- */
typedef struct { uint32_t base, size; } Mw32Section;
#define MW32_MAXSEC 96
static Mw32Section g_sec32[MW32_MAXSEC];
static int      g_nsec32 = 0;
static uint32_t g_image_base32 = 0;

static int load_pe32(const char* filepath) {
    mw32_world_init();
    struct stat st;
    if (stat(filepath, &st) != 0 || !S_ISREG(st.st_mode)) {
        fprintf(stderr, "[PE32] not a regular file: %s\n", filepath);
        return -1;
    }
    FILE* f = fopen(filepath, "rb");
    if (!f) { fprintf(stderr, "[PE32] cannot open %s\n", filepath); return -1; }
    fseek(f, 0, SEEK_END); long fsize = ftell(f); fseek(f, 0, SEEK_SET);
    if (fsize <= 0 || fsize > (512L << 20)) {
        fprintf(stderr, "[PE32] bad size %ld\n", fsize); fclose(f); return -1;
    }
    uint8_t* raw = (uint8_t*)malloc((size_t)fsize);
    if (!raw) { fclose(f); return -1; }
    if (fread(raw, 1, (size_t)fsize, f) != (size_t)fsize) { free(raw); fclose(f); return -1; }
    fclose(f);

    if (fsize < 0x40 || *(uint16_t*)raw != 0x5A4D) { free(raw); return -1; }
    uint32_t peoff = *(uint32_t*)(raw + 0x3C);
    if (peoff + 24 + 96 > (uint32_t)fsize || *(uint32_t*)(raw + peoff) != 0x00004550) { free(raw); return -1; }
    uint16_t machine = *(uint16_t*)(raw + peoff + 4);
    if (machine != 0x014C) {
        fprintf(stderr, "[PE32] machine 0x%04x not I386 (0x014c) — SW-1282 Win16/NE is a separate wagon\n", machine);
        free(raw); return -1;
    }
    uint16_t nsec = *(uint16_t*)(raw + peoff + 6);
    uint8_t* oh = raw + peoff + 24;
    uint16_t magic = *(uint16_t*)oh;
    if (magic != 0x10B) { free(raw); return -1; }
    uint32_t ep_rva     = *(uint32_t*)(oh + 16);
    uint32_t image_base = *(uint32_t*)(oh + 28);
    uint32_t sect_align = *(uint32_t*)(oh + 32);
    uint32_t file_align = *(uint32_t*)(oh + 36);
    uint32_t size_img   = *(uint32_t*)(oh + 56);
    uint32_t size_hdrs  = *(uint32_t*)(oh + 60);
    uint32_t nrva       = *(uint32_t*)(oh + 92);
    uint32_t* dd        = (uint32_t*)(oh + 96);
    (void)sect_align; (void)file_align;
    if (nsec > MW32_MAXSEC || size_img == 0 || size_img > (512u << 20)) { free(raw); return -1; }
    uint16_t osec_sz = *(uint16_t*)(raw + peoff + 20);
    uint8_t* sectab  = oh + osec_sz;

    /* Map image at preferred base (PE32 images sit low: 0x400000 typical) */
    void* img = mmap((void*)(uintptr_t)image_base, (size_t)((size_img + 0xFFF) & ~0xFFFu),
                     PROT_READ | PROT_WRITE | PROT_EXEC,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (img == MAP_FAILED) {
        fprintf(stderr, "[PE32] cannot map image at 0x%x\n", image_base);
        free(raw); return -1;
    }
    memcpy(img, raw, size_hdrs < size_img ? size_hdrs : size_img);
    for (uint16_t s = 0; s < nsec; s++) {
        uint8_t* se = sectab + 40 * s;
        uint32_t va = *(uint32_t*)(se + 12), vsz = *(uint32_t*)(se + 8);
        uint32_t ro = *(uint32_t*)(se + 20), rsz = *(uint32_t*)(se + 16);
        uint32_t n = rsz < vsz ? rsz : vsz;
        if (va + vsz > size_img) { fprintf(stderr, "[PE32] section %u overflows image\n", s); free(raw); return -1; }
        if (ro + rsz > (uint32_t)fsize) rsz = (uint32_t)fsize - ro; /* clamp overlay-truncated */
        if (n) memcpy((uint8_t*)img + va, raw + ro, n);
        g_sec32[s].base = image_base + va; g_sec32[s].size = vsz;
    }
    g_nsec32 = nsec;
    g_image_base32 = image_base;
    free(raw);

    /* Base relocations (HIGHLOW only for x86) */
    uint32_t rel_rva = dd[5 * 2], rel_sz = dd[5 * 2 + 1];
    uint32_t nreloc = 0, nrel_hi = 0;
    if (rel_rva && rel_sz) {
        uint8_t* rb = (uint8_t*)img + rel_rva;
        uint32_t off = 0;
        while (off + 8 <= rel_sz) {
            uint32_t page = *(uint32_t*)(rb + off);
            uint32_t blk = *(uint32_t*)(rb + off + 4);
            if (!blk) break;
            for (uint32_t e = 8; e + 2 <= blk; e += 2) {
                uint16_t ent = *(uint16_t*)(rb + off + e);
                uint32_t typ = ent >> 12, o = ent & 0xFFF;
                if (!typ) continue;
                if (typ == 1) { /* HIGHLOW */
                    uint32_t va = image_base + page + o;
                    m32_st32(va, m32_ld32(va) + image_base); /* delta = pref base (mapped at pref) */
                    nreloc++;
                } else nrel_hi++;
            }
            off += blk;
        }
    }
    MW_TRACE("[PE32] %s: base=0x%x EP=0x%x size=0x%x secs=%u relocs=%u (hi=%u)",
             filepath, image_base, ep_rva, size_img, nsec, nreloc, nrel_hi);

    /* Import binding: IAT slots = x86 API thunks (name-only rule) */
    uint32_t imp_rva = dd[1 * 2], imp_sz = dd[1 * 2 + 1];
    uint32_t unresolved = 0, resolved = 0;
    if (imp_rva && imp_sz) {
        for (uint32_t o = 0; o + 20 <= imp_sz; o += 20) {
            uint32_t* desc = (uint32_t*)((uint8_t*)img + imp_rva + o);
            uint32_t name_rva = desc[3], first_thunk = desc[4], oft = desc[0];
            if (!name_rva && !first_thunk) break;
            const char* dn = (const char*)((uint8_t*)img + name_rva);
            uint32_t* int_ = (uint32_t*)((uint8_t*)img + (oft ? oft : first_thunk));
            uint32_t* iat = (uint32_t*)((uint8_t*)img + first_thunk);
            for (uint32_t t = 0; int_[t]; t++) {
                if (int_[t] & 0x80000000u) {
                    /* ordinal import from a host-side DLL: v1 resolves via
                     * the SW-1276 fixed-ordinal map through the gate table */
                    const char* nm = NULL;
                    static const struct { const char* dll; uint16_t ord; const char* name; } ord32[] = {
                        { "ws2_32.dll", 115, "WSAStartup" }, { "ws2_32.dll", 111, "WSAGetLastError" },
                        { "ws2_32.dll", 52, "gethostbyname" }, { "ws2_32.dll", 23, "socket" },
                        { "ws2_32.dll", 16, "closesocket" }, { "ws2_32.dll", 9, "htons" },
                        { "ws2_32.dll", 8, "htonl" }, { "ws2_32.dll", 14, "ntohl" },
                        { "ws2_32.dll", 18, "select" },
                    };
                    char dlow[64]; size_t k = 0;
                    for (; dn[k] && k < sizeof(dlow) - 1; k++)
                        dlow[k] = (dn[k] >= 'A' && dn[k] <= 'Z') ? (char)(dn[k] + 32) : dn[k];
                    dlow[k] = 0;
                    for (size_t oi = 0; oi < sizeof(ord32)/sizeof(ord32[0]); oi++)
                        if (strcmp(dlow, ord32[oi].dll) == 0 && ord32[oi].ord == (uint16_t)(int_[t] & 0xFFFF))
                            nm = ord32[oi].name;
                    if (!nm) { iat[t] = mw32_api_thunk(dlow, "#ordinal"); unresolved++; }
                    else { iat[t] = mw32_api_thunk(dlow, nm); resolved++; }
                    continue;
                }
                const char* impname = (const char*)((uint8_t*)img + (int_[t] & 0x7FFFFFFF) + 2);
                uint32_t th = mw32_api_thunk(dn, impname);
                if (g_api32_slots[(th - MW32_THUNK_BASE - 16) / 10].fn) resolved++;
                else unresolved++;
                iat[t] = th;
            }
        }
    }
    MW_TRACE("[PE32] imports: %u resolved, %u unresolved (thunk page %u APIs)",
             resolved, unresolved, g_api32_count);

    /* Data imports: IAT slot = host data address (non-PIE host: < 4GB) */
    for (uint32_t i = 0; i < NUM_DATA_IMPORTS; i++) {
        /* walk the import descriptors again and patch matching names */
        if (!imp_rva || !imp_sz) break;
        for (uint32_t o = 0; o + 20 <= imp_sz; o += 20) {
            uint32_t* desc = (uint32_t*)((uint8_t*)img + imp_rva + o);
            uint32_t name_rva = desc[3], first_thunk = desc[4], oft = desc[0];
            if (!name_rva && !first_thunk) break;
            const char* dn = (const char*)((uint8_t*)img + name_rva);
            char dlow[64]; size_t k = 0;
            for (; dn[k] && k < sizeof(dlow) - 1; k++)
                dlow[k] = (dn[k] >= 'A' && dn[k] <= 'Z') ? (char)(dn[k] + 32) : dn[k];
            dlow[k] = 0;
            if (strcmp(dlow, g_data_imports[i].dll) != 0) continue;
            uint32_t* int_ = (uint32_t*)((uint8_t*)img + (oft ? oft : first_thunk));
            uint32_t* iat = (uint32_t*)((uint8_t*)img + first_thunk);
            for (uint32_t t = 0; int_[t]; t++) {
                if (int_[t] & 0x80000000u) continue;
                const char* impname = (const char*)((uint8_t*)img + (int_[t] & 0x7FFFFFFF) + 2);
                if (strcmp(impname, g_data_imports[i].name) == 0) {
                    uint64_t hostptr = (uint64_t)(uintptr_t)g_data_imports[i].data_addr;
                    if (hostptr > 0xFFFFFFFFull) {
                        MW_TRACE("[PE32] data import %s NOT 32-bit reachable", impname);
                    } else {
                        iat[t] = (uint32_t)hostptr;
                        resolved++; if (unresolved) unresolved--;
                    }
                }
            }
        }
    }

    /* export directory (for x86 GetProcAddress — served as thunks at gate) */
    uint32_t exp_rva = dd[0 * 2], exp_sz = dd[0 * 2 + 1];
    (void)exp_rva; (void)exp_sz;

    /* World + entry */
    uint32_t cmdline_va = 0;
    if (g_cmdline_ansi[0]) {
        /* copy into arena scratch so guest 32-bit GetCommandLineA works */
        size_t cl = strlen(g_cmdline_ansi) + 1;
        if (cl < 0x10000) {
            memcpy(g_arena32 + (MW32_STRA_VA - MW32_ARENA_BASE), g_cmdline_ansi, cl);
            cmdline_va = MW32_STRA_VA;
        }
    }
    mw32_setup_world(image_base, cmdline_va);

    /* Entry state: stdcall WinMainCRTStartup gets no stack args in practice;
     * x86 CRT startup reads its own env/args via the data imports. */
    t_cpu32.r[ESPi] = MW32_STACK_TOP - 0x1000;   /* leave a guard hole */
    t_cpu32.r[EBPi] = t_cpu32.r[ESPi];
    t_cpu32.eip = image_base + ep_rva;
    m32_st32(t_cpu32.r[ESPi], 0);                /* fake ret addr 0 */
    MW_TRACE("[PE32] jumping to EP 0x%x (interpreted i386)", t_cpu32.eip);

    g_cpu32_active = 1;
    mw32_gui_install_shims();
    int rc = cpu32_run(0);
    g_cpu32_active = 0;
    if (rc > 0) { g_pe32_exit = rc; g_ran_pe32 = 1; return 0; }
    if (rc == -2 || rc == 0) { g_pe32_exit = 0; g_ran_pe32 = 1; return 0; }
    if (rc == -3) { fprintf(stderr, "[PE32] step budget exhausted\n"); return 1; }
    return 1;
}
