# threadtest.s — the hot loop threadtest.c copies into an executable buffer.
#
# Lives in __DATA, not __TEXT, on purpose: the load-time trampoline pass rewrites
# faulting runs in the main image's __text, and we want the RUNTIME path
# (SIGILL -> hot_bump -> avxemu_relocate_block) to be the one that patches it.
#
#   void body(uint64_t iters /*rdi*/, uint32_t out[9] /*rsi*/)
#     out[0..7] = iters (each dword lane of ymm0), out[8] = iters (eax)
#
# The faulting op is 4 bytes (VEX.256 vpsubd, #UD without AVX2) followed by a
# 3-byte legal op, so the relocation window is [site, site+7) and the 5-byte jmp
# covers an INTERIOR instruction boundary at site+4 — the case a thread that
# was just emulated (rip_next = site+4) can be sitting on when the patch lands.
# Everything else is AVX1, native on the target CPU.

    .section __DATA,__data
    .globl _tt_body_start, _tt_body_site, _tt_body_end
    .p2align 4
_tt_body_start:
    vpxor       %xmm0, %xmm0, %xmm0          # ymm0 = 0 (VEX.128 zeroes the upper half)
    vpcmpeqd    %xmm1, %xmm1, %xmm1
    vinsertf128 $1, %xmm1, %ymm1, %ymm1      # ymm1 = all ones (AVX1)
    xorl        %eax, %eax
    movq        %rdi, %rcx
_tt_body_site:
    .byte 0xC5, 0xFD, 0xFA, 0xC1             # vpsubd %ymm1, %ymm0, %ymm0  (AVX2: ymm0 += 1 per lane)
    addl        $1, %eax                     # 3 bytes: window interior boundary at site+4
    decq        %rcx
    jnz         _tt_body_site
    vmovdqu     %ymm0, (%rsi)
    movl        %eax, 32(%rsi)
    vzeroupper
    ret
_tt_body_end:
