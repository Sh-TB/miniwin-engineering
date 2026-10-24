
/* ---------------- interpreter core ---------------- */
static uint64_t g_cpu32_steps = 0;
static int      g_cpu32_trace = 0; /* MW32_TRACE=1 */

/* API gate + host bridge: implemented after the arity table (above) and
 * before cpu32_run so the interpreter can call it inline. */
typedef uint64_t (*mw32_fn16_t)(uint64_t, uint64_t, uint64_t, uint64_t,
                                uint64_t, uint64_t, uint64_t, uint64_t,
                                uint64_t, uint64_t, uint64_t, uint64_t,
                                uint64_t, uint64_t, uint64_t, uint64_t);

static uint32_t mw32_unresolved_thunk_name_at(uint32_t thunk_va, char* out, size_t n);

/* forward: pump-side shim registration (defined with the GUI shims) */
static void mw32_gui_install_shims(void);

/* the gate: called with t_cpu32.eip == MW32_GATE_VA and the thunk slot
 * index in EAX. Performs the host call and returns the new EIP. */
static uint32_t mw32_api_gate(uint32_t slot_index) {
    if (slot_index >= g_api32_count) {
        fprintf(stderr, "[PE32] GATE slot %u out of range\n", slot_index);
        exit(3);
    }
    MwApi32Slot* s = &g_api32_slots[slot_index];
    s->calls++;
    g_api32_gate_calls++;

    uint32_t ret_addr = m32_pop();           /* stdcall ret addr */
    uint32_t arity = (s->arity == 0xFFFFFFFFu) ? MW32_ARITY_DEFAULT : s->arity;
    if (s->arity == 0xFFFFFFFFu) arity = 32; /* unresolved stub: args unknown */

    /* widen up to 16 dword args; callee reads what its prototype needs */
    uint64_t a[16];
    for (int i = 0; i < 16; i++) a[i] = m32_ld32(t_cpu32.r[ESPi] + (uint32_t)(4 * i));

    if (g_cpu32_trace) {
        MW_TRACE("[API32 #%u] %s esp=0x%x args=[%x %x %x %x]", slot_index,
                 s->name, t_cpu32.r[ESPi], (uint32_t)a[0], (uint32_t)a[1],
                 (uint32_t)a[2], (uint32_t)a[3]);
    }

    uint32_t ret = 0;
    if (s->arity == 0xFFFFFFFFu) {
        /* unresolved import: mirror mw_unresolved_import_stub (trace+0) */
        MW_TRACE("[PE32] UNRESOLVED call '%s' (gate) -> 0", s->name);
        ret = 0;
    } else {
        mw32_fn16_t fn = (mw32_fn16_t)(void*)s->fn;
        if (!fn) { fprintf(stderr, "[PE32] slot '%s' has NULL fn\n", s->name); exit(3); }
        uint64_t r = fn(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7],
                        a[8], a[9], a[10], a[11], a[12], a[13], a[14], a[15]);
        ret = (uint32_t)r;
        /* GetProcAddress-returns-host-ptr: convert to a 32-bit thunk VA so
         * the interpreter can gate it later (SW-1281 contract). */
        if ((strcmp(s->name, "GetProcAddress") == 0 ||
             strcmp(s->name, "GetProcAddressA") == 0 ||
             strcmp(s->name, "GetProcAddressW") == 0) && ret != 0) {
            ret = mw32_thunk_for_host_fn((void*)(uintptr_t)r);
        }
        /* host pointers that guests store/deref as data are fine (host is
         * non-PIE, everything < 4GB). */
    }

    /* ExitProcess/TerminateProcess interception: flag-unwind, never _exit
     * from inside the interpreter (pump/nested frames). */
    if (strcmp(s->name, "ExitProcess") == 0 || strcmp(s->name, "TerminateProcess") == 0) {
        g_cpu32_exit_req = (uint32_t)a[0] ? (uint32_t)a[0] : ret;
        MW_TRACE("[PE32] %s(%u) intercepted -> unwind", s->name, (uint32_t)a[0]);
        t_cpu32.eip = ret_addr;
        return ret_addr;
    }

    t_cpu32.r[ESPi] += arity;                /* stdcall callee pops */
    t_cpu32.r[EAXi] = ret;
    t_cpu32.eip = ret_addr;
    return ret_addr;
}

/* top-level run: executes until ExitProcess interception, gate-driven
 * quit, or step budget. Returns the guest exit code. */
static int cpu32_run(uint64_t max_steps);

/* callback re-entry: run guest code at `eip` with stdcall args until the
 * sentinel RET; saves/restores the caller's CPU state (nested pump calls). */
static uint32_t MW32_RET_SENTINEL = 0;
static uint32_t cpu32_call(uint32_t eip, int nargs, const uint32_t* args) {
    if (!MW32_RET_SENTINEL) {
        /* allocate a 16-byte dead zone inside the thunk page as sentinel */
        MW32_RET_SENTINEL = MW32_THUNK_BASE + MW32_THUNK_BYTES - 16;
    }
    MwCpu32 save = t_cpu32;
    int live = t_cpu32_live; t_cpu32_live = 1;
    /* thread stack: carve per re-entry from the arena top region */
    static __thread uint32_t t_cbstack;
    if (!t_cbstack) t_cbstack = mw32_heap_alloc(0x40000, 0) + 0x40000;
    t_cpu32.r[ESPi] = t_cbstack;
    t_cpu32.r[EBPi] = t_cbstack;
    m32_push(MW32_RET_SENTINEL);
    for (int i = nargs - 1; i >= 0; i--) m32_push(args[i]);
    t_cpu32.eip = eip;
    t_cpu32.eflags = 0x202;
    int exit_save = g_cpu32_exit_req; g_cpu32_exit_req = 0;
    cpu32_run(0); /* budget 0 = unbounded; returns on sentinel/exit */
    uint32_t rv = t_cpu32.r[EAXi];
    int code = g_cpu32_exit_req; g_cpu32_exit_req = exit_save;
    t_cpu32 = save; t_cpu32_live = live;
    if (code) g_cpu32_exit_req = code; /* propagate ExitProcess upward */
    return rv;
}

static void cpu32_fatal(const char* what, uint32_t op, uint32_t va) {
    fprintf(stderr,
        "[PE32-FATAL] %s opcode=0x%02x at EIP=0x%08x base=0x%x\n"
        "  EAX=%08x ECX=%08x EDX=%08x EBX=%08x ESP=%08x EBP=%08x ESI=%08x EDI=%08x\n"
        "  bytes: %02x %02x %02x %02x %02x %02x %02x %02x\n",
        what, op, t_cpu32.eip, g_image_base ? (uint32_t)(uintptr_t)g_image_base : 0,
        t_cpu32.r[0], t_cpu32.r[1], t_cpu32.r[2], t_cpu32.r[3],
        t_cpu32.r[4], t_cpu32.r[5], t_cpu32.r[6], t_cpu32.r[7],
        m32_ld8(t_cpu32.eip), m32_ld8(t_cpu32.eip+1), m32_ld8(t_cpu32.eip+2),
        m32_ld8(t_cpu32.eip+3), m32_ld8(t_cpu32.eip+4), m32_ld8(t_cpu32.eip+5),
        m32_ld8(t_cpu32.eip+6), m32_ld8(t_cpu32.eip+7));
    fflush(stderr);
    exit(86); /* 86 = SW-1281 interpreter abort code */
}

/* condition code evaluator (cc index = low nibble of jcc/setcc) */
static int cond_eval(int cc) {
    int r = 0;
    switch (cc) {
    case 0x0: r = f_get(F_OF); break;                     /* O  */
    case 0x1: r = !f_get(F_OF); break;                    /* NO */
    case 0x2: r = f_get(F_CF); break;                     /* B  */
    case 0x3: r = !f_get(F_CF); break;                    /* AE */
    case 0x4: r = f_get(F_ZF); break;                     /* E  */
    case 0x5: r = !f_get(F_ZF); break;                    /* NE */
    case 0x6: r = f_get(F_CF) || f_get(F_ZF); break;      /* BE */
    case 0x7: r = !f_get(F_CF) && !f_get(F_ZF); break;    /* A  */
    case 0x8: r = f_get(F_SF); break;                     /* S  */
    case 0x9: r = !f_get(F_SF); break;                    /* NS */
    case 0xA: r = f_get(F_PF); break;                     /* P  */
    case 0xB: r = !f_get(F_PF); break;                    /* NP */
    case 0xC: r = f_get(F_SF) != f_get(F_OF); break;      /* L  */
    case 0xD: r = f_get(F_SF) == f_get(F_OF); break;      /* GE */
    case 0xE: r = (f_get(F_SF) != f_get(F_OF)) || f_get(F_ZF); break; /* LE */
    case 0xF: r = (f_get(F_SF) == f_get(F_OF)) && !f_get(F_ZF); break; /* G */
    }
    return r;
}
