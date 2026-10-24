
/* ---------------- ALU helpers ---------------- */
static uint32_t alu_add32(uint32_t a, uint32_t b, int cin) {
    uint64_t r = (uint64_t)a + b + (uint32_t)cin;
    uint32_t v = (uint32_t)r;
    f_set(F_CF, r >> 32);
    f_set(F_AF, ((a ^ b ^ v) & 0x10) != 0);
    f_set(F_OF, (~(a ^ b) & (a ^ v) & 0x80000000u) != 0);
    f_setzsp32(v); return v;
}
static uint32_t alu_sub32(uint32_t a, uint32_t b, int cin) {
    uint64_t r = (uint64_t)a - b - (uint32_t)cin;
    uint32_t v = (uint32_t)r;
    f_set(F_CF, (r >> 32) & 1);
    f_set(F_AF, ((a ^ b ^ v) & 0x10) != 0);
    f_set(F_OF, ((a ^ b) & (a ^ v) & 0x80000000u) != 0);
    f_setzsp32(v); return v;
}
static uint32_t alu_logic32(uint32_t v) { f_set(F_CF,0); f_set(F_OF,0); f_set(F_AF,0); f_setzsp32(v); return v; }
static uint32_t alu_inc32(uint32_t a) { uint32_t cf = f_get(F_CF); uint32_t v = alu_add32(a,1,0); f_set(F_CF,cf); return v; }
static uint32_t alu_dec32(uint32_t a) { uint32_t cf = f_get(F_CF); uint32_t v = alu_sub32(a,1,0); f_set(F_CF,cf); return v; }
/* 16/8-bit narrow variants via widening to 32 helpers */
static uint16_t alu_add16(uint16_t a, uint16_t b, int cin) {
    uint32_t r = (uint32_t)a + b + (uint32_t)cin; uint16_t v = (uint16_t)r;
    f_set(F_CF, r >> 16);
    f_set(F_AF, ((a ^ b ^ v) & 0x10) != 0);
    f_set(F_OF, (~(a ^ b) & (a ^ v) & 0x8000u) != 0);
    f_setzsp16(v); return v;
}
static uint16_t alu_sub16(uint16_t a, uint16_t b, int cin) {
    uint32_t r = (uint32_t)a - b - (uint32_t)cin; uint16_t v = (uint16_t)r;
    f_set(F_CF, (r >> 16) & 1);
    f_set(F_AF, ((a ^ b ^ v) & 0x10) != 0);
    f_set(F_OF, ((a ^ b) & (a ^ v) & 0x8000u) != 0);
    f_setzsp16(v); return v;
}
static uint16_t alu_logic16(uint16_t v) { f_set(F_CF,0); f_set(F_OF,0); f_set(F_AF,0); f_setzsp16(v); return v; }
static uint8_t alu_add8(uint8_t a, uint8_t b, int cin) {
    uint32_t r = (uint32_t)a + b + (uint32_t)cin; uint8_t v = (uint8_t)r;
    f_set(F_CF, r >> 8);
    f_set(F_AF, ((a ^ b ^ v) & 0x10) != 0);
    f_set(F_OF, (~(a ^ b) & (a ^ v) & 0x80u) != 0);
    f_setzsp8(v); return v;
}
static uint8_t alu_sub8(uint8_t a, uint8_t b, int cin) {
    uint32_t r = (uint32_t)a - b - (uint32_t)cin; uint8_t v = (uint8_t)r;
    f_set(F_CF, (r >> 8) & 1);
    f_set(F_AF, ((a ^ b ^ v) & 0x10) != 0);
    f_set(F_OF, ((a ^ b) & (a ^ v) & 0x80u) != 0);
    f_setzsp8(v); return v;
}
static uint8_t alu_logic8(uint8_t v) { f_set(F_CF,0); f_set(F_OF,0); f_set(F_AF,0); f_setzsp8(v); return v; }

static uint32_t alu_shl32(uint32_t v, unsigned c) {
    c &= 31; if (!c) return v;
    f_set(F_CF, (v >> (32 - c)) & 1);
    v <<= c;
    f_setzsp32(v);
    f_set(F_OF, ((v >> 31) & 1) != f_get(F_CF)); /* MSB != CF */
    return v;
}
static uint32_t alu_shr32(uint32_t v, unsigned c) {
    c &= 31; if (!c) return v;
    f_set(F_CF, (v >> (c - 1)) & 1);
    v >>= c;
    f_setzsp32(v); f_set(F_OF, 0);
    return v;
}
static uint32_t alu_sar32(uint32_t v, unsigned c) {
    c &= 31; if (!c) return v;
    f_set(F_CF, ((int32_t)v >> (c - 1)) & 1);
    v = (uint32_t)((int32_t)v >> c);
    f_setzsp32(v); f_set(F_OF, 0);
    return v;
}
static uint32_t alu_ror32(uint32_t v, unsigned c) {
    c &= 31; if (!c) return v;
    v = (v >> c) | (v << (32 - c));
    f_set(F_CF, v & 1); return v;
}
static uint32_t alu_rol32(uint32_t v, unsigned c) {
    c &= 31; if (!c) return v;
    v = (v << c) | (v >> (32 - c));
    f_set(F_CF, v & 1); return v;
}
static uint16_t alu_shl16(uint16_t v, unsigned c) {
    c &= 31; if (!c) return v;
    if (c <= 16) f_set(F_CF, (v >> (16 - c)) & 1); else f_set(F_CF, 0);
    v = (uint16_t)(v << c); f_setzsp16(v); return v;
}
static uint16_t alu_shr16(uint16_t v, unsigned c) {
    c &= 31; if (!c) return v;
    f_set(F_CF, c <= 16 ? (v >> (c - 1)) & 1 : 0);
    v = (uint16_t)(v >> c); f_setzsp16(v); return v;
}
static uint16_t alu_sar16(uint16_t v, unsigned c) {
    c &= 31; if (!c) return v;
    f_set(F_CF, c <= 16 ? ((int16_t)v >> (c - 1)) & 1 : ((int16_t)v < 0));
    v = (uint16_t)((int16_t)v >> (c > 15 ? 15 : c)); f_setzsp16(v); return v;
}
static uint8_t alu_shl8(uint8_t v, unsigned c) {
    c &= 31; if (!c) return v;
    if (c <= 8) f_set(F_CF, (v >> (8 - c)) & 1); else f_set(F_CF, 0);
    v = (uint8_t)(v << c); f_setzsp8(v); return v;
}
static uint8_t alu_shr8(uint8_t v, unsigned c) {
    c &= 31; if (!c) return v;
    f_set(F_CF, c <= 8 ? (v >> (c - 1)) & 1 : 0);
    v = (uint8_t)(v >> c); f_setzsp8(v); return v;
}
static uint8_t alu_sar8(uint8_t v, unsigned c) {
    c &= 31; if (!c) return v;
    f_set(F_CF, c <= 8 ? ((int8_t)v >> (c - 1)) & 1 : ((int8_t)v < 0));
    v = (uint8_t)((int8_t)v >> (c > 7 ? 7 : c)); f_setzsp8(v); return v;
}

/* ---------------- x87 v1 ---------------- */
#define MW32_FCW_INIT 0x027Fu  /* all masks on, like real FPU init after FINIT */
static inline int fpu_top(void) { return (int)(t_cpu32.ftop & 7); }
static inline void fpu_push(double v) {
    t_cpu32.ftop = (t_cpu32.ftop - 1) & 7;
    t_cpu32.freg[t_cpu32.ftop] = v;
}
static inline double fpu_popv(void) {
    double v = t_cpu32.freg[t_cpu32.ftop];
    t_cpu32.ftop = (t_cpu32.ftop + 1) & 7;
    return v;
}
static inline double fpu_st(int i) { return t_cpu32.freg[(t_cpu32.ftop + i) & 7]; }
static inline void fpu_setst(int i, double v) { t_cpu32.freg[(t_cpu32.ftop + i) & 7] = v; }
static void fpu_compare(double a, double b) {
    t_cpu32.fsw &= ~0x4700u; /* C0 C2 C3 */
    int unordered = (a != a) || (b != b);
    uint32_t bits = 0;
    if (unordered) bits = 0x4500;             /* C3|C2|C0 pattern for unordered */
    else if (a > b) bits = 0x0000;
    else if (a < b) bits = 0x0100;            /* C0 */
    else            bits = 0x4000;            /* C3 */
    t_cpu32.fsw |= bits;
    f_set(F_ZF, bits == 0x4000u);
    f_set(F_PF, bits == 0x4500u);
    f_set(F_CF, bits == 0x0100u);
}

/* ---------------- SSE2 scalar v1 ---------------- */
static void xmm_zero_all(void) { memset(t_cpu32.xmm, 0, sizeof(t_cpu32.xmm)); }
static inline uint8_t* xmm_ptr(int i) { return t_cpu32.xmm[i & 7]; }
