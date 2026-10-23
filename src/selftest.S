#include "asm.h"

// e2e_* stubs: load regs, `ud2`, <target insn>, store regs, ret.
// On the target the ud2 traps into the handler, which decodes + emulates the
// following (otherwise-unsupported) instruction. The surrounding loads/stores
// are AVX1 (vmovupd ymm) and run natively. Used by the in-dylib self-test and
// by the host fault-injection test.

AVXEMU_RAW_SECTION
.p2align 4

// vpaddd ymm0 = ymm1 + ymm2   (vector reg-reg: ymm read x2, ymm write)
AVXEMU_FUNCTION(e2e_vpaddd)                 // rdi=src1, rsi=src2, rdx=dst (32-byte buffers)
    vmovdqu (%rdi), %ymm1
    vmovdqu (%rsi), %ymm2
    ud2
    vpaddd %ymm2, %ymm1, %ymm0
    vmovdqu %ymm0, (%rdx)
    vzeroupper
    ret
AVXEMU_END_FUNCTION(e2e_vpaddd)

// vpaddd ymm0 = ymm1 + [rsi]  (memory operand: exercises EA computation)
AVXEMU_FUNCTION(e2e_vpaddd_mem)             // rdi=src1 buf, rsi=mem src, rdx=dst
    vmovdqu (%rdi), %ymm1
    ud2
    vpaddd (%rsi), %ymm1, %ymm0
    vmovdqu %ymm0, (%rdx)
    vzeroupper
    ret
AVXEMU_END_FUNCTION(e2e_vpaddd_mem)

// vfmadd213pd ymm0 = ymm1*ymm0 + ymm2  (FMA: dst is also a source)
AVXEMU_FUNCTION(e2e_fma213pd)               // rdi=src1, rsi=src2, rdx=dst(c) in/out
    vmovupd (%rdi), %ymm1
    vmovupd (%rsi), %ymm2
    vmovupd (%rdx), %ymm0
    ud2
    vfmadd213pd %ymm2, %ymm1, %ymm0
    vmovupd %ymm0, (%rdx)
    vzeroupper
    ret
AVXEMU_END_FUNCTION(e2e_fma213pd)

// shlx (rax = r8 << rcx)   (BMI GPR read/write)
AVXEMU_FUNCTION(e2e_shlx)                   // rdi=value, rsi=count -> rax
    movq %rdi, %r8
    movq %rsi, %rcx
    ud2
    shlxq %rcx, %r8, %rax
    ret
AVXEMU_END_FUNCTION(e2e_shlx)

// mulx (rdx*r8 -> rax lo, *rcx hi)  (two-dest + implicit rdx + memory store)
AVXEMU_FUNCTION(e2e_mulx)                   // rdi=a, rsi=b, rdx=&hi -> rax=lo
    movq %rdx, %rcx
    movq %rdi, %rdx
    movq %rsi, %r8
    ud2
    mulxq %r8, %rax, %r9
    movq %r9, (%rcx)
    ret
AVXEMU_END_FUNCTION(e2e_mulx)

// movbe store: [rsi] <- bswap32(edi)  (memory-destination scalar)
AVXEMU_FUNCTION(e2e_movbe_store)            // rdi=value(32b), rsi=dst ptr
    movl %edi, %eax
    ud2
    movbe %eax, (%rsi)
    ret
AVXEMU_END_FUNCTION(e2e_movbe_store)

// BMI with a memory operand — the class that overflowed __memcpy_chk on the
// target (mem_bytes was mis-sized to 16). Both the a_src-mem (shlx) and
// b_src-mem (andn) paths. rdi=&mem, rsi=2nd operand -> rax.
AVXEMU_FUNCTION(e2e_st_shlx_mem)
    movq %rsi,%rcx
    ud2
    shlxq %rcx,(%rdi),%rax
    ret
AVXEMU_END_FUNCTION(e2e_st_shlx_mem)
AVXEMU_FUNCTION(e2e_st_andn_mem)
    movq %rsi,%r10
    ud2
    andnq (%rdi),%r10,%rax
    ret
AVXEMU_END_FUNCTION(e2e_st_andn_mem)

// Patched-form lzcnt/tzcnt: the exact bytes the in-memory patcher emits (F3
// prefix rewritten to F0/lock). No ud2 — `lock bsr/bsf` //UDs on its own, just
// as the patched instruction does on the target, so this exercises the real
// fault path the patcher relies on. rdi=src -> rax.
AVXEMU_FUNCTION(e2e_patched_lzcnt)
    .byte 0xF0,0x48,0x0F,0xBD,0xC7   // (lock) lzcnt %rdi,%rax
    ret
AVXEMU_END_FUNCTION(e2e_patched_lzcnt)
AVXEMU_FUNCTION(e2e_patched_tzcnt)
    .byte 0xF0,0x48,0x0F,0xBC,0xC7   // (lock) tzcnt %rdi,%rax
    ret
AVXEMU_END_FUNCTION(e2e_patched_tzcnt)

// vpaddd with a 0x67 padding prefix before the VEX instruction
AVXEMU_FUNCTION(e2e_pad_vpaddd)             // rdi=src1, rsi=src2, rdx=dst
    vmovdqu (%rdi), %ymm1
    vmovdqu (%rsi), %ymm2
    ud2
    .byte 0x67
    vpaddd %ymm2, %ymm1, %ymm0
    vmovdqu %ymm0, (%rdx)
    vzeroupper
    ret
AVXEMU_END_FUNCTION(e2e_pad_vpaddd)

AVXEMU_GNU_STACK
