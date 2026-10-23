#include "asm.h"
    .text
    .macro begin name
    AVXEMU_FUNCTION(\name)
    .cfi_startproc
    .endm
    .macro end name
    .cfi_endproc
    AVXEMU_END_FUNCTION(\name)
    .endm

    begin vector_add
    vmovdqu (%rdi), %ymm0
    vmovdqu (%rsi), %ymm1
    vpaddd %ymm1, %ymm0, %ymm0
    vmovdqu %ymm0, (%rdx)
    vzeroupper
    ret
    end vector_add

    begin vector_fma
    vmovups (%rdi), %ymm0
    vmovups (%rsi), %ymm1
    vmovups (%rdx), %ymm2
    vfmadd132ps %ymm1, %ymm2, %ymm0
    vmovups %ymm0, (%rcx)
    vzeroupper
    ret
    end vector_fma

    begin bmi_shift
    xorl %eax, %eax
    stc
    shlx %rsi, %rdi, %rax
    pushfq
    .cfi_adjust_cfa_offset 8
    popq (%rdx)
    .cfi_adjust_cfa_offset -8
    ret
    end bmi_shift

    begin bmi_multiply
    movq %rdx, %r8
    movq %rdi, %rdx
    xorl %eax, %eax
    stc
    mulx %rsi, %rax, %rdi
    movq %rdi, (%r8)
    pushfq
    .cfi_adjust_cfa_offset 8
    popq (%rcx)
    .cfi_adjust_cfa_offset -8
    ret
    end bmi_multiply

    begin lzcnt32
    lzcnt %edi, %eax
    ret
    end lzcnt32

    begin tzcnt32
    tzcnt %edi, %eax
    ret
    end tzcnt32

    begin count_flags
    lzcnt %edi, %eax
    pushfq
    .cfi_adjust_cfa_offset 8
    popq %rax
    .cfi_adjust_cfa_offset -8
    tzcnt %edi, %edx
    pushfq
    .cfi_adjust_cfa_offset 8
    popq (%rsi)
    .cfi_adjust_cfa_offset -8
    ret
    end count_flags
/* Invalid LOCK prefixes must trap for required count-instruction rewriting.
 * Keep these intentional invalid instructions outside runtime discovery. */
AVXEMU_TEST_SECTION
    begin invalid_lock_bsr
    .byte 0xf0, 0x0f, 0xbd, 0xc7
    ret
    end invalid_lock_bsr
    begin invalid_lock_bsf
    .byte 0xf0, 0x0f, 0xbc, 0xc7
    ret
    end invalid_lock_bsf
AVXEMU_GNU_STACK
