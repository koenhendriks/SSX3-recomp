/*
 * MMIO trap for the POSIX host: the counterpart of the Windows VEH path.
 *
 * Device registers (APU, OHCI) are unmapped guest pages. A guest load/store
 * faults; the SIGSEGV handler decodes the *host* instruction, runs the device
 * model, writes the result back and steps over it. Unknown encodings are
 * declined (never guessed at) so the crash reporter names the opcode.
 *
 * aarch64: the GPR load/store forms a compiler emits for a single
 * volatile-ish access. LDP/STP, SIMD, exclusives are not.
 * x86-64: moves, zero/sign extension, the ALU/test/compare forms with a
 * register or an immediate, xchg, inc/dec and SSE movd/movq. String
 * instructions and anything else are not. The access address is the fault
 * address, so only the instruction's length is decoded from ModRM, not its
 * operand address.
 */
#if !defined(_WIN32)
#define _GNU_SOURCE
#include <signal.h>
#include "uc_regs.h"
#include <stdint.h>
#include <stdio.h>

#include "mmio_posix.h"

#if defined(__aarch64__)

typedef struct {
    int      is_write;
    int      size;       /* bytes */
    int      rt;         /* 0..31 (31 = zero register) */
    int      sign_ext;   /* 0 none, 32 -> W destination, 64 -> X destination */
    int      rn;         /* base register, for writeback */
    int      wb_mode;    /* 0 none, 1 pre-index, 2 post-index */
    int64_t  wb_imm;
} a64_access;

static int a64_decode(uint32_t insn, a64_access *a)
{
    uint32_t size = insn >> 30, opc = (insn >> 22) & 3;

    /* Load/store register: bits 29:27 = 111, V (26) = 0. */
    if (((insn >> 27) & 7) != 7 || ((insn >> 26) & 1))
        return 0;

    switch ((insn >> 24) & 3) {
    case 1:                                  /* unsigned offset */
        break;
    case 0:
        if ((insn >> 21) & 1) {
            if (((insn >> 10) & 3) != 2)     /* register offset only */
                return 0;
        } else {
            switch ((insn >> 10) & 3) {      /* imm9 forms */
            case 0: break;                   /* LDUR/STUR */
            case 1: a->wb_mode = 2; break;   /* post-index */
            case 3: a->wb_mode = 1; break;   /* pre-index */
            default: return 0;               /* unprivileged */
            }
            a->wb_imm = ((int64_t)((insn >> 12) & 0x1FF) << 55) >> 55;
        }
        break;
    default:
        return 0;
    }

    a->size = 1 << size;
    a->rt = insn & 31;
    a->rn = (insn >> 5) & 31;
    if (a->rn == 31)
        return 0;                            /* SP-based: not device access */

    switch (opc) {
    case 0: a->is_write = 1; break;
    case 1: a->is_write = 0; break;
    case 2:                                  /* LDRSx -> X */
        if (size == 3) return 0;             /* PRFM */
        a->sign_ext = 64; break;
    case 3:                                  /* LDRSx -> W */
        if (size >= 2) return 0;
        a->sign_ext = 32; break;
    }
    return 1;
}

int mmio_posix_step(void *uc_, uintptr_t fault, mmio_read_fn rd, mmio_write_fn wr,
                    uint32_t *insn_out)
{
    ucontext_t *uc = (ucontext_t *)uc_;
    uint32_t insn = *(const uint32_t *)(uintptr_t)UC_PC(uc);
    a64_access a = {0};

    if (insn_out)
        *insn_out = insn;
    if (!a64_decode(insn, &a))
        return 0;

    if (a.is_write) {
        uint64_t v = a.rt == 31 ? 0 : UC_XREG(uc, a.rt);
        if (a.size < 8)
            v &= (1ULL << (a.size * 8)) - 1;
        wr(fault, v, a.size);
    } else {
        uint64_t v = rd(fault, a.size);
        if (a.size < 8)
            v &= (1ULL << (a.size * 8)) - 1;
        if (a.sign_ext) {
            int sh = 64 - a.size * 8;
            v = (uint64_t)(((int64_t)(v << sh)) >> sh);
            if (a.sign_ext == 32)
                v &= 0xFFFFFFFFULL;
        }
        if (a.rt != 31)
            UC_XREG(uc, a.rt) = v;
    }

    if (a.wb_mode == 1)
        UC_XREG(uc, a.rn) = fault;
    else if (a.wb_mode == 2)
        UC_XREG(uc, a.rn) = fault + a.wb_imm;

    UC_PC(uc) += 4;
    return 1;
}

#elif defined(__x86_64__)

#include <string.h>

/* Register numbers as ModRM/REX encode them, to ucontext gregs slots. */
static const int x64_greg[16] = {
    REG_RAX, REG_RCX, REG_RDX, REG_RBX, REG_RSP, REG_RBP, REG_RSI, REG_RDI,
    REG_R8,  REG_R9,  REG_R10, REG_R11, REG_R12, REG_R13, REG_R14, REG_R15,
};

#define X64_CF 0x0001u
#define X64_PF 0x0004u
#define X64_ZF 0x0040u
#define X64_SF 0x0080u
#define X64_OF 0x0800u

typedef struct {
    ucontext_t *uc;
    int rex;                                 /* 0 when there is no REX prefix */
} x64_ctx;

static uint64_t x64_mask(int size)
{
    return size >= 8 ? ~0ULL : (1ULL << (size * 8)) - 1;
}

/* Without REX, byte registers 4-7 are AH, CH, DH, BH. */
static uint64_t x64_get(const x64_ctx *c, int reg, int size)
{
    greg_t *g = c->uc->uc_mcontext.gregs;

    if (size == 1 && !c->rex && reg >= 4 && reg < 8)
        return ((uint64_t)g[x64_greg[reg - 4]] >> 8) & 0xFF;
    return (uint64_t)g[x64_greg[reg]] & x64_mask(size);
}

/* x86-64 write rules: 32-bit results clear the high half, 8- and 16-bit
 * results merge into the register. */
static void x64_set(const x64_ctx *c, int reg, int size, uint64_t v)
{
    greg_t *g = c->uc->uc_mcontext.gregs;
    uint64_t old;

    if (size == 1 && !c->rex && reg >= 4 && reg < 8) {
        old = (uint64_t)g[x64_greg[reg - 4]];
        g[x64_greg[reg - 4]] = (greg_t)((old & ~0xFF00ULL) | ((v & 0xFF) << 8));
        return;
    }
    old = (uint64_t)g[x64_greg[reg]];
    if (size == 4)
        g[x64_greg[reg]] = (greg_t)(v & 0xFFFFFFFFULL);
    else if (size == 8)
        g[x64_greg[reg]] = (greg_t)v;
    else
        g[x64_greg[reg]] = (greg_t)((old & ~x64_mask(size)) | (v & x64_mask(size)));
}

static int x64_modrm_len(const uint8_t *p)
{
    int mod = p[0] >> 6, rm = p[0] & 7, len = 1;

    if (mod == 3)
        return 0;                            /* register operand: not an access */
    if (rm == 4) {
        len++;
        if (mod == 0 && (p[1] & 7) == 5)
            len += 4;                        /* SIB, no base: disp32 */
    }
    if (mod == 0 && rm == 5)
        len += 4;                            /* RIP-relative disp32 */
    else if (mod == 1)
        len += 1;
    else if (mod == 2)
        len += 4;
    return len;
}

/* op: 0 add, 1 or, 4 and, 5 sub, 6 xor, 7 cmp (the 0x80 group's /r). Sets
 * CF, PF, ZF, SF, OF as the CPU does; returns the result. */
static uint64_t x64_alu(const x64_ctx *c, int op, uint64_t a, uint64_t b, int size)
{
    greg_t *g = c->uc->uc_mcontext.gregs;
    uint64_t m = x64_mask(size), sign = 1ULL << (size * 8 - 1), r;
    uint64_t f = (uint64_t)g[REG_EFL] & ~(uint64_t)(X64_CF | X64_PF | X64_ZF | X64_SF | X64_OF);

    a &= m;
    b &= m;
    switch (op) {
    case 0:
        r = (a + b) & m;
        if (r < a) f |= X64_CF;
        if (~(a ^ b) & (a ^ r) & sign) f |= X64_OF;
        break;
    case 5: case 7:
        r = (a - b) & m;
        if (a < b) f |= X64_CF;
        if ((a ^ b) & (a ^ r) & sign) f |= X64_OF;
        break;
    case 1: r = a | b; break;
    case 4: r = a & b; break;
    case 6: r = a ^ b; break;
    default: return 0;
    }
    if (!r) f |= X64_ZF;
    if (r & sign) f |= X64_SF;
    if (!(__builtin_popcount((unsigned)(r & 0xFF)) & 1)) f |= X64_PF;
    g[REG_EFL] = (greg_t)f;
    return r;
}

/* Reads a device register, or the page itself when rd is NULL (the AC'97
 * write trap opens the page and only needs the writes seen). */
static uint64_t x64_rd(mmio_read_fn rd, uintptr_t fault, int size)
{
    uint64_t v = 0;

    if (rd)
        return rd(fault, size) & x64_mask(size);
    memcpy(&v, (const void *)fault, (size_t)size);
    return v;
}

int mmio_posix_step(void *uc_, uintptr_t fault, mmio_read_fn rd, mmio_write_fn wr,
                    uint32_t *insn_out)
{
    ucontext_t *uc = (ucontext_t *)uc_;
    const uint8_t *ip = (const uint8_t *)uc->uc_mcontext.gregs[REG_RIP];
    x64_ctx c = { uc, 0 };
    int pfx = 0, has66 = 0, hasF2 = 0, hasF3 = 0;
    int size, reg, mlen, sub;
    const uint8_t *op;
    uint64_t v;

    if (insn_out)
        memcpy(insn_out, ip, 4);
    for (;; pfx++) {
        uint8_t b = ip[pfx];
        if (b == 0x66) has66 = 1;
        else if (b == 0xF2) hasF2 = 1;
        else if (b == 0xF3) hasF3 = 1;
        else if (b == 0xF0 || b == 0x2E || b == 0x3E || b == 0x26 || b == 0x36) ;
        else break;
        if (pfx > 4)
            return 0;
    }
    if ((ip[pfx] & 0xF0) == 0x40)
        c.rex = ip[pfx++];
    op = ip + pfx;
    size = (c.rex & 8) ? 8 : has66 ? 2 : 4;

#define REGF(m)  ((((m) >> 3) & 7) | ((c.rex & 4) ? 8 : 0))
#define MLEN(p)  do { mlen = x64_modrm_len(p); if (!mlen) return 0; } while (0)
#define DONE(n)  do { uc->uc_mcontext.gregs[REG_RIP] += pfx + (n); return 1; } while (0)

    if (hasF2 || (hasF3 && op[0] != 0x0F))
        return 0;                            /* string ops and the like */

    switch (op[0]) {
    case 0x88: case 0x89:                    /* mov r/m, r */
        if (op[0] == 0x88) size = 1;
        MLEN(op + 1);
        wr(fault, x64_get(&c, REGF(op[1]), size), size);
        DONE(1 + mlen);

    case 0x8A: case 0x8B:                    /* mov r, r/m */
        if (op[0] == 0x8A) size = 1;
        MLEN(op + 1);
        x64_set(&c, REGF(op[1]), size, x64_rd(rd, fault, size));
        DONE(1 + mlen);

    case 0xC6: case 0xC7: {                  /* mov r/m, imm */
        int isz;
        if (op[0] == 0xC6) size = 1;
        if ((op[1] >> 3) & 7)
            return 0;
        MLEN(op + 1);
        isz = size == 8 ? 4 : size;
        v = 0;
        memcpy(&v, op + 1 + mlen, (size_t)isz);
        if (size == 8)
            v = (uint64_t)(int64_t)(int32_t)v;
        wr(fault, v, size);
        DONE(1 + mlen + isz);
    }

    case 0x63:                               /* movsxd r64, r/m32 */
        MLEN(op + 1);
        v = x64_rd(rd, fault, 4);
        x64_set(&c, REGF(op[1]), size, (uint64_t)(int64_t)(int32_t)v);
        DONE(1 + mlen);

    case 0x86: case 0x87: {                  /* xchg r/m, r */
        uint64_t old;
        if (op[0] == 0x86) size = 1;
        MLEN(op + 1);
        reg = REGF(op[1]);
        old = x64_rd(rd, fault, size);
        wr(fault, x64_get(&c, reg, size), size);
        x64_set(&c, reg, size, old);
        DONE(1 + mlen);
    }

    case 0x84: case 0x85:                    /* test r/m, r */
        if (op[0] == 0x84) size = 1;
        MLEN(op + 1);
        x64_alu(&c, 4, x64_rd(rd, fault, size), x64_get(&c, REGF(op[1]), size), size);
        DONE(1 + mlen);

    case 0xF6: case 0xF7: {                  /* test r/m, imm */
        int isz;
        if (op[0] == 0xF6) size = 1;
        if ((op[1] >> 3) & 7)
            return 0;
        MLEN(op + 1);
        isz = size == 8 ? 4 : size;
        v = 0;
        memcpy(&v, op + 1 + mlen, (size_t)isz);
        if (size == 8)
            v = (uint64_t)(int64_t)(int32_t)v;
        x64_alu(&c, 4, x64_rd(rd, fault, size), v, size);
        DONE(1 + mlen + isz);
    }

    case 0x80: case 0x81: case 0x83: {      /* alu r/m, imm */
        int isz = op[0] == 0x81 ? (size == 2 ? 2 : 4) : 1;
        uint64_t r;
        if (op[0] == 0x80) size = 1;
        sub = (op[1] >> 3) & 7;
        if (sub == 2 || sub == 3)
            return 0;                        /* adc / sbb */
        MLEN(op + 1);
        v = 0;
        memcpy(&v, op + 1 + mlen, (size_t)isz);
        v = isz == 1 ? (uint64_t)(int64_t)(int8_t)v
          : isz == 2 ? (uint64_t)(int64_t)(int16_t)v
          : (uint64_t)(int64_t)(int32_t)v;
        r = x64_alu(&c, sub, x64_rd(rd, fault, size), v, size);
        if (sub != 7)
            wr(fault, r, size);
        DONE(1 + mlen + isz);
    }

    case 0x00: case 0x01: case 0x08: case 0x09: case 0x20: case 0x21:
    case 0x28: case 0x29: case 0x30: case 0x31: case 0x38: case 0x39: {
        uint64_t r;                          /* alu r/m, r */
        if (!(op[0] & 1)) size = 1;
        sub = op[0] >> 3;
        MLEN(op + 1);
        r = x64_alu(&c, sub, x64_rd(rd, fault, size), x64_get(&c, REGF(op[1]), size), size);
        if (sub != 7)
            wr(fault, r, size);
        DONE(1 + mlen);
    }

    case 0x02: case 0x03: case 0x0A: case 0x0B: case 0x22: case 0x23:
    case 0x2A: case 0x2B: case 0x32: case 0x33: case 0x3A: case 0x3B: {
        uint64_t r;                          /* alu r, r/m */
        if (!(op[0] & 1)) size = 1;
        sub = op[0] >> 3;
        MLEN(op + 1);
        reg = REGF(op[1]);
        r = x64_alu(&c, sub, x64_get(&c, reg, size), x64_rd(rd, fault, size), size);
        if (sub != 7)
            x64_set(&c, reg, size, r);
        DONE(1 + mlen);
    }

    case 0xFE: case 0xFF: {                  /* inc / dec r/m: CF kept */
        greg_t cf;
        uint64_t r;
        if (op[0] == 0xFE) size = 1;
        sub = (op[1] >> 3) & 7;
        if (sub > 1)
            return 0;
        MLEN(op + 1);
        cf = uc->uc_mcontext.gregs[REG_EFL] & X64_CF;
        r = x64_alu(&c, sub ? 5 : 0, x64_rd(rd, fault, size), 1, size);
        uc->uc_mcontext.gregs[REG_EFL] = (uc->uc_mcontext.gregs[REG_EFL] & ~(greg_t)X64_CF) | cf;
        wr(fault, r, size);
        DONE(1 + mlen);
    }

    case 0x0F:
        switch (op[1]) {
        case 0xB6: case 0xB7: case 0xBE: case 0xBF: {   /* movzx / movsx */
            int s = (op[1] & 1) ? 2 : 1;
            MLEN(op + 2);
            v = x64_rd(rd, fault, s);
            if (op[1] >= 0xBE)
                v = s == 1 ? (uint64_t)(int64_t)(int8_t)v : (uint64_t)(int64_t)(int16_t)v;
            x64_set(&c, REGF(op[2]), size, v);
            DONE(2 + mlen);
        }
        case 0x6E: case 0x7E: case 0xD6: {   /* movd / movq with an xmm register */
            int x = REGF(op[2]), load, s;
            uint8_t *xmm = (uint8_t *)&uc->uc_mcontext.fpregs->_xmm[x];
            if (has66 && op[1] == 0x6E)      { load = 1; s = (c.rex & 8) ? 8 : 4; }
            else if (has66 && op[1] == 0x7E) { load = 0; s = (c.rex & 8) ? 8 : 4; }
            else if (hasF3 && op[1] == 0x7E) { load = 1; s = 8; }
            else if (has66 && op[1] == 0xD6) { load = 0; s = 8; }
            else return 0;
            MLEN(op + 2);
            if (load) {
                v = x64_rd(rd, fault, s);
                memset(xmm, 0, 16);
                memcpy(xmm, &v, (size_t)s);
            } else {
                v = 0;
                memcpy(&v, xmm, (size_t)s);
                wr(fault, v, s);
            }
            DONE(2 + mlen);
        }
        default:
            return 0;
        }

    default:
        return 0;
    }
#undef REGF
#undef MLEN
#undef DONE
}

#else

int mmio_posix_step(void *uc, uintptr_t fault, mmio_read_fn rd, mmio_write_fn wr,
                    uint32_t *insn_out)
{
    (void)uc; (void)fault; (void)rd; (void)wr; (void)insn_out;
    return 0;
}

#endif
#endif /* !_WIN32 */
