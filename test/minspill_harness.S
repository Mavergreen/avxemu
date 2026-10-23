#include "asm.h"

// minspill_harness.S — drive a MINIMAL-SPILL live-register thunk with a known
// GPR+flags state and capture the state it produces.
//
// Unlike tramp_harness.S (full tt2 spill, regfile-slot operands), a minimal-spill
// thunk runs on the LIVE program registers in place: it is entered by a `jmp` and
// ends with a `jmp resume`, having saved only the scratch it borrows. To unit-test
// it we load arbitrary live GPR/flags state, `jmp` the thunk, and at a FIXED global
// resume label (_avxemu_minspill_capture) record every live GPR + rflags.
//
// Signature (5 args; the resume target is the fixed global label below, whose
// address the C driver bakes into the thunk as `resume` — a small, documented
// deviation from the plan's 6-arg form, matching tramp_harness's proven pattern):
//   void avxemu_minspill_run(const uint64_t in_gpr[16], uint64_t in_rflags,
//                            void *thunk_entry,
//                            uint64_t out_gpr[16], uint64_t *out_rflags);
//   rdi=in_gpr  rsi=in_rflags  rdx=thunk_entry  rcx=out_gpr  r8=out_rflags
//
// out_gpr[4] (the rsp slot) is NOT a captured register value: it holds the rsp
// DELTA across the thunk (0 == the thunk balanced its push/pops). in_gpr[4] is
// never loaded (rsp stays harness-owned throughout). All other slots round-trip.

.data
.p2align 8
g_out_gpr:    .quad 0     // out_gpr base, stashed across the test-state load
g_out_rflags: .quad 0     // out_rflags pointer
g_thunk:      .quad 0     // thunk entry, jmp'd after state load
g_scratch:    .quad 0     // saves the live rax at capture entry
g_rsp_save:   .quad 0     // rsp recorded just before `jmp thunk`
g_rsp_atcap:  .quad 0     // rsp recorded at capture entry (delta source)
g_flags_tmp:  .quad 0     // thunk's output rflags, captured before any flag-clobber
// Red-zone sentinels: the harness writes a known value just below rsp before
// jmp'ing the thunk; the capture snapshots those same bytes. A thunk that uses
// the stack WITHOUT first stepping past the 128-byte red zone overwrites them.
.globl AVXEMU_SYM(avxemu_minspill_rz8)
.globl AVXEMU_SYM(avxemu_minspill_rz16)
AVXEMU_SYM(avxemu_minspill_rz8):  .quad 0   // snapshot of [rsp_at_jmp - 8]  after the thunk
AVXEMU_SYM(avxemu_minspill_rz16): .quad 0   // snapshot of [rsp_at_jmp - 16] after the thunk

.text
.p2align 4
AVXEMU_FUNCTION(avxemu_minspill_run)
    pushq %rbx; pushq %rbp; pushq %r12; pushq %r13; pushq %r14; pushq %r15
    movq  %rcx, g_out_gpr(%rip)
    movq  %r8,  g_out_rflags(%rip)
    movq  %rdx, g_thunk(%rip)

    // Load flags from in_rflags (still in rsi) BEFORE clobbering rsi below.
    pushq %rsi
    popfq

    // Stage red-zone sentinels at [rsp-8]/[rsp-16] (rsp is unchanged from here to
    // the jmp — only movs follow). rax is free (not yet loaded with test state).
    // movabs/mov do not affect flags, so the rflags loaded above survive.
    movabsq $0x5A5A5A5A5A5A5A5A, %rax
    movq    %rax, -8(%rsp)
    movq    %rax, -16(%rsp)

    // Load 16 live GPRs from in_gpr (rdi). Skip slot 4 (rsp, harness-owned).
    // mov does not affect flags, so the rflags loaded above survive. rdi last.
    movq  0*8(%rdi),  %rax
    movq  1*8(%rdi),  %rcx
    movq  2*8(%rdi),  %rdx
    movq  3*8(%rdi),  %rbx
    movq  5*8(%rdi),  %rbp
    movq  6*8(%rdi),  %rsi
    movq  8*8(%rdi),  %r8
    movq  9*8(%rdi),  %r9
    movq  10*8(%rdi), %r10
    movq  11*8(%rdi), %r11
    movq  12*8(%rdi), %r12
    movq  13*8(%rdi), %r13
    movq  14*8(%rdi), %r14
    movq  15*8(%rdi), %r15
    movq  7*8(%rdi),  %rdi        // rdi last

    movq  %rsp, g_rsp_save(%rip)  // mov: no flag effect
    jmp   *g_thunk(%rip)
AVXEMU_END_FUNCTION(avxemu_minspill_run)

AVXEMU_FUNCTION(avxemu_minspill_capture)        // the thunk `jmp`s here with its post-run live state
    movq  %rax, g_scratch(%rip)  // save live rax (mov: flags intact)
    movq  %rsp, g_rsp_atcap(%rip)
    // Snapshot the red-zone sentinels BEFORE pushfq (which writes [rsp-8]).
    // All movs — no flag effect, so the thunk's output flags survive to pushfq.
    movq  g_rsp_save(%rip), %rax
    movq  -8(%rax), %rax
    movq  %rax, AVXEMU_SYM(avxemu_minspill_rz8)(%rip)
    movq  g_rsp_save(%rip), %rax
    movq  -16(%rax), %rax
    movq  %rax, AVXEMU_SYM(avxemu_minspill_rz16)(%rip)
    pushfq                        // capture the thunk's output flags
    popq  %rax
    movq  %rax, g_flags_tmp(%rip)
    // From here flag clobbering is fine.
    movq  g_out_gpr(%rip), %rax   // rax = out base (original rax already saved)
    movq  %rcx, 1*8(%rax)
    movq  %rdx, 2*8(%rax)
    movq  %rbx, 3*8(%rax)
    movq  %rbp, 5*8(%rax)
    movq  %rsi, 6*8(%rax)
    movq  %rdi, 7*8(%rax)
    movq  %r8,  8*8(%rax)
    movq  %r9,  9*8(%rax)
    movq  %r10, 10*8(%rax)
    movq  %r11, 11*8(%rax)
    movq  %r12, 12*8(%rax)
    movq  %r13, 13*8(%rax)
    movq  %r14, 14*8(%rax)
    movq  %r15, 15*8(%rax)
    movq  g_scratch(%rip), %rcx   // original rax
    movq  %rcx, 0*8(%rax)
    // rsp delta -> slot 4 (0 == balanced)
    movq  g_rsp_atcap(%rip), %rcx
    subq  g_rsp_save(%rip), %rcx
    movq  %rcx, 4*8(%rax)
    // rflags -> *out_rflags
    movq  g_out_rflags(%rip), %rcx
    movq  g_flags_tmp(%rip), %rdx
    movq  %rdx, (%rcx)

    popq %r15; popq %r14; popq %r13; popq %r12; popq %rbp; popq %rbx
    ret
AVXEMU_END_FUNCTION(avxemu_minspill_capture)

// Sample LZCNT instructions (never executed — the host may lack LZCNT; these are
// decoded as bytes to obtain `decoded` structs, exactly like tramp_harness's
// _ti samples). Coverage: opsize 32/64, dst!=src and dst==src, low and high regs.
.text
.p2align 4
AVXEMU_SYM(mi0): lzcntl %edi, %eax       // 32  src=rdi(7) dst=rax(0)  dst!=src
AVXEMU_SYM(mi1): lzcntl %eax, %eax       // 32  dst==src (rax)
AVXEMU_SYM(mi2): lzcntq %rdi, %rax       // 64  src=rdi(7) dst=rax(0)  dst!=src
AVXEMU_SYM(mi3): lzcntq %rax, %rax       // 64  dst==src (rax)
AVXEMU_SYM(mi4): lzcntl %r9d, %r8d       // 32  src=r9(9) dst=r8(8)    high regs
AVXEMU_SYM(mi5): lzcntq %r15, %r14       // 64  src=r15   dst=r14       high regs
AVXEMU_SYM(mi6): lzcntl %ecx, %edx       // 32  src=rcx(1) dst=rdx(2)
AVXEMU_SYM(mi7): lzcntq %rsp, %rax       // 64  src=rsp(4) -> emitter must DECLINE (fall back)
AVXEMU_SYM(mi8):

// shlx samples (AT&T: shlx count, value, dst -> dst = value << count).
AVXEMU_SYM(si0): shlxl %ecx, %edi, %eax  // 32  dst=rax(0) value=rdi(7) count=rcx(1)
AVXEMU_SYM(si1): shlxq %rcx, %rdi, %rax  // 64  dst=rax value=rdi count=rcx
AVXEMU_SYM(si2): shlxl %edx, %eax, %eax  // 32  dst==value (rax), count=rdx(2)
AVXEMU_SYM(si3): shlxq %r9, %r10, %r11   // 64  high regs  dst=r11 value=r10 count=r9
AVXEMU_SYM(si4): shlxl %edi, %edi, %esi  // 32  count==value (rdi), dst=rsi(6)
AVXEMU_SYM(si5): shlxq %rax, %rax, %rdx  // 64  count==dst? no: dst=rdx value=rax count=rax (count==value)
AVXEMU_SYM(si6): shlxq %rdx, %r8, %rcx   // 64  dst==rcx(1) -> emitter must DECLINE (rcx is CL scratch)
AVXEMU_SYM(si7):

// Sample TZCNT instructions (never executed — the host may lack TZCNT; decoded as
// bytes to obtain `decoded` structs). Coverage mirrors lzcnt: opsize 32/64,
// dst!=src and dst==src, low and high regs.
AVXEMU_SYM(zi0): tzcntl %edi, %eax       // 32  src=rdi(7) dst=rax(0)  dst!=src
AVXEMU_SYM(zi1): tzcntl %eax, %eax       // 32  dst==src (rax)
AVXEMU_SYM(zi2): tzcntq %rdi, %rax       // 64  src=rdi(7) dst=rax(0)  dst!=src
AVXEMU_SYM(zi3): tzcntq %rax, %rax       // 64  dst==src (rax)
AVXEMU_SYM(zi4): tzcntl %r9d, %r8d       // 32  src=r9(9) dst=r8(8)    high regs
AVXEMU_SYM(zi5): tzcntq %r15, %r14       // 64  src=r15   dst=r14       high regs
AVXEMU_SYM(zi6): tzcntl %ecx, %edx       // 32  src=rcx(1) dst=rdx(2)
AVXEMU_SYM(zi7): tzcntq %rsp, %rax       // 64  src=rsp(4) -> emitter must DECLINE (fall back)
AVXEMU_SYM(zi8):

// Sample SHRX instructions (never executed; decoded as bytes for `decoded` structs).
// Coverage: opsize 32/64, dst!=value, dst==value, dst==count, count==value, high regs.
AVXEMU_SYM(xi0): shrxl %ecx, %edi, %eax   // 32  dst=rax(0) value=rdi(7) count=rcx(1)
AVXEMU_SYM(xi1): shrxq %rcx, %rdi, %rax   // 64  dst=rax(0) value=rdi(7) count=rcx(1)
AVXEMU_SYM(xi2): shrxl %edx, %eax, %eax   // 32  dst==value (rax), count=rdx(2)
AVXEMU_SYM(xi3): shrxq %r11, %r10, %r11   // 64  high regs, dst==count (r11), value=r10
AVXEMU_SYM(xi4): shrxl %edi, %edi, %esi   // 32  count==value (rdi), dst=rsi(6)
AVXEMU_SYM(xi5): shrxq %rax, %rax, %rdx   // 64  count==value (rax), dst=rdx(2)
AVXEMU_SYM(xi6): shrxq %rdx, %r8, %rcx    // 64  dst==rcx(1) -> emitter must DECLINE
AVXEMU_SYM(xi7): shrxq %rcx, %rsp, %rax   // 64  value=rsp(4) -> emitter must DECLINE
AVXEMU_SYM(xi8):

// Sample SARX instructions (never executed; decoded as bytes for `decoded` structs).
// Coverage mirrors shrx: opsize 32/64, aliasing variants, high regs, rsp/rcx declines.
AVXEMU_SYM(ai0): sarxl %ecx, %edi, %eax   // 32  dst=rax(0) value=rdi(7) count=rcx(1)
AVXEMU_SYM(ai1): sarxq %rcx, %rdi, %rax   // 64  dst=rax(0) value=rdi(7) count=rcx(1)
AVXEMU_SYM(ai2): sarxl %edx, %eax, %eax   // 32  dst==value (rax), count=rdx(2)
AVXEMU_SYM(ai3): sarxq %r11, %r10, %r11   // 64  high regs, dst==count (r11), value=r10
AVXEMU_SYM(ai4): sarxl %edi, %edi, %esi   // 32  count==value (rdi), dst=rsi(6)
AVXEMU_SYM(ai5): sarxq %rax, %rax, %rdx   // 64  count==value (rax), dst=rdx(2)
AVXEMU_SYM(ai6): sarxq %rdx, %r8, %rcx    // 64  dst==rcx(1) -> emitter must DECLINE
AVXEMU_SYM(ai7): sarxq %rcx, %rsp, %rax   // 64  value=rsp(4) -> emitter must DECLINE
AVXEMU_SYM(ai8):

// Sample RORX instructions (never executed; decoded as bytes for `decoded` structs).
// Coverage: opsize 32/64, imm=0, imm=1, imm=opsize-1, imm>opsize-1 (masking),
// dst==value, dst==rcx (ACCEPTED for rorx — no CL needed), high regs, rsp decline.
AVXEMU_SYM(ri0): rorxl $1, %edi, %eax     // 32  imm=1  value=rdi(7) dst=rax(0)
AVXEMU_SYM(ri1): rorxq $1, %rdi, %rax     // 64  imm=1  value=rdi(7) dst=rax(0)
AVXEMU_SYM(ri2): rorxl $0, %edi, %esi     // 32  imm=0 (no-op rotation)
AVXEMU_SYM(ri3): rorxq $63, %r10, %r11    // 64  high regs, imm=63=opsize64-1
AVXEMU_SYM(ri4): rorxl $31, %edi, %esi    // 32  imm=31=opsize32-1
AVXEMU_SYM(ri5): rorxl $5, %eax, %eax     // 32  dst==value (rax), imm=5
AVXEMU_SYM(ri6): rorxl $33, %eax, %ecx    // 32  imm=33 -> n=33&31=1 (masking), dst=rcx(1) [ACCEPTED]
AVXEMU_SYM(ri7): rorxq $1, %rsp, %rax     // 64  value=rsp(4) -> emitter must DECLINE
AVXEMU_SYM(ri8):

// Sample BZHI instructions (never executed on no-BMI2 host; decoded as bytes for
// `decoded` structs). AT&T syntax: bzhi index, value, dst (VEX vvvv=index, rm=value).
// In decoded: a_src=value(rm), b_src=index(vvvv).
// Coverage: opsize 32/64, dst==value, value==index, dst==index, high regs, rsp/rcx declines.
AVXEMU_SYM(bhi0): bzhil %ecx, %edi, %eax      // 32: dst=rax(0), value=rdi(7), index=rcx(1)
AVXEMU_SYM(bhi1): bzhiq %rcx, %rdi, %rax      // 64: dst=rax(0), value=rdi(7), index=rcx(1)
AVXEMU_SYM(bhi2): bzhil %edx, %eax, %eax      // 32: dst==value (rax), index=rdx(2)
AVXEMU_SYM(bhi3): bzhiq %rdx, %rax, %rax      // 64: dst==value (rax), index=rdx(2)
AVXEMU_SYM(bhi4): bzhiq %rdi, %rdi, %rsi      // 64: value==index (rdi=7), dst=rsi(6)
AVXEMU_SYM(bhi5): bzhil %r11d, %r10d, %r11d   // 32: high regs, dst==index (r11=11), value=r10(10)
AVXEMU_SYM(bhi6): bzhiq %r9, %r10, %r11       // 64: high regs, dst=r11(11), value=r10(10), index=r9(9)
AVXEMU_SYM(bhi7): bzhiq %rdx, %r8, %rcx       // 64: dst==rcx(1) -> emitter must DECLINE
AVXEMU_SYM(bhi8): bzhiq %rcx, %rsp, %rax      // 64: value=rsp(4) -> emitter must DECLINE
AVXEMU_SYM(bhi9):

// Sample BLSR instructions (never executed; decoded as bytes for `decoded` structs).
// AT&T syntax: blsrl/blsrq src, dst  (src=rm=a_src, dst=vvvv=d->dst)
// Coverage: opsize 32/64, dst==src, high regs, rsp-src decline.
// BLSR: dst = src & (src-1)  CF=(src==0)  ZF=(result==0)  SF=signbit(result)  OF=0
AVXEMU_SYM(blsr0): blsrl %edi, %eax          // 32: dst=rax(0), src=rdi(7), dst!=src
AVXEMU_SYM(blsr1): blsrq %rdi, %rax          // 64: dst=rax(0), src=rdi(7)
AVXEMU_SYM(blsr2): blsrl %eax, %eax          // 32: dst==src (rax)
AVXEMU_SYM(blsr3): blsrq %rax, %rax          // 64: dst==src
AVXEMU_SYM(blsr4): blsrl %r9d, %r8d          // 32: high regs, src=r9(9), dst=r8(8)
AVXEMU_SYM(blsr5): blsrq %r15, %r14          // 64: high regs
AVXEMU_SYM(blsr6): blsrl %ecx, %edx          // 32: src=rcx(1), dst=rdx(2) — rcx is a src, not a constraint
AVXEMU_SYM(blsr7): blsrq %rsp, %rax          // 64: src=rsp(4) -> emitter must DECLINE (fall back)
AVXEMU_SYM(blsr8):

// Sample BLSI instructions (never executed; decoded as bytes).
// BLSI: dst = src & (0-src)  CF=(src!=0)  ZF=(result==0)  SF=signbit(result)  OF=0
AVXEMU_SYM(blsi0): blsil %edi, %eax          // 32: dst=rax(0), src=rdi(7), dst!=src
AVXEMU_SYM(blsi1): blsiq %rdi, %rax          // 64: dst=rax(0), src=rdi(7)
AVXEMU_SYM(blsi2): blsil %eax, %eax          // 32: dst==src (rax)
AVXEMU_SYM(blsi3): blsiq %rax, %rax          // 64: dst==src
AVXEMU_SYM(blsi4): blsil %r9d, %r8d          // 32: high regs, src=r9(9), dst=r8(8)
AVXEMU_SYM(blsi5): blsiq %r15, %r14          // 64: high regs
AVXEMU_SYM(blsi6): blsil %ecx, %edx          // 32: src=rcx(1), dst=rdx(2)
AVXEMU_SYM(blsi7): blsiq %rsp, %rax          // 64: src=rsp(4) -> emitter must DECLINE (fall back)
AVXEMU_SYM(blsi8):

// Sample BLSMSK instructions (never executed; decoded as bytes).
// BLSMSK: dst = src ^ (src-1)  CF=(src==0)  ZF=(result==0)  SF=signbit(result)  OF=0
AVXEMU_SYM(blsm0): blsmskl %edi, %eax        // 32: dst=rax(0), src=rdi(7), dst!=src
AVXEMU_SYM(blsm1): blsmskq %rdi, %rax        // 64: dst=rax(0), src=rdi(7)
AVXEMU_SYM(blsm2): blsmskl %eax, %eax        // 32: dst==src (rax)
AVXEMU_SYM(blsm3): blsmskq %rax, %rax        // 64: dst==src
AVXEMU_SYM(blsm4): blsmskl %r9d, %r8d        // 32: high regs, src=r9(9), dst=r8(8)
AVXEMU_SYM(blsm5): blsmskq %r15, %r14        // 64: high regs
AVXEMU_SYM(blsm6): blsmskl %ecx, %edx        // 32: src=rcx(1), dst=rdx(2)
AVXEMU_SYM(blsm7): blsmskq %rsp, %rax        // 64: src=rsp(4) -> emitter must DECLINE (fall back)
AVXEMU_SYM(blsm8):

// Sample ANDN instructions (never executed; decoded as bytes for `decoded` structs).
// ANDN: dst = ~src1 & src2  CF=0  ZF=(result==0)  SF=signbit(result)  OF=0
// AT&T syntax: andnl src2(b_src/rm), src1(a_src/vvvv), dst(reg)
// In decoded: a_src=src1(VEX.vvvv), b_src=src2(ModRM.rm), dst=ModRM.reg
// Semantics: result = (~a_src) & b_src  (NOT src1, then AND with src2)
AVXEMU_SYM(andn0):  andnl %edi, %eax, %ecx       // 32: dst=rcx(1) src1=rax(0) src2=rdi(7)  basic dst!=src1!=src2
AVXEMU_SYM(andn1):  andnq %rdi, %rax, %rcx       // 64: same regs as _andn0
AVXEMU_SYM(andn2):  andnl %eax, %eax, %edx       // 32: src1==src2(rax), dst=rdx(2) -> result always 0, ZF=1
AVXEMU_SYM(andn3):  andnq %rax, %rax, %rdx       // 64: src1==src2(rax), dst=rdx(2)
AVXEMU_SYM(andn4):  andnl %r9d, %r10d, %r8d      // 32: high regs dst=r8(8) src1=r10(10) src2=r9(9)
AVXEMU_SYM(andn5):  andnq %r9, %r10, %r11        // 64: high regs dst=r11(11) src1=r10(10) src2=r9(9)
AVXEMU_SYM(andn6):  andnl %eax, %edi, %eax       // 32: dst==src2(rax) src1=rdi(7); aliasing safety
AVXEMU_SYM(andn7):  andnq %rax, %rdi, %rax       // 64: dst==src2(rax) src1=rdi(7)
AVXEMU_SYM(andn8):  andnl %edi, %eax, %eax       // 32: dst==src1(rax) src2=rdi(7); aliasing safety
AVXEMU_SYM(andn9):  andnq %rdi, %rax, %rax       // 64: dst==src1(rax) src2=rdi(7)
AVXEMU_SYM(andn10): andnq %rsp, %rdi, %rax       // 64: src2=rsp(4) -> emitter must DECLINE (fall back)
AVXEMU_SYM(andn11):

// Sample MULX instructions (never executed on no-BMI2 host; decoded as bytes for
// `decoded` structs). AT&T syntax: mulx src, dlo, dhi  (OP1=rm=src=b_src,
// OP2=vvvv=dlo=d->dst=LOW, OP3=reg=dhi=d->bmi_dst2=HIGH). s1 is implicit rdx(2).
// Aliasing coverage is the whole game — {dlo,dhi} x {rax(0),rdx(2),src}, incl. swap.
AVXEMU_SYM(mx0):  mulxq %rbx, %rsi, %rdi     // 64 normal: src=rbx(3) dlo=rsi(6) dhi=rdi(7); none rax/rdx
AVXEMU_SYM(mx1):  mulxl %ebx, %esi, %edi     // 32 normal: src=3 dlo=6 dhi=7
AVXEMU_SYM(mx2):  mulxq %rdx, %rsi, %rdi     // 64 src==rdx(2): s2 aliases implicit s1; dlo=6 dhi=7
AVXEMU_SYM(mx3):  mulxq %rax, %rsi, %rdi     // 64 src==rax(0): dlo=6 dhi=7
AVXEMU_SYM(mx4):  mulxq %rbx, %rax, %rsi     // 64 dlo==rax(0): src=3 dhi=6
AVXEMU_SYM(mx5):  mulxq %rbx, %rsi, %rdx     // 64 dhi==rdx(2): src=3 dlo=6
AVXEMU_SYM(mx6):  mulxq %rbx, %rsi, %rax     // 64 dhi==rax(0): src=3 dlo=6
AVXEMU_SYM(mx7):  mulxq %rbx, %rdx, %rsi     // 64 dlo==rdx(2): src=3 dhi=6
AVXEMU_SYM(mx8):  mulxq %rbx, %rax, %rdx     // 64 dlo==rax(0) & dhi==rdx(2): src=3
AVXEMU_SYM(mx9):  mulxq %rbx, %rdx, %rax     // 64 SWAPPED dlo==rdx(2) & dhi==rax(0): src=3
AVXEMU_SYM(mx10): mulxl %ebx, %edx, %eax     // 32 SWAPPED dlo==rdx(2) & dhi==rax(0): src=3
AVXEMU_SYM(mx11): mulxq %rsi, %rsi, %rdi     // 64 dlo==src(rsi=6): dhi=7
AVXEMU_SYM(mx12): mulxq %rdi, %rsi, %rdi     // 64 dhi==src(rdi=7): dlo=6
AVXEMU_SYM(mx13): mulxq %r10, %r11, %r12     // 64 high regs (REX): src=10 dlo=11 dhi=12
AVXEMU_SYM(mx14): mulxl %r10d, %r11d, %r12d  // 32 high regs (REX): src=10 dlo=11 dhi=12
AVXEMU_SYM(mx15): mulxl %eax, %edx, %esi     // 32 src==rax(0) & dlo==rdx(2): dhi=6
AVXEMU_SYM(mx16): mulxq %rsp, %rsi, %rdi     // 64 src==rsp(4) -> emitter must DECLINE
AVXEMU_SYM(mx17): mulxq %rbx, %rsp, %rdi     // 64 dlo==rsp(4) -> emitter must DECLINE
AVXEMU_SYM(mx18): mulxq %rbx, %rsi, %rsp     // 64 dhi==rsp(4) -> emitter must DECLINE
// DLO==DHI (real-decode forms from the 2.1.185 binary; hardware keeps the HIGH
// half — DEST2(lo) is written first, DEST1(hi) last, per the SDM pseudocode):
AVXEMU_SYM(mx19): mulxq %rbx, %rsi, %rsi     // 64 dlo==dhi=rsi(6): src=rbx(3)
AVXEMU_SYM(mx20): mulxq %rax, %rax, %rax     // 64 ALL-SAME rax(0) — most common real form (459 sites)
AVXEMU_SYM(mx21): mulxq %rcx, %rcx, %rcx     // 64 all-same rcx(1) (95 sites)
AVXEMU_SYM(mx22): mulxq %r8, %rdx, %rdx      // 64 dlo==dhi==rdx(2) (also implicit s1): src=r8 (87 sites)
AVXEMU_SYM(mx23): mulxl %ebx, %esi, %esi     // 32 dlo==dhi=esi(6): src=ebx(3)
AVXEMU_SYM(mx24):

.data
.p2align 8
.globl AVXEMU_SYM(minspill_ti_labels)
AVXEMU_SYM(minspill_ti_labels): .quad AVXEMU_SYM(mi0), AVXEMU_SYM(mi1), AVXEMU_SYM(mi2), AVXEMU_SYM(mi3), AVXEMU_SYM(mi4), AVXEMU_SYM(mi5), AVXEMU_SYM(mi6), AVXEMU_SYM(mi7), AVXEMU_SYM(mi8)
.globl AVXEMU_SYM(minspill_shlx_labels)
AVXEMU_SYM(minspill_shlx_labels): .quad AVXEMU_SYM(si0), AVXEMU_SYM(si1), AVXEMU_SYM(si2), AVXEMU_SYM(si3), AVXEMU_SYM(si4), AVXEMU_SYM(si5), AVXEMU_SYM(si6), AVXEMU_SYM(si7)
.globl AVXEMU_SYM(minspill_tz_labels)
AVXEMU_SYM(minspill_tz_labels): .quad AVXEMU_SYM(zi0), AVXEMU_SYM(zi1), AVXEMU_SYM(zi2), AVXEMU_SYM(zi3), AVXEMU_SYM(zi4), AVXEMU_SYM(zi5), AVXEMU_SYM(zi6), AVXEMU_SYM(zi7), AVXEMU_SYM(zi8)
.globl AVXEMU_SYM(minspill_shrx_labels)
AVXEMU_SYM(minspill_shrx_labels): .quad AVXEMU_SYM(xi0), AVXEMU_SYM(xi1), AVXEMU_SYM(xi2), AVXEMU_SYM(xi3), AVXEMU_SYM(xi4), AVXEMU_SYM(xi5), AVXEMU_SYM(xi6), AVXEMU_SYM(xi7), AVXEMU_SYM(xi8)
.globl AVXEMU_SYM(minspill_sarx_labels)
AVXEMU_SYM(minspill_sarx_labels): .quad AVXEMU_SYM(ai0), AVXEMU_SYM(ai1), AVXEMU_SYM(ai2), AVXEMU_SYM(ai3), AVXEMU_SYM(ai4), AVXEMU_SYM(ai5), AVXEMU_SYM(ai6), AVXEMU_SYM(ai7), AVXEMU_SYM(ai8)
.globl AVXEMU_SYM(minspill_rorx_labels)
AVXEMU_SYM(minspill_rorx_labels): .quad AVXEMU_SYM(ri0), AVXEMU_SYM(ri1), AVXEMU_SYM(ri2), AVXEMU_SYM(ri3), AVXEMU_SYM(ri4), AVXEMU_SYM(ri5), AVXEMU_SYM(ri6), AVXEMU_SYM(ri7), AVXEMU_SYM(ri8)
.globl AVXEMU_SYM(minspill_bzhi_labels)
AVXEMU_SYM(minspill_bzhi_labels): .quad AVXEMU_SYM(bhi0), AVXEMU_SYM(bhi1), AVXEMU_SYM(bhi2), AVXEMU_SYM(bhi3), AVXEMU_SYM(bhi4), AVXEMU_SYM(bhi5), AVXEMU_SYM(bhi6), AVXEMU_SYM(bhi7), AVXEMU_SYM(bhi8), AVXEMU_SYM(bhi9)
.globl AVXEMU_SYM(minspill_blsr_labels)
AVXEMU_SYM(minspill_blsr_labels): .quad AVXEMU_SYM(blsr0), AVXEMU_SYM(blsr1), AVXEMU_SYM(blsr2), AVXEMU_SYM(blsr3), AVXEMU_SYM(blsr4), AVXEMU_SYM(blsr5), AVXEMU_SYM(blsr6), AVXEMU_SYM(blsr7), AVXEMU_SYM(blsr8)
.globl AVXEMU_SYM(minspill_blsi_labels)
AVXEMU_SYM(minspill_blsi_labels): .quad AVXEMU_SYM(blsi0), AVXEMU_SYM(blsi1), AVXEMU_SYM(blsi2), AVXEMU_SYM(blsi3), AVXEMU_SYM(blsi4), AVXEMU_SYM(blsi5), AVXEMU_SYM(blsi6), AVXEMU_SYM(blsi7), AVXEMU_SYM(blsi8)
.globl AVXEMU_SYM(minspill_blsm_labels)
AVXEMU_SYM(minspill_blsm_labels): .quad AVXEMU_SYM(blsm0), AVXEMU_SYM(blsm1), AVXEMU_SYM(blsm2), AVXEMU_SYM(blsm3), AVXEMU_SYM(blsm4), AVXEMU_SYM(blsm5), AVXEMU_SYM(blsm6), AVXEMU_SYM(blsm7), AVXEMU_SYM(blsm8)
.globl AVXEMU_SYM(minspill_andn_labels)
AVXEMU_SYM(minspill_andn_labels): .quad AVXEMU_SYM(andn0), AVXEMU_SYM(andn1), AVXEMU_SYM(andn2), AVXEMU_SYM(andn3), AVXEMU_SYM(andn4), AVXEMU_SYM(andn5), AVXEMU_SYM(andn6), AVXEMU_SYM(andn7), AVXEMU_SYM(andn8), AVXEMU_SYM(andn9), AVXEMU_SYM(andn10), AVXEMU_SYM(andn11)
.globl AVXEMU_SYM(minspill_mulx_labels)
AVXEMU_SYM(minspill_mulx_labels): .quad AVXEMU_SYM(mx0), AVXEMU_SYM(mx1), AVXEMU_SYM(mx2), AVXEMU_SYM(mx3), AVXEMU_SYM(mx4), AVXEMU_SYM(mx5), AVXEMU_SYM(mx6), AVXEMU_SYM(mx7), AVXEMU_SYM(mx8), AVXEMU_SYM(mx9), AVXEMU_SYM(mx10), AVXEMU_SYM(mx11), AVXEMU_SYM(mx12), AVXEMU_SYM(mx13), AVXEMU_SYM(mx14), AVXEMU_SYM(mx15), AVXEMU_SYM(mx16), AVXEMU_SYM(mx17), AVXEMU_SYM(mx18), AVXEMU_SYM(mx19), AVXEMU_SYM(mx20), AVXEMU_SYM(mx21), AVXEMU_SYM(mx22), AVXEMU_SYM(mx23), AVXEMU_SYM(mx24)

AVXEMU_GNU_STACK
