#include "asm.h"

// Register/flags round-trip harness for reloctest.
   .data
   .p2align 3
AVXEMU_SYM(rh_code): .quad 0
AVXEMU_SYM(rh_out):  .quad 0
AVXEMU_SYM(rh_rax):  .quad 0
   .text
   .p2align 4
AVXEMU_FUNCTION(reloc_test_invoke)  // rdi=code, rsi=in, rdx=out
   pushq %rbx
   pushq %rbp
   pushq %r12
   pushq %r13
   pushq %r14
   pushq %r15
   movq %rdi, AVXEMU_SYM(rh_code)(%rip)
   movq %rdx, AVXEMU_SYM(rh_out)(%rip)
   pushq 128(%rsi)                 // input rflags
   popfq
   movq 0*8(%rsi), %rax
   movq 1*8(%rsi), %rcx
   movq 2*8(%rsi), %rdx
   movq 3*8(%rsi), %rbx
   movq 5*8(%rsi), %rbp
   movq 7*8(%rsi), %rdi
   movq 8*8(%rsi), %r8
   movq 9*8(%rsi), %r9
   movq 10*8(%rsi), %r10
   movq 11*8(%rsi), %r11
   movq 12*8(%rsi), %r12
   movq 13*8(%rsi), %r13
   movq 14*8(%rsi), %r14
   movq 15*8(%rsi), %r15
   movq 6*8(%rsi), %rsi            // load rsi last
   callq *AVXEMU_SYM(rh_code)(%rip) // the buffer ends in ret
   movq %rax, AVXEMU_SYM(rh_rax)(%rip) // stash rax without disturbing flags
   movq AVXEMU_SYM(rh_out)(%rip), %rax
   movq %rcx, 1*8(%rax)
   movq %rdx, 2*8(%rax)
   movq %rbx, 3*8(%rax)
   movq %rbp, 5*8(%rax)
   movq %rsi, 6*8(%rax)
   movq %rdi, 7*8(%rax)
   movq %r8,  8*8(%rax)
   movq %r9,  9*8(%rax)
   movq %r10, 10*8(%rax)
   movq %r11, 11*8(%rax)
   movq %r12, 12*8(%rax)
   movq %r13, 13*8(%rax)
   movq %r14, 14*8(%rax)
   movq %r15, 15*8(%rax)
   pushfq                         // capture the buffer's flags
   popq %rcx
   movq %rcx, 128(%rax)
   movq AVXEMU_SYM(rh_rax)(%rip), %rcx
   movq %rcx, 0*8(%rax)
   popq %r15
   popq %r14
   popq %r13
   popq %r12
   popq %rbp
   popq %rbx
   ret
AVXEMU_END_FUNCTION(reloc_test_invoke)

AVXEMU_GNU_STACK
