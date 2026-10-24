
/* continuation of the opcode dispatch: control flow, shifts, grp3,
 * string ops, flags, x87 (Intel-accurate mod=3 map), SSE2 escape —
 * called from cpu32_step for opcodes the first switch does not claim. */
static int cpu32_step2(uint8_t op, int o32, int arep, int frep) {
    ModRM m; int have_m = 0;
#define NEEDM2() do { if (!have_m) { modrm_decode(&m); modrm_apply_seg(&m); have_m = 1; } } while (0)
#define R32b(i) (t_cpu32.r[i])
#define LD32M() (m.is_reg ? R32b(m.rm) : m32_ld32(m.ea))
#define ST32M(v) do { if (m.is_reg) R32b(m.rm) = (v); else m32_st32(m.ea, (v)); } while (0)
#define LD16M() (m.is_reg ? (uint16_t)(R32b(m.rm) & 0xFFFF) : m32_ld16(m.ea))
#define ST16M(v) do { if (m.is_reg) R32b(m.rm) = ((R32b(m.rm) & 0xFFFF0000u) | (uint32_t)(uint16_t)(v)); else m32_st16(m.ea, (v)); } while (0)
#define LD8M()  (m.is_reg ? (uint8_t)(R32b(m.rm & 7) & 0xFF) : m32_ld8(m.ea))
#define ST8M(v) do { if (m.is_reg) { \
        if (m.rm < 4) R32b(m.rm) = (R32b(m.rm) & 0xFFFFFF00u) | (uint32_t)(v); \
        else R32b(m.rm - 4) = (R32b(m.rm - 4) & 0xFFFF00FFu) | ((uint32_t)(v) << 8); \
    } else m32_st8(m.ea, (v)); } while (0)

    switch (op) {
    /* ---- test ---- */
    case 0x85: { NEEDM2(); alu_logic32(LD32M() & R32b(m.reg)); break; }
    case 0x84: { NEEDM2(); alu_logic8(LD8M() & (uint8_t)R32b(m.reg & 7)); break; }
    case 0xA9: { alu_logic32(R32b(EAXi) & m32_ld32(t_cpu32.eip)); t_cpu32.eip += 4; break; }
    case 0xA8: { alu_logic8((uint8_t)R32b(EAXi) & m32_ld8(t_cpu32.eip)); t_cpu32.eip++; break; }
    /* ---- grp3 (F6/F7) ---- */
    case 0xF7: { NEEDM2(); uint8_t sub = m.reg;
        switch (sub) {
        case 0: case 1: { uint32_t v = LD32M(); alu_logic32(v & m32_ld32(t_cpu32.eip)); t_cpu32.eip += 4; break; }
        case 2: { uint32_t v = LD32M(); ST32M(~v); break; }
        case 3: { uint32_t v = LD32M(); ST32M(alu_sub32(0, v, 0)); break; }
        case 4: { uint32_t v = LD32M(); uint64_t p = (uint64_t)R32b(EAXi) * v;
            R32b(EAXi) = (uint32_t)p; R32b(EDXi) = (uint32_t)(p >> 32);
            f_set(F_CF, R32b(EDXi) != 0); f_set(F_OF, R32b(EDXi) != 0); f_setzsp32(R32b(EAXi)); break; }
        case 5: { uint32_t v = LD32M(); int64_t p = (int64_t)(int32_t)R32b(EAXi) * (int64_t)(int32_t)v;
            R32b(EAXi) = (uint32_t)(int64_t)p; R32b(EDXi) = (uint32_t)(((uint64_t)p) >> 32);
            int ov = p != (int64_t)(int32_t)(uint32_t)p; f_set(F_CF, ov); f_set(F_OF, ov); break; }
        case 6: { uint32_t v = LD32M();
            if (!v) cpu32_fatal("div by 0", op, t_cpu32.eip);
            uint64_t nn = ((uint64_t)R32b(EDXi) << 32) | R32b(EAXi);
            if (nn / v > 0xFFFFFFFFull) cpu32_fatal("div overflow", op, t_cpu32.eip);
            R32b(EAXi) = (uint32_t)(nn / v); R32b(EDXi) = (uint32_t)(nn % v); break; }
        case 7: { uint32_t v = LD32M();
            if (!v) cpu32_fatal("idiv by 0", op, t_cpu32.eip);
            int64_t nn = ((int64_t)(int32_t)R32b(EDXi) << 32) | (uint32_t)R32b(EAXi);
            int64_t q = nn / (int32_t)v;
            if (q > 2147483647ll || q < -2147483648ll) cpu32_fatal("idiv overflow", op, t_cpu32.eip);
            R32b(EAXi) = (uint32_t)(int32_t)q; R32b(EDXi) = (uint32_t)(int32_t)(nn % (int32_t)v); break; }
        } break; }
    case 0xF6: { NEEDM2(); uint8_t sub = m.reg;
        switch (sub) {
        case 0: case 1: { uint8_t v = LD8M(); alu_logic8(v & m32_ld8(t_cpu32.eip)); t_cpu32.eip++; break; }
        case 2: { uint8_t v = LD8M(); ST8M(~v); break; }
        case 3: { uint8_t v = LD8M(); ST8M(alu_sub8(0, v, 0)); break; }
        case 4: { uint8_t v = LD8M(); uint16_t p = (uint16_t)((uint8_t)R32b(EAXi) * v);
            R32b(EAXi) = (R32b(EAXi) & 0xFFFF0000u) | p;
            f_set(F_CF, (p >> 8) != 0); f_set(F_OF, (p >> 8) != 0); break; }
        case 5: { uint8_t v = LD8M(); int16_t p = (int16_t)((int8_t)(uint8_t)R32b(EAXi) * (int8_t)v);
            R32b(EAXi) = (R32b(EAXi) & 0xFFFF0000u) | (uint16_t)p; break; }
        case 6: { uint8_t v = LD8M(); uint16_t nn = (uint16_t)R32b(EAXi);
            if (!v) cpu32_fatal("div8 by 0", op, t_cpu32.eip);
            R32b(EAXi) = (R32b(EAXi) & 0xFFFF0000u) | ((uint32_t)(uint8_t)(nn % v) << 8) | (uint8_t)(nn / v); break; }
        case 7: { uint8_t v = LD8M(); int16_t nn = (int16_t)R32b(EAXi);
            if (!v) cpu32_fatal("idiv8 by 0", op, t_cpu32.eip);
            R32b(EAXi) = (R32b(EAXi) & 0xFFFF0000u) | ((uint32_t)(uint8_t)(int8_t)(nn % (int8_t)v) << 8) | (uint8_t)(int8_t)(nn / (int8_t)v); break; }
        } break; }
    /* ---- imul r32, r/m32, imm ---- */
    case 0x69: { NEEDM2(); uint32_t v = LD32M(); int32_t imm = (int32_t)m32_ld32(t_cpu32.eip); t_cpu32.eip += 4;
        int64_t p = (int64_t)(int32_t)v * (int64_t)imm; R32b(m.reg) = (uint32_t)p;
        int ov = p != (int64_t)(int32_t)p; f_set(F_CF, ov); f_set(F_OF, ov); break; }
    case 0x6B: { NEEDM2(); uint32_t v = LD32M(); int32_t imm = (int8_t)m32_ld8(t_cpu32.eip); t_cpu32.eip++;
        int64_t p = (int64_t)(int32_t)v * (int64_t)imm; R32b(m.reg) = (uint32_t)p;
        int ov = p != (int64_t)(int32_t)p; f_set(F_CF, ov); f_set(F_OF, ov); break; }
    /* ---- shift group ---- */
    case 0xC1: { NEEDM2(); uint8_t sub = m.reg; uint32_t c = m32_ld8(t_cpu32.eip) & 31; t_cpu32.eip++;
        if (!o32) { uint16_t v = LD16M(), r2 = v;
            switch (sub) {
            case 0: r2 = alu_shl16(v, c); break; case 1: case 5: r2 = alu_shr16(v, c); break;
            case 7: r2 = alu_sar16(v, c); break;
            default: cpu32_fatal("shift16 imm /r", op, t_cpu32.eip); break; }
            ST16M(r2); break; }
        uint32_t v = LD32M(), r2 = v;
        switch (sub) {
        case 0: case 4: r2 = alu_shl32(v, c); break; case 1: case 5: r2 = alu_shr32(v, c); break;
        case 7: r2 = alu_sar32(v, c); break; case 6: r2 = alu_rol32(v, c); break;
        case 3: r2 = alu_rcr32(v, c); break; case 2: r2 = alu_rcl32(v, c); break; }
        ST32M(r2); break; }
    case 0xD1: { NEEDM2(); uint8_t sub = m.reg;
        if (!o32) { uint16_t v = LD16M(), r2 = v;
            switch (sub) {
            case 0: r2 = alu_shl16(v, 1); break; case 1: case 5: r2 = alu_shr16(v, 1); break;
            case 7: r2 = alu_sar16(v, 1); break;
            default: cpu32_fatal("shift16 /1", op, t_cpu32.eip); break; }
            ST16M(r2); break; }
        uint32_t v = LD32M(), r2 = v;
        switch (sub) {
        case 0: case 4: r2 = alu_shl32(v, 1); break; case 1: case 5: r2 = alu_shr32(v, 1); break;
        case 7: r2 = alu_sar32(v, 1); break; case 6: r2 = alu_rol32(v, 1); break;
        case 3: r2 = alu_rcr32(v, 1); break; case 2: r2 = alu_rcl32(v, 1); break; }
        ST32M(r2); break; }
    case 0xD3: { NEEDM2(); uint8_t sub = m.reg; uint32_t c = R32b(ECXi) & 31;
        if (!o32) { uint16_t v = LD16M(), r2 = v;
            switch (sub) {
            case 0: r2 = alu_shl16(v, c); break; case 1: case 5: r2 = alu_shr16(v, c); break;
            case 7: r2 = alu_sar16(v, c); break;
            default: cpu32_fatal("shift16 cl /r", op, t_cpu32.eip); break; }
            ST16M(r2); break; }
        uint32_t v = LD32M(), r2 = v;
        switch (sub) {
        case 0: case 4: r2 = alu_shl32(v, c); break; case 1: case 5: r2 = alu_shr32(v, c); break;
        case 7: r2 = alu_sar32(v, c); break; case 6: r2 = alu_rol32(v, c); break;
        case 3: r2 = alu_rcr32(v, c); break; case 2: r2 = alu_rcl32(v, c); break; }
        ST32M(r2); break; }
    case 0xC0: { NEEDM2(); uint8_t sub = m.reg; uint32_t c = m32_ld8(t_cpu32.eip) & 31; t_cpu32.eip++;
        uint8_t v = LD8M(), r2 = v;
        switch (sub) {
        case 0: case 4: r2 = alu_shl8(v, c); break; case 1: case 5: r2 = alu_shr8(v, c); break;
        case 7: r2 = alu_sar8(v, c); break;
        case 6: { c &= 7; if (c) { f_set(F_CF, (v >> (8 - c)) & 1); v = (uint8_t)((v << c) | (v >> (8 - c))); f_set(F_CF, v & 1); } r2 = v; break; }
        default: cpu32_fatal("shift8 imm /r", op, t_cpu32.eip); break; }
        ST8M(r2); break; }
    case 0xD0: { NEEDM2(); uint8_t sub = m.reg; uint8_t v = LD8M(), r2 = v;
        switch (sub) {
        case 0: case 4: r2 = alu_shl8(v, 1); break; case 1: case 5: r2 = alu_shr8(v, 1); break;
        case 7: r2 = alu_sar8(v, 1); break;
        default: cpu32_fatal("shift8 /1", op, t_cpu32.eip); break; }
        ST8M(r2); break; }
    case 0xD2: { NEEDM2(); uint8_t sub = m.reg; uint32_t c = R32b(ECXi) & 31; uint8_t v = LD8M(), r2 = v;
        switch (sub) {
        case 0: case 4: r2 = alu_shl8(v, c); break; case 1: case 5: r2 = alu_shr8(v, c); break;
        case 7: r2 = alu_sar8(v, c); break;
        default: cpu32_fatal("shift8 cl /r", op, t_cpu32.eip); break; }
        ST8M(r2); break; }
    case 0x0F + 0x100: break; /* dead guard (real 0F handled in step1) */
    /* ---- shld/shrd (0F A4/A5/AC/AD handled in step1's 0F switch) ---- */
    /* ---- control flow ---- */
    case 0xE8: { int32_t rel = (int32_t)m32_ld32(t_cpu32.eip); t_cpu32.eip += 4;
        m32_push(t_cpu32.eip); t_cpu32.eip = (uint32_t)(int64_t)((int64_t)t_cpu32.eip + rel); break; }
    case 0xE9: { int32_t rel = (int32_t)m32_ld32(t_cpu32.eip); t_cpu32.eip += 4;
        t_cpu32.eip = (uint32_t)(int64_t)((int64_t)t_cpu32.eip + rel); break; }
    case 0xEB: { int8_t rel = (int8_t)m32_ld8(t_cpu32.eip); t_cpu32.eip++;
        t_cpu32.eip = (uint32_t)(int64_t)((int64_t)t_cpu32.eip + rel); break; }
    case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75: case 0x76: case 0x77:
    case 0x78: case 0x79: case 0x7A: case 0x7B: case 0x7C: case 0x7D: case 0x7E: case 0x7F: {
        int8_t rel = (int8_t)m32_ld8(t_cpu32.eip); t_cpu32.eip++;
        if (cond_eval(op & 0xF)) t_cpu32.eip = (uint32_t)(int64_t)((int64_t)t_cpu32.eip + rel);
        break; }
    case 0xC3: t_cpu32.eip = m32_pop(); break;
    case 0xC2: { uint16_t n = m32_ld16(t_cpu32.eip); t_cpu32.eip += 2;
        uint32_t t = m32_pop(); t_cpu32.r[ESPi] += n; t_cpu32.eip = t; break; }
    case 0xC9: { t_cpu32.r[ESPi] = R32b(EBPi); R32b(EBPi) = m32_pop(); break; }
    case 0xC8: { uint16_t sz = m32_ld16(t_cpu32.eip); t_cpu32.eip += 2; t_cpu32.eip++;
        m32_push(R32b(EBPi)); uint32_t fp = R32b(ESPi);
        if (sz) R32b(ESPi) -= sz; R32b(EBPi) = fp; break; }
    case 0x90: break; /* nop */
    case 0x98: { if (o32) R32b(EAXi) = (uint32_t)(int32_t)(int16_t)(R32b(EAXi) & 0xFFFF);
                 else R32b(EAXi) = (R32b(EAXi) & 0xFFFF0000u) | (uint16_t)(int8_t)(uint8_t)R32b(EAXi); break; }
    case 0x99: { if (o32) R32b(EDXi) = ((int32_t)R32b(EAXi) < 0) ? 0xFFFFFFFFu : 0;
                 else R32b(EAXi) = (R32b(EAXi) & 0xFFFF0000u) | ((R32b(EAXi) & 0x8000u) ? 0xFFFFu : 0u); break; }
    case 0x9E: { uint8_t ah = (uint8_t)(R32b(EAXi) >> 8);
        t_cpu32.eflags = (t_cpu32.eflags & ~0xD5u) | (ah & 0xD5u); break; }
    case 0x9F: { uint8_t f = (uint8_t)(t_cpu32.eflags & 0xD5u) | 0x02;
        R32b(EAXi) = (R32b(EAXi) & 0xFFFF00FFu) | ((uint32_t)f << 8); break; }
    case 0xF5: t_cpu32.eflags ^= F_CF; break;
    case 0xF8: f_set(F_CF, 0); break;
    case 0xF9: f_set(F_CF, 1); break;
    case 0xFC: f_set(F_DF, 0); break;
    case 0xFD: f_set(F_DF, 1); break;
    case 0xFA: case 0xFB: break;
    case 0xE0: case 0xE1: case 0xE2: { int8_t rel = (int8_t)m32_ld8(t_cpu32.eip); t_cpu32.eip++;
        uint32_t c = --R32b(ECXi);
        int taken = (op == 0xE2) ? (c != 0) : (op == 0xE0) ? (c != 0 && !f_get(F_ZF)) : (c != 0 && f_get(F_ZF));
        if (taken) t_cpu32.eip = (uint32_t)(int64_t)((int64_t)t_cpu32.eip + rel); break; }
    case 0xE3: { int8_t rel = (int8_t)m32_ld8(t_cpu32.eip); t_cpu32.eip++;
        if (R32b(ECXi) == 0) t_cpu32.eip = (uint32_t)(int64_t)((int64_t)t_cpu32.eip + rel); break; }
    case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97: {
        uint32_t t = R32b(EAXi); R32b(EAXi) = R32b(op & 7); R32b(op & 7) = t; break; }
    case 0x87: { NEEDM2(); uint32_t t = LD32M(); ST32M(R32b(m.reg)); R32b(m.reg) = t; break; }
    case 0x86: { NEEDM2(); uint8_t d = LD8M();
        uint8_t s = (m.reg < 4) ? (uint8_t)R32b(m.reg) : (uint8_t)(R32b(m.reg - 4) >> 8);
        ST8M(s);
        if (m.reg < 4) R32b(m.reg) = (R32b(m.reg) & 0xFFFFFF00u) | d;
        else R32b(m.reg - 4) = (R32b(m.reg - 4) & 0xFFFF00FFu) | ((uint32_t)d << 8); break; }
    /* ---- string ops (with rep/repe/repne) ---- */
    case 0xA4: case 0xA5: case 0xA6: case 0xA7: case 0xAA: case 0xAB: case 0xAC: case 0xAD: case 0xAE: case 0xAF: {
        int w32 = (op & 1);
        uint32_t sz = w32 ? (o32 ? 4 : 2) : 1;
        int dir = f_get(F_DF) ? -1 : 1;
        uint32_t src = R32b(ESIi), dst = R32b(EDIi);
        if (g_segreg_ovr == 0) src += t_cpu32.fsbase;
        else if (g_segreg_ovr == 1) src += t_cpu32.gsbase;
        if ((arep || frep) && R32b(ECXi) == 0) break;
        uint32_t done = 0;
        for (;;) {
            if (arep || frep) {
                if (R32b(ECXi) == 0) break;
                R32b(ECXi)--;
            }
            switch (op) {
            case 0xA4: m32_st8(dst, m32_ld8(src)); break;
            case 0xA5: if (o32) m32_st32(dst, m32_ld32(src)); else m32_st16(dst, m32_ld16(src)); break;
            case 0xA6: case 0xA7: { uint32_t a = (op == 0xA7) ? (o32 ? m32_ld32(src) : m32_ld16(src)) : m32_ld8(src);
                                   uint32_t b = (op == 0xA7) ? (o32 ? m32_ld32(dst) : m32_ld16(dst)) : m32_ld8(dst);
                if (op == 0xA7) { if (o32) alu_sub32(a, b, 0); else alu_sub16((uint16_t)a, (uint16_t)b, 0); }
                else alu_sub8((uint8_t)a, (uint8_t)b, 0); break; }
            case 0xAA: m32_st8(dst, (uint8_t)R32b(EAXi)); break;
            case 0xAB: if (o32) m32_st32(dst, R32b(EAXi)); else m32_st16(dst, (uint16_t)R32b(EAXi)); break;
            case 0xAC: case 0xAD: { uint32_t v = (op == 0xAD) ? (o32 ? m32_ld32(src) : m32_ld16(src)) : m32_ld8(src);
                if (op == 0xAD) { if (o32) R32b(EAXi) = v; else R32b(EAXi) = (R32b(EAXi) & 0xFFFF0000u) | (uint16_t)v; }
                else R32b(EAXi) = (R32b(EAXi) & 0xFFFFFF00u) | (uint8_t)v; break; }
            case 0xAE: case 0xAF: { uint32_t a = (op == 0xAF) ? (o32 ? m32_ld32(src) : m32_ld16(src)) : m32_ld8(src);
                if (op == 0xAF) { if (o32) alu_sub32(R32b(EAXi), a, 0); else alu_sub16((uint16_t)R32b(EAXi), (uint16_t)a, 0); }
                else alu_sub8((uint8_t)R32b(EAXi), (uint8_t)a, 0); break; }
            }
            src += dir * sz; dst += dir * sz;
            if (!(arep || frep)) break;
            if (op == 0xA6 || op == 0xA7 || op == 0xAE || op == 0xAF) {
                int z = f_get(F_ZF);
                if (arep && z) break;
                if (frep && !z) break;
            }
            if (++done > 0x10000000u) cpu32_fatal("rep runaway", op, t_cpu32.eip);
        }
        R32b(ESIi) = src; R32b(EDIi) = dst;
        break; }
    /* ---- x87 (D8-DF), Intel-accurate mod=3 map ---- */
    case 0xD8: case 0xD9: case 0xDA: case 0xDB: case 0xDC: case 0xDD: case 0xDE: case 0xDF: {
        NEEDM2();
        if (m.mod == 3) {
            int i = m.rm;
            switch (op) {
            case 0xD9:
                switch (m.reg) {
                case 0: { double v = fpu_st(i); fpu_push(v); break; }                     /* FLD st(i) */
                case 1: { double t = fpu_st(0); fpu_setst(0, fpu_st(i)); fpu_setst(i, t); break; } /* FXCH */
                case 2: if (i == 0) break; /* FNOP */ else cpu32_fatal("x87 D9 D8+", op, t_cpu32.eip); break;
                case 4:
                    if (i == 0) fpu_setst(0, -fpu_st(0));                                 /* FCHS */
                    else if (i == 1) fpu_setst(0, fabs(fpu_st(0)));                       /* FABS */
                    else if (i == 4) fpu_compare(fpu_st(0), 0.0);                         /* FTST */
                    else cpu32_fatal("x87 D9 E4+", op, t_cpu32.eip);
                    break;
                case 5:
                    if (i == 0) fpu_push(1.0);                                            /* FLD1 */
                    else if (i == 1) fpu_push(3.3219280948873623);                        /* FLDL2T */
                    else if (i == 2) fpu_push(1.4426950408889634);                        /* FLDL2E */
                    else if (i == 3) fpu_push(3.141592653589793);                         /* FLDPI */
                    else if (i == 6) fpu_push(0.0);                                       /* FLDZ */
                    else cpu32_fatal("x87 D9 E8+", op, t_cpu32.eip);
                    break;
                case 7:
                    if (i == 0) { double v = fpu_st(0), r = fmod(v, 1.0); /* FPREM v1 */
                        double q = trunc(v / 1.0); fpu_setst(0, v - q); (void)r; break; }
                    if (i == 1) { fpu_setst(0, pow(2.0, fpu_st(0)) - 1.0); break; }       /* F2XM1 */
                    if (i == 2) { fpu_setst(0, sqrt(fpu_st(0))); break; }                 /* FSQRT */
                    if (i == 4) { fpu_setst(0, round(fpu_st(0))); break; }                /* FRNDINT */
                    if (i == 5) { break; }                                                /* FSCALE v1: no-op */
                    if (i == 6) { fpu_setst(0, sin(fpu_st(0))); break; }                  /* FSIN */
                    if (i == 7) { fpu_setst(0, cos(fpu_st(0))); break; }                  /* FCOS */
                    break;
                default: cpu32_fatal("x87 D9 mod3", op, t_cpu32.eip); break;
                }
                break;
            case 0xD8:
                switch (m.reg) {
                case 0: fpu_setst(0, fpu_st(0) + fpu_st(i)); break;
                case 1: fpu_setst(0, fpu_st(0) * fpu_st(i)); break;
                case 2: fpu_compare(fpu_st(0), fpu_st(i)); break;
                case 3: fpu_compare(fpu_st(0), fpu_st(i)); fpu_popv(); break;
                case 4: fpu_setst(0, fpu_st(0) - fpu_st(i)); break;
                case 5: fpu_setst(0, fpu_st(i) - fpu_st(0)); break;
                case 6: fpu_setst(0, fpu_st(0) / fpu_st(i)); break;
                case 7: fpu_setst(0, fpu_st(i) / fpu_st(0)); break;
                }
                break;
            case 0xDC:
                switch (m.reg) {
                case 0: fpu_setst(i, fpu_st(i) + fpu_st(0)); break;
                case 1: fpu_setst(i, fpu_st(i) * fpu_st(0)); break;
                case 2: fpu_compare(fpu_st(0), fpu_st(i)); break;
                case 3: fpu_compare(fpu_st(0), fpu_st(i)); fpu_popv(); break;
                case 4: fpu_setst(i, fpu_st(i) - fpu_st(0)); break;
                case 5: fpu_setst(i, fpu_st(0) - fpu_st(i)); break;
                case 6: fpu_setst(i, fpu_st(i) / fpu_st(0)); break;
                case 7: fpu_setst(i, fpu_st(0) / fpu_st(i)); break;
                }
                break;
            case 0xDE:
                if (m.reg == 2 && i == 1) { /* FCOMPP */
                    fpu_compare(fpu_st(0), fpu_st(1)); fpu_popv(); fpu_popv(); break; }
                switch (m.reg) {
                case 0: fpu_setst(i, fpu_st(i) + fpu_st(0)); fpu_popv(); break;   /* FADDP */
                case 1: fpu_setst(i, fpu_st(i) * fpu_st(0)); fpu_popv(); break;   /* FMULP */
                case 2: fpu_compare(fpu_st(0), fpu_st(i)); fpu_popv(); break;     /* FCOMP */
                case 4: fpu_setst(i, fpu_st(0) - fpu_st(i)); fpu_popv(); break;   /* FSUBRP */
                case 5: fpu_setst(i, fpu_st(i) - fpu_st(0)); fpu_popv(); break;   /* FSUBP */
                case 6: fpu_setst(i, fpu_st(0) / fpu_st(i)); fpu_popv(); break;   /* FDIVRP */
                case 7: fpu_setst(i, fpu_st(i) / fpu_st(0)); fpu_popv(); break;   /* FDIVP */
                default: cpu32_fatal("x87 DE mod3", op, t_cpu32.eip); break;
                }
                break;
            case 0xDD:
                switch (m.reg) {
                case 0: break;                                                    /* FFREE */
                case 2: fpu_setst(i, fpu_st(0)); break;                           /* FST st(i) */
                case 3: fpu_setst(i, fpu_st(0)); fpu_popv(); break;               /* FSTP st(i) */
                case 4: fpu_compare(fpu_st(0), fpu_st(i)); break;                 /* FUCOM */
                case 5: fpu_compare(fpu_st(0), fpu_st(i)); fpu_popv(); break;     /* FUCOMP */
                default: cpu32_fatal("x87 DD mod3", op, t_cpu32.eip); break;
                }
                break;
            case 0xDB:
                if (m.reg == 7 && i == 2) break;                                  /* FNCLEX */
                if (m.reg == 7 && i == 3) { t_cpu32.ftop = 0; t_cpu32.fsw = 0; break; } /* FINIT */
                cpu32_fatal("x87 DB mod3", op, t_cpu32.eip); break;
            case 0xDA:
                if (m.reg == 5 && i == 1) { /* FUCOMIP (DA E9) */
                    fpu_compare(fpu_st(0), fpu_st(1)); fpu_popv(); break; }
                cpu32_fatal("x87 DA mod3", op, t_cpu32.eip); break;
            case 0xDF:
                if (m.reg == 7 && i == 0) { /* FNSTSW AX */
                    uint16_t sw = (uint16_t)((t_cpu32.fsw & ~0x3800u) | (t_cpu32.ftop << 11));
                    R32b(EAXi) = (R32b(EAXi) & 0xFFFF0000u) | sw; break; }
                cpu32_fatal("x87 DF mod3", op, t_cpu32.eip); break;
            }
        } else {
            switch (op) {
            case 0xD9:
                switch (m.reg) {
                case 0: fpu_push((double)mdl_f32(m.ea)); break;                   /* FLD m32 */
                case 2: m32_st32(m.ea, mdl_f32_from(fpu_st(0))); break;           /* FST m32 */
                case 3: m32_st32(m.ea, mdl_f32_from(fpu_st(0))); fpu_popv(); break; /* FSTP m32 */
                case 4: break;                                                    /* FLDENV stub */
                case 5: t_cpu32.fcw = m32_ld16(m.ea); break;                      /* FLDCW */
                case 6: break;                                                    /* FNSTENV stub */
                case 7: m32_st16(m.ea, t_cpu32.fcw); break;                       /* FNSTCW */
                }
                break;
            case 0xD8: {
                float mv = mdl_f32(m.ea);
                switch (m.reg) {
                case 0: fpu_setst(0, fpu_st(0) + mv); break;
                case 1: fpu_setst(0, fpu_st(0) * mv); break;
                case 2: fpu_compare(fpu_st(0), mv); break;
                case 3: fpu_compare(fpu_st(0), mv); fpu_popv(); break;
                case 4: fpu_setst(0, fpu_st(0) - mv); break;
                case 5: fpu_setst(0, mv - fpu_st(0)); break;
                case 6: fpu_setst(0, fpu_st(0) / mv); break;
                case 7: fpu_setst(0, mv / fpu_st(0)); break;
                }
                break; }
            case 0xDC: {
                double mv = m32_ld64(m.ea);
                switch (m.reg) {
                case 0: fpu_setst(0, fpu_st(0) + mv); break;
                case 1: fpu_setst(0, fpu_st(0) * mv); break;
                case 2: fpu_compare(fpu_st(0), mv); break;
                case 3: fpu_compare(fpu_st(0), mv); fpu_popv(); break;
                case 4: fpu_setst(0, fpu_st(0) - mv); break;
                case 5: fpu_setst(0, mv - fpu_st(0)); break;
                case 6: fpu_setst(0, fpu_st(0) / mv); break;
                case 7: fpu_setst(0, mv / fpu_st(0)); break;
                }
                break; }
            case 0xDD:
                switch (m.reg) {
                case 0: fpu_push(m32_ld64(m.ea)); break;                          /* FLD m64 */
                case 2: m32_st64(m.ea, fpu_st(0)); break;                         /* FST m64 */
                case 3: m32_st64(m.ea, fpu_st(0)); fpu_popv(); break;             /* FSTP m64 */
                case 7: m32_st16(m.ea, (uint16_t)((t_cpu32.fsw & ~0x3800u) | (t_cpu32.ftop << 11))); break; /* FNSTSW m16 */
                default: cpu32_fatal("x87 DD m", op, t_cpu32.eip); break;
                }
                break;
            case 0xDB: /* int32 / m80 */
                switch (m.reg) {
                case 0: fpu_push((double)(int32_t)m32_ld32(m.ea)); break;         /* FILD m32 */
                case 2: m32_st32(m.ea, (uint32_t)(int32_t)fpu_st(0)); break;      /* FIST m32 */
                case 3: m32_st32(m.ea, (uint32_t)(int32_t)fpu_st(0)); fpu_popv(); break; /* FISTP m32 */
                case 5: { /* FLD m80 — decode 10-byte extended into double */
                    uint8_t* b = m32p(m.ea);
                    uint16_t e15 = (uint16_t)(b[8] | (b[9] << 8));
                    uint64_t mant = (uint64_t)b[0] | ((uint64_t)b[1] << 8) | ((uint64_t)b[2] << 16) |
                                    ((uint64_t)b[3] << 24) | ((uint64_t)b[4] << 32) | ((uint64_t)b[5] << 40) |
                                    ((uint64_t)b[6] << 48) | ((uint64_t)b[7] << 56);
                    int sign = (e15 >> 15) & 1;
                    int exp = (e15 & 0x7FFF) - 16383 + 1023;
                    double v;
                    if ((e15 & 0x7FFF) == 0 && mant == 0) v = 0.0;
                    else if (exp <= 0) v = ldexp((double)mant, exp - 63);
                    else if (exp >= 0x7FF) v = 1.0 / 0.0;
                    else v = ldexp((double)(mant | (1ULL << 63)), exp - 63 - 52);
                    if (sign) v = -v;
                    fpu_push(v); break; }
                case 7: mdl_st_f80(m.ea, fpu_st(0)); fpu_popv(); break;           /* FSTP m80 */
                default: cpu32_fatal("x87 DB m", op, t_cpu32.eip); break;
                }
                break;
            case 0xDA: { int32_t mv = (int32_t)m32_ld32(m.ea);                    /* int32 arith */
                switch (m.reg) {
                case 0: fpu_setst(0, fpu_st(0) + mv); break;                      /* FIADD */
                case 1: fpu_setst(0, fpu_st(0) * mv); break;                      /* FIMUL */
                case 2: fpu_compare(fpu_st(0), mv); break;                        /* FICOM */
                case 3: fpu_compare(fpu_st(0), mv); fpu_popv(); break;            /* FICOMP */
                case 4: fpu_setst(0, fpu_st(0) - mv); break;                      /* FISUB */
                case 5: fpu_setst(0, mv - fpu_st(0)); break;                      /* FISUBR */
                case 6: fpu_setst(0, fpu_st(0) / mv); break;                      /* FIDIV */
                case 7: fpu_setst(0, mv / fpu_st(0)); break;                      /* FIDIVR */
                }
                break; }
            case 0xDE: { int16_t mv = (int16_t)m32_ld16(m.ea);                    /* int16 arith */
                switch (m.reg) {
                case 0: fpu_setst(0, fpu_st(0) + mv); break;
                case 1: fpu_setst(0, fpu_st(0) * mv); break;
                case 2: fpu_compare(fpu_st(0), mv); break;
                case 3: fpu_compare(fpu_st(0), mv); fpu_popv(); break;
                case 4: fpu_setst(0, fpu_st(0) - mv); break;
                case 5: fpu_setst(0, mv - fpu_st(0)); break;
                case 6: fpu_setst(0, fpu_st(0) / mv); break;
                case 7: fpu_setst(0, mv / fpu_st(0)); break;
                }
                break; }
            case 0xDF:
                switch (m.reg) {
                case 0: fpu_push((double)(int16_t)m32_ld16(m.ea)); break;         /* FILD m16 */
                case 2: m32_st16(m.ea, (uint16_t)(int16_t)fpu_st(0)); break;      /* FIST m16 */
                case 3: m32_st16(m.ea, (uint16_t)(int16_t)fpu_st(0)); fpu_popv(); break; /* FISTP m16 */
                case 5: fpu_push(m32_ld64(m.ea)); break;                          /* FILD m64 */
                case 7: m32_st64(m.ea, (uint64_t)(int64_t)fpu_st(0)); fpu_popv(); break; /* FISTP m64 */
                default: cpu32_fatal("x87 DF m", op, t_cpu32.eip); break;
                }
                break;
            }
        }
        break; }
    default:
        cpu32_fatal("opcode not implemented (step2)", op, t_cpu32.eip - 1);
        break;
    }
    g_cpu32_steps++;
    if (g_cpu32_exit_req) return -1;
    return 0;
}
