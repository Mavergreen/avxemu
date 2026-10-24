/*
 * lde.c — x86-64 length decoder (see lde.h).
 *
 * Length only: prefixes -> REX/VEX -> opcode -> ModRM(+SIB+disp) -> immediate.
 * The per-opcode "has ModRM" / immediate tables cover the encodings a compiler
 * emits; the whole thing is checked against otool's instruction boundaries over
 * the entire binary, so any gap shows up as a desync and is fixed.
 */

#include "lde.h"
#include <stdlib.h>
#include <string.h>

/* imm codes: 0 none, 1 imm8, 2 imm16, 3 immZ(2/4), 4 immV(2/4/8), 5 moffs(8), 6 enter(imm16+imm8) */

static int modrm1(uint8_t op) {
    switch (op) {
    case 0x00:case 0x01:case 0x02:case 0x03: case 0x08:case 0x09:case 0x0A:case 0x0B:
    case 0x10:case 0x11:case 0x12:case 0x13: case 0x18:case 0x19:case 0x1A:case 0x1B:
    case 0x20:case 0x21:case 0x22:case 0x23: case 0x28:case 0x29:case 0x2A:case 0x2B:
    case 0x30:case 0x31:case 0x32:case 0x33: case 0x38:case 0x39:case 0x3A:case 0x3B:
    case 0x63: case 0x69:case 0x6B:
    case 0x80:case 0x81:case 0x83: case 0x84:case 0x85:case 0x86:case 0x87:
    case 0x88:case 0x89:case 0x8A:case 0x8B:case 0x8C:case 0x8D:case 0x8E:case 0x8F:
    case 0xC0:case 0xC1:case 0xC6:case 0xC7:
    case 0xD0:case 0xD1:case 0xD2:case 0xD3:
    case 0xD8:case 0xD9:case 0xDA:case 0xDB:case 0xDC:case 0xDD:case 0xDE:case 0xDF:
    case 0xF6:case 0xF7:case 0xFE:case 0xFF:
        return 1;
    default: return 0;
    }
}
static int imm1(uint8_t op) {
    switch (op) {
    case 0x04:case 0x0C:case 0x14:case 0x1C:case 0x24:case 0x2C:case 0x34:case 0x3C: return 1;
    case 0x05:case 0x0D:case 0x15:case 0x1D:case 0x25:case 0x2D:case 0x35:case 0x3D: return 3;
    case 0x68: return 3; case 0x6A: return 1; case 0x69: return 3; case 0x6B: return 1;
    case 0x70:case 0x71:case 0x72:case 0x73:case 0x74:case 0x75:case 0x76:case 0x77:
    case 0x78:case 0x79:case 0x7A:case 0x7B:case 0x7C:case 0x7D:case 0x7E:case 0x7F: return 1;
    case 0x80: return 1; case 0x81: return 3; case 0x83: return 1;
    case 0xA0:case 0xA1:case 0xA2:case 0xA3: return 5;
    case 0xA8: return 1; case 0xA9: return 3;
    case 0xB0:case 0xB1:case 0xB2:case 0xB3:case 0xB4:case 0xB5:case 0xB6:case 0xB7: return 1;
    case 0xB8:case 0xB9:case 0xBA:case 0xBB:case 0xBC:case 0xBD:case 0xBE:case 0xBF: return 4;
    case 0xC0:case 0xC1:case 0xC6: return 1; case 0xC7: return 3;
    case 0xC2:case 0xCA: return 2; case 0xC8: return 6; case 0xCD: return 1;
    case 0xE0:case 0xE1:case 0xE2:case 0xE3:case 0xE4:case 0xE5:case 0xE6:case 0xE7: return 1;
    case 0xE8:case 0xE9: return 3; case 0xEB: return 1;
    default: return 0;
    }
}
static int modrm0F(uint8_t op) {
    switch (op) {
    case 0x05:case 0x06:case 0x07:case 0x08:case 0x09:case 0x0B:case 0x0E:
    case 0x30:case 0x31:case 0x32:case 0x33:case 0x34:case 0x35:case 0x37:case 0x77:
    case 0x80:case 0x81:case 0x82:case 0x83:case 0x84:case 0x85:case 0x86:case 0x87:
    case 0x88:case 0x89:case 0x8A:case 0x8B:case 0x8C:case 0x8D:case 0x8E:case 0x8F:
    case 0xA0:case 0xA1:case 0xA2:case 0xA8:case 0xA9:case 0xAA:
    case 0xC8:case 0xC9:case 0xCA:case 0xCB:case 0xCC:case 0xCD:case 0xCE:case 0xCF:
        return 0;
    default: return 1;
    }
}
static int imm0F(uint8_t op) {
    switch (op) {
    case 0x70:case 0x71:case 0x72:case 0x73: return 1;
    case 0xA4:case 0xAC:case 0xBA: return 1;
    case 0xC2:case 0xC4:case 0xC5:case 0xC6: return 1;
    case 0x80:case 0x81:case 0x82:case 0x83:case 0x84:case 0x85:case 0x86:case 0x87:
    case 0x88:case 0x89:case 0x8A:case 0x8B:case 0x8C:case 0x8D:case 0x8E:case 0x8F: return 3;
    default: return 0;
    }
}

static const uint8_t *dmod(const uint8_t *p, const uint8_t *end) {
    if (p >= end) return 0;
    uint8_t m = *p++; int mod = m >> 6, rm = m & 7;
    if (mod == 3) return p;
    if (rm == 4) {
        if (p >= end) return 0;
        uint8_t sib = *p++;
        if (mod == 0 && (sib & 7) == 5) p += 4;
    }
    else if (mod == 0 && rm == 5) p += 4;       /* rip-relative / disp32 */
    if (mod == 1) p += 1; else if (mod == 2) p += 4;
    return p;
}
static const uint8_t *addimm(const uint8_t *p, const uint8_t *end, int code, int opsz, int rexw) {
    int n = 0;
    switch (code) {
    case 1: n = 1; break; case 2: n = 2; break;
    case 3: n = opsz ? 2 : 4; break;
    case 4: n = rexw ? 8 : (opsz ? 2 : 4); break;
    case 5: n = 8; break; case 6: n = 3; break; default: n = 0;
    }
    if (p + n > end) return 0;
    return p + n;
}

int x86_len(const uint8_t *p, const uint8_t *end, int *zk, int *pfx_off) {
    const uint8_t *s = p;
    int opsz = 0, rexw = 0, f3 = 0, f3_pos = -1;
    *zk = 0; *pfx_off = -1;

    for (;;) {
        if (p >= end) return 0;
        uint8_t c = *p;
        if (c == 0x66) { opsz = 1; p++; }
        else if (c == 0x67) { p++; }
        else if (c == 0xF0 || c == 0xF2) { p++; }
        else if (c == 0xF3) { f3 = 1; f3_pos = (int)(p - s); p++; }
        else if (c==0x2E||c==0x36||c==0x3E||c==0x26||c==0x64||c==0x65) { p++; }
        else break;
    }
    if (p < end && (*p & 0xF0) == 0x40) { rexw = (*p >> 3) & 1; p++; }
    if (p >= end) return 0;
    uint8_t op = *p++;

    if (op == 0xC5) {                                  /* 2-byte VEX */
        if (p >= end) return 0;
        p++;                   /* vex byte */
        if (p >= end) return 0;
        uint8_t vop = *p++;
        if (vop != 0x77) { p = dmod(p, end); if (!p) return 0; }  /* 0F77 = vzeroupper/all: no ModRM */
        if (imm0F(vop) == 1) { if (p >= end) return 0; p++; }
    } else if (op == 0xC4) {                            /* 3-byte VEX */
        if (p + 1 >= end) return 0;
        int map = p[0] & 0x1f; p += 2;
        if (p >= end) return 0;
        uint8_t vop = *p++;
        if (!(map == 1 && vop == 0x77)) { p = dmod(p, end); if (!p) return 0; }
        int imm = (map == 3) ? 1 : ((map == 1 && imm0F(vop) == 1) ? 1 : 0);
        if (imm) { if (p >= end) return 0; p++; }
    } else if (op == 0x62) {                            /* EVEX (AVX-512); 4-byte prefix */
        if (p + 2 >= end) return 0;
        int map = p[0] & 0x03; p += 3;
        if (p >= end) return 0;
        uint8_t vop = *p++;
        p = dmod(p, end); if (!p) return 0;             /* disp8 is 1 byte (compressed) — same length */
        int imm = (map == 3) ? 1 : ((map == 1 && imm0F(vop) == 1) ? 1 : 0);
        if (imm) { if (p >= end) return 0; p++; }
    } else if (op == 0x0F) {
        if (p >= end) return 0;
        uint8_t o2 = *p++;
        if (o2 == 0x38) {
            if (p >= end) return 0;
            p++;                /* third opcode byte */
            p = dmod(p, end); if (!p) return 0;         /* 0F38: no imm */
        } else if (o2 == 0x3A) {
            if (p >= end) return 0;
            p++;
            p = dmod(p, end); if (!p) return 0;
            if (p >= end) return 0;
            p++;                /* imm8 */
        } else if (o2 == 0x0F) {                        /* 3DNow!: modrm + imm8 suffix */
            p = dmod(p, end); if (!p) return 0;
            if (p >= end) return 0;
            p++;
        } else {
            if (f3 && o2 == 0xBD) { *zk = 1; *pfx_off = f3_pos; }   /* lzcnt */
            if (f3 && o2 == 0xBC) { *zk = 2; *pfx_off = f3_pos; }   /* tzcnt */
            if (modrm0F(o2)) { p = dmod(p, end); if (!p) return 0; }
            p = addimm(p, end, imm0F(o2), opsz, rexw); if (!p) return 0;
        }
    } else {
        if (modrm1(op)) {
            if (p >= end) return 0;
            uint8_t modrm = *p;
            p = dmod(p, end); if (!p) return 0;
            int ic = imm1(op);
            if (op == 0xF6) ic = (((modrm >> 3) & 7) < 2) ? 1 : 0;
            else if (op == 0xF7) ic = (((modrm >> 3) & 7) < 2) ? 3 : 0;
            p = addimm(p, end, ic, opsz, rexw); if (!p) return 0;
        } else {
            p = addimm(p, end, imm1(op), opsz, rexw); if (!p) return 0;
        }
    }
    if (p > end) return 0;
    return (int)(p - s);
}

/* classify control flow of the instruction at p (length len, offset insn_off):
 * *term = 1 if it has no fallthrough; *tgt = branch target offset, or -1. */
static void cflow(const uint8_t *p, const uint8_t *end, int len, long insn_off,
                  int *term, long *tgt) {
    *term = 0; *tgt = -1;
    const uint8_t *q = p;
    while (q < end) { uint8_t c = *q;
        if (c==0x66||c==0x67||c==0xF0||c==0xF2||c==0xF3||c==0x2E||c==0x36||c==0x3E||c==0x26||c==0x64||c==0x65) q++;
        else break; }
    if (q < end && (*q & 0xF0) == 0x40) q++;
    if (q >= end) return;
    uint8_t op = *q;
    const uint8_t *nx = p + len;                 /* next instruction */
    if (op == 0xEB) { *term = 1; *tgt = insn_off + len + (int8_t)nx[-1]; }
    else if (op == 0xE9) { int32_t r; memcpy(&r, nx-4, 4); *term = 1; *tgt = insn_off + len + r; }
    else if ((op >= 0x70 && op <= 0x7F) || (op >= 0xe0 && op <= 0xe3)) {
        *tgt = insn_off + len + (int8_t)nx[-1];
    }
    else if (op == 0xe8) { int32_t r; memcpy(&r, nx - 4, 4); *tgt = insn_off + len + r; }
    else if (op == 0x0F && q+1 < end && q[1] >= 0x80 && q[1] <= 0x8F) { int32_t r; memcpy(&r, nx-4, 4); *tgt = insn_off + len + r; }
    else if (op == 0xC3 || op == 0xC2 || op == 0xCB || op == 0xCA) *term = 1;     /* ret */
    else if (op == 0xF4) *term = 1;                                              /* hlt */
    else if (op == 0x0F && q+1 < end && q[1] == 0x0B) *term = 1;                 /* ud2 */
    /* The linear target collector walks past indirect jumps. Recursive
     * discovery terminates them and enters only resolved switch targets. */
}

/* is the instruction at p a `leaq disp(%rip), reg`? if so return its rip-rel
 * disp (and the opcode-relative position is handled by caller via length). */
static int is_lea_rip(const uint8_t *p, const uint8_t *end, int32_t *disp) {
    const uint8_t *q = p;
    while (q < end) { uint8_t c=*q; if(c==0x66||c==0x67||c==0xF0||c==0xF2||c==0xF3||c==0x2E||c==0x36||c==0x3E||c==0x26||c==0x64||c==0x65)q++; else break; }
    if (q >= end || (*q & 0xF8) != 0x48) return 0;          /* REX.W (48-4f with W) */
    if (!(*q & 0x08)) return 0;
    q++;
    if (q+1 >= end || *q != 0x8D) return 0;                 /* lea */
    uint8_t m = q[1];
    if ((m >> 6) != 0 || (m & 7) != 5) return 0;            /* mod=00 rm=101 => rip-rel */
    memcpy(disp, q + 2, 4);
    return 1;
}
/* FF group selector, after all legacy/REX prefixes. Callers classify only
 * instructions whose complete length has already been validated by x86_len. */
static int indirect_group(const uint8_t *p, const uint8_t *end) {
    const uint8_t *q = p;
    while (q < end) {
        uint8_t c=*q;
        if ((c>=0x40 && c<=0x4f) || c==0x66 || c==0x67 || c==0xf0 ||
            c==0xf2 || c==0xf3 || c==0x2e || c==0x36 || c==0x3e ||
            c==0x26 || c==0x64 || c==0x65) q++;
        else break;
    }
    return end-q>=2 && *q==0xff ? (q[1]>>3)&7 : -1;
}
static int is_jmp_indirect(const uint8_t *p, const uint8_t *end) {
    return indirect_group(p,end)==4;
}
/* Public wrapper for the trampoline scanner: classify the instruction at p.
 * *term=1 if no fallthrough; *tgt = direct-branch target offset or -1;
 * *indirect = 1 if an indirect jmp (ff /4). */
void lde_cflow(const uint8_t *p, const uint8_t *end, int len, long insn_off,
               int *term, long *tgt, int *indirect) {
    cflow(p, end, len, insn_off, term, tgt);
    *indirect = is_jmp_indirect(p, end);
}

long lde_rd_zcnt(const uint8_t *text, size_t begin, size_t end, size_t readable,
                 size_t *out, int capacity) {
    if (capacity < 0) return -1;
    return lde_scan_sites(text, begin, end, readable, LDE_LZCNT | LDE_TZCNT,
                          1, out, (size_t)capacity, 0);
}

static size_t terminal_padding(const uint8_t *text, size_t begin, size_t end) {
    size_t padding=end;
    while(padding>begin && end-padding<=16 &&
        (text[padding-1]==0 || text[padding-1]==0x90 || text[padding-1]==0xcc)) padding--;
    return end-padding>16?end:padding;
}

static int rd_map(const uint8_t *text, size_t fstart, size_t fend, size_t readable,
                  uint8_t *code, uint8_t *tgt, int *has_indirect,
                  lde_trap_lookup traps, int heuristic, const uint8_t *data, const uint8_t *entries) {
    if (fstart >= fend || fend > readable) return 0;
    size_t n = fend - fstart;
    if(data && data[0]) return 0;
    if (n > SIZE_MAX / sizeof(size_t)) return 0;
    size_t *stk = malloc(n * sizeof(size_t));
    if (!stk) return 0;
    size_t sp = 0; long last_lea = -1; *has_indirect = 0;
    /* A bounded alignment suffix is unknown terminal data, not a proof that
     * the function is complete. Only stop at a decoded instruction boundary;
     * never trim an immediate/displacement or accept a truncated opcode.
     * Sixteen bytes is the conservative x86 function-alignment allowance. */
    size_t padding=heuristic?fend:terminal_padding(text,fstart,fend);
    #define RD_PUSH(X) do { size_t _x = (size_t)(X); \
        if (_x >= fstart && _x < fend && !code[_x - fstart]) { \
            code[_x - fstart] = 2; stk[sp++] = _x; } } while (0)
    RD_PUSH(fstart);
    if(entries) for(size_t i=0;i<n;i++) if(entries[i]) RD_PUSH(fstart+i);
    while (sp) {
        size_t off = stk[--sp];
        if (off < fstart || off >= fend || code[off - fstart] == 1) continue;
        if (data && data[off-fstart]) { code[off-fstart]=0; continue; }
        if (off>=padding) { code[off-fstart]=0; *has_indirect=1; continue; }
        int zk, o2; int len = x86_len(text + off, text + fend, &zk, &o2);
        if (len <= 0) { free(stk); return 0; }
        if(data) for(int j=0;j<len;j++) if(data[off-fstart+(size_t)j]) { free(stk); return 0; }
        code[off - fstart] = 1;

        int32_t disp;
        if (is_lea_rip(text + off, text + fend, &disp)) {
            last_lea = (long)(off + len) + disp;           /* switch-table base (offset space) */
        } else if (is_jmp_indirect(text + off, text + fend)) {
            int resolved = 0;
            if (heuristic && last_lea >= 0)
                for (int i = 0; i < 4096; i++) {           /* resolve 4-byte switch entries */
                    size_t e = (size_t)last_lea + 4 * i;
                    if (e + 4 > readable) break;
                    int32_t entry; memcpy(&entry, text + e, 4);
                    long t = last_lea + entry;
                    if (t < (long)fstart || t >= (long)fend) break;   /* past the table */
                    resolved = 1;
                    tgt[t - fstart] = 1;
                    if (!code[t - fstart]) RD_PUSH(t);
                }
            if (!resolved) *has_indirect = 1;              /* unknown targets remain */
        }
        int group=indirect_group(text+off,text+off+len);
        if (!heuristic && group>=2 && group<=5) *has_indirect=1;
        int term; long t;
        cflow(text + off, text + fend, len, (long)off, &term, &t);
        if (is_jmp_indirect(text + off, text + fend) || (!heuristic && group==5)) term = 1;
        if (term && len == 2 && text[off] == 0x0f && text[off + 1] == 0x0b &&
            traps && traps((uintptr_t)(text + off))) term = 0;
        if (t >= (long)fstart && (size_t)t < fend) {
            if(data && data[(size_t)t-fstart]) { free(stk); return 0; }
            tgt[t - fstart] = 1;
            if (!code[t - fstart]) RD_PUSH(t);
        }
        if (!term) { size_t fa = off + len; if (fa < fend && !code[fa - fstart]) RD_PUSH(fa); }
    }
    #undef RD_PUSH
    free(stk);
    return 1;
}

int lde_rd_map_traps(const uint8_t *text, size_t begin, size_t end, size_t readable,
                     uint8_t *code, uint8_t *targets, int *has_indirect,
                     lde_trap_lookup traps) {
    return rd_map(text,begin,end,readable,code,targets,has_indirect,traps,1,0,0);
}
int lde_rd_map_direct(const uint8_t *text, size_t begin, size_t end, size_t readable,
                      uint8_t *code, uint8_t *targets, int *has_indirect,
                      lde_trap_lookup traps, const uint8_t *data) {
    return rd_map(text,begin,end,readable,code,targets,has_indirect,traps,0,data,0);
}

int lde_rd_map_direct_roots(const uint8_t *text, size_t begin, size_t end, size_t readable,
    uint8_t *code, uint8_t *targets, int *has_indirect, lde_trap_lookup traps,
    const uint8_t *data, const uint8_t *entries) {
    return rd_map(text,begin,end,readable,code,targets,has_indirect,traps,0,data,entries);
}

int lde_rd_map(const uint8_t *text, size_t begin, size_t end, size_t readable,
               uint8_t *code, uint8_t *targets, int *has_indirect) {
    return lde_rd_map_traps(text, begin, end, readable, code, targets, has_indirect, 0);
}

static long scan_sites(const uint8_t *text, size_t begin, size_t end,
                    size_t readable, unsigned required, int recursive,
                    size_t *out, size_t capacity, lde_trap_lookup traps,
                    const uint8_t *excluded, uint8_t *selected, int partial) {
    if (!text || (capacity && !out && !selected) || begin >= end || end > readable) return -1;
    size_t n = end - begin, count = 0;
    uint8_t *code = 0, *targets = 0;
    if (recursive) {
        int indirect;
        code = calloc(n, 1); targets = calloc(n, 1);
        if (!code || !targets ||
            !rd_map(text, begin, end, readable, code, targets, &indirect, traps,1,
                    excluded?excluded+begin:0,0)) {
            free(code); free(targets); return -1;
        }
    }
    for (size_t q = begin; q < end;) {
        if(excluded && excluded[q]) { do { q++; } while(q<end && excluded[q]); continue; }
        if (!recursive && end - q <= 16) {
            int padding = 1;
            for (size_t r = q; r < end; r++)
                if (text[r] != 0 && text[r] != 0xcc && text[r] != 0x90) { padding = 0; break; }
            if (padding) break;
        }
        if (code && !code[q - begin]) { q++; continue; }
        int zk, prefix;
        int len = x86_len(text + q, text + end, &zk, &prefix);
        if (len <= 0) {
            if(partial) break;
            free(code); free(targets); return -1;
        }
        if(excluded) {
            size_t j=0;
            while(j<(size_t)len && !excluded[q+j]) j++;
            if(j<(size_t)len) { q+=j; continue; }
        }
        unsigned op = (unsigned)zk;
        if (len == 2 && text[q] == 0x0f && text[q + 1] == 0xa2) op = LDE_CPUID;
        if ((op & required) && (op == LDE_CPUID ||
            (prefix >= 0 && text[q + prefix] == 0xf3))) {
            if (count == capacity) { free(code); free(targets); return -1; }
            if(selected) selected[q+(size_t)prefix]=1;
            else out[count]=q;
            count++;
        }
        q += (size_t)len;
    }
    free(code); free(targets);
    return (long)count;
}

long lde_scan_sites(const uint8_t *text, size_t begin, size_t end,
                    size_t readable, unsigned required, int recursive,
                    size_t *out, size_t capacity, lde_trap_lookup traps) {
    return scan_sites(text,begin,end,readable,required,recursive,out,capacity,traps,0,0,0);
}
long lde_collect_zcnt(const uint8_t *text, size_t begin, size_t end,
                      size_t readable, unsigned required, int recursive,
                      const uint8_t *excluded, uint8_t *selected, int partial,
                      lde_trap_lookup traps) {
    if(!selected || (required & ~(LDE_LZCNT|LDE_TZCNT))) return -1;
    return scan_sites(text,begin,end,readable,required,recursive,0,SIZE_MAX,traps,excluded,selected,partial);
}

long lde_scan_func_mask(uint8_t *text, size_t begin, size_t end, size_t readable,
                        int patch, unsigned required, int recursive,
                        size_t *out, int capacity, lde_trap_lookup traps) {
    if (capacity < 0) return -1;
    long count = lde_scan_sites(text, begin, end, readable, required,
                                recursive, out, (size_t)capacity, traps);
    if (count < 0 && !recursive)
        count = lde_scan_sites(text, begin, end, readable, required,
                               1, out, (size_t)capacity, traps);
    if (patch && count > 0)
        for (long i = 0; i < count; i++) {
            int zk, prefix;
            x86_len(text + out[i], text + end, &zk, &prefix);
            if (prefix >= 0) text[out[i] + prefix] = 0xf0;
        }
    return count;
}

long lde_scan_func(uint8_t *text, size_t begin, size_t end, size_t readable,
                   int patch, size_t *out, int capacity) {
    return lde_scan_func_mask(text, begin, end, readable, patch,
                              LDE_LZCNT | LDE_TZCNT, 0, out, capacity, 0);
}

long lde_scan_zcnt_mask(uint8_t *text, size_t n, int patch, unsigned required) {
    uint8_t *end = text + n, *p = text;
    long count = 0;
    while (p < end) {
        int zk, off;
        int len = x86_len(p, end, &zk, &off);
        if (len <= 0) return -1;
        if (((unsigned)zk & required) && off >= 0 && p[off] == 0xf3) {
            count++;
            if (patch) p[off] = 0xf0;
        }
        p += len;
    }
    return count;
}

long lde_scan_zcnt(uint8_t *text, size_t n, int patch) {
    return lde_scan_zcnt_mask(text, n, patch, LDE_LZCNT | LDE_TZCNT);
}
