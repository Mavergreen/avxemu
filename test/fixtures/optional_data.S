.text
.globl cpuid_dispatch
.type cpuid_dispatch,@function
cpuid_dispatch:
.cfi_startproc
 lea unrelated_table(%rip),%rdx
 jmp *%rdi
 .fill 11,1,0x90
.globl embedded_cpuid
.type embedded_cpuid,@notype
embedded_cpuid:
 .byte 0x0f,0xa2,0xc3
 .fill 9,1,0x90
unrelated_table:
 .long embedded_cpuid-unrelated_table
 .long -2147483647
.cfi_endproc
.size cpuid_dispatch,.-cpuid_dispatch
.section .note.GNU-stack,"",@progbits
.text
.globl vector_dispatch
.type vector_dispatch,@function
vector_dispatch:
.cfi_startproc
 lea vector_table(%rip),%rdx
 jmp *%rdi
 .fill 11,1,0x90
.globl embedded_vector
.type embedded_vector,@notype
embedded_vector:
 .byte 0xc5,0xfd,0xfe,0xc1,0xc5,0xfd,0xfe,0xc1,0xc3
 .fill 9,1,0x90
vector_table:
 .long embedded_vector-vector_table
 .long -2147483647
.cfi_endproc
.size vector_dispatch,.-vector_dispatch
.section .note.GNU-stack,"",@progbits

.text
.globl real_cpuid
.type real_cpuid,@function
real_cpuid:
.cfi_startproc
 push %rbx
.cfi_adjust_cfa_offset 8
 xor %eax,%eax
 cpuid
 pop %rbx
.cfi_adjust_cfa_offset -8
 ret
.cfi_endproc
.size real_cpuid,.-real_cpuid
