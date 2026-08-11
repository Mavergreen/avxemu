// minspill_harness.s — drive a MINIMAL-SPILL live-register thunk with a known
// GPR+flags state and capture the state it produces.
//
// Unlike tramp_harness.s (full tt2 spill, regfile-slot operands), a minimal-spill
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
.align 8
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
.globl _avxemu_minspill_rz8
.globl _avxemu_minspill_rz16
_avxemu_minspill_rz8:  .quad 0   // snapshot of [rsp_at_jmp - 8]  after the thunk
_avxemu_minspill_rz16: .quad 0   // snapshot of [rsp_at_jmp - 16] after the thunk

.text
.align 4
.globl _avxemu_minspill_run
_avxemu_minspill_run:
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

.globl _avxemu_minspill_capture
_avxemu_minspill_capture:        // the thunk `jmp`s here with its post-run live state
    movq  %rax, g_scratch(%rip)  // save live rax (mov: flags intact)
    movq  %rsp, g_rsp_atcap(%rip)
    // Snapshot the red-zone sentinels BEFORE pushfq (which writes [rsp-8]).
    // All movs — no flag effect, so the thunk's output flags survive to pushfq.
    movq  g_rsp_save(%rip), %rax
    movq  -8(%rax), %rax
    movq  %rax, _avxemu_minspill_rz8(%rip)
    movq  g_rsp_save(%rip), %rax
    movq  -16(%rax), %rax
    movq  %rax, _avxemu_minspill_rz16(%rip)
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

// Sample LZCNT instructions (never executed — the host may lack LZCNT; these are
// decoded as bytes to obtain `decoded` structs, exactly like tramp_harness's
// _ti samples). Coverage: opsize 32/64, dst!=src and dst==src, low and high regs.
.text
.align 4
_mi0: lzcntl %edi, %eax       // 32  src=rdi(7) dst=rax(0)  dst!=src
_mi1: lzcntl %eax, %eax       // 32  dst==src (rax)
_mi2: lzcntq %rdi, %rax       // 64  src=rdi(7) dst=rax(0)  dst!=src
_mi3: lzcntq %rax, %rax       // 64  dst==src (rax)
_mi4: lzcntl %r9d, %r8d       // 32  src=r9(9) dst=r8(8)    high regs
_mi5: lzcntq %r15, %r14       // 64  src=r15   dst=r14       high regs
_mi6: lzcntl %ecx, %edx       // 32  src=rcx(1) dst=rdx(2)
_mi7: lzcntq %rsp, %rax       // 64  src=rsp(4) -> emitter must DECLINE (fall back)
_mi8:

// shlx samples (AT&T: shlx count, value, dst -> dst = value << count).
_si0: shlxl %ecx, %edi, %eax  // 32  dst=rax(0) value=rdi(7) count=rcx(1)
_si1: shlxq %rcx, %rdi, %rax  // 64  dst=rax value=rdi count=rcx
_si2: shlxl %edx, %eax, %eax  // 32  dst==value (rax), count=rdx(2)
_si3: shlxq %r9, %r10, %r11   // 64  high regs  dst=r11 value=r10 count=r9
_si4: shlxl %edi, %edi, %esi  // 32  count==value (rdi), dst=rsi(6)
_si5: shlxq %rax, %rax, %rdx  // 64  count==dst? no: dst=rdx value=rax count=rax (count==value)
_si6: shlxq %rdx, %r8, %rcx   // 64  dst==rcx(1) -> emitter must DECLINE (rcx is CL scratch)
_si7:

// Sample TZCNT instructions (never executed — the host may lack TZCNT; decoded as
// bytes to obtain `decoded` structs). Coverage mirrors lzcnt: opsize 32/64,
// dst!=src and dst==src, low and high regs.
_zi0: tzcntl %edi, %eax       // 32  src=rdi(7) dst=rax(0)  dst!=src
_zi1: tzcntl %eax, %eax       // 32  dst==src (rax)
_zi2: tzcntq %rdi, %rax       // 64  src=rdi(7) dst=rax(0)  dst!=src
_zi3: tzcntq %rax, %rax       // 64  dst==src (rax)
_zi4: tzcntl %r9d, %r8d       // 32  src=r9(9) dst=r8(8)    high regs
_zi5: tzcntq %r15, %r14       // 64  src=r15   dst=r14       high regs
_zi6: tzcntl %ecx, %edx       // 32  src=rcx(1) dst=rdx(2)
_zi7: tzcntq %rsp, %rax       // 64  src=rsp(4) -> emitter must DECLINE (fall back)
_zi8:

// Sample SHRX instructions (never executed; decoded as bytes for `decoded` structs).
// Coverage: opsize 32/64, dst!=value, dst==value, dst==count, count==value, high regs.
_xi0: shrxl %ecx, %edi, %eax   // 32  dst=rax(0) value=rdi(7) count=rcx(1)
_xi1: shrxq %rcx, %rdi, %rax   // 64  dst=rax(0) value=rdi(7) count=rcx(1)
_xi2: shrxl %edx, %eax, %eax   // 32  dst==value (rax), count=rdx(2)
_xi3: shrxq %r11, %r10, %r11   // 64  high regs, dst==count (r11), value=r10
_xi4: shrxl %edi, %edi, %esi   // 32  count==value (rdi), dst=rsi(6)
_xi5: shrxq %rax, %rax, %rdx   // 64  count==value (rax), dst=rdx(2)
_xi6: shrxq %rdx, %r8, %rcx    // 64  dst==rcx(1) -> emitter must DECLINE
_xi7: shrxq %rcx, %rsp, %rax   // 64  value=rsp(4) -> emitter must DECLINE
_xi8:

// Sample SARX instructions (never executed; decoded as bytes for `decoded` structs).
// Coverage mirrors shrx: opsize 32/64, aliasing variants, high regs, rsp/rcx declines.
_ai0: sarxl %ecx, %edi, %eax   // 32  dst=rax(0) value=rdi(7) count=rcx(1)
_ai1: sarxq %rcx, %rdi, %rax   // 64  dst=rax(0) value=rdi(7) count=rcx(1)
_ai2: sarxl %edx, %eax, %eax   // 32  dst==value (rax), count=rdx(2)
_ai3: sarxq %r11, %r10, %r11   // 64  high regs, dst==count (r11), value=r10
_ai4: sarxl %edi, %edi, %esi   // 32  count==value (rdi), dst=rsi(6)
_ai5: sarxq %rax, %rax, %rdx   // 64  count==value (rax), dst=rdx(2)
_ai6: sarxq %rdx, %r8, %rcx    // 64  dst==rcx(1) -> emitter must DECLINE
_ai7: sarxq %rcx, %rsp, %rax   // 64  value=rsp(4) -> emitter must DECLINE
_ai8:

// Sample RORX instructions (never executed; decoded as bytes for `decoded` structs).
// Coverage: opsize 32/64, imm=0, imm=1, imm=opsize-1, imm>opsize-1 (masking),
// dst==value, dst==rcx (ACCEPTED for rorx — no CL needed), high regs, rsp decline.
_ri0: rorxl $1, %edi, %eax     // 32  imm=1  value=rdi(7) dst=rax(0)
_ri1: rorxq $1, %rdi, %rax     // 64  imm=1  value=rdi(7) dst=rax(0)
_ri2: rorxl $0, %edi, %esi     // 32  imm=0 (no-op rotation)
_ri3: rorxq $63, %r10, %r11    // 64  high regs, imm=63=opsize64-1
_ri4: rorxl $31, %edi, %esi    // 32  imm=31=opsize32-1
_ri5: rorxl $5, %eax, %eax     // 32  dst==value (rax), imm=5
_ri6: rorxl $33, %eax, %ecx    // 32  imm=33 -> n=33&31=1 (masking), dst=rcx(1) [ACCEPTED]
_ri7: rorxq $1, %rsp, %rax     // 64  value=rsp(4) -> emitter must DECLINE
_ri8:

// Sample BZHI instructions (never executed on no-BMI2 host; decoded as bytes for
// `decoded` structs). AT&T syntax: bzhi index, value, dst (VEX vvvv=index, rm=value).
// In decoded: a_src=value(rm), b_src=index(vvvv).
// Coverage: opsize 32/64, dst==value, value==index, dst==index, high regs, rsp/rcx declines.
_bhi0: bzhil %ecx, %edi, %eax      // 32: dst=rax(0), value=rdi(7), index=rcx(1)
_bhi1: bzhiq %rcx, %rdi, %rax      // 64: dst=rax(0), value=rdi(7), index=rcx(1)
_bhi2: bzhil %edx, %eax, %eax      // 32: dst==value (rax), index=rdx(2)
_bhi3: bzhiq %rdx, %rax, %rax      // 64: dst==value (rax), index=rdx(2)
_bhi4: bzhiq %rdi, %rdi, %rsi      // 64: value==index (rdi=7), dst=rsi(6)
_bhi5: bzhil %r11d, %r10d, %r11d   // 32: high regs, dst==index (r11=11), value=r10(10)
_bhi6: bzhiq %r9, %r10, %r11       // 64: high regs, dst=r11(11), value=r10(10), index=r9(9)
_bhi7: bzhiq %rdx, %r8, %rcx       // 64: dst==rcx(1) -> emitter must DECLINE
_bhi8: bzhiq %rcx, %rsp, %rax      // 64: value=rsp(4) -> emitter must DECLINE
_bhi9:

// Sample BLSR instructions (never executed; decoded as bytes for `decoded` structs).
// AT&T syntax: blsrl/blsrq src, dst  (src=rm=a_src, dst=vvvv=d->dst)
// Coverage: opsize 32/64, dst==src, high regs, rsp-src decline.
// BLSR: dst = src & (src-1)  CF=(src==0)  ZF=(result==0)  SF=signbit(result)  OF=0
_blsr0: blsrl %edi, %eax          // 32: dst=rax(0), src=rdi(7), dst!=src
_blsr1: blsrq %rdi, %rax          // 64: dst=rax(0), src=rdi(7)
_blsr2: blsrl %eax, %eax          // 32: dst==src (rax)
_blsr3: blsrq %rax, %rax          // 64: dst==src
_blsr4: blsrl %r9d, %r8d          // 32: high regs, src=r9(9), dst=r8(8)
_blsr5: blsrq %r15, %r14          // 64: high regs
_blsr6: blsrl %ecx, %edx          // 32: src=rcx(1), dst=rdx(2) — rcx is a src, not a constraint
_blsr7: blsrq %rsp, %rax          // 64: src=rsp(4) -> emitter must DECLINE (fall back)
_blsr8:

// Sample BLSI instructions (never executed; decoded as bytes).
// BLSI: dst = src & (0-src)  CF=(src!=0)  ZF=(result==0)  SF=signbit(result)  OF=0
_blsi0: blsil %edi, %eax          // 32: dst=rax(0), src=rdi(7), dst!=src
_blsi1: blsiq %rdi, %rax          // 64: dst=rax(0), src=rdi(7)
_blsi2: blsil %eax, %eax          // 32: dst==src (rax)
_blsi3: blsiq %rax, %rax          // 64: dst==src
_blsi4: blsil %r9d, %r8d          // 32: high regs, src=r9(9), dst=r8(8)
_blsi5: blsiq %r15, %r14          // 64: high regs
_blsi6: blsil %ecx, %edx          // 32: src=rcx(1), dst=rdx(2)
_blsi7: blsiq %rsp, %rax          // 64: src=rsp(4) -> emitter must DECLINE (fall back)
_blsi8:

// Sample BLSMSK instructions (never executed; decoded as bytes).
// BLSMSK: dst = src ^ (src-1)  CF=(src==0)  ZF=(result==0)  SF=signbit(result)  OF=0
_blsm0: blsmskl %edi, %eax        // 32: dst=rax(0), src=rdi(7), dst!=src
_blsm1: blsmskq %rdi, %rax        // 64: dst=rax(0), src=rdi(7)
_blsm2: blsmskl %eax, %eax        // 32: dst==src (rax)
_blsm3: blsmskq %rax, %rax        // 64: dst==src
_blsm4: blsmskl %r9d, %r8d        // 32: high regs, src=r9(9), dst=r8(8)
_blsm5: blsmskq %r15, %r14        // 64: high regs
_blsm6: blsmskl %ecx, %edx        // 32: src=rcx(1), dst=rdx(2)
_blsm7: blsmskq %rsp, %rax        // 64: src=rsp(4) -> emitter must DECLINE (fall back)
_blsm8:

// Sample ANDN instructions (never executed; decoded as bytes for `decoded` structs).
// ANDN: dst = ~src1 & src2  CF=0  ZF=(result==0)  SF=signbit(result)  OF=0
// AT&T syntax: andnl src2(b_src/rm), src1(a_src/vvvv), dst(reg)
// In decoded: a_src=src1(VEX.vvvv), b_src=src2(ModRM.rm), dst=ModRM.reg
// Semantics: result = (~a_src) & b_src  (NOT src1, then AND with src2)
_andn0:  andnl %edi, %eax, %ecx       // 32: dst=rcx(1) src1=rax(0) src2=rdi(7)  basic dst!=src1!=src2
_andn1:  andnq %rdi, %rax, %rcx       // 64: same regs as _andn0
_andn2:  andnl %eax, %eax, %edx       // 32: src1==src2(rax), dst=rdx(2) -> result always 0, ZF=1
_andn3:  andnq %rax, %rax, %rdx       // 64: src1==src2(rax), dst=rdx(2)
_andn4:  andnl %r9d, %r10d, %r8d      // 32: high regs dst=r8(8) src1=r10(10) src2=r9(9)
_andn5:  andnq %r9, %r10, %r11        // 64: high regs dst=r11(11) src1=r10(10) src2=r9(9)
_andn6:  andnl %eax, %edi, %eax       // 32: dst==src2(rax) src1=rdi(7); aliasing safety
_andn7:  andnq %rax, %rdi, %rax       // 64: dst==src2(rax) src1=rdi(7)
_andn8:  andnl %edi, %eax, %eax       // 32: dst==src1(rax) src2=rdi(7); aliasing safety
_andn9:  andnq %rdi, %rax, %rax       // 64: dst==src1(rax) src2=rdi(7)
_andn10: andnq %rsp, %rdi, %rax       // 64: src2=rsp(4) -> emitter must DECLINE (fall back)
_andn11:

// Sample MULX instructions (never executed on no-BMI2 host; decoded as bytes for
// `decoded` structs). AT&T syntax: mulx src, dlo, dhi  (OP1=rm=src=b_src,
// OP2=vvvv=dlo=d->dst=LOW, OP3=reg=dhi=d->bmi_dst2=HIGH). s1 is implicit rdx(2).
// Aliasing coverage is the whole game — {dlo,dhi} x {rax(0),rdx(2),src}, incl. swap.
_mx0:  mulxq %rbx, %rsi, %rdi     // 64 normal: src=rbx(3) dlo=rsi(6) dhi=rdi(7); none rax/rdx
_mx1:  mulxl %ebx, %esi, %edi     // 32 normal: src=3 dlo=6 dhi=7
_mx2:  mulxq %rdx, %rsi, %rdi     // 64 src==rdx(2): s2 aliases implicit s1; dlo=6 dhi=7
_mx3:  mulxq %rax, %rsi, %rdi     // 64 src==rax(0): dlo=6 dhi=7
_mx4:  mulxq %rbx, %rax, %rsi     // 64 dlo==rax(0): src=3 dhi=6
_mx5:  mulxq %rbx, %rsi, %rdx     // 64 dhi==rdx(2): src=3 dlo=6
_mx6:  mulxq %rbx, %rsi, %rax     // 64 dhi==rax(0): src=3 dlo=6
_mx7:  mulxq %rbx, %rdx, %rsi     // 64 dlo==rdx(2): src=3 dhi=6
_mx8:  mulxq %rbx, %rax, %rdx     // 64 dlo==rax(0) & dhi==rdx(2): src=3
_mx9:  mulxq %rbx, %rdx, %rax     // 64 SWAPPED dlo==rdx(2) & dhi==rax(0): src=3
_mx10: mulxl %ebx, %edx, %eax     // 32 SWAPPED dlo==rdx(2) & dhi==rax(0): src=3
_mx11: mulxq %rsi, %rsi, %rdi     // 64 dlo==src(rsi=6): dhi=7
_mx12: mulxq %rdi, %rsi, %rdi     // 64 dhi==src(rdi=7): dlo=6
_mx13: mulxq %r10, %r11, %r12     // 64 high regs (REX): src=10 dlo=11 dhi=12
_mx14: mulxl %r10d, %r11d, %r12d  // 32 high regs (REX): src=10 dlo=11 dhi=12
_mx15: mulxl %eax, %edx, %esi     // 32 src==rax(0) & dlo==rdx(2): dhi=6
_mx16: mulxq %rsp, %rsi, %rdi     // 64 src==rsp(4) -> emitter must DECLINE
_mx17: mulxq %rbx, %rsp, %rdi     // 64 dlo==rsp(4) -> emitter must DECLINE
_mx18: mulxq %rbx, %rsi, %rsp     // 64 dhi==rsp(4) -> emitter must DECLINE
// DLO==DHI (real-decode forms from the 2.1.185 binary; hardware keeps the HIGH
// half — DEST2(lo) is written first, DEST1(hi) last, per the SDM pseudocode):
_mx19: mulxq %rbx, %rsi, %rsi     // 64 dlo==dhi=rsi(6): src=rbx(3)
_mx20: mulxq %rax, %rax, %rax     // 64 ALL-SAME rax(0) — most common real form (459 sites)
_mx21: mulxq %rcx, %rcx, %rcx     // 64 all-same rcx(1) (95 sites)
_mx22: mulxq %r8, %rdx, %rdx      // 64 dlo==dhi==rdx(2) (also implicit s1): src=r8 (87 sites)
_mx23: mulxl %ebx, %esi, %esi     // 32 dlo==dhi=esi(6): src=ebx(3)
_mx24:

.data
.align 8
.globl _minspill_ti_labels
_minspill_ti_labels: .quad _mi0, _mi1, _mi2, _mi3, _mi4, _mi5, _mi6, _mi7, _mi8
.globl _minspill_shlx_labels
_minspill_shlx_labels: .quad _si0, _si1, _si2, _si3, _si4, _si5, _si6, _si7
.globl _minspill_tz_labels
_minspill_tz_labels: .quad _zi0, _zi1, _zi2, _zi3, _zi4, _zi5, _zi6, _zi7, _zi8
.globl _minspill_shrx_labels
_minspill_shrx_labels: .quad _xi0, _xi1, _xi2, _xi3, _xi4, _xi5, _xi6, _xi7, _xi8
.globl _minspill_sarx_labels
_minspill_sarx_labels: .quad _ai0, _ai1, _ai2, _ai3, _ai4, _ai5, _ai6, _ai7, _ai8
.globl _minspill_rorx_labels
_minspill_rorx_labels: .quad _ri0, _ri1, _ri2, _ri3, _ri4, _ri5, _ri6, _ri7, _ri8
.globl _minspill_bzhi_labels
_minspill_bzhi_labels: .quad _bhi0, _bhi1, _bhi2, _bhi3, _bhi4, _bhi5, _bhi6, _bhi7, _bhi8, _bhi9
.globl _minspill_blsr_labels
_minspill_blsr_labels: .quad _blsr0, _blsr1, _blsr2, _blsr3, _blsr4, _blsr5, _blsr6, _blsr7, _blsr8
.globl _minspill_blsi_labels
_minspill_blsi_labels: .quad _blsi0, _blsi1, _blsi2, _blsi3, _blsi4, _blsi5, _blsi6, _blsi7, _blsi8
.globl _minspill_blsm_labels
_minspill_blsm_labels: .quad _blsm0, _blsm1, _blsm2, _blsm3, _blsm4, _blsm5, _blsm6, _blsm7, _blsm8
.globl _minspill_andn_labels
_minspill_andn_labels: .quad _andn0, _andn1, _andn2, _andn3, _andn4, _andn5, _andn6, _andn7, _andn8, _andn9, _andn10, _andn11
.globl _minspill_mulx_labels
_minspill_mulx_labels: .quad _mx0, _mx1, _mx2, _mx3, _mx4, _mx5, _mx6, _mx7, _mx8, _mx9, _mx10, _mx11, _mx12, _mx13, _mx14, _mx15, _mx16, _mx17, _mx18, _mx19, _mx20, _mx21, _mx22, _mx23, _mx24
