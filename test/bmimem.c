/*
 * bmimem.c — BMI-with-memory-operand emulation vs native hardware.
 *
 * The reg-only fuzzer never exercises memory operands, so a mis-sized memory
 * load (mem_bytes defaulting to 16/32 instead of the GPR width) went unnoticed
 * until the real target aborted in __memcpy_chk. Each op is run both emulated
 * (ud2-injected) and native, over several inputs; results must match exactly.
 */
#include "refdigest.h"
#include "cpu.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern int avxemu_test_ud2;
extern void avxemu_force_install(void);

#define DECL(n) extern uint64_t e2e_##n(const void*, uint64_t); extern uint64_t nat_##n(const void*, uint64_t);
DECL(shlx_mem) DECL(sarx_mem) DECL(shrx_mem) DECL(bextr_mem) DECL(bzhi_mem)
DECL(blsr_mem) DECL(rorx_mem) DECL(shlx_mem32)
DECL(andn_mem) DECL(mulx_mem) DECL(pdep_mem) DECL(pext_mem)

static const uint64_t vals[] = {
    0x0000000000000000ull, 0x0000000000000001ull, 0xFFFFFFFFFFFFFFFFull,
    0x0123456789ABCDEFull, 0x8000000000000000ull, 0x00000000DEADBEEFull,
    0xF0F0F0F0F0F0F0F0ull, 0x00FF00FF00FF00FFull, 0x0000000000000100ull,
};
static const uint64_t args[] = { 0, 1, 7, 12, 31, 32, 63, 0x1020ull, 0xDEADBEEFull };

#define NOPS 12
static struct ref_op ops[NOPS];
static int nops, rec;

#define CHECK(n) do { \
    struct ref_op *o = &ops[nops]; ref_op_init(o, #n); nops++; \
    for (unsigned i = 0; i < sizeof vals / sizeof *vals; i++) \
      for (unsigned j = 0; j < sizeof args / sizeof *args; j++) { \
        uint64_t m = vals[i]; uint64_t e = e2e_##n(&m, args[j]); int ok = 1; \
        if (rec) { uint64_t r = nat_##n(&m, args[j]); ref_hw(o, &r, 8); ok = (e == r); \
          if (!ok) printf("  %-12s mem=%016llx arg=%llx emu=%016llx nat=%016llx MISMATCH\n", #n, \
            (unsigned long long)m, (unsigned long long)args[j], (unsigned long long)e, (unsigned long long)r); } \
        ref_emu(o, &e, 8); ref_case(o, ok); } } while (0)

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    rec = ref_init(argc, argv, "bmimem", CPU_BMI1 | CPU_BMI2) == REF_RECORD;
    avxemu_force_install();
    avxemu_test_ud2 = 1;
    printf("== BMI with memory operand: emulated vs native ==\n");
    CHECK(shlx_mem); CHECK(sarx_mem); CHECK(shrx_mem); CHECK(bextr_mem); CHECK(bzhi_mem);
    CHECK(blsr_mem); CHECK(rorx_mem); CHECK(shlx_mem32);
    CHECK(andn_mem); CHECK(mulx_mem); CHECK(pdep_mem); CHECK(pext_mem);
    int fail = ref_report(ops, nops) + ref_finish();
    printf("\nBMIMEM TOTAL: %d failure(s)\n", fail);
    return fail ? 1 : 0;
}
