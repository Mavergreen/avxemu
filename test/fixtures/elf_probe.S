.text
.globl elf_asm_probe, elf_asm_end, elf_asm_alias
.type elf_asm_probe,@function
elf_asm_probe:
.cfi_startproc
    leal 7(%rdi), %eax
    ret
.cfi_endproc
elf_asm_end:
.size elf_asm_probe, .-elf_asm_probe
.set elf_asm_alias, elf_asm_probe
.type elf_asm_alias,@function
.size elf_asm_alias, elf_asm_end-elf_asm_probe
/* These bytes resemble instructions but are outside any function. */
.globl elf_data_island
.type elf_data_island,@object
elf_data_island:
.byte 0xf3, 0x0f, 0xbc, 0xc0, 0xf3, 0x0f, 0xbd, 0xc0
.size elf_data_island, .-elf_data_island
.section .avxemu_raw,"ax",@progbits
.globl elf_raw_probe
.type elf_raw_probe,@function
elf_raw_probe:
.cfi_startproc
    ret
.cfi_endproc
.size elf_raw_probe, .-elf_raw_probe
.section .avxemu_test,"ax",@progbits
.globl elf_test_probe
.type elf_test_probe,@function
elf_test_probe:
.cfi_startproc
    ret
.cfi_endproc
.size elf_test_probe, .-elf_test_probe
.section .text.other,"ax",@progbits
.globl elf_other_probe
.type elf_other_probe,@function
elf_other_probe:
.cfi_startproc
    leal 9(%rdi), %eax
    ret
.cfi_endproc
.size elf_other_probe, .-elf_other_probe
.section .note.GNU-stack,"",@progbits
