
/* ---------------- CPU32 state + memory ---------------- */
typedef struct {
    uint32_t r[8];      /* EAX ECX EDX EBX ESP EBP ESI EDI (index order) */
    uint32_t eip;
    uint32_t eflags;
    uint32_t fsbase, gsbase;
    /* x87 */
    uint32_t ftop;              /* stack top index 0..7 */
    uint64_t fst[8];            /* 80-bit truncated to f64 payload + sign exp stored as double bits */
    double   freg[8];           /* v1: doubles (f64 precision; f80 extra bits not modeled) */
    uint16_t fcw, fsw;
    /* SSE regs v1: xmm0..7 low 128 bits */
    uint8_t  xmm[8][16];
} MwCpu32;

static __thread MwCpu32 t_cpu32;
static __thread int     t_cpu32_live = 0;
/* g_cpu32_active defined in the SW-1281 fwd block (needed by earlier code) */
static volatile uint32_t g_cpu32_exit_req = 0; /* ExitProcess interception */

enum { EAXi = 0, ECXi, EDXi, EBXi, ESPi, EBPi, ESIi, EDIi };

/* EFLAGS bits */
#define F_CF 0x0001u
#define F_PF 0x0004u
#define F_AF 0x0010u
#define F_ZF 0x0040u
#define F_SF 0x0080u
#define F_TF 0x0100u
#define F_IF 0x0200u
#define F_DF 0x0400u
#define F_OF 0x0800u

static inline void f_set(uint32_t f, int c) { if (c) t_cpu32.eflags |= f; else t_cpu32.eflags &= ~f; }
static inline int  f_get(uint32_t f)        { return (t_cpu32.eflags & f) != 0; }
static inline void f_setzsp32(uint32_t v) {
    f_set(F_ZF, v == 0); f_set(F_SF, (int32_t)v < 0);
    f_set(F_PF, !__builtin_parity(v & 0xFF));
}
static inline void f_setzsp16(uint16_t v) {
    f_set(F_ZF, v == 0); f_set(F_SF, (int16_t)v < 0);
    f_set(F_PF, !__builtin_parity(v & 0xFF));
}
static inline void f_setzsp8(uint8_t v) {
    f_set(F_ZF, v == 0); f_set(F_SF, (int8_t)v < 0);
    f_set(F_PF, !__builtin_parity(v & 0xFF));
}

/* guest memory (flat, in-process): image mapped at ImageBase, arena mapped.
 * A wild pointer here segfaults the engine exactly like the native x64
 * path — same crash discipline; the watchdog captures the trace. */
static inline uint8_t* m32p(uint32_t va) { return (uint8_t*)(uintptr_t)va; }
static inline uint8_t  m32_ld8 (uint32_t va) { return *(uint8_t*)m32p(va); }
static inline uint16_t m32_ld16(uint32_t va) { uint16_t v; memcpy(&v, m32p(va), 2); return v; }
static inline uint32_t m32_ld32(uint32_t va) { uint32_t v; memcpy(&v, m32p(va), 4); return v; }
static inline uint64_t m32_ld64(uint32_t va) { uint64_t v; memcpy(&v, m32p(va), 8); return v; }
static inline void m32_st8 (uint32_t va, uint8_t v)  { *(uint8_t*)m32p(va) = v; }
static inline void m32_st16(uint32_t va, uint16_t v) { memcpy(m32p(va), &v, 2); }
static inline void m32_st32(uint32_t va, uint32_t v) { memcpy(m32p(va), &v, 4); }
static inline void m32_st64(uint32_t va, uint64_t v) { memcpy(m32p(va), &v, 8); }

static inline uint32_t m32_push(uint32_t v) { t_cpu32.r[ESPi] -= 4; m32_st32(t_cpu32.r[ESPi], v); return t_cpu32.r[ESPi]; }
static inline uint32_t m32_pop(void)        { uint32_t v = m32_ld32(t_cpu32.r[ESPi]); t_cpu32.r[ESPi] += 4; return v; }

/* ---------------- ModRM / SIB ---------------- */
typedef struct { int mod, reg, rm; uint32_t ea; int is_reg; } ModRM;
static int g_segreg_ovr = -1; /* -1 none, else fs(4)/gs(5) base selector marker */

static void modrm_decode(ModRM* m) {
    uint8_t op = m32_ld8(t_cpu32.eip);
    t_cpu32.eip++;
    m->mod = op >> 6; m->reg = (op >> 3) & 7; m->rm = op & 7;
    if (m->mod == 3) { m->is_reg = 1; m->ea = m->rm; return; }
    m->is_reg = 0;
    uint32_t base = 0, disp = 0; int have_disp = 0;
    if (m->rm == 4) { /* SIB */
        uint8_t sib = m32_ld8(t_cpu32.eip++);
        int scale = sib >> 6, idx = (sib >> 3) & 7, b = sib & 7;
        base = (b == 5 && m->mod == 0) ? 0 : t_cpu32.r[b];
        if (b == 5 && m->mod == 0) have_disp = 1; /* disp32 follows */
        if (idx != 4) base += t_cpu32.r[idx] << scale;
    } else if (m->rm == 5 && m->mod == 0) {
        have_disp = 1;
    } else {
        base = t_cpu32.r[m->rm];
    }
    if (have_disp) { disp = m32_ld32(t_cpu32.eip); t_cpu32.eip += 4; }
    else if (m->mod == 1) { disp = (uint32_t)(int32_t)(int8_t)m32_ld8(t_cpu32.eip); t_cpu32.eip += 1; }
    else if (m->mod == 2) { disp = m32_ld32(t_cpu32.eip); t_cpu32.eip += 4; }
    m->ea = base + disp;
    /* segment base applied ONCE by modrm_apply_seg() after prefix scan */
}

/* apply segment override to an EA (called when prefix seen) */
static void modrm_apply_seg(ModRM* m) {
    if (!m->is_reg) {
        if (g_segreg_ovr == 0) m->ea += t_cpu32.fsbase;
        else if (g_segreg_ovr == 1) m->ea += t_cpu32.gsbase;
    }
}
