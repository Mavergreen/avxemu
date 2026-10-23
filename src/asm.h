#ifndef AVXEMU_ASM_H
#define AVXEMU_ASM_H

/* Object-format spelling shared by preprocessed assembly and raw C helpers.
 * Keep raw/test instructions in their own executable sections: image discovery
 * excludes these section bounds even when the ordinary symbol table is stripped. */
#ifdef __APPLE__
#define AVXEMU_SYM(name) _##name
#define AVXEMU_RAW_ATTR __attribute__((noinline, section("__TEXT,__avxemu_raw,regular,pure_instructions")))
#else
#define AVXEMU_SYM(name) name
#define AVXEMU_RAW_ATTR __attribute__((noinline, section(".avxemu_raw")))
#endif

#ifdef __ASSEMBLER__
#ifdef __APPLE__
#define AVXEMU_RAW_SECTION .section __TEXT,__avxemu_raw,regular,pure_instructions
#define AVXEMU_TEST_SECTION .section __TEXT,__avxemu_test,regular,pure_instructions
#define AVXEMU_FUNCTION(name) .globl AVXEMU_SYM(name); AVXEMU_SYM(name):
#define AVXEMU_END_FUNCTION(name)
#define AVXEMU_HIDDEN(name)
#define AVXEMU_GNU_STACK
#else
#define AVXEMU_RAW_SECTION .section .avxemu_raw,"ax",@progbits
#define AVXEMU_TEST_SECTION .section .avxemu_test,"ax",@progbits
#define AVXEMU_FUNCTION(name) .globl AVXEMU_SYM(name); .type AVXEMU_SYM(name),@function; AVXEMU_SYM(name):
#define AVXEMU_END_FUNCTION(name) .size AVXEMU_SYM(name),.-AVXEMU_SYM(name)
#define AVXEMU_HIDDEN(name) .hidden AVXEMU_SYM(name)
#define AVXEMU_GNU_STACK .section .note.GNU-stack,"",@progbits
#endif
#endif

#endif
