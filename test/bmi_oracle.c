/*
 * bmi_oracle.c — differential test of bmi_exec() vs real BMI1/BMI2/LZCNT/MOVBE.
 * The driver is SSE-only; ground truth lives in bmi_hw.c.
 *
 * Values are checked against hardware (intrinsics, or inline asm for ops with
 * no intrinsic). Flag-setting ops also capture hardware RFLAGS and compare the
 * architecturally-defined bits (CF/ZF/SF/OF).
 */
#include "vexops.h"
#include "regs.h"
#include "bmi_hw.h"
#include "refdigest.h"
#include "cpu.h"
#include <stdint.h>
#include <stdio.h>

static uint64_t rng=0xdeadbeef12345ull;
static uint64_t xs(void){uint64_t x=rng;x^=x<<13;x^=x>>7;x^=x<<17;return rng=x;}

/* hardware exec + flag capture for the 3 categories we need */
#define DEFINED (FLAG_CF|FLAG_ZF|FLAG_SF|FLAG_OF)

enum { ANDN, BLSI, BLSR, BLSMSK, BZHI, BEXTR, MULX, PDEP, PEXT, RORX, SHLX, SHRX, SARX, TZCNT, LZCNT, MOVBE, NOPS };
static const char *const names[NOPS] = {"andn","blsi","blsr","blsmsk","bzhi","bextr","mulx","pdep",
    "pext","rorx","shlx","shrx","sarx","tzcnt","lzcnt","movbe"};
static struct ref_op ops[NOPS];
static int rec;

static void feed(int k, uint64_t hv, uint64_t hx, uint64_t ev, uint64_t ex) {
    if (rec) { uint64_t h[2] = {hv, hx}; ref_hw(&ops[k], h, sizeof h); }
    uint64_t e[2] = {ev, ex}; ref_emu(&ops[k], e, sizeof e);
    ref_case(&ops[k], !rec || (hv == ev && hx == ex));
}

int main(int argc, char **argv) {
    rec = ref_init(argc, argv, "bmi_oracle", CPU_BMI1 | CPU_BMI2 | CPU_LZCNT) == REF_RECORD;
    for (int k = 0; k < NOPS; k++) ref_op_init(&ops[k], names[k]);
    const int N = 300000;
    for (int t = 0; t < N; t++) {
        uint64_t a = xs(), b = xs();
        if ((t & 7) == 0) a &= (xs() & 63);
        uint64_t d = 0, d2 = 0, fl, hf = 0, hr = 0, hi = 0;
        const uint64_t CZ = FLAG_CF | FLAG_ZF;

        fl = 0; bmi_exec(BMI_ANDN, 64, a, b, &d, &d2, &fl);   if (rec) hr = bmi_hw_andn(a, b, &hf);
        feed(ANDN, hr, hf & DEFINED, d, fl & DEFINED);
        fl = 0; bmi_exec(BMI_BLSI, 64, a, 0, &d, &d2, &fl);   if (rec) hr = bmi_hw_blsi(a, &hf);
        feed(BLSI, hr, hf & DEFINED, d, fl & DEFINED);
        fl = 0; bmi_exec(BMI_BLSR, 64, a, 0, &d, &d2, &fl);   if (rec) hr = bmi_hw_blsr(a, &hf);
        feed(BLSR, hr, hf & DEFINED, d, fl & DEFINED);
        fl = 0; bmi_exec(BMI_BLSMSK, 64, a, 0, &d, &d2, &fl); if (rec) hr = bmi_hw_blsmsk(a, &hf);
        feed(BLSMSK, hr, hf & DEFINED, d, fl & DEFINED);
        fl = 0; bmi_exec(BMI_BZHI, 64, a, b, &d, &d2, &fl);   if (rec) hr = bmi_hw_bzhi(a, b, &hf);
        feed(BZHI, hr, hf & DEFINED, d, fl & DEFINED);
        fl = 0; bmi_exec(BMI_BEXTR, 64, a, b, &d, &d2, &fl);  if (rec) hr = bmi_hw_bextr(a, b, &hf);
        feed(BEXTR, hr, hf & FLAG_ZF, d, fl & FLAG_ZF);
        fl = 0; bmi_exec(BMI_MULX, 64, a, b, &d, &d2, &fl);   if (rec) hr = bmi_hw_mulx(a, b, &hi);
        feed(MULX, hr, hi, d, d2);
        fl = 0; bmi_exec(BMI_PDEP, 64, a, b, &d, &d2, &fl);   if (rec) hr = bmi_hw_pdep(a, b);
        feed(PDEP, hr, 0, d, 0);
        fl = 0; bmi_exec(BMI_PEXT, 64, a, b, &d, &d2, &fl);   if (rec) hr = bmi_hw_pext(a, b);
        feed(PEXT, hr, 0, d, 0);
        fl = 0; bmi_exec(BMI_RORX, 64, a, 13, &d, &d2, &fl);  if (rec) hr = bmi_hw_rorx13(a);
        feed(RORX, hr, 0, d, 0);
        fl = 0; bmi_exec(BMI_SHLX, 64, a, b, &d, &d2, &fl);   if (rec) hr = bmi_hw_shlx(a, b);
        feed(SHLX, hr, 0, d, 0);
        fl = 0; bmi_exec(BMI_SHRX, 64, a, b, &d, &d2, &fl);   if (rec) hr = bmi_hw_shrx(a, b);
        feed(SHRX, hr, 0, d, 0);
        fl = 0; bmi_exec(BMI_SARX, 64, a, b, &d, &d2, &fl);   if (rec) hr = bmi_hw_sarx(a, b);
        feed(SARX, hr, 0, d, 0);
        fl = 0; bmi_exec(BMI_TZCNT, 64, a, 0, &d, &d2, &fl);  if (rec) hr = bmi_hw_tzcnt(a, &hf);
        feed(TZCNT, hr, hf & CZ, d, fl & CZ);
        fl = 0; bmi_exec(BMI_LZCNT, 64, a, 0, &d, &d2, &fl);  if (rec) hr = bmi_hw_lzcnt(a, &hf);
        feed(LZCNT, hr, hf & CZ, d, fl & CZ);
        fl = 0; bmi_exec(BMI_MOVBE, 64, a, 0, &d, &d2, &fl);  if (rec) hr = bmi_hw_movbe(a);
        feed(MOVBE, hr, 0, d, 0);
    }
    int fails = ref_report(ops, NOPS) + ref_finish();
    printf("BMI: %s (%d failing)\n", fails ? "FAIL" : "all ok", fails);
    return fails ? 1 : 0;
}
