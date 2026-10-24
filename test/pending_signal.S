.section .avxemu_test,"ax",@progbits
.globl pending_unblock
.type pending_unblock,@function
pending_unblock:
 mov %rdi,%rsi
 mov $14,%eax
 mov $1,%edi
 xor %edx,%edx
 mov $8,%r10d
 syscall
 tzcnt %eax,%eax
 ret
.size pending_unblock,.-pending_unblock
.section .note.GNU-stack,"",@progbits
