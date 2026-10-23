#include "regfile.h"
#include <stdio.h>

/* These offsets are the ABI used by the assembly spill and signal probes. */
_Static_assert(offsetof(avxemu_regfile, gpr) == 512, "assembly GPR offset");
_Static_assert(offsetof(avxemu_regfile, rflags) == 640, "assembly flags offset");
_Static_assert(offsetof(avxemu_regfile, rip) == 648, "assembly RIP offset");

#define TEMPLATE(name) \
    extern const unsigned char name##_start[], name##_dispatchptr[], \
        name##_resumeptr[], name##_record[], name##_end[]
TEMPLATE(avxemu_tt);
TEMPLATE(avxemu_ttg);
TEMPLATE(avxemu_tt2);

static int layout(const char *name, const unsigned char *start,
                  const unsigned char *dispatch, const unsigned char *resume,
                  const unsigned char *record, const unsigned char *end,
                  size_t slot, size_t length) {
    uintptr_t base = (uintptr_t)start;
    int valid = (uintptr_t)dispatch - base == slot &&
        (uintptr_t)resume - base == slot + 8 &&
        (uintptr_t)record - base == length &&
        (uintptr_t)end - base == length;
    printf("%s template layout: %s\n", name, valid ? "OK" : "FAIL");
    return !valid;
}

#define CHECK(name, slot, length) \
    layout(#name, name##_start, name##_dispatchptr, name##_resumeptr, \
           name##_record, name##_end, slot, length)

int main(void) {
    /* Preserve the original Mach-O lengths, including pointer-slot alignment. */
    int failed = CHECK(avxemu_tt, 768, 784);
    failed += CHECK(avxemu_ttg, 752, 768);
    failed += CHECK(avxemu_tt2, 496, 512);
    return failed != 0;
}
