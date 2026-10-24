
/* one instruction step. returns 0 = ok, -1 = stop (sentinel/exit). */
static int cpu32_step(void) {
    uint32_t op;
    int o32 = 1;            /* operand size 32 (0x66 -> 16) */
    int arep = 0, frep = 0; /* REPNE / REP(RE) */
    g_segreg_ovr = -1;

restart_prefix:
    op = m32_ld8(t_cpu32.eip);
    switch (op) {
    case 0x66: t_cpu32.eip++; o32 = 0; goto restart_prefix;
    case 0x67: t_cpu32.eip++; goto restart_prefix; /* 32-bit default already */
    case 0x64: t_cpu32.eip++; g_segreg_ovr = 0; goto restart_prefix; /* FS: */
    case 0x65: t_cpu32.eip++; g_segreg_ovr = 1; goto restart_prefix; /* GS: */
    case 0x2E: case 0x36: case 0x3E: case 0x26: t_cpu32.eip++; goto restart_prefix; /* flat */
    case 0xF0: t_cpu32.eip++; goto restart_prefix;  /* LOCK: ignore */
    case 0xF2: t_cpu32.eip++; arep = 1; goto restart_prefix;
    case 0xF3: t_cpu32.eip++; frep = 1; goto restart_prefix;
    default: break;
    }
    t_cpu32.eip++; /* consume opcode */

    /* API gate */
    if ((t_cpu32.eip - 1) == MW32_GATE_VA) {
        mw32_api_gate(t_cpu32.r[EAXi]);
        return g_cpu32_exit_req ? -1 : 0;
    }
    /* sentinel return -> callback frame complete */
    if ((t_cpu32.eip - 1) == MW32_RET_SENTINEL) return -1;

    ModRM m; int have_m = 0;
#define NEEDM() do { if (!have_m) { modrm_decode(&m); modrm_apply_seg(&m); have_m = 1; } } while (0)
#define R32(i) (t_cpu32.r[i])
#define LD_RorM32() (m.is_reg ? R32(m.rm) : (modrm_apply_seg(&m), m32_ld32(m.ea)))
#define ST_RorM32(v) do { if (m.is_reg) R32(m.rm) = (v); else m32_st32(m.ea, (v)); } while (0)
#define LD_RorM16() (m.is_reg ? (uint16_t)(R32(m.rm) & 0xFFFF) : (modrm_apply_seg(&m), m32_ld16(m.ea)))
#define ST_RorM16(v) do { if (m.is_reg) R32(m.rm) = ((R32(m.rm) & 0xFFFF0000u) | (uint32_t)(uint16_t)(v)); else m32_st16(m.ea, (v)); } while (0)
#define LD_RorM8() (m.is_reg ? (uint8_t)(R32(m.rm & 7) & 0xFF) : (modrm_apply_seg(&m), m32_ld8(m.ea)))
/* 8-bit register mapping with the 0x04/0x05 AH/CH/DH/BH quirk handled by
 * callers that need it; interpreter stores BH in EBX bits 8..15 as real. */
#define ST_RorM8(v) do { if (m.is_reg) { \
        if (m.rm < 4) R32(m.rm) = (R32(m.rm) & 0xFFFFFF00u) | (uint32_t)(v); \
        else R32(m.rm - 4) = (R32(m.rm - 4) & 0xFFFF00FFu) | ((uint32_t)(v) << 8); \
    } else m32_st8(m.ea, (v)); } while (0)

    switch (op) {
    /* ---- add / or / adc / sbb / and / sub / xor / cmp, r/m,r and r,r/m and imm ---- */
    case 0x01: { NEEDM(); uint32_t v = LD_RorM32(); uint32_t r2 = alu_add32(v, R32(m.reg), 0); ST_RorM32(r2); break; }
    case 0x03: { NEEDM(); R32(m.reg) = alu_add32(R32(m.reg), LD_RorM32(), 0); break; }
    case 0x00: { NEEDM(); uint8_t v = LD_RorM8(); uint8_t r2 = alu_add8(v, (uint8_t)R32(m.reg & 7) & 0xFF, 0); ST_RorM8(r2); break; }
    case 0x02: { NEEDM(); uint8_t v = LD_RorM8(); uint8_t regv = (m.reg < 4) ? (uint8_t)(R32(m.reg) & 0xFF) : (uint8_t)(R32(m.reg - 4) >> 8); uint8_t r2 = alu_add8(regv, v, 0); if (m.reg < 4) R32(m.reg) = (R32(m.reg) & 0xFFFFFF00u) | r2; else R32(m.reg - 4) = (R32(m.reg - 4) & 0xFFFF00FFu) | ((uint32_t)r2 << 8); break; }
    case 0x05: R32(EAXi) = alu_add32(R32(EAXi), m32_ld32(t_cpu32.eip), 0); t_cpu32.eip += 4; break;
    case 0x04: { uint8_t v = m32_ld8(t_cpu32.eip); t_cpu32.eip++; uint8_t nv = alu_add8((uint8_t)R32(EAXi), v, 0); R32(EAXi) = (R32(EAXi) & 0xFFFFFF00u) | nv; break; }
    case 0x09: { NEEDM(); uint32_t v = LD_RorM32(); ST_RorM32(alu_logic32(v | R32(m.reg))); break; }
    case 0x0B: { NEEDM(); R32(m.reg) = alu_logic32(R32(m.reg) | LD_RorM32()); break; }
    case 0x08: { NEEDM(); uint8_t v = LD_RorM8(); ST_RorM8(alu_logic8(v | (uint8_t)R32(m.reg & 7))); break; }
    case 0x0A: { NEEDM(); uint8_t v = LD_RorM8(); uint8_t regv = (m.reg < 4) ? (uint8_t)R32(m.reg) : (uint8_t)(R32(m.reg - 4) >> 8); uint8_t r2 = alu_logic8(regv | v); if (m.reg < 4) R32(m.reg) = (R32(m.reg) & 0xFFFFFF00u) | r2; else R32(m.reg - 4) = (R32(m.reg - 4) & 0xFFFF00FFu) | ((uint32_t)r2 << 8); break; }
    case 0x0D: R32(EAXi) = alu_logic32(R32(EAXi) | m32_ld32(t_cpu32.eip)); t_cpu32.eip += 4; break;
    case 0x11: { NEEDM(); uint32_t v = LD_RorM32(); ST_RorM32(alu_add32(v, R32(m.reg), f_get(F_CF))); break; }
    case 0x13: { NEEDM(); R32(m.reg) = alu_add32(R32(m.reg), LD_RorM32(), f_get(F_CF)); break; }
    case 0x15: R32(EAXi) = alu_add32(R32(EAXi), m32_ld32(t_cpu32.eip), f_get(F_CF)); t_cpu32.eip += 4; break;
    case 0x19: { NEEDM(); uint32_t v = LD_RorM32(); ST_RorM32(alu_sub32(v, R32(m.reg), f_get(F_CF))); break; }
    case 0x1B: { NEEDM(); R32(m.reg) = alu_sub32(R32(m.reg), LD_RorM32(), f_get(F_CF)); break; }
    case 0x1D: R32(EAXi) = alu_sub32(R32(EAXi), m32_ld32(t_cpu32.eip), f_get(F_CF)); t_cpu32.eip += 4; break;
    case 0x21: { NEEDM(); uint32_t v = LD_RorM32(); ST_RorM32(alu_logic32(v & R32(m.reg))); break; }
    case 0x23: { NEEDM(); R32(m.reg) = alu_logic32(R32(m.reg) & LD_RorM32()); break; }
    case 0x20: { NEEDM(); uint8_t v = LD_RorM8(); ST_RorM8(alu_logic8(v & (uint8_t)R32(m.reg & 7))); break; }
    case 0x22: { NEEDM(); uint8_t v = LD_RorM8(); uint8_t regv = (m.reg < 4) ? (uint8_t)R32(m.reg) : (uint8_t)(R32(m.reg - 4) >> 8); uint8_t r2 = alu_logic8(regv & v); if (m.reg < 4) R32(m.reg) = (R32(m.reg) & 0xFFFFFF00u) | r2; else R32(m.reg - 4) = (R32(m.reg - 4) & 0xFFFF00FFu) | ((uint32_t)r2 << 8); break; }
    case 0x25: R32(EAXi) = alu_logic32(R32(EAXi) & m32_ld32(t_cpu32.eip)); t_cpu32.eip += 4; break;
    case 0x29: { NEEDM(); uint32_t v = LD_RorM32(); ST_RorM32(alu_sub32(v, R32(m.reg), 0)); break; }
    case 0x2B: { NEEDM(); R32(m.reg) = alu_sub32(R32(m.reg), LD_RorM32(), 0); break; }
    case 0x28: { NEEDM(); uint8_t v = LD_RorM8(); ST_RorM8(alu_sub8(v, (uint8_t)R32(m.reg & 7), 0)); break; }
    case 0x2A: { NEEDM(); uint8_t v = LD_RorM8(); uint8_t regv = (m.reg < 4) ? (uint8_t)R32(m.reg) : (uint8_t)(R32(m.reg - 4) >> 8); uint8_t r2 = alu_sub8(regv, v, 0); if (m.reg < 4) R32(m.reg) = (R32(m.reg) & 0xFFFFFF00u) | r2; else R32(m.reg - 4) = (R32(m.reg - 4) & 0xFFFF00FFu) | ((uint32_t)r2 << 8); break; }
    case 0x2D: R32(EAXi) = alu_sub32(R32(EAXi), m32_ld32(t_cpu32.eip), 0); t_cpu32.eip += 4; break;
    case 0x31: { NEEDM(); uint32_t v = LD_RorM32(); ST_RorM32(alu_logic32(v ^ R32(m.reg))); break; }
    case 0x33: { NEEDM(); R32(m.reg) = alu_logic32(R32(m.reg) ^ LD_RorM32()); break; }
    case 0x30: { NEEDM(); uint8_t v = LD_RorM8(); ST_RorM8(alu_logic8(v ^ (uint8_t)R32(m.reg & 7))); break; }
    case 0x32: { NEEDM(); uint8_t v = LD_RorM8(); uint8_t regv = (m.reg < 4) ? (uint8_t)R32(m.reg) : (uint8_t)(R32(m.reg - 4) >> 8); uint8_t r2 = alu_logic8(regv ^ v); if (m.reg < 4) R32(m.reg) = (R32(m.reg) & 0xFFFFFF00u) | r2; else R32(m.reg - 4) = (R32(m.reg - 4) & 0xFFFF00FFu) | ((uint32_t)r2 << 8); break; }
    case 0x35: R32(EAXi) = alu_logic32(R32(EAXi) ^ m32_ld32(t_cpu32.eip)); t_cpu32.eip += 4; break;
    case 0x39: { NEEDM(); uint32_t v = LD_RorM32(); alu_sub32(v, R32(m.reg), 0); break; }
    case 0x3B: { NEEDM(); alu_sub32(R32(m.reg), LD_RorM32(), 0); break; }
    case 0x38: { NEEDM(); uint8_t v = LD_RorM8(); alu_sub8(v, (uint8_t)R32(m.reg & 7), 0); break; }
    case 0x3A: { NEEDM(); uint8_t v = LD_RorM8(); uint8_t regv = (m.reg < 4) ? (uint8_t)R32(m.reg) : (uint8_t)(R32(m.reg - 4) >> 8); alu_sub8(regv, v, 0); break; }
    case 0x3D: alu_sub32(R32(EAXi), m32_ld32(t_cpu32.eip), 0); t_cpu32.eip += 4; break;
    case 0x83: { /* grp1 imm8 sign-extended */
        NEEDM(); uint8_t sub = m32_ld8(t_cpu32.eip++);
        int32_t imm = (int8_t)m32_ld8(t_cpu32.eip); t_cpu32.eip++;
        uint32_t v = LD_RorM32(), r2 = v;
        switch (sub & 7) {
        case 0: r2 = alu_add32(v, (uint32_t)imm, 0); break;
        case 1: r2 = alu_logic32(v | (uint32_t)imm); break;
        case 2: r2 = alu_add32(v, (uint32_t)imm, f_get(F_CF)); break;
        case 3: r2 = alu_sub32(v, (uint32_t)imm, f_get(F_CF)); break;
        case 4: r2 = alu_logic32(v & (uint32_t)imm); break;
        case 5: r2 = alu_sub32(v, (uint32_t)imm, 0); break;
        case 6: r2 = alu_logic32(v ^ (uint32_t)imm); break;
        case 7: alu_sub32(v, (uint32_t)imm, 0); r2 = v; break;
        }
        if ((sub & 7) != 7) ST_RorM32(r2);
        break; }
    case 0x81: { /* grp1 imm32 */
        NEEDM(); uint8_t sub = m32_ld8(t_cpu32.eip++);
        uint32_t imm = m32_ld32(t_cpu32.eip); t_cpu32.eip += 4;
        uint32_t v = LD_RorM32(), r2 = v;
        switch (sub & 7) {
        case 0: r2 = alu_add32(v, imm, 0); break;
        case 1: r2 = alu_logic32(v | imm); break;
        case 2: r2 = alu_add32(v, imm, f_get(F_CF)); break;
        case 3: r2 = alu_sub32(v, imm, f_get(F_CF)); break;
        case 4: r2 = alu_logic32(v & imm); break;
        case 5: r2 = alu_sub32(v, imm, 0); break;
        case 6: r2 = alu_logic32(v ^ imm); break;
        case 7: alu_sub32(v, imm, 0); r2 = v; break;
        }
        if ((sub & 7) != 7) ST_RorM32(r2);
        break; }
    case 0x80: { /* grp1 imm8 (byte) */
        NEEDM(); uint8_t sub = m32_ld8(t_cpu32.eip++);
        uint8_t imm = m32_ld8(t_cpu32.eip); t_cpu32.eip++;
        uint8_t v = LD_RorM8(), r2 = v;
        switch (sub & 7) {
        case 0: r2 = alu_add8(v, imm, 0); break;
        case 1: r2 = alu_logic8(v | imm); break;
        case 2: r2 = alu_add8(v, imm, f_get(F_CF)); break;
        case 3: r2 = alu_sub8(v, imm, f_get(F_CF)); break;
        case 4: r2 = alu_logic8(v & imm); break;
        case 5: r2 = alu_sub8(v, imm, 0); break;
        case 6: r2 = alu_logic8(v ^ imm); break;
        case 7: alu_sub8(v, imm, 0); r2 = v; break;
        }
        if ((sub & 7) != 7) ST_RorM8(r2);
        break; }
    /* ---- inc/dec r/m and reg ---- */
    case 0xFF: {
        NEEDM(); uint8_t sub = m.reg;
        switch (sub) {
        case 0: { uint32_t v = LD_RorM32(); ST_RorM32(alu_inc32(v)); break; }
        case 1: { uint32_t v = LD_RorM32(); ST_RorM32(alu_dec32(v)); break; }
        case 2: { uint32_t t = LD_RorM32(); m32_push(t_cpu32.eip); t_cpu32.eip = t; break; } /* CALL r/m */
        case 3: { /* CALL FAR m16:32 — v1 loud abort */
            cpu32_fatal("CALL FAR m16:32 unsupported", op, t_cpu32.eip); break; }
        case 4: { uint32_t t = LD_RorM32(); t_cpu32.eip = t; break; } /* JMP r/m */
        case 5: cpu32_fatal("JMP FAR m16:32 unsupported", op, t_cpu32.eip); break;
        case 6: { uint32_t v = LD_RorM32(); m32_push(v); break; } /* PUSH r/m */
        case 7: cpu32_fatal("grp5 /7 undefined", op, t_cpu32.eip); break;
        }
        break; }
    case 0xFE: {
        NEEDM(); uint8_t sub = m.reg;
        if (sub == 0) { uint8_t v = LD_RorM8(); ST_RorM8(alu_add8(v, 1, 0)); }
        else if (sub == 1) { uint8_t v = LD_RorM8(); ST_RorM8(alu_sub8(v, 1, 0)); }
        else cpu32_fatal("grp4 /r unsupported", op, t_cpu32.eip);
        break; }
    case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47:
        R32(op & 7) = alu_inc32(R32(op & 7)); break;
    case 0x48: case 0x49: case 0x4A: case 0x4B: case 0x4C: case 0x4D: case 0x4E: case 0x4F:
        R32(op & 7) = alu_dec32(R32(op & 7)); break;
    /* ---- push/pop ---- */
    case 0x50: case 0x51: case 0x52: case 0x53: case 0x55: case 0x56: case 0x57:
        m32_push(R32(op & 7)); break;
    case 0x54: m32_push(R32(ESPi)); break; /* push esp pushes pre-increment value */
    case 0x58: case 0x59: case 0x5A: case 0x5B: case 0x5C: case 0x5D: case 0x5E: case 0x5F:
        R32(op & 7) = m32_pop(); break;
    case 0x60: { uint32_t orig_esp = R32(ESPi);
        for (int i = 0; i < 8; i++) m32_push(i == ESPi ? orig_esp : R32(i)); break; } /* pushad */
    case 0x61: { for (int i = 7; i >= 0; i--) R32(i) = m32_pop(); break; } /* popad */
    case 0x9C: m32_push(t_cpu32.eflags); break; /* pushfd */
    case 0x9D: t_cpu32.eflags = m32_pop() | 0x2; break; /* popfd */
    case 0x68: m32_push(m32_ld32(t_cpu32.eip)); t_cpu32.eip += 4; break;
    case 0x6A: m32_push((uint32_t)(int32_t)(int8_t)m32_ld8(t_cpu32.eip)); t_cpu32.eip++; break;
    case 0x8F: { NEEDM(); uint32_t v = m32_pop(); ST_RorM32(v); break; } /* pop r/m (ea computed BEFORE pop in real ISA: handled by decode-then-pop here — decode first) */
    /* ---- mov family ---- */
    case 0x89: { NEEDM(); ST_RorM32(R32(m.reg)); break; }
    case 0x8B: { NEEDM(); R32(m.reg) = LD_RorM32(); break; }
    case 0x88: { NEEDM(); uint8_t v = (m.reg < 4) ? (uint8_t)R32(m.reg) : (uint8_t)(R32(m.reg - 4) >> 8); ST_RorM8(v); break; }
    case 0x8A: { NEEDM(); uint8_t v = LD_RorM8(); if (m.reg < 4) R32(m.reg) = (R32(m.reg) & 0xFFFFFF00u) | v; else R32(m.reg - 4) = (R32(m.reg - 4) & 0xFFFF00FFu) | ((uint32_t)v << 8); break; }
    case 0xA1: { uint32_t off = m32_ld32(t_cpu32.eip); t_cpu32.eip += 4; if (g_segreg_ovr == 0) off += t_cpu32.fsbase; R32(EAXi) = m32_ld32(off); break; }
    case 0xA3: { uint32_t off = m32_ld32(t_cpu32.eip); t_cpu32.eip += 4; if (g_segreg_ovr == 0) off += t_cpu32.fsbase; m32_st32(off, R32(EAXi)); break; }
    case 0xA0: { uint32_t off = m32_ld32(t_cpu32.eip); t_cpu32.eip += 4; if (g_segreg_ovr == 0) off += t_cpu32.fsbase; uint8_t v = m32_ld8(off); R32(EAXi) = (R32(EAXi) & 0xFFFFFF00u) | v; break; }
    case 0xA2: { uint32_t off = m32_ld32(t_cpu32.eip); t_cpu32.eip += 4; if (g_segreg_ovr == 0) off += t_cpu32.fsbase; m32_st8(off, (uint8_t)R32(EAXi)); break; }
    case 0xB0: case 0xB1: case 0xB2: case 0xB3: case 0xB4: case 0xB5: case 0xB6: case 0xB7: {
        uint8_t v = m32_ld8(t_cpu32.eip); t_cpu32.eip++;
        int r8 = op & 7;
        if (r8 < 4) R32(r8) = (R32(r8) & 0xFFFFFF00u) | v;
        else R32(r8 - 4) = (R32(r8 - 4) & 0xFFFF00FFu) | ((uint32_t)v << 8);
        break; }
    case 0xB8: case 0xB9: case 0xBA: case 0xBB: case 0xBC: case 0xBD: case 0xBE: case 0xBF:
        R32(op & 7) = m32_ld32(t_cpu32.eip); t_cpu32.eip += 4; break;
    case 0xC7: { NEEDM(); ST_RorM32(m32_ld32(t_cpu32.eip)); t_cpu32.eip += 4; break; }
    case 0xC6: { NEEDM(); ST_RorM8(m32_ld8(t_cpu32.eip)); t_cpu32.eip += 1; break; }
    case 0x8C: { NEEDM(); ST_RorM32(0x23); break; } /* mov r/m, Sreg: fake flat data selector */
    case 0x8E: { NEEDM(); (void)LD_RorM32(); break; } /* mov Sreg, r/m: ignore (flat) */
    /* ---- movzx / movsx ---- */
    case 0x0F: {
        uint8_t op2 = m32_ld8(t_cpu32.eip++);
        switch (op2) {
        case 0xB6: { NEEDM(); uint8_t v = LD_RorM8(); R32(m.reg) = v; break; }             /* movzx r32, rm8 */
        case 0xB7: { NEEDM(); uint16_t v = LD_RorM16(); R32(m.reg) = v; break; }           /* movzx r32, rm16 */
        case 0xBE: { NEEDM(); uint8_t v = LD_RorM8(); R32(m.reg) = (uint32_t)(int32_t)(int8_t)v; break; } /* movsx */
        case 0xBF: { NEEDM(); uint16_t v = LD_RorM16(); R32(m.reg) = (uint32_t)(int32_t)(int16_t)v; break; }
        case 0xAF: { NEEDM(); uint32_t d = LD_RorM32(); /* imul r32, r/m32 */
            int64_t p = (int64_t)(int32_t)R32(m.reg) * (int64_t)(int32_t)d;
            R32(m.reg) = (uint32_t)p;
            int ov = p != (int64_t)(int32_t)p;
            f_set(F_CF, ov); f_set(F_OF, ov); f_setzsp32(R32(m.reg)); break; }
        case 0x1F: { NEEDM(); (void)LD_RorM32(); break; } /* NOP multi-byte (hint nop) */
        case 0x05: cpu32_fatal("syscall", op2, t_cpu32.eip); break;
        case 0x0B: { fprintf(stderr, "[PE32] ud2 at 0x%x\n", t_cpu32.eip - 2); cpu32_fatal("ud2", op2, t_cpu32.eip); break; }
        case 0x80: case 0x81: case 0x82: case 0x83: case 0x84: case 0x85: case 0x86: case 0x87: {
            int32_t rel = (int32_t)m32_ld32(t_cpu32.eip); t_cpu32.eip += 4;
            if (cond_eval(op2 & 0xF)) t_cpu32.eip = (uint32_t)(int32_t)((int64_t)t_cpu32.eip + rel);
            break; }
        case 0x90: case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97:
        case 0x98: case 0x99: case 0x9A: case 0x9B: case 0x9C: case 0x9D: case 0x9E: case 0x9F: {
            NEEDM(); ST_RorM8(cond_eval(op2 & 0xF) ? 1 : 0); break; } /* setcc */
        case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47:
        case 0x48: case 0x49: case 0x4A: case 0x4B: case 0x4C: case 0x4D: case 0x4E: case 0x4F: {
            NEEDM(); if (cond_eval(op2 & 0xF)) R32(m.reg) = LD_RorM32(); break; } /* cmovcc */
        case 0xA2: cpu32_fatal("cpuid not needed on v1", op2, t_cpu32.eip); break; /* guests rarely gate on it */
        case 0xA3: { NEEDM(); uint32_t v = LD_RorM32(); int bit = (R32(m.reg) >> (v & 31)) & 1; f_set(F_CF, bit); break; } /* bt r/m, r */
        case 0xAB: { NEEDM(); uint32_t v = LD_RorM32(); int bit = (R32(m.reg) >> (v & 31)) & 1; f_set(F_CF, bit); v |= (1u << (R32(m.reg) & 31)); ST_RorM32(v); break; } /* bts */
        case 0xB3: { NEEDM(); uint32_t v = LD_RorM32(); int bit = (R32(m.reg) >> (v & 31)) & 1; f_set(F_CF, bit); v &= ~(1u << (R32(m.reg) & 31)); ST_RorM32(v); break; } /* btr */
        case 0xBB: { NEEDM(); uint32_t v = LD_RorM32(); int bit = (R32(m.reg) >> (v & 31)) & 1; f_set(F_CF, bit); v ^= (1u << (R32(m.reg) & 31)); ST_RorM32(v); break; } /* btc */
        case 0xBC: { NEEDM(); uint32_t v = LD_RorM32(); f_set(F_ZF, v == 0); if (v) { R32(m.reg) = __builtin_ctz(v); f_set(F_CF, 0); } else f_set(F_CF, 1); break; } /* bsf */
        case 0xBD: { NEEDM(); uint32_t v = LD_RorM32(); f_set(F_ZF, v == 0); if (v) { R32(m.reg) = 31 - __builtin_clz(v); f_set(F_CF, 0); } else f_set(F_CF, 1); break; } /* bsr */
        case 0xC0: { NEEDM(); uint8_t d = LD_RorM8(); uint8_t s = (m.reg < 4) ? (uint8_t)R32(m.reg) : (uint8_t)(R32(m.reg - 4) >> 8); uint8_t sum = alu_add8(d, s, 0); ST_RorM8(sum); if (m.reg < 4) R32(m.reg) = (R32(m.reg) & 0xFFFFFF00u) | d; else R32(m.reg - 4) = (R32(m.reg - 4) & 0xFFFF00FFu) | ((uint32_t)d << 8); break; } /* xadd 8 */
        case 0xC1: { NEEDM(); uint32_t d = LD_RorM32(); uint32_t s = R32(m.reg); ST_RorM32(alu_add32(d, s, 0)); R32(m.reg) = d; break; } /* xadd 32 */
        case 0xB0: { NEEDM(); uint8_t d = LD_RorM8(); uint8_t s = (m.reg < 4) ? (uint8_t)R32(m.reg) : (uint8_t)(R32(m.reg - 4) >> 8); uint8_t r2 = alu_sub8(d, s, 0); ST_RorM8(r2); break; } /* cmpxchg8 simplified (no cmp first) */
        case 0xB1: { NEEDM(); uint32_t d = LD_RorM32(); uint32_t s = R32(m.reg); ST_RorM32(alu_sub32(d, s, 0)); break; } /* cmpxchg32 simplified */
        case 0xAE: { NEEDM(); if (m.reg == 5 || m.reg == 3) { /* lfence/mfence */ break; } cpu32_fatal("0F AE /r", op2, t_cpu32.eip); break; }
        /* ---- SSE2 scalar subset (prefix-dependent) ---- */
        case 0x10: case 0x11: case 0x28: case 0x29: case 0x2A: case 0x2C: case 0x2D:
        case 0x2E: case 0x2F: case 0x51: case 0x54: case 0x55: case 0x56: case 0x57:
        case 0x58: case 0x59: case 0x5A: case 0x5C: case 0x5D: case 0x5E: case 0x5F:
        case 0x6E: case 0x7E: case 0xD6: case 0xEF: {
            uint8_t pfx = arep ? 0xF2 : (frep ? 0xF3 : (o32 ? 0x00 : 0x66));
            if (cpu32_sse2(pfx, op2) != 0) cpu32_fatal("SSE2 op failed", op2, t_cpu32.eip - 1);
            break; }
        case 0x38: case 0x3A: cpu32_fatal("SSE3+ extension op", op2, t_cpu32.eip); break;
        default: cpu32_fatal("0F two-byte op not implemented", op2, t_cpu32.eip - 1); break;
        }
        break; }
    default:
        return cpu32_step2(op, o32, arep, frep); /* SW-1281: second dispatch plane */
    }
    g_cpu32_steps++;
    if (g_cpu32_exit_req) return -1;
    return 0;
}
