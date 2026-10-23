#include "asm.h"
AVXEMU_TEST_SECTION
AVXEMU_FUNCTION(guard_operand)
    vxorps %ymm1, %ymm1, %ymm1
    ud2
    vpaddd (%rdi), %ymm1, %ymm0
    vzeroupper
    ret
AVXEMU_END_FUNCTION(guard_operand)
AVXEMU_GNU_STACK
