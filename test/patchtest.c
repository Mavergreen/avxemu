/*
 * patchtest.c — end-to-end validation of the patched (F0/lock) lzcnt/tzcnt path.
 *
 *  (1) Decoder: the F0-prefixed bytes the in-memory patcher emits must decode
 *      as LZCNT/TZCNT with the right length and operands.
 *  (2) Fault path: execute those bytes (they #UD on this host) and confirm the
 *      production handler emulates them to the same value real lzcnt/tzcnt give.
 *
 * Built with -mlzcnt so the hardware reference is unambiguous. The handler is
 * installed but auto-patching is disabled (AVXEMU_NOPATCH) — we hand-embed the
 * patched bytes, so the test never rewrites its own text.
 */
#include "decode.h"
#include "patchtest_hw.h"
#include "refdigest.h"
#include "cpu.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

extern void avxemu_force_install(void);
extern int  avxemu_test_ud2;

extern uint64_t e2e_plzcnt64(uint64_t);
extern uint64_t e2e_ptzcnt64(uint64_t);
extern uint32_t e2e_plzcnt32(uint32_t);
extern uint32_t e2e_ptzcnt32(uint32_t);
extern uint64_t e2e_plzcnt_mem(const uint64_t *);

static int test_decode(void){
    struct { uint8_t b[6]; int len; vex_op op; const char *nm; } v[] = {
        {{0xF0,0x48,0x0F,0xBD,0xC7},5,BMI_LZCNT,"lzcnt r64"},
        {{0xF0,0x48,0x0F,0xBC,0xC7},5,BMI_TZCNT,"tzcnt r64"},
        {{0xF0,0x0F,0xBD,0xC7},4,BMI_LZCNT,"lzcnt r32"},
        {{0xF0,0x0F,0xBC,0xC7},4,BMI_TZCNT,"tzcnt r32"},
        {{0xF0,0x48,0x0F,0xBD,0x07},5,BMI_LZCNT,"lzcnt (mem)"},
    };
    int fail=0;
    for(unsigned i=0;i<sizeof v/sizeof v[0];i++){
        decoded d; int len=decode(v[i].b,&d);
        int ok = len==v[i].len && d.op==v[i].op && d.is_bmi;
        if(!ok){ printf("  DECODE FAIL %-12s got op=%d len=%d (want op=%d len=%d)\n",
                 v[i].nm,d.op,len,v[i].op,v[i].len); fail++; }
    }
    printf("decode patched form: %d/%lu ok\n",(int)(sizeof v/sizeof v[0])-fail,sizeof v/sizeof v[0]);
    return fail;
}

static int test_path(int rec) {
    uint64_t inputs[80]; int n = 0;
    inputs[n++] = 0; inputs[n++] = ~0ull; inputs[n++] = 1; inputs[n++] = 0x8000000000000000ull;
    for (int b = 0; b < 64; b++) inputs[n++] = 1ull << b;
    inputs[n++] = 0x00000000FFFFFFFFull; inputs[n++] = 0xFFFFFFFF00000000ull;
    inputs[n++] = 0x0123456789ABCDEFull; inputs[n++] = 0xFEDCBA9876543210ull;
    inputs[n++] = 0x00FF00FF00FF00FFull; inputs[n++] = 0xDEADBEEFull;

    enum { L64, T64, L32, T32, LMEM, NOPS };
    static const char *const names[NOPS] = {"lzcnt64", "tzcnt64", "lzcnt32", "tzcnt32", "lzcnt_mem"};
    struct ref_op ops[NOPS];
    for (int k = 0; k < NOPS; k++) ref_op_init(&ops[k], names[k]);
    for (int i = 0; i < n; i++) {
        uint64_t x = inputs[i]; uint32_t x32 = (uint32_t)x;
        uint64_t emu[NOPS] = { e2e_plzcnt64(x), e2e_ptzcnt64(x), e2e_plzcnt32(x32), e2e_ptzcnt32(x32), e2e_plzcnt_mem(&x) };
        uint64_t hw[NOPS] = {0};
        if (rec) {
            hw[L64] = patch_hw_lzcnt64(x); hw[T64] = patch_hw_tzcnt64(x);
            hw[L32] = patch_hw_lzcnt32(x32); hw[T32] = patch_hw_tzcnt32(x32); hw[LMEM] = patch_hw_lzcnt64(x);
        }
        for (int k = 0; k < NOPS; k++) {
            if (rec) {
                ref_hw(&ops[k], &hw[k], 8);
                if (hw[k] != emu[k])
                    printf("  %s(%016llx) emu=%llu hw=%llu MISMATCH\n", names[k], (unsigned long long)x,
                           (unsigned long long)emu[k], (unsigned long long)hw[k]);
            }
            ref_emu(&ops[k], &emu[k], 8);
            ref_case(&ops[k], !rec || hw[k] == emu[k]);
        }
    }
    printf("fault->emulate patched lzcnt/tzcnt:\n");
    return ref_report(ops, NOPS);
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    int rec = ref_init(argc, argv, "patchtest", CPU_LZCNT | CPU_BMI1) == REF_RECORD;
    setenv("AVXEMU_NOPATCH", "1", 1);
    avxemu_test_ud2 = 0;
    avxemu_force_install();
    printf("== patched (F0/lock) lzcnt/tzcnt ==\n");
    int tot = test_decode() + test_path(rec) + ref_finish();
    printf("\nPATCHTEST TOTAL: %d failure(s)\n", tot);
    return tot ? 1 : 0;
}
