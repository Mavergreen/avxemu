/* Native signal-return probe. Keep injected instructions outside scanned text. */
.section __TEXT,__avxemu_test,regular,pure_instructions
.globl _context_probe
_context_probe:
 pushq %rbx
 pushq %rbp
 pushq %r12
 pushq %r13
 pushq %r14
 pushq %r15
 movq %rsp, _context_input+544(%rip)
 pushq $0x247
 popfq
 vmovdqu _context_input+0(%rip), %ymm0
 vmovdqu _context_input+32(%rip), %ymm1
 vmovdqu _context_input+64(%rip), %ymm2
 vmovdqu _context_input+96(%rip), %ymm3
 vmovdqu _context_input+128(%rip), %ymm4
 vmovdqu _context_input+160(%rip), %ymm5
 vmovdqu _context_input+192(%rip), %ymm6
 vmovdqu _context_input+224(%rip), %ymm7
 vmovdqu _context_input+256(%rip), %ymm8
 vmovdqu _context_input+288(%rip), %ymm9
 vmovdqu _context_input+320(%rip), %ymm10
 vmovdqu _context_input+352(%rip), %ymm11
 vmovdqu _context_input+384(%rip), %ymm12
 vmovdqu _context_input+416(%rip), %ymm13
 vmovdqu _context_input+448(%rip), %ymm14
 vmovdqu _context_input+480(%rip), %ymm15
 movq _context_input+512(%rip), %rax
 movq _context_input+520(%rip), %rcx
 movq _context_input+528(%rip), %rdx
 movq _context_input+536(%rip), %rbx
 movq _context_input+552(%rip), %rbp
 movq _context_input+560(%rip), %rsi
 movq _context_input+568(%rip), %rdi
 movq _context_input+576(%rip), %r8
 movq _context_input+584(%rip), %r9
 movq _context_input+592(%rip), %r10
 movq _context_input+600(%rip), %r11
 movq _context_input+608(%rip), %r12
 movq _context_input+616(%rip), %r13
 movq _context_input+624(%rip), %r14
 movq _context_input+632(%rip), %r15
 movq $0x5a5a, -8(%rsp)
 movq $0x5a5a, -16(%rsp)
 movq $0x5a5a, -24(%rsp)
 movq $0x5a5a, -32(%rsp)
 movq $0x5a5a, -40(%rsp)
 movq $0x5a5a, -48(%rsp)
 movq $0x5a5a, -56(%rsp)
 movq $0x5a5a, -64(%rsp)
 movq $0x5a5a, -72(%rsp)
 movq $0x5a5a, -80(%rsp)
 movq $0x5a5a, -88(%rsp)
 movq $0x5a5a, -96(%rsp)
 movq $0x5a5a, -104(%rsp)
 movq $0x5a5a, -112(%rsp)
 movq $0x5a5a, -120(%rsp)
 movq $0x5a5a, -128(%rsp)
 ud2
 vpaddd %ymm1, %ymm0, %ymm0
 ud2
 shlxq %rdx, %rax, %rax
 movq $1, _context_resumed(%rip)
 movq %rax, _context_output+512(%rip)
 movq %rcx, _context_output+520(%rip)
 movq %rdx, _context_output+528(%rip)
 movq %rbx, _context_output+536(%rip)
 movq %rsp, _context_output+544(%rip)
 movq %rbp, _context_output+552(%rip)
 movq %rsi, _context_output+560(%rip)
 movq %rdi, _context_output+568(%rip)
 movq %r8, _context_output+576(%rip)
 movq %r9, _context_output+584(%rip)
 movq %r10, _context_output+592(%rip)
 movq %r11, _context_output+600(%rip)
 movq %r12, _context_output+608(%rip)
 movq %r13, _context_output+616(%rip)
 movq %r14, _context_output+624(%rip)
 movq %r15, _context_output+632(%rip)
 movq -8(%rsp), %rax
 movq %rax, _context_redzone+0(%rip)
 movq -16(%rsp), %rax
 movq %rax, _context_redzone+8(%rip)
 movq -24(%rsp), %rax
 movq %rax, _context_redzone+16(%rip)
 movq -32(%rsp), %rax
 movq %rax, _context_redzone+24(%rip)
 movq -40(%rsp), %rax
 movq %rax, _context_redzone+32(%rip)
 movq -48(%rsp), %rax
 movq %rax, _context_redzone+40(%rip)
 movq -56(%rsp), %rax
 movq %rax, _context_redzone+48(%rip)
 movq -64(%rsp), %rax
 movq %rax, _context_redzone+56(%rip)
 movq -72(%rsp), %rax
 movq %rax, _context_redzone+64(%rip)
 movq -80(%rsp), %rax
 movq %rax, _context_redzone+72(%rip)
 movq -88(%rsp), %rax
 movq %rax, _context_redzone+80(%rip)
 movq -96(%rsp), %rax
 movq %rax, _context_redzone+88(%rip)
 movq -104(%rsp), %rax
 movq %rax, _context_redzone+96(%rip)
 movq -112(%rsp), %rax
 movq %rax, _context_redzone+104(%rip)
 movq -120(%rsp), %rax
 movq %rax, _context_redzone+112(%rip)
 movq -128(%rsp), %rax
 movq %rax, _context_redzone+120(%rip)
 pushfq
 popq _context_output+640(%rip)
 vmovdqu %ymm0, _context_output+0(%rip)
 vmovdqu %ymm1, _context_output+32(%rip)
 vmovdqu %ymm2, _context_output+64(%rip)
 vmovdqu %ymm3, _context_output+96(%rip)
 vmovdqu %ymm4, _context_output+128(%rip)
 vmovdqu %ymm5, _context_output+160(%rip)
 vmovdqu %ymm6, _context_output+192(%rip)
 vmovdqu %ymm7, _context_output+224(%rip)
 vmovdqu %ymm8, _context_output+256(%rip)
 vmovdqu %ymm9, _context_output+288(%rip)
 vmovdqu %ymm10, _context_output+320(%rip)
 vmovdqu %ymm11, _context_output+352(%rip)
 vmovdqu %ymm12, _context_output+384(%rip)
 vmovdqu %ymm13, _context_output+416(%rip)
 vmovdqu %ymm14, _context_output+448(%rip)
 vmovdqu %ymm15, _context_output+480(%rip)
 stmxcsr _context_mxcsr(%rip)
 fnstcw _context_x87(%rip)
 vzeroupper
 popq %r15
 popq %r14
 popq %r13
 popq %r12
 popq %rbp
 popq %rbx
 ret
