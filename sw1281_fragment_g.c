
/* ---------------- glue helpers ---------------- */
static float mdl_f32(uint32_t va) { uint32_t b = m32_ld32(va); float f; memcpy(&f, &b, 4); return f; }
static uint32_t mdl_f32_from(double d) { float f = (float)d; uint32_t b; memcpy(&b, &f, 4); return b; }
static double mdl_f64_from(double d) { return d; }
static uint32_t alu_rcl32(uint32_t v, unsigned c) {
    c &= 31; if (!c) return v;
    for (unsigned i = 0; i < c; i++) {
        uint32_t msb = (v >> 31) & 1, cf = (uint32_t)f_get(F_CF);
        v = (v << 1) | cf; f_set(F_CF, msb);
    }
    f_set(F_OF, ((v >> 31) & 1) != f_get(F_CF));
    return v;
}
static uint32_t alu_rcr32(uint32_t v, unsigned c) {
    c &= 31; if (!c) return v;
    for (unsigned i = 0; i < c; i++) {
        uint32_t lsb = v & 1, cf = (uint32_t)f_get(F_CF);
        v = (v >> 1) | (cf << 31); f_set(F_CF, lsb);
    }
    f_set(F_OF, ((v >> 31) ^ (v >> 30)) & 1);
    return v;
}
/* F80 store (10-byte extended) synthesized from a double (v1 precision) */
static void mdl_st_f80(uint32_t va, double d) {
    uint8_t buf[10]; memset(buf, 0, 10);
    uint64_t bits; memcpy(&bits, &d, 8);
    int sign = (int)(bits >> 63);
    int exp = (int)((bits >> 52) & 0x7FF);
    uint64_t mant = bits & 0xFFFFFFFFFFFFFULL;
    uint32_t mlo = (uint32_t)(mant & 0xFFFFFFFFu);
    uint32_t mhi = (uint32_t)(mant >> 32);
    uint16_t e15;
    if (exp == 0) e15 = (uint16_t)(sign << 15);
    else if (exp == 0x7FF) e15 = (uint16_t)((sign << 15) | 0x7FFF);
    else e15 = (uint16_t)((sign << 15) | (uint16_t)(exp - 1023 + 16383));
    buf[0] = (uint8_t)(mlo & 0xFF); buf[1] = (uint8_t)(mlo >> 8);
    buf[2] = (uint8_t)(mlo >> 16); buf[3] = (uint8_t)(mlo >> 24);
    buf[4] = (uint8_t)(mhi & 0xFF); buf[5] = (uint8_t)(mhi >> 8);
    buf[6] = (uint8_t)(mhi >> 16); buf[7] = (uint8_t)(mhi >> 24);
    buf[8] = (uint8_t)(e15 & 0xFF); buf[9] = (uint8_t)(e15 >> 8);
    memcpy(m32p(va), buf, 10);
}

/* ---------------- SSE2 scalar subset ---------------- */
/* executed for prefixes F2/F3/66 + 0F + op2. returns 0 handled, -1 fatal. */
static int cpu32_sse2(uint8_t pfx, uint8_t op2) {
    ModRM m; modrm_decode(&m); modrm_apply_seg(&m);
    uint8_t* xr = xmm_ptr(m.reg);
    switch (op2) {
    case 0x10: case 0x11: { /* movups/movss/movupd/movsd */
        int dbl = (pfx == 0x66), sc = (pfx == 0xF2), ss = (pfx == 0xF3);
        if (op2 == 0x10) {
            if (m.is_reg) { memcpy(xr, xmm_ptr(m.rm), 16); }
            else if (sc) { uint64_t v = m32_ld64(m.ea); memcpy(xr, &v, 8); }
            else if (ss) { uint32_t v = m32_ld32(m.ea); memcpy(xr, &v, 4); }
            else if (dbl) { uint64_t v = m32_ld64(m.ea); memcpy(xr, &v, 8); }
            else { for (int i = 0; i < 4; i++) { uint32_t v = m32_ld32(m.ea + 4 * i); memcpy(xr + 4 * i, &v, 4); } }
        } else {
            if (m.is_reg) { memcpy(xmm_ptr(m.rm), xr, 16); }
            else if (ss) { m32_st32(m.ea, *(uint32_t*)xr); }
            else if (dbl || sc) { m32_st64(m.ea, *(uint64_t*)xr); }
            else { for (int i = 0; i < 4; i++) m32_st32(m.ea + 4 * i, *(uint32_t*)(xr + 4 * i)); }
        }
        return 0; }
    case 0x28: case 0x29: { /* movaps/movapd */
        if (op2 == 0x28) { if (m.is_reg) memcpy(xr, xmm_ptr(m.rm), 16); else memcpy(xr, m32p(m.ea), 16); }
        else { if (m.is_reg) memcpy(xmm_ptr(m.rm), xr, 16); else memcpy(m32p(m.ea), xr, 16); }
        return 0; }
    case 0x2A: { /* cvtsi2sd (F2) / cvtsi2ss (F3) */
        if (m.is_reg) { double v = (double)(int32_t)R32(m.rm); memcpy(xr, &v, 8); }
        else { double v = (double)(int32_t)m32_ld32(m.ea); memcpy(xr, &v, 8); }
        return 0; }
    case 0x2C: { /* cvttss2si (F3) / cvttsd2si (F2) */
        double v = (pfx == 0xF2) ? *(double*)xmm_ptr(m.rm & 7) : (double)*(float*)xmm_ptr(m.rm & 7);
        R32(m.reg) = (uint32_t)(int32_t)v; return 0; }
    case 0x2D: { R32(m.reg) = (uint32_t)(int32_t)*(double*)xmm_ptr(m.rm & 7); return 0; } /* cvtsd2si round */
    case 0x2E: case 0x2F: { /* ucomisd/comisd */
        double a = *(double*)xmm_ptr(m.reg), b;
        if (m.is_reg) b = *(double*)xmm_ptr(m.rm); else b = m32_ld64(m.ea);
        f_set(F_ZF, a == b); f_set(F_PF, (a != a) || (b != b)); f_set(F_CF, (a != a || b != b) ? 1 : a < b);
        f_set(F_SF, 0); f_set(F_OF, 0);
        return 0; }
    case 0x51: { /* sqrtsd/sqrtss */
        if (pfx == 0xF3) { float f = sqrtf(*(float*)xmm_ptr(m.rm & 7)); memcpy(xr, &f, 4); }
        else { double v = sqrt(*(double*)xmm_ptr(m.rm & 7)); memcpy(xr, &v, 8); }
        return 0; }
    case 0x54: case 0x55: case 0x56: case 0x57: { /* and/andn/or/xor pd/ps */
        uint8_t* xs = xmm_ptr(m.rm & 7);
        for (int i = 0; i < 16; i++) {
            switch (op2) {
            case 0x54: xr[i] &= xs[i]; break;
            case 0x55: xr[i] = (uint8_t)(~xr[i] & xs[i]); break;
            case 0x56: xr[i] |= xs[i]; break;
            case 0x57: xr[i] ^= xs[i]; break;
            }
        }
        return 0; }
    case 0x58: case 0x59: case 0x5C: case 0x5E: case 0x5D: case 0x5F: { /* add/sub/mul/div/min/max */
        int dbl = (pfx == 0x66 || pfx == 0xF2), ss = (pfx == 0xF3);
        double a, b;
        if (dbl) { a = *(double*)xr; b = m.is_reg ? *(double*)xmm_ptr(m.rm) : m32_ld64(m.ea); }
        else if (ss) { float fa = *(float*)xr, fb = m.is_reg ? *(float*)xmm_ptr(m.rm) : mdl_f32(m.ea); a = fa; b = fb; }
        else { a = 0; b = 0; }
        double r = a;
        switch (op2) {
        case 0x58: r = a + b; break; case 0x59: r = a * b; break;
        case 0x5C: r = a - b; break; case 0x5E: r = b ? a / b : 0; break;
        case 0x5D: r = a < b ? a : b; break; case 0x5F: r = a > b ? a : b; break;
        }
        if (dbl) memcpy(xr, &r, 8);
        else if (ss) { float f = (float)r; memcpy(xr, &f, 4); }
        return 0; }
    case 0x5A: { /* cvtsd2ss (F2) / cvtss2sd (F3) */
        if (pfx == 0xF2) { double d = *(double*)xmm_ptr(m.rm & 7); float f = (float)d; memcpy(xr, &f, 4); }
        else { float f = *(float*)xmm_ptr(m.rm & 7); double d = f; memcpy(xr, &d, 8); }
        return 0; }
    case 0x6E: { uint32_t v = m.is_reg ? R32(m.rm) : m32_ld32(m.ea); memset(xr, 0, 16); memcpy(xr, &v, 4); return 0; } /* movd xmm, r/m */
    case 0x7E: { if (pfx == 0xF3) { uint64_t v; if (m.is_reg) memcpy(&v, xmm_ptr(m.rm), 8); else v = m32_ld64(m.ea); memcpy(xr, &v, 8); }
                  else { uint32_t v = *(uint32_t*)xr; if (m.is_reg) R32(m.rm) = v; else m32_st32(m.ea, v); } return 0; } /* movq/movd */
    case 0xD6: { uint64_t v; memcpy(&v, xr, 8); if (m.is_reg) memcpy(xmm_ptr(m.rm), &v, 8); else m32_st64(m.ea, v); return 0; } /* movq */
    case 0xEF: memset(xr, 0, 16); return 0; /* pxor */
    default: return -1;
    }
}

/* ---------------- run loop ---------------- */
static int cpu32_run(uint64_t max_steps) {
    uint64_t n = 0;
    for (;;) {
        if (g_cpu32_exit_req) return (int)g_cpu32_exit_req;
        if (t_cpu32.eip == MW32_RET_SENTINEL) return -2; /* callback frame done */
        if (max_steps && n >= max_steps) return -3;
        int rc = cpu32_step();
        n++;
        if (rc != 0) return rc;
    }
}
