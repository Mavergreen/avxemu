/*
 * minspilltest.c — validate the MINIMAL-SPILL live-register thunk in isolation.
 *
 * A minimal-spill thunk operates on the live program registers in place (saving
 * only the scratch its lowering borrows), entered by a `jmp` and ending in a
 * `jmp resume`. minspill_harness.s loads a known live GPR/flags state, jumps the
 * thunk, and at a fixed global resume label captures the resulting live state.
 *
 * This file starts with a NO-OP thunk (just `jmp resume`) to validate the harness
 * plumbing — state load, resume, capture — independent of any lowering. Task 2
 * adds the differential lzcnt cases.
 */
#include "decode.h"
#include "vexops.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern int   avxemu_pool_init(void *hint, size_t cap);
extern void *avxemu_pool_alloc(size_t n);
extern void  avxemu_minspill_run(const uint64_t in_gpr[16], uint64_t in_rflags,
                                 void *thunk_entry, uint64_t out_gpr[16],
                                 uint64_t *out_rflags);
extern void  avxemu_minspill_capture(void);   /* fixed resume target (baked into thunk) */
/* emit a minimal-spill live-register lzcnt thunk into the pool; jmp's `resume` at end */
extern void *avxemu_emit_minspill_block(const decoded *d, uint64_t resume);
extern uint64_t minspill_ti_labels[];         /* _mi0.._mi8 lzcnt sample addresses */
extern uint64_t minspill_shlx_labels[];       /* _si0.._si7 shlx sample addresses */
extern uint64_t minspill_tz_labels[];         /* _zi0.._zi8 tzcnt sample addresses */
extern uint64_t minspill_shrx_labels[];       /* _xi0.._xi8 shrx sample addresses */
extern uint64_t minspill_sarx_labels[];       /* _ai0.._ai8 sarx sample addresses */
extern uint64_t minspill_rorx_labels[];       /* _ri0.._ri8 rorx sample addresses */
extern uint64_t minspill_bzhi_labels[];       /* _bhi0.._bhi9 bzhi sample addresses */
extern uint64_t minspill_blsr_labels[];       /* _blsr0.._blsr8 blsr sample addresses */
extern uint64_t minspill_blsi_labels[];       /* _blsi0.._blsi8 blsi sample addresses */
extern uint64_t minspill_blsm_labels[];       /* _blsm0.._blsm8 blsmsk sample addresses */
extern uint64_t minspill_andn_labels[];       /* _andn0.._andn11 andn sample addresses */
extern uint64_t minspill_mulx_labels[];       /* _mx0.._mx23 mulx sample addresses */
extern uint64_t avxemu_minspill_rz8, avxemu_minspill_rz16;  /* red-zone snapshots */
#define RZ_SENTINEL 0x5A5A5A5A5A5A5A5Aull

#define F_CF 0x001u
#define F_PF 0x004u
#define F_AF 0x010u
#define F_ZF 0x040u
#define F_SF 0x080u
#define F_OF 0x800u
#define F_OWNED (F_CF | F_ZF | F_SF | F_OF)   /* flags lzcnt defines (our emitter writes) */
#define F_PFAF  (F_PF | F_AF)                  /* flags lzcnt leaves undefined; we preserve */

static int g_fail;

/* Emit a `jmp rel32` to `target` into the RWX pool; return the thunk entry. */
static void *emit_jmp_thunk(uint64_t target) {
    uint8_t *blk = avxemu_pool_alloc(8);
    if (!blk) { printf("  pool_alloc failed\n"); g_fail++; return 0; }
    uint8_t *p = blk;
    *p++ = 0xE9;
    int32_t rel = (int32_t)((int64_t)target - (int64_t)(blk + 5));
    memcpy(p, &rel, 4);
    return blk;
}

/* Resume target for every thunk: a `jmp *(%rip)` absolute trampoline emitted
 * INTO the pool, pointing at the in-image capture routine. The thunks reach
 * resume via a position-dependent jmp rel32; when the kernel places the RWX
 * pool >2GB from the image (macOS 15 routes it to a far zone) a direct rel32
 * to the in-image capture overflows and jumps into garbage (the segfault this
 * fixes). Bouncing through a pooled absolute jump keeps the rel32 within the
 * pool and makes pool placement irrelevant. Zero register clobber (indirect
 * through the 8 bytes that follow the instruction). Cached: one per run. */
static uint64_t resume_target(void) {
    static uint64_t cached;
    if (cached) return cached;
    uint8_t *blk = avxemu_pool_alloc(16);
    if (!blk) { printf("  pool_alloc failed (resume tramp)\n"); g_fail++; return 0; }
    blk[0] = 0xFF; blk[1] = 0x25;           /* jmp *0(%rip) */
    blk[2] = blk[3] = blk[4] = blk[5] = 0;
    uint64_t tgt = (uint64_t)(void *)avxemu_minspill_capture;
    memcpy(blk + 6, &tgt, 8);
    cached = (uint64_t)(uintptr_t)blk;
    return cached;
}

/* distinctive, non-trivial live GPR contents */
static void seed(uint64_t g[16]) {
    for (int i = 0; i < 16; i++) g[i] = 0x1111111100000000ull * (i + 1) + 0xABCD + i;
}

/* decode the idx'th sample from a label array; verify decoded length matches asm. */
static decoded dec_from(uint64_t *labels, int idx) {
    decoded d; int n = decode((const uint8_t *)labels[idx], &d);
    int asmlen = (int)(labels[idx + 1] - labels[idx]);
    if (n != asmlen) { printf("  decode len mismatch idx %d: %d vs %d\n", idx, n, asmlen); g_fail++; }
    return d;
}
static decoded dec_at(int idx)      { return dec_from(minspill_ti_labels, idx); }
static decoded dec_shlx(int idx)    { return dec_from(minspill_shlx_labels, idx); }
static decoded dec_tzcnt(int idx)   { return dec_from(minspill_tz_labels, idx); }
static decoded dec_shrx(int idx)    { return dec_from(minspill_shrx_labels, idx); }
static decoded dec_sarx(int idx)    { return dec_from(minspill_sarx_labels, idx); }
static decoded dec_rorx(int idx)    { return dec_from(minspill_rorx_labels, idx); }
static decoded dec_bzhi(int idx)    { return dec_from(minspill_bzhi_labels, idx); }
static decoded dec_blsr(int idx)    { return dec_from(minspill_blsr_labels, idx); }
static decoded dec_blsi(int idx)    { return dec_from(minspill_blsi_labels, idx); }
static decoded dec_blsm(int idx)    { return dec_from(minspill_blsm_labels, idx); }
static decoded dec_andn(int idx)    { return dec_from(minspill_andn_labels, idx); }
static decoded dec_mulx(int idx)    { return dec_from(minspill_mulx_labels, idx); }

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    /* Bump allocator with no free: every differential case leaks a fresh thunk, so the
     * cap must cover the cumulative total across ALL ops (the MULX suite alone adds
     * 1024 blocks). 16 MiB is comfortably above the sum. */
    if (!avxemu_pool_init(0, 1 << 24)) { printf("pool_init failed\n"); return 1; }
    printf("== minimal-spill live-register thunk ==\n");

    /* (0) no-op thunk: jmp resume. Validates plumbing: out == in for every GPR
     * (slot 4 = rsp delta, must be 0) and rflags. */
    {
        uint64_t in[16], out[16]; uint64_t in_fl = 0xAD7, out_fl = 0;
        seed(in); memset(out, 0xEE, sizeof out);
        void *thunk = emit_jmp_thunk(resume_target());
        avxemu_minspill_run(in, in_fl, thunk, out, &out_fl);
        int bad = 0;
        for (int i = 0; i < 16; i++) {
            if (i == 4) {
                if (out[i] != 0) { printf("  [noop] rsp delta %lld != 0\n", (long long)out[i]); bad = 1; }
                continue;
            }
            if (out[i] != in[i]) {
                printf("  [noop] gpr%d CLOBBERED %016llx->%016llx\n",
                       i, (unsigned long long)in[i], (unsigned long long)out[i]); bad = 1;
            }
        }
        if (out_fl != in_fl) {
            printf("  [noop] rflags CHANGED %llx->%llx\n",
                   (unsigned long long)in_fl, (unsigned long long)out_fl); bad = 1;
        }
        printf("  noop passthrough: %s\n", bad ? "FAIL" : "ok"); g_fail += bad;
    }

    /* (1) lzcnt differential: result + owned flags vs bmi_exec; PF/AF preserved;
     * only dst changed (scratch restored, nothing else clobbered); rsp intact. */
    {
        static const uint64_t srcs[] = {
            0, 1, 0xFFFFFFFFull, ~0ull, 0x80000000ull, 0x8000000000000000ull,
            0xFFull, 0xDEADBEEFull, 0x100000000ull, 0x0001234500000000ull,
        };
        const int NSAMP = 7;   /* _mi0.._mi6 */
        int cases = 0, fails0 = g_fail;
        for (int i = 0; i < NSAMP; i++) {
            decoded d = dec_at(i);
            if (!(d.is_bmi && d.op == BMI_LZCNT)) { printf("  sample %d is not lzcnt\n", i); g_fail++; continue; }
            uint64_t opmask = (d.opsize == 64) ? ~0ull : 0xFFFFFFFFull;
            for (size_t k = 0; k < sizeof(srcs) / sizeof(srcs[0]); k++) {
                uint64_t v = srcs[k];
                uint64_t in[16], out[16]; uint64_t in_fl = 0xAD7, out_fl = 0;
                seed(in); in[d.a_src] = v; memset(out, 0xEE, sizeof out);

                void *thunk = avxemu_emit_minspill_block(&d, resume_target());
                if (!thunk) { printf("  [lz] sample %d: emit returned NULL\n", i); g_fail++; continue; }
                avxemu_minspill_run(in, in_fl, thunk, out, &out_fl);

                uint64_t res = 0, dst2 = 0, rf = in_fl;
                bmi_exec(BMI_LZCNT, d.opsize, v, 0, &res, &dst2, &rf);

                int bad = 0;
                if ((out[d.dst] & opmask) != (res & opmask)) {
                    printf("  [lz s%d v=%016llx] result %016llx != %016llx\n",
                           i, (unsigned long long)v, (unsigned long long)out[d.dst], (unsigned long long)res); bad = 1;
                }
                if (d.opsize == 32 && (out[d.dst] >> 32) != 0) {
                    printf("  [lz s%d v=%016llx] opsize32 dst not zero-extended: %016llx\n",
                           i, (unsigned long long)v, (unsigned long long)out[d.dst]); bad = 1;
                }
                if ((out_fl & F_OWNED) != (rf & F_OWNED)) {
                    printf("  [lz s%d v=%016llx] owned flags %03llx != %03llx\n",
                           i, (unsigned long long)v, (unsigned long long)(out_fl & F_OWNED),
                           (unsigned long long)(rf & F_OWNED)); bad = 1;
                }
                if ((out_fl & F_PFAF) != (in_fl & F_PFAF)) {
                    printf("  [lz s%d v=%016llx] PF/AF changed %03llx -> %03llx\n",
                           i, (unsigned long long)v, (unsigned long long)(in_fl & F_PFAF),
                           (unsigned long long)(out_fl & F_PFAF)); bad = 1;
                }
                for (int j = 0; j < 16; j++) {
                    if (j == d.dst) continue;
                    if (j == 4) { if (out[j] != 0) { printf("  [lz s%d] rsp delta %lld != 0\n", i, (long long)out[j]); bad = 1; } continue; }
                    if (out[j] != in[j]) {
                        printf("  [lz s%d v=%016llx] gpr%d CLOBBERED %016llx->%016llx\n",
                               i, (unsigned long long)v, j, (unsigned long long)in[j], (unsigned long long)out[j]); bad = 1;
                    }
                }
                /* red-zone preservation: the thunk must step past the 128-byte red
                 * zone before using the stack — [rsp-8]/[rsp-16] stay sentinel. */
                if (avxemu_minspill_rz8 != RZ_SENTINEL || avxemu_minspill_rz16 != RZ_SENTINEL) {
                    printf("  [lz s%d v=%016llx] RED ZONE clobbered: rz8=%016llx rz16=%016llx\n",
                           i, (unsigned long long)v, (unsigned long long)avxemu_minspill_rz8,
                           (unsigned long long)avxemu_minspill_rz16); bad = 1;
                }
                g_fail += bad; cases++;
            }
        }
        printf("  lzcnt differential (%d cases): %s\n", cases, (g_fail == fails0) ? "ok" : "FAIL");
    }

    /* (2) rsp-operand fallback: `lzcnt rax, rsp` (sample _mi7) is legal but the
     * live-register thunk can't use rsp as an operand (it IS the working stack);
     * the emitter must DECLINE so emit_run falls back to the slot-based path. */
    {
        decoded d = dec_at(7);
        int ok = (d.is_bmi && d.op == BMI_LZCNT && d.a_src == 4);
        void *thunk = avxemu_emit_minspill_block(&d, resume_target());
        if (!ok) { printf("  rsp-operand sample _mi7 didn't decode as lzcnt w/ rsp src\n"); g_fail++; }
        printf("  rsp-operand declined: %s\n", (thunk == NULL) ? "ok" : "FAIL (emitter accepted rsp!)");
        if (thunk != NULL) g_fail++;
    }

    /* (3) shlx differential: dst = value << (count & (opsize-1)); NO flags touched.
     * Check result vs bmi_exec; ALL flags preserved; only dst changed; rsp + red
     * zone intact. Samples _si0.._si5 (si6 is the dst==rcx decline, tested below). */
    {
        static const uint64_t vals[] = {
            0, 1, 0xFFFFFFFFull, ~0ull, 0x80000000ull, 0x8000000000000000ull,
            0xDEADBEEFCAFEBABEull, 0x0000000012345678ull,
        };
        static const uint64_t cnts[] = { 0, 1, 7, 31, 32, 63, 64, 0xFF };
        const int NSAMP = 6;   /* _si0.._si5 */
        int cases = 0, fails0 = g_fail;
        for (int i = 0; i < NSAMP; i++) {
            decoded d = dec_shlx(i);
            if (!(d.is_bmi && d.op == BMI_SHLX)) { printf("  shlx sample %d not shlx\n", i); g_fail++; continue; }
            uint64_t opmask = (d.opsize == 64) ? ~0ull : 0xFFFFFFFFull;
            for (size_t a = 0; a < sizeof(vals)/sizeof(vals[0]); a++) {
                for (size_t b = 0; b < sizeof(cnts)/sizeof(cnts[0]); b++) {
                    uint64_t in[16], out[16]; uint64_t in_fl = 0xAD7, out_fl = 0;
                    seed(in);
                    in[d.a_src] = vals[a]; in[d.b_src] = cnts[b];  /* count set after value: if same reg, count wins (matches our & masking) */
                    memset(out, 0xEE, sizeof out);
                    void *thunk = avxemu_emit_minspill_block(&d, resume_target());
                    if (!thunk) { printf("  [shlx s%d] emit NULL\n", i); g_fail++; continue; }
                    avxemu_minspill_run(in, in_fl, thunk, out, &out_fl);

                    /* reference: bmi_exec(SHLX, opsize, value=in[a_src], count=in[b_src]) */
                    uint64_t res = 0, dst2 = 0, rf = in_fl;
                    bmi_exec(BMI_SHLX, d.opsize, in[d.a_src], in[d.b_src], &res, &dst2, &rf);

                    int bad = 0;
                    if ((out[d.dst] & opmask) != (res & opmask)) {
                        printf("  [shlx s%d v=%016llx c=%llu] result %016llx != %016llx\n", i,
                               (unsigned long long)in[d.a_src], (unsigned long long)in[d.b_src],
                               (unsigned long long)out[d.dst], (unsigned long long)res); bad = 1;
                    }
                    if (d.opsize == 32 && (out[d.dst] >> 32) != 0) {
                        printf("  [shlx s%d] opsize32 dst not zero-extended: %016llx\n", i, (unsigned long long)out[d.dst]); bad = 1;
                    }
                    /* shlx defines NO flags -> every flag bit preserved */
                    if (out_fl != in_fl) {
                        printf("  [shlx s%d] flags changed %llx -> %llx\n", i,
                               (unsigned long long)in_fl, (unsigned long long)out_fl); bad = 1;
                    }
                    for (int j = 0; j < 16; j++) {
                        if (j == d.dst) continue;
                        if (j == 4) { if (out[j] != 0) { printf("  [shlx s%d] rsp delta %lld\n", i, (long long)out[j]); bad = 1; } continue; }
                        if (out[j] != in[j]) {
                            printf("  [shlx s%d] gpr%d CLOBBERED %016llx->%016llx\n", i, j,
                                   (unsigned long long)in[j], (unsigned long long)out[j]); bad = 1;
                        }
                    }
                    if (avxemu_minspill_rz8 != RZ_SENTINEL || avxemu_minspill_rz16 != RZ_SENTINEL) {
                        printf("  [shlx s%d] RED ZONE clobbered\n", i); bad = 1;
                    }
                    g_fail += bad; cases++;
                }
            }
        }
        printf("  shlx differential (%d cases): %s\n", cases, (g_fail == fails0) ? "ok" : "FAIL");
    }

    /* (4) shlx dst==rcx fallback: `shlx rcx, r8, rdx` (sample _si6) — rcx is our CL
     * scratch, so the emitter must DECLINE (falls back to the slot-based path). */
    {
        decoded d = dec_shlx(6);
        int ok = (d.is_bmi && d.op == BMI_SHLX && d.dst == 1);
        void *thunk = avxemu_emit_minspill_block(&d, resume_target());
        if (!ok) { printf("  shlx dst==rcx sample _si6 didn't decode as expected\n"); g_fail++; }
        printf("  shlx dst==rcx declined: %s\n", (thunk == NULL) ? "ok" : "FAIL (accepted rcx dst!)");
        if (thunk != NULL) g_fail++;
    }

    /* (5) tzcnt differential: result + owned flags vs bmi_exec; PF/AF preserved;
     * only dst changed (scratch restored, nothing else clobbered); rsp intact. */
    {
        static const uint64_t srcs[] = {
            0, 1, 0xFFFFFFFFull, ~0ull, 0x80000000ull, 0x8000000000000000ull,
            0xFFull, 0xDEADBEEFull, 0x100000000ull, 0x0001234500000000ull,
        };
        const int NSAMP = 7;   /* _zi0.._zi6 */
        int cases = 0, fails0 = g_fail;
        for (int i = 0; i < NSAMP; i++) {
            decoded d = dec_tzcnt(i);
            if (!(d.is_bmi && d.op == BMI_TZCNT)) { printf("  tzcnt sample %d is not tzcnt\n", i); g_fail++; continue; }
            uint64_t opmask = (d.opsize == 64) ? ~0ull : 0xFFFFFFFFull;
            for (size_t k = 0; k < sizeof(srcs) / sizeof(srcs[0]); k++) {
                uint64_t v = srcs[k];
                uint64_t in[16], out[16]; uint64_t in_fl = 0xAD7, out_fl = 0;
                seed(in); in[d.a_src] = v; memset(out, 0xEE, sizeof out);

                void *thunk = avxemu_emit_minspill_block(&d, resume_target());
                if (!thunk) { printf("  [tz] sample %d: emit returned NULL\n", i); g_fail++; continue; }
                avxemu_minspill_run(in, in_fl, thunk, out, &out_fl);

                uint64_t res = 0, dst2 = 0, rf = in_fl;
                bmi_exec(BMI_TZCNT, d.opsize, v, 0, &res, &dst2, &rf);

                int bad = 0;
                if ((out[d.dst] & opmask) != (res & opmask)) {
                    printf("  [tz s%d v=%016llx] result %016llx != %016llx\n",
                           i, (unsigned long long)v, (unsigned long long)out[d.dst], (unsigned long long)res); bad = 1;
                }
                if (d.opsize == 32 && (out[d.dst] >> 32) != 0) {
                    printf("  [tz s%d v=%016llx] opsize32 dst not zero-extended: %016llx\n",
                           i, (unsigned long long)v, (unsigned long long)out[d.dst]); bad = 1;
                }
                if ((out_fl & F_OWNED) != (rf & F_OWNED)) {
                    printf("  [tz s%d v=%016llx] owned flags %03llx != %03llx\n",
                           i, (unsigned long long)v, (unsigned long long)(out_fl & F_OWNED),
                           (unsigned long long)(rf & F_OWNED)); bad = 1;
                }
                if ((out_fl & F_PFAF) != (in_fl & F_PFAF)) {
                    printf("  [tz s%d v=%016llx] PF/AF changed %03llx -> %03llx\n",
                           i, (unsigned long long)v, (unsigned long long)(in_fl & F_PFAF),
                           (unsigned long long)(out_fl & F_PFAF)); bad = 1;
                }
                for (int j = 0; j < 16; j++) {
                    if (j == d.dst) continue;
                    if (j == 4) { if (out[j] != 0) { printf("  [tz s%d] rsp delta %lld != 0\n", i, (long long)out[j]); bad = 1; } continue; }
                    if (out[j] != in[j]) {
                        printf("  [tz s%d v=%016llx] gpr%d CLOBBERED %016llx->%016llx\n",
                               i, (unsigned long long)v, j, (unsigned long long)in[j], (unsigned long long)out[j]); bad = 1;
                    }
                }
                if (avxemu_minspill_rz8 != RZ_SENTINEL || avxemu_minspill_rz16 != RZ_SENTINEL) {
                    printf("  [tz s%d v=%016llx] RED ZONE clobbered: rz8=%016llx rz16=%016llx\n",
                           i, (unsigned long long)v, (unsigned long long)avxemu_minspill_rz8,
                           (unsigned long long)avxemu_minspill_rz16); bad = 1;
                }
                g_fail += bad; cases++;
            }
        }
        printf("  tzcnt differential (%d cases): %s\n", cases, (g_fail == fails0) ? "ok" : "FAIL");
    }

    /* (6) tzcnt rsp-operand fallback: `tzcnt rax, rsp` (sample _zi7) is legal but
     * the live-register thunk can't use rsp as an operand; emitter must DECLINE. */
    {
        decoded d = dec_tzcnt(7);
        int ok = (d.is_bmi && d.op == BMI_TZCNT && d.a_src == 4);
        void *thunk = avxemu_emit_minspill_block(&d, resume_target());
        if (!ok) { printf("  tzcnt rsp-operand sample _zi7 didn't decode as tzcnt w/ rsp src\n"); g_fail++; }
        printf("  tzcnt rsp-operand declined: %s\n", (thunk == NULL) ? "ok" : "FAIL (emitter accepted rsp!)");
        if (thunk != NULL) g_fail++;
    }

    /* (7) shrx differential: dst = value >> (count & (opsize-1)); NO flags touched.
     * Check result vs bmi_exec; ALL flags preserved; only dst changed; rsp intact.
     * Samples _xi0.._xi5: opsize 32/64, dst!=value, dst==value, dst==count,
     * count==value, high regs. */
    {
        static const uint64_t vals[] = {
            0, 1, 0xFFFFFFFFull, ~0ull, 0x80000000ull, 0x8000000000000000ull,
            0xDEADBEEFCAFEBABEull, 0x0000000012345678ull,
        };
        static const uint64_t cnts[] = { 0, 1, 7, 31, 32, 63, 64, 0xFF };
        const int NSAMP = 6;   /* _xi0.._xi5 */
        int cases = 0, fails0 = g_fail;
        for (int i = 0; i < NSAMP; i++) {
            decoded d = dec_shrx(i);
            if (!(d.is_bmi && d.op == BMI_SHRX)) { printf("  shrx sample %d not shrx\n", i); g_fail++; continue; }
            uint64_t opmask = (d.opsize == 64) ? ~0ull : 0xFFFFFFFFull;
            for (size_t a = 0; a < sizeof(vals)/sizeof(vals[0]); a++) {
                for (size_t b = 0; b < sizeof(cnts)/sizeof(cnts[0]); b++) {
                    uint64_t in[16], out[16]; uint64_t in_fl = 0xAD7, out_fl = 0;
                    seed(in);
                    in[d.a_src] = vals[a]; in[d.b_src] = cnts[b];
                    memset(out, 0xEE, sizeof out);
                    void *thunk = avxemu_emit_minspill_block(&d, resume_target());
                    if (!thunk) { printf("  [shrx s%d] emit NULL\n", i); g_fail++; continue; }
                    avxemu_minspill_run(in, in_fl, thunk, out, &out_fl);

                    uint64_t res = 0, dst2 = 0, rf = in_fl;
                    bmi_exec(BMI_SHRX, d.opsize, in[d.a_src], in[d.b_src], &res, &dst2, &rf);

                    int bad = 0;
                    if ((out[d.dst] & opmask) != (res & opmask)) {
                        printf("  [shrx s%d v=%016llx c=%llu] result %016llx != %016llx\n", i,
                               (unsigned long long)in[d.a_src], (unsigned long long)in[d.b_src],
                               (unsigned long long)out[d.dst], (unsigned long long)res); bad = 1;
                    }
                    if (d.opsize == 32 && (out[d.dst] >> 32) != 0) {
                        printf("  [shrx s%d] opsize32 dst not zero-extended: %016llx\n", i, (unsigned long long)out[d.dst]); bad = 1;
                    }
                    /* shrx defines NO flags -> every flag bit preserved */
                    if (out_fl != in_fl) {
                        printf("  [shrx s%d v=%016llx c=%llu] flags changed %llx -> %llx\n", i,
                               (unsigned long long)in[d.a_src], (unsigned long long)in[d.b_src],
                               (unsigned long long)in_fl, (unsigned long long)out_fl); bad = 1;
                    }
                    for (int j = 0; j < 16; j++) {
                        if (j == d.dst) continue;
                        if (j == 4) { if (out[j] != 0) { printf("  [shrx s%d] rsp delta %lld\n", i, (long long)out[j]); bad = 1; } continue; }
                        if (out[j] != in[j]) {
                            printf("  [shrx s%d] gpr%d CLOBBERED %016llx->%016llx\n", i, j,
                                   (unsigned long long)in[j], (unsigned long long)out[j]); bad = 1;
                        }
                    }
                    if (avxemu_minspill_rz8 != RZ_SENTINEL || avxemu_minspill_rz16 != RZ_SENTINEL) {
                        printf("  [shrx s%d] RED ZONE clobbered\n", i); bad = 1;
                    }
                    g_fail += bad; cases++;
                }
            }
        }
        printf("  shrx differential (%d cases): %s\n", cases, (g_fail == fails0) ? "ok" : "FAIL");
    }

    /* (8) shrx dst==rcx fallback: `shrxq rdx, r8, rcx` (sample _xi6) — rcx is our
     * CL scratch, so the emitter must DECLINE (falls back to slot-based path). */
    {
        decoded d = dec_shrx(6);
        int ok = (d.is_bmi && d.op == BMI_SHRX && d.dst == 1);
        void *thunk = avxemu_emit_minspill_block(&d, resume_target());
        if (!ok) { printf("  shrx dst==rcx sample _xi6 not as expected\n"); g_fail++; }
        printf("  shrx dst==rcx declined: %s\n", (thunk == NULL) ? "ok" : "FAIL (accepted rcx dst!)");
        if (thunk != NULL) g_fail++;
    }

    /* (9) shrx rsp-operand fallback: `shrxq rcx, rsp, rax` (sample _xi7) — rsp
     * as value operand; emitter must DECLINE. */
    {
        decoded d = dec_shrx(7);
        int ok = (d.is_bmi && d.op == BMI_SHRX && d.a_src == 4);
        void *thunk = avxemu_emit_minspill_block(&d, resume_target());
        if (!ok) { printf("  shrx rsp-value sample _xi7 not as expected\n"); g_fail++; }
        printf("  shrx rsp-operand declined: %s\n", (thunk == NULL) ? "ok" : "FAIL (emitter accepted rsp!)");
        if (thunk != NULL) g_fail++;
    }

    /* (10) sarx differential: dst = value sar (count & (opsize-1)); NO flags touched.
     * Key difference from shrx: arithmetic right shift fills with sign bit.
     * Samples _ai0.._ai5 mirror shrx samples. */
    {
        static const uint64_t vals[] = {
            0, 1, 0xFFFFFFFFull, ~0ull, 0x80000000ull, 0x8000000000000000ull,
            0xDEADBEEFCAFEBABEull, 0x0000000012345678ull,
        };
        static const uint64_t cnts[] = { 0, 1, 7, 31, 32, 63, 64, 0xFF };
        const int NSAMP = 6;   /* _ai0.._ai5 */
        int cases = 0, fails0 = g_fail;
        for (int i = 0; i < NSAMP; i++) {
            decoded d = dec_sarx(i);
            if (!(d.is_bmi && d.op == BMI_SARX)) { printf("  sarx sample %d not sarx\n", i); g_fail++; continue; }
            uint64_t opmask = (d.opsize == 64) ? ~0ull : 0xFFFFFFFFull;
            for (size_t a = 0; a < sizeof(vals)/sizeof(vals[0]); a++) {
                for (size_t b = 0; b < sizeof(cnts)/sizeof(cnts[0]); b++) {
                    uint64_t in[16], out[16]; uint64_t in_fl = 0xAD7, out_fl = 0;
                    seed(in);
                    in[d.a_src] = vals[a]; in[d.b_src] = cnts[b];
                    memset(out, 0xEE, sizeof out);
                    void *thunk = avxemu_emit_minspill_block(&d, resume_target());
                    if (!thunk) { printf("  [sarx s%d] emit NULL\n", i); g_fail++; continue; }
                    avxemu_minspill_run(in, in_fl, thunk, out, &out_fl);

                    uint64_t res = 0, dst2 = 0, rf = in_fl;
                    bmi_exec(BMI_SARX, d.opsize, in[d.a_src], in[d.b_src], &res, &dst2, &rf);

                    int bad = 0;
                    if ((out[d.dst] & opmask) != (res & opmask)) {
                        printf("  [sarx s%d v=%016llx c=%llu] result %016llx != %016llx\n", i,
                               (unsigned long long)in[d.a_src], (unsigned long long)in[d.b_src],
                               (unsigned long long)out[d.dst], (unsigned long long)res); bad = 1;
                    }
                    if (d.opsize == 32 && (out[d.dst] >> 32) != 0) {
                        printf("  [sarx s%d] opsize32 dst not zero-extended: %016llx\n", i, (unsigned long long)out[d.dst]); bad = 1;
                    }
                    /* sarx defines NO flags -> every flag bit preserved */
                    if (out_fl != in_fl) {
                        printf("  [sarx s%d v=%016llx c=%llu] flags changed %llx -> %llx\n", i,
                               (unsigned long long)in[d.a_src], (unsigned long long)in[d.b_src],
                               (unsigned long long)in_fl, (unsigned long long)out_fl); bad = 1;
                    }
                    for (int j = 0; j < 16; j++) {
                        if (j == d.dst) continue;
                        if (j == 4) { if (out[j] != 0) { printf("  [sarx s%d] rsp delta %lld\n", i, (long long)out[j]); bad = 1; } continue; }
                        if (out[j] != in[j]) {
                            printf("  [sarx s%d] gpr%d CLOBBERED %016llx->%016llx\n", i, j,
                                   (unsigned long long)in[j], (unsigned long long)out[j]); bad = 1;
                        }
                    }
                    if (avxemu_minspill_rz8 != RZ_SENTINEL || avxemu_minspill_rz16 != RZ_SENTINEL) {
                        printf("  [sarx s%d] RED ZONE clobbered\n", i); bad = 1;
                    }
                    g_fail += bad; cases++;
                }
            }
        }
        printf("  sarx differential (%d cases): %s\n", cases, (g_fail == fails0) ? "ok" : "FAIL");
    }

    /* (11) sarx dst==rcx fallback (sample _ai6) */
    {
        decoded d = dec_sarx(6);
        int ok = (d.is_bmi && d.op == BMI_SARX && d.dst == 1);
        void *thunk = avxemu_emit_minspill_block(&d, resume_target());
        if (!ok) { printf("  sarx dst==rcx sample _ai6 not as expected\n"); g_fail++; }
        printf("  sarx dst==rcx declined: %s\n", (thunk == NULL) ? "ok" : "FAIL (accepted rcx dst!)");
        if (thunk != NULL) g_fail++;
    }

    /* (12) sarx rsp-operand fallback (sample _ai7) */
    {
        decoded d = dec_sarx(7);
        int ok = (d.is_bmi && d.op == BMI_SARX && d.a_src == 4);
        void *thunk = avxemu_emit_minspill_block(&d, resume_target());
        if (!ok) { printf("  sarx rsp-value sample _ai7 not as expected\n"); g_fail++; }
        printf("  sarx rsp-operand declined: %s\n", (thunk == NULL) ? "ok" : "FAIL (emitter accepted rsp!)");
        if (thunk != NULL) g_fail++;
    }

    /* (13) rorx differential: dst = ror(value, imm & (opsize-1)); NO flags touched.
     * Count is an IMMEDIATE baked into each sample instruction, not a register.
     * Samples _ri0.._ri6: opsize 32/64, imm=0 (no-op), imm=1, imm=opsize-1,
     * imm>opsize-1 (masking test), dst==value, dst==rcx (ACCEPTED for rorx), high regs. */
    {
        static const uint64_t vals[] = {
            0, 1, 0xFFFFFFFFull, ~0ull, 0x80000000ull, 0x8000000000000000ull,
            0xDEADBEEFCAFEBABEull, 0x0000000012345678ull,
        };
        const int NSAMP = 7;   /* _ri0.._ri6 */
        int cases = 0, fails0 = g_fail;
        for (int i = 0; i < NSAMP; i++) {
            decoded d = dec_rorx(i);
            if (!(d.is_bmi && d.op == BMI_RORX)) { printf("  rorx sample %d not rorx\n", i); g_fail++; continue; }
            uint64_t opmask = (d.opsize == 64) ? ~0ull : 0xFFFFFFFFull;
            for (size_t a = 0; a < sizeof(vals)/sizeof(vals[0]); a++) {
                uint64_t in[16], out[16]; uint64_t in_fl = 0xAD7, out_fl = 0;
                seed(in);
                in[d.a_src] = vals[a];
                memset(out, 0xEE, sizeof out);
                void *thunk = avxemu_emit_minspill_block(&d, resume_target());
                if (!thunk) { printf("  [rorx s%d] emit NULL\n", i); g_fail++; continue; }
                avxemu_minspill_run(in, in_fl, thunk, out, &out_fl);

                uint64_t res = 0, dst2 = 0, rf = in_fl;
                bmi_exec(BMI_RORX, d.opsize, in[d.a_src], (uint64_t)d.imm, &res, &dst2, &rf);

                int bad = 0;
                if ((out[d.dst] & opmask) != (res & opmask)) {
                    printf("  [rorx s%d v=%016llx imm=%d] result %016llx != %016llx\n", i,
                           (unsigned long long)in[d.a_src], (int)d.imm,
                           (unsigned long long)out[d.dst], (unsigned long long)res); bad = 1;
                }
                if (d.opsize == 32 && (out[d.dst] >> 32) != 0) {
                    printf("  [rorx s%d] opsize32 dst not zero-extended: %016llx\n", i, (unsigned long long)out[d.dst]); bad = 1;
                }
                /* rorx defines NO flags -> every flag bit preserved */
                if (out_fl != in_fl) {
                    printf("  [rorx s%d v=%016llx imm=%d] flags changed %llx -> %llx\n", i,
                           (unsigned long long)in[d.a_src], (int)d.imm,
                           (unsigned long long)in_fl, (unsigned long long)out_fl); bad = 1;
                }
                for (int j = 0; j < 16; j++) {
                    if (j == d.dst) continue;
                    if (j == 4) { if (out[j] != 0) { printf("  [rorx s%d] rsp delta %lld\n", i, (long long)out[j]); bad = 1; } continue; }
                    if (out[j] != in[j]) {
                        printf("  [rorx s%d] gpr%d CLOBBERED %016llx->%016llx\n", i, j,
                               (unsigned long long)in[j], (unsigned long long)out[j]); bad = 1;
                    }
                }
                if (avxemu_minspill_rz8 != RZ_SENTINEL || avxemu_minspill_rz16 != RZ_SENTINEL) {
                    printf("  [rorx s%d] RED ZONE clobbered\n", i); bad = 1;
                }
                g_fail += bad; cases++;
            }
        }
        printf("  rorx differential (%d cases): %s\n", cases, (g_fail == fails0) ? "ok" : "FAIL");
    }

    /* (14) rorx rsp-operand fallback: `rorxq $1, rsp, rax` (sample _ri7) — rsp
     * as value operand; emitter must DECLINE. */
    {
        decoded d = dec_rorx(7);
        int ok = (d.is_bmi && d.op == BMI_RORX && d.a_src == 4);
        void *thunk = avxemu_emit_minspill_block(&d, resume_target());
        if (!ok) { printf("  rorx rsp-value sample _ri7 not as expected\n"); g_fail++; }
        printf("  rorx rsp-operand declined: %s\n", (thunk == NULL) ? "ok" : "FAIL (emitter accepted rsp!)");
        if (thunk != NULL) g_fail++;
    }

    /* (15) bzhi differential: result + owned flags (CF/ZF/SF/OF) vs bmi_exec; PF/AF
     * preserved; only dst changed (scratch restored, nothing else clobbered); rsp intact.
     * BZHI has REAL CF (from index vs opsize) and REAL SF (sign bit of result).
     * Samples _bhi0.._bhi6 (7 samples). */
    {
        /* Values: 0, 1, opsize-1-all-ones, all-ones, sign-bit-set, high-bits */
        static const uint64_t vals[] = {
            0, 1, 0xFFFFFFFFull, ~0ull, 0x80000000ull, 0x8000000000000000ull,
            0xDEADBEEFCAFEBABEull, 0x0000000012345678ull,
        };
        /* Indices: 0 (ZF=1), 1, opsize-1 (SF possible), opsize (CF=1), opsize+1 (CF=1),
         * large (CF=1), with upper-byte garbage (masked to low 8 bits), typical mid-range. */
        static const uint64_t idxs[] = {
            0, 1, 7, 15, 31, 32, 33, 63, 64, 65, 255,
            0x1200 /* high garbage: &0xFF = 0 */,
            0x1201 /* high garbage: &0xFF = 1 */,
            0x1231 /* high garbage: &0xFF = 0x31 = 49 */,
        };
        const int NSAMP = 7;   /* _bhi0.._bhi6 */
        int cases = 0, fails0 = g_fail;
        for (int i = 0; i < NSAMP; i++) {
            decoded d = dec_bzhi(i);
            if (!(d.is_bmi && d.op == BMI_BZHI)) {
                printf("  bzhi sample %d is not bzhi\n", i); g_fail++; continue;
            }
            uint64_t opmask = (d.opsize == 64) ? ~0ull : 0xFFFFFFFFull;
            for (size_t a = 0; a < sizeof(vals)/sizeof(vals[0]); a++) {
                for (size_t b = 0; b < sizeof(idxs)/sizeof(idxs[0]); b++) {
                    uint64_t vval = vals[a], vidx = idxs[b];
                    uint64_t in[16], out[16]; uint64_t in_fl = 0xAD7, out_fl = 0;
                    seed(in);
                    /* Set value and index — if same register, index wins (last write;
                     * bmi_exec masks index to 0xFF so the test will use vidx's low byte). */
                    in[d.a_src] = vval;
                    in[d.b_src] = vidx;   /* if a_src==b_src, this overwrites value */
                    memset(out, 0xEE, sizeof out);

                    void *thunk = avxemu_emit_minspill_block(&d, resume_target());
                    if (!thunk) { printf("  [bz s%d] emit NULL\n", i); g_fail++; continue; }
                    avxemu_minspill_run(in, in_fl, thunk, out, &out_fl);

                    /* Reference: bmi_exec(BZHI, opsize, value=in[a_src], index=in[b_src]) */
                    uint64_t res = 0, dst2 = 0, rf = in_fl;
                    bmi_exec(BMI_BZHI, d.opsize, in[d.a_src], in[d.b_src], &res, &dst2, &rf);

                    int bad = 0;
                    if ((out[d.dst] & opmask) != (res & opmask)) {
                        printf("  [bz s%d v=%016llx idx=%016llx] result %016llx != %016llx\n",
                               i, (unsigned long long)in[d.a_src], (unsigned long long)in[d.b_src],
                               (unsigned long long)(out[d.dst] & opmask), (unsigned long long)(res & opmask));
                        bad = 1;
                    }
                    if (d.opsize == 32 && (out[d.dst] >> 32) != 0) {
                        printf("  [bz s%d v=%016llx idx=%016llx] opsize32 dst not zero-extended: %016llx\n",
                               i, (unsigned long long)in[d.a_src], (unsigned long long)in[d.b_src],
                               (unsigned long long)out[d.dst]);
                        bad = 1;
                    }
                    if ((out_fl & F_OWNED) != (rf & F_OWNED)) {
                        printf("  [bz s%d v=%016llx idx=%016llx] owned flags %03llx != %03llx\n",
                               i, (unsigned long long)in[d.a_src], (unsigned long long)in[d.b_src],
                               (unsigned long long)(out_fl & F_OWNED), (unsigned long long)(rf & F_OWNED));
                        bad = 1;
                    }
                    if ((out_fl & F_PFAF) != (in_fl & F_PFAF)) {
                        printf("  [bz s%d v=%016llx idx=%016llx] PF/AF changed %03llx -> %03llx\n",
                               i, (unsigned long long)in[d.a_src], (unsigned long long)in[d.b_src],
                               (unsigned long long)(in_fl & F_PFAF), (unsigned long long)(out_fl & F_PFAF));
                        bad = 1;
                    }
                    for (int j = 0; j < 16; j++) {
                        if (j == d.dst) continue;
                        if (j == 4) {
                            if (out[j] != 0) {
                                printf("  [bz s%d] rsp delta %lld != 0\n", i, (long long)out[j]);
                                bad = 1;
                            }
                            continue;
                        }
                        if (out[j] != in[j]) {
                            printf("  [bz s%d v=%016llx idx=%016llx] gpr%d CLOBBERED %016llx->%016llx\n",
                                   i, (unsigned long long)in[d.a_src], (unsigned long long)in[d.b_src],
                                   j, (unsigned long long)in[j], (unsigned long long)out[j]);
                            bad = 1;
                        }
                    }
                    if (avxemu_minspill_rz8 != RZ_SENTINEL || avxemu_minspill_rz16 != RZ_SENTINEL) {
                        printf("  [bz s%d] RED ZONE clobbered: rz8=%016llx rz16=%016llx\n",
                               i, (unsigned long long)avxemu_minspill_rz8,
                               (unsigned long long)avxemu_minspill_rz16);
                        bad = 1;
                    }
                    g_fail += bad; cases++;
                }
            }
        }
        printf("  bzhi differential (%d cases): %s\n", cases, (g_fail == fails0) ? "ok" : "FAIL");
    }

    /* (16) bzhi dst==rcx fallback: `bzhiq %rdx, %r8, %rcx` (sample _bhi7) — rcx is
     * our shift-count scratch; emitter must DECLINE (falls back to slot-based path). */
    {
        decoded d = dec_bzhi(7);
        int ok = (d.is_bmi && d.op == BMI_BZHI && d.dst == 1);
        void *thunk = avxemu_emit_minspill_block(&d, resume_target());
        if (!ok) { printf("  bzhi dst==rcx sample _bhi7 not as expected\n"); g_fail++; }
        printf("  bzhi dst==rcx declined: %s\n", (thunk == NULL) ? "ok" : "FAIL (accepted rcx dst!)");
        if (thunk != NULL) g_fail++;
    }

    /* (17) bzhi value=rsp fallback: `bzhiq %rcx, %rsp, %rax` (sample _bhi8) — rsp
     * as value operand; emitter must DECLINE. */
    {
        decoded d = dec_bzhi(8);
        int ok = (d.is_bmi && d.op == BMI_BZHI && d.a_src == 4);
        void *thunk = avxemu_emit_minspill_block(&d, resume_target());
        if (!ok) { printf("  bzhi value=rsp sample _bhi8 not as expected\n"); g_fail++; }
        printf("  bzhi rsp-operand declined: %s\n", (thunk == NULL) ? "ok" : "FAIL (emitter accepted rsp!)");
        if (thunk != NULL) g_fail++;
    }

    /* (18) BLSR differential: dst = src & (src-1); real CF/ZF/SF/OF; PF/AF preserved.
     * CF=(src==0); ZF=(result==0); SF=sign bit of result; OF=0.
     * Samples _blsr0.._blsr6 (7 samples): opsize32/64, dst==src, dst!=src, high regs. */
    {
        static const uint64_t srcs[] = {
            0, 1, 2, 3,
            0xFFFFFFFFull,      /* 32-bit all-ones: CF=0, ZF=0, SF=0 (result=0xFFFFFFFE) */
            ~0ull,              /* 64-bit all-ones */
            0x80000000ull,      /* 32-bit: result=(0x7FFFFFFF&0x80000000)=0, ZF=1, SF=0 */
            0xC0000000ull,      /* 32-bit: result=0x80000000, SF=1 */
            0x8000000000000000ull, /* 64-bit MSB: result=0, ZF=1 */
            0x8000000000000001ull, /* 64-bit: result=0x8000000000000000, SF=1 */
            0xDEADBEEFull,
            0xDEADBEEFCAFEBABEull,
        };
        const int NSAMP = 7;   /* _blsr0.._blsr6 */
        int cases = 0, fails0 = g_fail;
        for (int i = 0; i < NSAMP; i++) {
            decoded d = dec_blsr(i);
            if (!(d.is_bmi && d.op == BMI_BLSR)) { printf("  blsr sample %d is not BLSR\n", i); g_fail++; continue; }
            uint64_t opmask = (d.opsize == 64) ? ~0ull : 0xFFFFFFFFull;
            for (size_t k = 0; k < sizeof(srcs)/sizeof(srcs[0]); k++) {
                uint64_t v = srcs[k];
                uint64_t in[16], out[16]; uint64_t in_fl = 0xAD7, out_fl = 0;
                seed(in); in[d.a_src] = v; memset(out, 0xEE, sizeof out);

                void *thunk = avxemu_emit_minspill_block(&d, resume_target());
                if (!thunk) { printf("  [blsr s%d] emit returned NULL\n", i); g_fail++; continue; }
                avxemu_minspill_run(in, in_fl, thunk, out, &out_fl);

                uint64_t res = 0, dst2 = 0, rf = in_fl;
                bmi_exec(BMI_BLSR, d.opsize, v, 0, &res, &dst2, &rf);

                int bad = 0;
                if ((out[d.dst] & opmask) != (res & opmask)) {
                    printf("  [blsr s%d v=%016llx] result %016llx != %016llx\n",
                           i, (unsigned long long)v,
                           (unsigned long long)(out[d.dst] & opmask), (unsigned long long)(res & opmask)); bad = 1;
                }
                if (d.opsize == 32 && (out[d.dst] >> 32) != 0) {
                    printf("  [blsr s%d v=%016llx] opsize32 dst not zero-extended: %016llx\n",
                           i, (unsigned long long)v, (unsigned long long)out[d.dst]); bad = 1;
                }
                if ((out_fl & F_OWNED) != (rf & F_OWNED)) {
                    printf("  [blsr s%d v=%016llx] owned flags %03llx != %03llx\n",
                           i, (unsigned long long)v,
                           (unsigned long long)(out_fl & F_OWNED), (unsigned long long)(rf & F_OWNED)); bad = 1;
                }
                if ((out_fl & F_PFAF) != (in_fl & F_PFAF)) {
                    printf("  [blsr s%d v=%016llx] PF/AF changed %03llx -> %03llx\n",
                           i, (unsigned long long)v,
                           (unsigned long long)(in_fl & F_PFAF), (unsigned long long)(out_fl & F_PFAF)); bad = 1;
                }
                for (int j = 0; j < 16; j++) {
                    if (j == d.dst) continue;
                    if (j == 4) { if (out[j] != 0) { printf("  [blsr s%d] rsp delta %lld != 0\n", i, (long long)out[j]); bad = 1; } continue; }
                    if (out[j] != in[j]) {
                        printf("  [blsr s%d v=%016llx] gpr%d CLOBBERED %016llx->%016llx\n",
                               i, (unsigned long long)v, j, (unsigned long long)in[j], (unsigned long long)out[j]); bad = 1;
                    }
                }
                if (avxemu_minspill_rz8 != RZ_SENTINEL || avxemu_minspill_rz16 != RZ_SENTINEL) {
                    printf("  [blsr s%d v=%016llx] RED ZONE clobbered\n", i, (unsigned long long)v); bad = 1;
                }
                g_fail += bad; cases++;
            }
        }
        printf("  blsr differential (%d cases): %s\n", cases, (g_fail == fails0) ? "ok" : "FAIL");
    }

    /* (19) BLSR rsp-operand fallback: `blsrq %rsp, %rax` (sample _blsr7) — emitter must DECLINE. */
    {
        decoded d = dec_blsr(7);
        int ok = (d.is_bmi && d.op == BMI_BLSR && d.a_src == 4);
        void *thunk = avxemu_emit_minspill_block(&d, resume_target());
        if (!ok) { printf("  blsr rsp-src sample _blsr7 not as expected\n"); g_fail++; }
        printf("  blsr rsp-operand declined: %s\n", (thunk == NULL) ? "ok" : "FAIL (emitter accepted rsp!)");
        if (thunk != NULL) g_fail++;
    }

    /* (20) BLSI differential: dst = src & (0-src); real CF/ZF/SF/OF; PF/AF preserved.
     * CF=(src!=0); ZF=(result==0); SF=sign bit of result; OF=0.
     * Samples _blsi0.._blsi6 (7 samples). */
    {
        static const uint64_t srcs[] = {
            0,                  /* CF=0 (src==0), result=0, ZF=1, SF=0 */
            1,                  /* CF=1, result=1, ZF=0, SF=0 */
            2,                  /* CF=1, result=2, ZF=0, SF=0 */
            0xFFFFFFFFull,      /* 32-bit all-ones: result=1 (lowest bit), CF=1 */
            ~0ull,              /* 64-bit all-ones: result=1, CF=1 */
            0x80000000ull,      /* 32-bit: result=0x80000000, SF=1 (only bit), CF=1, ZF=0 */
            0x8000000000000000ull, /* 64-bit MSB only: result=MSB, SF=1 */
            0xC0000000ull,      /* 32-bit: lowest set bit=0x40000000, result=0x40000000 */
            0x8000000000000001ull, /* 64-bit: lowest bit=bit0, result=1 */
            0xDEADBEEFull,
            0xDEADBEEFCAFEBABEull,
        };
        const int NSAMP = 7;   /* _blsi0.._blsi6 */
        int cases = 0, fails0 = g_fail;
        for (int i = 0; i < NSAMP; i++) {
            decoded d = dec_blsi(i);
            if (!(d.is_bmi && d.op == BMI_BLSI)) { printf("  blsi sample %d is not BLSI\n", i); g_fail++; continue; }
            uint64_t opmask = (d.opsize == 64) ? ~0ull : 0xFFFFFFFFull;
            for (size_t k = 0; k < sizeof(srcs)/sizeof(srcs[0]); k++) {
                uint64_t v = srcs[k];
                uint64_t in[16], out[16]; uint64_t in_fl = 0xAD7, out_fl = 0;
                seed(in); in[d.a_src] = v; memset(out, 0xEE, sizeof out);

                void *thunk = avxemu_emit_minspill_block(&d, resume_target());
                if (!thunk) { printf("  [blsi s%d] emit returned NULL\n", i); g_fail++; continue; }
                avxemu_minspill_run(in, in_fl, thunk, out, &out_fl);

                uint64_t res = 0, dst2 = 0, rf = in_fl;
                bmi_exec(BMI_BLSI, d.opsize, v, 0, &res, &dst2, &rf);

                int bad = 0;
                if ((out[d.dst] & opmask) != (res & opmask)) {
                    printf("  [blsi s%d v=%016llx] result %016llx != %016llx\n",
                           i, (unsigned long long)v,
                           (unsigned long long)(out[d.dst] & opmask), (unsigned long long)(res & opmask)); bad = 1;
                }
                if (d.opsize == 32 && (out[d.dst] >> 32) != 0) {
                    printf("  [blsi s%d v=%016llx] opsize32 dst not zero-extended: %016llx\n",
                           i, (unsigned long long)v, (unsigned long long)out[d.dst]); bad = 1;
                }
                if ((out_fl & F_OWNED) != (rf & F_OWNED)) {
                    printf("  [blsi s%d v=%016llx] owned flags %03llx != %03llx\n",
                           i, (unsigned long long)v,
                           (unsigned long long)(out_fl & F_OWNED), (unsigned long long)(rf & F_OWNED)); bad = 1;
                }
                if ((out_fl & F_PFAF) != (in_fl & F_PFAF)) {
                    printf("  [blsi s%d v=%016llx] PF/AF changed %03llx -> %03llx\n",
                           i, (unsigned long long)v,
                           (unsigned long long)(in_fl & F_PFAF), (unsigned long long)(out_fl & F_PFAF)); bad = 1;
                }
                for (int j = 0; j < 16; j++) {
                    if (j == d.dst) continue;
                    if (j == 4) { if (out[j] != 0) { printf("  [blsi s%d] rsp delta %lld != 0\n", i, (long long)out[j]); bad = 1; } continue; }
                    if (out[j] != in[j]) {
                        printf("  [blsi s%d v=%016llx] gpr%d CLOBBERED %016llx->%016llx\n",
                               i, (unsigned long long)v, j, (unsigned long long)in[j], (unsigned long long)out[j]); bad = 1;
                    }
                }
                if (avxemu_minspill_rz8 != RZ_SENTINEL || avxemu_minspill_rz16 != RZ_SENTINEL) {
                    printf("  [blsi s%d v=%016llx] RED ZONE clobbered\n", i, (unsigned long long)v); bad = 1;
                }
                g_fail += bad; cases++;
            }
        }
        printf("  blsi differential (%d cases): %s\n", cases, (g_fail == fails0) ? "ok" : "FAIL");
    }

    /* (21) BLSI rsp-operand fallback: `blsiq %rsp, %rax` (sample _blsi7) — emitter must DECLINE. */
    {
        decoded d = dec_blsi(7);
        int ok = (d.is_bmi && d.op == BMI_BLSI && d.a_src == 4);
        void *thunk = avxemu_emit_minspill_block(&d, resume_target());
        if (!ok) { printf("  blsi rsp-src sample _blsi7 not as expected\n"); g_fail++; }
        printf("  blsi rsp-operand declined: %s\n", (thunk == NULL) ? "ok" : "FAIL (emitter accepted rsp!)");
        if (thunk != NULL) g_fail++;
    }

    /* (22) BLSMSK differential: dst = src ^ (src-1); real CF/ZF/SF/OF; PF/AF preserved.
     * CF=(src==0); ZF=(result==0); SF=sign bit of result; OF=0.
     * Note: for any src, result=(src-1)^src always has all low bits set so ZF is
     * usually 0; bmi_exec is the authoritative reference.
     * Samples _blsm0.._blsm6 (7 samples). */
    {
        static const uint64_t srcs[] = {
            0,                  /* CF=1 (src==0), result=0xFFFF... (32/64 all-ones), SF=1, ZF=0 */
            1,                  /* CF=0, result=(0^1)=1, ZF=0, SF=0 */
            2,                  /* CF=0, result=(1^2)=3, ZF=0, SF=0 */
            0xFFFFFFFFull,      /* 32-bit all-ones: result=(0xFFFFFFFE^0xFFFFFFFF)=1 for 32-bit */
            ~0ull,              /* 64-bit all-ones: result=1 */
            0x80000000ull,      /* 32-bit: result=(0x7FFFFFFF^0x80000000)=0xFFFFFFFF, SF=1 */
            0x8000000000000000ull, /* 64-bit: result=0xFFFFFFFFFFFFFFFF, SF=1 */
            0xC0000000ull,      /* 32-bit: result=(0xBFFFFFFF^0xC0000000)=0x7FFFFFFF, SF=0 */
            0x80000001ull,      /* 32-bit: result=(0x80000000^0x80000001)=1 */
            0xDEADBEEFull,
            0xDEADBEEFCAFEBABEull,
        };
        const int NSAMP = 7;   /* _blsm0.._blsm6 */
        int cases = 0, fails0 = g_fail;
        for (int i = 0; i < NSAMP; i++) {
            decoded d = dec_blsm(i);
            if (!(d.is_bmi && d.op == BMI_BLSMSK)) { printf("  blsm sample %d is not BLSMSK\n", i); g_fail++; continue; }
            uint64_t opmask = (d.opsize == 64) ? ~0ull : 0xFFFFFFFFull;
            for (size_t k = 0; k < sizeof(srcs)/sizeof(srcs[0]); k++) {
                uint64_t v = srcs[k];
                uint64_t in[16], out[16]; uint64_t in_fl = 0xAD7, out_fl = 0;
                seed(in); in[d.a_src] = v; memset(out, 0xEE, sizeof out);

                void *thunk = avxemu_emit_minspill_block(&d, resume_target());
                if (!thunk) { printf("  [blsm s%d] emit returned NULL\n", i); g_fail++; continue; }
                avxemu_minspill_run(in, in_fl, thunk, out, &out_fl);

                uint64_t res = 0, dst2 = 0, rf = in_fl;
                bmi_exec(BMI_BLSMSK, d.opsize, v, 0, &res, &dst2, &rf);

                int bad = 0;
                if ((out[d.dst] & opmask) != (res & opmask)) {
                    printf("  [blsm s%d v=%016llx] result %016llx != %016llx\n",
                           i, (unsigned long long)v,
                           (unsigned long long)(out[d.dst] & opmask), (unsigned long long)(res & opmask)); bad = 1;
                }
                if (d.opsize == 32 && (out[d.dst] >> 32) != 0) {
                    printf("  [blsm s%d v=%016llx] opsize32 dst not zero-extended: %016llx\n",
                           i, (unsigned long long)v, (unsigned long long)out[d.dst]); bad = 1;
                }
                if ((out_fl & F_OWNED) != (rf & F_OWNED)) {
                    printf("  [blsm s%d v=%016llx] owned flags %03llx != %03llx\n",
                           i, (unsigned long long)v,
                           (unsigned long long)(out_fl & F_OWNED), (unsigned long long)(rf & F_OWNED)); bad = 1;
                }
                if ((out_fl & F_PFAF) != (in_fl & F_PFAF)) {
                    printf("  [blsm s%d v=%016llx] PF/AF changed %03llx -> %03llx\n",
                           i, (unsigned long long)v,
                           (unsigned long long)(in_fl & F_PFAF), (unsigned long long)(out_fl & F_PFAF)); bad = 1;
                }
                for (int j = 0; j < 16; j++) {
                    if (j == d.dst) continue;
                    if (j == 4) { if (out[j] != 0) { printf("  [blsm s%d] rsp delta %lld != 0\n", i, (long long)out[j]); bad = 1; } continue; }
                    if (out[j] != in[j]) {
                        printf("  [blsm s%d v=%016llx] gpr%d CLOBBERED %016llx->%016llx\n",
                               i, (unsigned long long)v, j, (unsigned long long)in[j], (unsigned long long)out[j]); bad = 1;
                    }
                }
                if (avxemu_minspill_rz8 != RZ_SENTINEL || avxemu_minspill_rz16 != RZ_SENTINEL) {
                    printf("  [blsm s%d v=%016llx] RED ZONE clobbered\n", i, (unsigned long long)v); bad = 1;
                }
                g_fail += bad; cases++;
            }
        }
        printf("  blsmsk differential (%d cases): %s\n", cases, (g_fail == fails0) ? "ok" : "FAIL");
    }

    /* (23) BLSMSK rsp-operand fallback: `blsmskq %rsp, %rax` (sample _blsm7) — emitter must DECLINE. */
    {
        decoded d = dec_blsm(7);
        int ok = (d.is_bmi && d.op == BMI_BLSMSK && d.a_src == 4);
        void *thunk = avxemu_emit_minspill_block(&d, resume_target());
        if (!ok) { printf("  blsmsk rsp-src sample _blsm7 not as expected\n"); g_fail++; }
        printf("  blsmsk rsp-operand declined: %s\n", (thunk == NULL) ? "ok" : "FAIL (emitter accepted rsp!)");
        if (thunk != NULL) g_fail++;
    }

    /* (24) ANDN differential: dst = ~src1 & src2; CF=0, ZF=(result==0), SF=signbit(result), OF=0.
     * PF/AF preserved; only dst changed (scratch restored, nothing else clobbered); rsp intact.
     * Samples _andn0.._andn9 (10 samples): opsize32/64, basic, src1==src2, high regs,
     * dst==src2, dst==src1.
     * Test covers: result=0 (ZF=1) from src1==src2 and from src1=all-ones; SF=1 from MSB
     * result; src1=0 (result=src2); CF always 0; OF always 0. */
    {
        static const uint64_t s1s[] = {
            0, 1,
            0xFFFFFFFFull,          /* all 32-bit ones -> NOT=0, result=0, ZF=1 */
            ~0ull,                  /* all 64-bit ones -> NOT=0, result=0, ZF=1 */
            0x80000000ull,          /* 32-bit MSB -> NOT=0x7FFFFFFF */
            0x8000000000000000ull,  /* 64-bit MSB */
            0xAAAAAAAAull,          /* alternating bits */
            0x55555555ull,
            0xDEADBEEFull,
            0xDEADBEEFCAFEBABEull,
        };
        static const uint64_t s2s[] = {
            0, 1,
            0xFFFFFFFFull,
            ~0ull,
            0x80000000ull,          /* MSB set -> may give SF=1 if NOT(src1) has MSB set */
            0x8000000000000000ull,
            0xAAAAAAAAull,
            0x55555555ull,
            0xDEADBEEFull,
            0xDEADBEEFCAFEBABEull,
        };
        const int NSAMP = 10;   /* _andn0.._andn9 */
        int cases = 0, fails0 = g_fail;
        for (int i = 0; i < NSAMP; i++) {
            decoded d = dec_andn(i);
            if (!(d.is_bmi && d.op == BMI_ANDN)) {
                printf("  andn sample %d is not ANDN\n", i); g_fail++; continue;
            }
            uint64_t opmask = (d.opsize == 64) ? ~0ull : 0xFFFFFFFFull;
            for (size_t a = 0; a < sizeof(s1s)/sizeof(s1s[0]); a++) {
                for (size_t b = 0; b < sizeof(s2s)/sizeof(s2s[0]); b++) {
                    uint64_t vs1 = s1s[a], vs2 = s2s[b];
                    uint64_t in[16], out[16]; uint64_t in_fl = 0xAD7, out_fl = 0;
                    seed(in);
                    /* Set src1=a_src and src2=b_src. If same register, last write (vs2) wins;
                     * bmi_exec will receive s1=vs2, s2=vs2 -> result=(~vs2)&vs2=0. */
                    in[d.a_src] = vs1;
                    in[d.b_src] = vs2;
                    memset(out, 0xEE, sizeof out);

                    void *thunk = avxemu_emit_minspill_block(&d, resume_target());
                    if (!thunk) { printf("  [andn s%d] emit returned NULL\n", i); g_fail++; continue; }
                    avxemu_minspill_run(in, in_fl, thunk, out, &out_fl);

                    /* Reference: bmi_exec(ANDN, opsize, s1=in[a_src], s2=in[b_src]) */
                    uint64_t res = 0, dst2 = 0, rf = in_fl;
                    bmi_exec(BMI_ANDN, d.opsize, in[d.a_src], in[d.b_src], &res, &dst2, &rf);

                    int bad = 0;
                    if ((out[d.dst] & opmask) != (res & opmask)) {
                        printf("  [andn s%d s1=%016llx s2=%016llx] result %016llx != %016llx\n",
                               i, (unsigned long long)in[d.a_src], (unsigned long long)in[d.b_src],
                               (unsigned long long)(out[d.dst] & opmask), (unsigned long long)(res & opmask));
                        bad = 1;
                    }
                    if (d.opsize == 32 && (out[d.dst] >> 32) != 0) {
                        printf("  [andn s%d] opsize32 dst not zero-extended: %016llx\n",
                               i, (unsigned long long)out[d.dst]);
                        bad = 1;
                    }
                    if ((out_fl & F_OWNED) != (rf & F_OWNED)) {
                        printf("  [andn s%d s1=%016llx s2=%016llx] owned flags %03llx != %03llx\n",
                               i, (unsigned long long)in[d.a_src], (unsigned long long)in[d.b_src],
                               (unsigned long long)(out_fl & F_OWNED), (unsigned long long)(rf & F_OWNED));
                        bad = 1;
                    }
                    /* ANDN must produce CF=0 and OF=0 always */
                    if (out_fl & F_CF) {
                        printf("  [andn s%d s1=%016llx s2=%016llx] CF must be 0 but got flags=%03llx\n",
                               i, (unsigned long long)in[d.a_src], (unsigned long long)in[d.b_src],
                               (unsigned long long)(out_fl & F_OWNED));
                        bad = 1;
                    }
                    if (out_fl & F_OF) {
                        printf("  [andn s%d s1=%016llx s2=%016llx] OF must be 0 but got flags=%03llx\n",
                               i, (unsigned long long)in[d.a_src], (unsigned long long)in[d.b_src],
                               (unsigned long long)(out_fl & F_OWNED));
                        bad = 1;
                    }
                    if ((out_fl & F_PFAF) != (in_fl & F_PFAF)) {
                        printf("  [andn s%d s1=%016llx s2=%016llx] PF/AF changed %03llx -> %03llx\n",
                               i, (unsigned long long)in[d.a_src], (unsigned long long)in[d.b_src],
                               (unsigned long long)(in_fl & F_PFAF), (unsigned long long)(out_fl & F_PFAF));
                        bad = 1;
                    }
                    for (int j = 0; j < 16; j++) {
                        if (j == d.dst) continue;
                        if (j == 4) {
                            if (out[j] != 0) {
                                printf("  [andn s%d] rsp delta %lld != 0\n", i, (long long)out[j]);
                                bad = 1;
                            }
                            continue;
                        }
                        if (out[j] != in[j]) {
                            printf("  [andn s%d s1=%016llx s2=%016llx] gpr%d CLOBBERED %016llx->%016llx\n",
                                   i, (unsigned long long)in[d.a_src], (unsigned long long)in[d.b_src],
                                   j, (unsigned long long)in[j], (unsigned long long)out[j]);
                            bad = 1;
                        }
                    }
                    if (avxemu_minspill_rz8 != RZ_SENTINEL || avxemu_minspill_rz16 != RZ_SENTINEL) {
                        printf("  [andn s%d] RED ZONE clobbered: rz8=%016llx rz16=%016llx\n",
                               i, (unsigned long long)avxemu_minspill_rz8,
                               (unsigned long long)avxemu_minspill_rz16);
                        bad = 1;
                    }
                    g_fail += bad; cases++;
                }
            }
        }
        printf("  andn differential (%d cases): %s\n", cases, (g_fail == fails0) ? "ok" : "FAIL");
    }

    /* (25) ANDN src2=rsp fallback: `andnq %rsp, %rdi, %rax` (sample _andn10) — rsp as
     * src2 operand; emitter must DECLINE (falls back to slot-based path). */
    {
        decoded d = dec_andn(10);
        int ok = (d.is_bmi && d.op == BMI_ANDN && d.b_src == 4);
        void *thunk = avxemu_emit_minspill_block(&d, resume_target());
        if (!ok) { printf("  andn src2=rsp sample _andn10 not as expected\n"); g_fail++; }
        printf("  andn rsp-operand declined: %s\n", (thunk == NULL) ? "ok" : "FAIL (emitter accepted rsp!)");
        if (thunk != NULL) g_fail++;
    }

    /* (26) MULX differential: dhi:dlo = rdx(s1) * src(s2); widening unsigned multiply;
     * NO flags touched. s1 is IMPLICIT rdx (gpr[2]); s2 = gpr[b_src]; dlo=d->dst (low),
     * dhi=d->bmi_dst2 (high). Aliasing coverage is the whole game — {dlo,dhi} against
     * {rax,rdx,src} incl. the swapped case AND dlo==dhi (real-decode forms; hardware
     * writes lo first, hi last, so equal dests keep the HIGH half — SDM pseudocode,
     * matched by emulate_bmi_reg's dst-then-dst2 write order). Assert: dlo/dhi match
     * bmi_exec; ALL flags preserved (out_fl==in_fl); ALL non-{dlo,dhi} GPRs unchanged
     * (ESPECIALLY rax and rdx when they aren't dests); opsize32 upper-32 zero-extended;
     * rsp delta 0; red zone intact. Samples _mx0.._mx15 + _mx19.._mx23 accepted;
     * _mx16.._mx18 decline (rsp operand). */
    {
        static const uint64_t s1v[] = {
            0, 1, 0xFFFFFFFFull, ~0ull, 0x80000000ull, 0x8000000000000000ull,
            0xDEADBEEFull, 0x0123456789ABCDEFull,
        };
        static const uint64_t s2v[] = {
            0, 1, 0xFFFFFFFFull, ~0ull, 0x80000000ull, 0x100000000ull,
            0xCAFEBABEull, 0xFEDCBA9876543210ull,
        };
        const int NSAMP = 24;   /* _mx0.._mx23; 16..18 are the rsp-decline samples */
        int cases = 0, fails0 = g_fail;
        for (int i = 0; i < NSAMP; i++) {
            if (i >= 16 && i <= 18) continue;   /* rsp operands: covered by test (27) */
            decoded d = dec_mulx(i);
            if (!(d.is_bmi && d.op == BMI_MULX && d.bmi_s1_rdx)) {
                printf("  mulx sample %d is not MULX(s1=rdx)\n", i); g_fail++; continue;
            }
            uint64_t opmask = (d.opsize == 64) ? ~0ull : 0xFFFFFFFFull;
            for (size_t a = 0; a < sizeof(s1v)/sizeof(s1v[0]); a++) {
                for (size_t b = 0; b < sizeof(s2v)/sizeof(s2v[0]); b++) {
                    uint64_t in[16], out[16]; uint64_t in_fl = 0xAD7, out_fl = 0;
                    seed(in);
                    /* s1 is implicit rdx (gpr[2]); s2 = gpr[b_src]. Set rdx first, then
                     * src — if src==rdx (b_src==2), s2 wins and s1 reads the same value
                     * (matches hardware: s1 reads live rdx). */
                    in[2] = s1v[a];
                    in[d.b_src] = s2v[b];
                    memset(out, 0xEE, sizeof out);

                    void *thunk = avxemu_emit_minspill_block(&d, resume_target());
                    if (!thunk) { printf("  [mulx s%d] emit returned NULL\n", i); g_fail++; cases++; continue; }
                    avxemu_minspill_run(in, in_fl, thunk, out, &out_fl);

                    /* Reference: bmi_exec(MULX, opsize, s1=in[2], s2=in[b_src], &low, &high) */
                    uint64_t low = 0, high = 0, rf = in_fl;
                    bmi_exec(BMI_MULX, d.opsize, in[2], in[d.b_src], &low, &high, &rf);

                    int bad = 0;
                    /* dlo==dhi: lo is written first, hi last -> the register keeps HIGH */
                    uint64_t exp_lo = (d.dst == d.bmi_dst2) ? high : low;
                    if ((out[d.dst] & opmask) != (exp_lo & opmask)) {
                        printf("  [mulx s%d s1=%016llx s2=%016llx] low(dlo=%d) %016llx != %016llx\n",
                               i, (unsigned long long)in[2], (unsigned long long)in[d.b_src], d.dst,
                               (unsigned long long)(out[d.dst] & opmask), (unsigned long long)(exp_lo & opmask)); bad = 1;
                    }
                    if ((out[d.bmi_dst2] & opmask) != (high & opmask)) {
                        printf("  [mulx s%d s1=%016llx s2=%016llx] high(dhi=%d) %016llx != %016llx\n",
                               i, (unsigned long long)in[2], (unsigned long long)in[d.b_src], d.bmi_dst2,
                               (unsigned long long)(out[d.bmi_dst2] & opmask), (unsigned long long)(high & opmask)); bad = 1;
                    }
                    if (d.opsize == 32) {
                        if ((out[d.dst] >> 32) != 0) {
                            printf("  [mulx s%d] opsize32 dlo not zero-extended: %016llx\n", i, (unsigned long long)out[d.dst]); bad = 1;
                        }
                        if ((out[d.bmi_dst2] >> 32) != 0) {
                            printf("  [mulx s%d] opsize32 dhi not zero-extended: %016llx\n", i, (unsigned long long)out[d.bmi_dst2]); bad = 1;
                        }
                    }
                    /* MULX defines NO flags -> every flag bit preserved */
                    if (out_fl != in_fl) {
                        printf("  [mulx s%d s1=%016llx s2=%016llx] flags changed %llx -> %llx\n",
                               i, (unsigned long long)in[2], (unsigned long long)in[d.b_src],
                               (unsigned long long)in_fl, (unsigned long long)out_fl); bad = 1;
                    }
                    for (int j = 0; j < 16; j++) {
                        if (j == d.dst || j == d.bmi_dst2) continue;   /* both dests legitimately change */
                        if (j == 4) { if (out[j] != 0) { printf("  [mulx s%d] rsp delta %lld != 0\n", i, (long long)out[j]); bad = 1; } continue; }
                        if (out[j] != in[j]) {
                            printf("  [mulx s%d s1=%016llx s2=%016llx] gpr%d CLOBBERED %016llx->%016llx\n",
                                   i, (unsigned long long)in[2], (unsigned long long)in[d.b_src],
                                   j, (unsigned long long)in[j], (unsigned long long)out[j]); bad = 1;
                        }
                    }
                    if (avxemu_minspill_rz8 != RZ_SENTINEL || avxemu_minspill_rz16 != RZ_SENTINEL) {
                        printf("  [mulx s%d] RED ZONE clobbered: rz8=%016llx rz16=%016llx\n",
                               i, (unsigned long long)avxemu_minspill_rz8, (unsigned long long)avxemu_minspill_rz16); bad = 1;
                    }
                    g_fail += bad; cases++;
                }
            }
        }
        printf("  mulx differential (%d cases): %s\n", cases, (g_fail == fails0) ? "ok" : "FAIL");
    }

    /* (27) MULX rsp-operand fallbacks: src=rsp (_mx16), dlo=rsp (_mx17), dhi=rsp
     * (_mx18) — the live-register thunk uses rsp as its working stack; emitter must
     * DECLINE each (falls back to the slot-based path). */
    {
        struct { int idx; const char *what; int is_rsp; } cs[3];
        decoded d16 = dec_mulx(16), d17 = dec_mulx(17), d18 = dec_mulx(18);
        cs[0].idx = 16; cs[0].what = "src=rsp";  cs[0].is_rsp = (d16.b_src == 4);
        cs[1].idx = 17; cs[1].what = "dlo=rsp";  cs[1].is_rsp = (d17.dst == 4);
        cs[2].idx = 18; cs[2].what = "dhi=rsp";  cs[2].is_rsp = (d18.bmi_dst2 == 4);
        decoded ds[3] = { d16, d17, d18 };
        int allok = 1;
        for (int k = 0; k < 3; k++) {
            if (!(ds[k].is_bmi && ds[k].op == BMI_MULX && cs[k].is_rsp)) {
                printf("  mulx %s sample _mx%d not as expected\n", cs[k].what, cs[k].idx); g_fail++; allok = 0;
            }
            void *thunk = avxemu_emit_minspill_block(&ds[k], resume_target());
            if (thunk != NULL) { printf("  mulx %s NOT declined (emitter accepted rsp!)\n", cs[k].what); g_fail++; allok = 0; }
        }
        printf("  mulx rsp-operand declined: %s\n", allok ? "ok" : "FAIL");
    }

    printf("\nMINSPILLTEST TOTAL: %d failure(s)\n", g_fail);
    return g_fail ? 1 : 0;
}
