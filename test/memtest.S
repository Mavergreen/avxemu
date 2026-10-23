#include "asm.h"

// Memory-addressing e2e stubs: each loads ymm1, ud2, then a vpaddd whose memory
// operand uses a specific addressing mode, then stores the result. The driver
// points the address at a scratch buffer and checks against ymm1 + mem.
// These are fixed (not copied), so RIP-relative works.

AVXEMU_TEST_SECTION
.p2align 4

// base + disp8 : 16(%rsi)
AVXEMU_FUNCTION(m_disp8)                    // rdi=src1, rsi=buf, rdx=out
    vmovdqu (%rdi), %ymm1
    ud2
    vpaddd 16(%rsi), %ymm1, %ymm0
    vmovdqu %ymm0, (%rdx)
    vzeroupper
    ret
AVXEMU_END_FUNCTION(m_disp8)

// base + disp32 : 0x200(%rsi)
AVXEMU_FUNCTION(m_disp32)                   // rdi=src1, rsi=buf, rdx=out  (mem at buf+0x200)
    vmovdqu (%rdi), %ymm1
    ud2
    vpaddd 0x200(%rsi), %ymm1, %ymm0
    vmovdqu %ymm0, (%rdx)
    vzeroupper
    ret
AVXEMU_END_FUNCTION(m_disp32)

// base + index*scale : (%rsi,%rcx,4)
AVXEMU_FUNCTION(m_sib)                      // rdi=src1, rsi=buf, rdx=out, rcx=index (addr=buf+index*4)
    vmovdqu (%rdi), %ymm1
    ud2
    vpaddd (%rsi,%rcx,4), %ymm1, %ymm0
    vmovdqu %ymm0, (%rdx)
    vzeroupper
    ret
AVXEMU_END_FUNCTION(m_sib)

// RIP-relative : _m_ripdata(%rip)
AVXEMU_FUNCTION(m_riprel)                   // rdi=src1, rsi=out
    vmovdqu (%rdi), %ymm1
    ud2
    vpaddd AVXEMU_SYM(m_ripdata)(%rip), %ymm1, %ymm0
    vmovdqu %ymm0, (%rsi)
    vzeroupper
    ret
AVXEMU_END_FUNCTION(m_riprel)

// high-register base : (%r13)  (REX.B + r13's forced-disp8 encoding)
AVXEMU_FUNCTION(m_r13base)                  // rdi=src1, rsi=buf, rdx=out
    pushq %r13
    vmovdqu (%rdi), %ymm1
    movq %rsi, %r13
    ud2
    vpaddd (%r13), %ymm1, %ymm0
    vmovdqu %ymm0, (%rdx)
    popq %r13
    vzeroupper
    ret
AVXEMU_END_FUNCTION(m_r13base)

// high-register index : (%rsi,%r14,4)  (REX.X)
AVXEMU_FUNCTION(m_r14idx)                   // rdi=src1, rsi=buf, rdx=out, rcx=index
    pushq %r14
    vmovdqu (%rdi), %ymm1
    movq %rcx, %r14
    ud2
    vpaddd (%rsi,%r14,4), %ymm1, %ymm0
    vmovdqu %ymm0, (%rdx)
    popq %r14
    vzeroupper
    ret
AVXEMU_END_FUNCTION(m_r14idx)

// memory-destination EA : vextracti128 to (%rsi)
AVXEMU_FUNCTION(m_extract_store)            // rdi=src(ymm), rsi=buf(out 16 bytes)
    vmovdqu (%rdi), %ymm2
    ud2
    vextracti128 $1, %ymm2, (%rsi)
    vzeroupper
    ret
AVXEMU_END_FUNCTION(m_extract_store)

.data
.p2align 5
.globl AVXEMU_SYM(m_ripdata)
AVXEMU_SYM(m_ripdata): .space 32

AVXEMU_GNU_STACK
