#include "asm.h"

// Decoder-test blob: a contiguous run of target instructions whose byte ranges
// the decoder test reads (never executed). The e2e_* fault-injection stubs live
// in ../src/selftest.S (shared with the in-dylib self-test).

AVXEMU_TEST_SECTION
.p2align 4
.globl AVXEMU_SYM(DI0)
AVXEMU_SYM(DI0):  vpaddd %ymm2,%ymm1,%ymm0
AVXEMU_SYM(DI1):  vpand %ymm2,%ymm1,%ymm0
AVXEMU_SYM(DI2):  vpsubq %ymm2,%ymm1,%ymm0
AVXEMU_SYM(DI3):  vpcmpeqb %ymm2,%ymm1,%ymm0
AVXEMU_SYM(DI4):  vpshufb %ymm2,%ymm1,%ymm0
AVXEMU_SYM(DI5):  vpmaxsd %ymm2,%ymm1,%ymm0
AVXEMU_SYM(DI6):  vpshufd $0x1b,%ymm1,%ymm0
AVXEMU_SYM(DI7):  vpermq $0x1b,%ymm1,%ymm0
AVXEMU_SYM(DI8):  vpslld $5,%ymm1,%ymm0
AVXEMU_SYM(DI9):  vpslld %xmm2,%ymm1,%ymm0
AVXEMU_SYM(DI10): vpsllvd %ymm2,%ymm1,%ymm0
AVXEMU_SYM(DI11): vpbroadcastb %xmm1,%ymm0
AVXEMU_SYM(DI12): vpmovzxbw %xmm1,%ymm0
AVXEMU_SYM(DI13): vextracti128 $1,%ymm1,%xmm0
AVXEMU_SYM(DI14): vinserti128 $1,%xmm2,%ymm1,%ymm0
AVXEMU_SYM(DI15): vperm2i128 $0x20,%ymm2,%ymm1,%ymm0
AVXEMU_SYM(DI16): vpblendd $0xc,%ymm2,%ymm1,%ymm0
AVXEMU_SYM(DI17): vpalignr $5,%ymm2,%ymm1,%ymm0
AVXEMU_SYM(DI18): vpblendvb %ymm3,%ymm2,%ymm1,%ymm0
AVXEMU_SYM(DI19): vpmovmskb %ymm1,%eax
AVXEMU_SYM(DI20): vfmadd213pd %ymm2,%ymm1,%ymm0
AVXEMU_SYM(DI21): vfmadd231sd %xmm2,%xmm1,%xmm0
AVXEMU_SYM(DI22): vcvtph2ps %xmm1,%ymm0
AVXEMU_SYM(DI23): andnq %r8,%rdi,%rax
AVXEMU_SYM(DI24): mulxq %r8,%rax,%r9
AVXEMU_SYM(DI25): shlxq %rcx,%r8,%rax
AVXEMU_SYM(DI26): rorxq $13,%r8,%rax
AVXEMU_SYM(DI27): tzcntq %r8,%rax
AVXEMU_SYM(DI28): pdepq %r8,%rdi,%rax
AVXEMU_SYM(DI29): vpaddd (%rsi),%ymm1,%ymm0
AVXEMU_SYM(DIEND):

.data
.p2align 4
.globl AVXEMU_SYM(dec_labels)
AVXEMU_SYM(dec_labels):
    .quad AVXEMU_SYM(DI0),AVXEMU_SYM(DI1),AVXEMU_SYM(DI2),AVXEMU_SYM(DI3),AVXEMU_SYM(DI4),AVXEMU_SYM(DI5),AVXEMU_SYM(DI6),AVXEMU_SYM(DI7),AVXEMU_SYM(DI8),AVXEMU_SYM(DI9)
    .quad AVXEMU_SYM(DI10),AVXEMU_SYM(DI11),AVXEMU_SYM(DI12),AVXEMU_SYM(DI13),AVXEMU_SYM(DI14),AVXEMU_SYM(DI15),AVXEMU_SYM(DI16),AVXEMU_SYM(DI17),AVXEMU_SYM(DI18),AVXEMU_SYM(DI19)
    .quad AVXEMU_SYM(DI20),AVXEMU_SYM(DI21),AVXEMU_SYM(DI22),AVXEMU_SYM(DI23),AVXEMU_SYM(DI24),AVXEMU_SYM(DI25),AVXEMU_SYM(DI26),AVXEMU_SYM(DI27),AVXEMU_SYM(DI28),AVXEMU_SYM(DI29)
    .quad AVXEMU_SYM(DIEND)
.globl AVXEMU_SYM(dec_n)
AVXEMU_SYM(dec_n): .quad 30

AVXEMU_GNU_STACK
