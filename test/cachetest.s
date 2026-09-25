# cachetest.s — code for avxemu's load-time passes to act on. In __text ON
# PURPOSE (unlike the other tests' stubs): the cache records and replays exactly
# what those passes do to the main image's __text.
    .text
    .globl _ct_cpuid7_ebx, _ct_lzcnt, _ct_quadruple
_ct_cpuid7_ebx:                  # uint32_t (void): cpuid leaf 7 EBX -> cpuid pass site
    pushq   %rbx
    movl    $7, %eax
    xorl    %ecx, %ecx
    cpuid
    movl    %ebx, %eax
    popq    %rbx
    ret
_ct_lzcnt:                       # uint32_t (uint32_t): lzcnt -> lzcnt pass site
    lzcntl  %edi, %eax               # 4 bytes, then ret: too short for a trampoline's
    ret                              # jmp, so only the prefix edit makes it fault
                                     # (a 5-byte lzcntq would be trampolined over,
                                     # hiding whether the edit was replayed)
_ct_quadruple:                   # void (uint32_t v[8]): v *= 4, as two vpaddd -> a trampoline
    vmovdqu (%rdi), %ymm0
    vpaddd  %ymm0, %ymm0, %ymm0
    vpaddd  %ymm0, %ymm0, %ymm0
    vmovdqu %ymm0, (%rdi)
    vzeroupper
    ret
