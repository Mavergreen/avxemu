#include "cpu.h"
#include <cpuid.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#ifdef __APPLE__
#include <sys/types.h>
#include <sys/sysctl.h>
#endif

/*
 * The functions that execute cpuid live outside __text, in the section the test stubs use. A
 * test program that links the emulator's core gets avxemu's load-time cpuid pass, which turns
 * every cpuid in __text into a trap answered with the features avxemu advertises; a record-mode
 * feature check asked that way would find BMI on a CPU without it, and record the emulator
 * against itself. The pass never scans this section, so these ask the CPU itself.
 */
#define RAW_CPUID __attribute__((section("__TEXT,__avxemu_test,regular,pure_instructions")))

static uint64_t xgetbv0(void) {
    uint32_t lo, hi;
    __asm__ volatile(".byte 0x0f,0x01,0xd0" : "=a"(lo), "=d"(hi) : "c"(0));
    return ((uint64_t)hi << 32) | lo;
}

RAW_CPUID unsigned cpu_features(void) {
    const char *forced = getenv("AVXEMU_TEST_FEATURES");
    if (forced && *forced) return (unsigned)strtoul(forced, NULL, 16);
    unsigned a, b, c, d, f = 0, max = __get_cpuid_max(0, 0);
    if (max < 1 || !__get_cpuid(1, &a, &b, &c, &d)) return 0;
    int ymm = ((c >> 27) & 1) && ((xgetbv0() & 6) == 6);
    if (ymm && ((c >> 28) & 1)) f |= CPU_AVX;
    if (ymm && ((c >> 12) & 1)) f |= CPU_FMA;
    if (ymm && ((c >> 29) & 1)) f |= CPU_F16C;
    if ((c >> 22) & 1) f |= CPU_MOVBE;
    if (max >= 7) {
        __cpuid_count(7, 0, a, b, c, d);
        if ((b >> 3) & 1) f |= CPU_BMI1;
        if (ymm && ((b >> 5) & 1)) f |= CPU_AVX2;
        if ((b >> 8) & 1) f |= CPU_BMI2;
    }
    if (__get_cpuid(0x80000001u, &a, &b, &c, &d) && ((c >> 5) & 1)) f |= CPU_LZCNT;
    return f;
}

/*
 * Does this CPU have everything avxemu emulates, so that avxemu stays inert on it? A copy, to
 * the bit, of the CPU half of cpu_has_everything() in src/handler.c: FMA, MOVBE and F16C from
 * leaf 1, BMI1, AVX2 and BMI2 from leaf 7, LZCNT from leaf 0x80000001, asked of the raw CPU
 * with no OS (xgetbv) check. Keep the two in step.
 */
RAW_CPUID int cpu_avxemu_capable(void) {
    unsigned a, b, c1, c8, d, b7 = 0, c, x;
    if (!__get_cpuid(1, &a, &b, &c1, &d)) return 0;
    if (__get_cpuid_max(0, 0) >= 7) __cpuid_count(7, 0, a, b7, c, d);
    if (!__get_cpuid(0x80000001u, &a, &x, &c8, &d)) return 0;
    const unsigned l1  = (1u<<12) | (1u<<22) | (1u<<29);   /* FMA, MOVBE, F16C */
    const unsigned l7  = (1u<<3)  | (1u<<5)  | (1u<<8);    /* BMI1, AVX2, BMI2 */
    const unsigned l81 = (1u<<5);                           /* LZCNT */
    return (c1 & l1) == l1 && (b7 & l7) == l7 && (c8 & l81) == l81;
}

int cpu_translated(void) {
    const char *forced = getenv("AVXEMU_TEST_TRANSLATED");
    if (forced && strcmp(forced, "1") == 0) return 1;
    if (forced && strcmp(forced, "0") == 0) return 0;
#ifdef __APPLE__
    int v = 0;
    size_t sz = sizeof v;
    if (sysctlbyname("sysctl.proc_translated", &v, &sz, NULL, 0) == 0 && v == 1) return 1;
#endif
    return 0;
}

const char *cpu_feature_name(unsigned bit) {
    static const char *const names[CPU_NFEATURES] =
        {"avx", "avx2", "fma", "f16c", "bmi1", "bmi2", "lzcnt", "movbe"};
    for (int i = 0; i < CPU_NFEATURES; i++) if (bit == (1u << i)) return names[i];
    return "?";
}

RAW_CPUID void cpu_brand(char out[49]) {
    unsigned r[12] = {0};
    memset(out, 0, 49);
    if (__get_cpuid_max(0x80000000u, 0) < 0x80000004u) { strcpy(out, "unknown"); return; }
    for (unsigned i = 0; i < 3; i++)
        __get_cpuid(0x80000002u + i, &r[4*i], &r[4*i + 1], &r[4*i + 2], &r[4*i + 3]);
    memcpy(out, r, 48);
    char *p = out; while (*p == ' ') p++;
    memmove(out, p, strlen(p) + 1);
}
