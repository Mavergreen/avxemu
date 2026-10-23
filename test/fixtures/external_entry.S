.text
.globl ordinary_entry
.type ordinary_entry,@function
ordinary_entry:
.cfi_startproc
#ifdef AVXEMU_BAD_ENTRY
 tzcnt %eax,%eax
#endif
 ret
.globl side_entry
.type side_entry,@notype
side_entry:
 tzcnt %eax,%eax
 ret
.cfi_endproc
.size ordinary_entry,.-ordinary_entry
.globl count_from_other
.type count_from_other,@function
count_from_other:
.cfi_startproc
 xor %eax,%eax
#ifdef AVXEMU_BAD_ENTRY
 jmp ordinary_entry+1
#else
 jmp side_entry
#endif
.cfi_endproc
.size count_from_other,.-count_from_other
.section .note.GNU-stack,"",@progbits
