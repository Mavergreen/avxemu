#define _GNU_SOURCE
#include "platform/context.h"
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

avxemu_regfile context_input, context_output;
uint64_t context_redzone[16], context_resumed;
unsigned context_mxcsr;
unsigned short context_x87;
extern void context_probe(void);
extern void avxemu_force_install(void);
extern int avxemu_test_ud2;

static volatile sig_atomic_t adapter_fail;
static void inspect(int sig, siginfo_t *info, void *context) {
    avxemu_regfile integer, vector, after;
    (void)sig; (void)info;
    memset(&integer,0x5a,sizeof integer);
    if (!avxemu_context_read(context,&integer,0) ||
        !avxemu_context_read(context,&vector,1)) { adapter_fail=1; return; }
    for (int i=0;i<16;i++) for (int j=0;j<32;j++)
        if(integer.ymm[i][j]!=0x5a) adapter_fail=1;
    if (integer.rip!=avxemu_context_ip(context)) adapter_fail=1;
    /* Integer-only writes must leave every vector untouched. */
    if (!avxemu_context_write(context,&integer,0) ||
        !avxemu_context_read(context,&after,1) ||
        memcmp(vector.ymm,after.ymm,sizeof vector.ymm)) adapter_fail=1;
}

#ifdef __linux__
#include "platform/linux/xstate.h"
#include <ucontext.h>
extern void context_initial_probe(void);
uint8_t context_initial_output[32];
static void initialize_vector(int sig, siginfo_t *info, void *context) {
    avxemu_xstate state;
    avxemu_regfile rf;
    ucontext_t *uc=context;
    (void)info;
    if (sig!=SIGILL || !avxemu_xstate_live(uc->uc_mcontext.fpregs,&state) ||
        (state.active&4) || !avxemu_context_read(context,&rf,1)) {
        adapter_fail=1;
    } else {
        for (int i=0; i<16; i++) for (int j=16; j<32; j++)
            if (rf.ymm[i][j]) adapter_fail=1;
        for (int j=0; j<32; j++) rf.ymm[0][j]=(uint8_t)(j+1);
        rf.rip+=2;
        if (!avxemu_context_write(context,&rf,1)) adapter_fail=1;
        else return;
    }
    avxemu_context_set_ip(context,avxemu_context_ip(context)+2);
}
static int initial_sigreturn(void) {
    struct sigaction action, old;
    memset(&action,0,sizeof action); sigemptyset(&action.sa_mask);
    action.sa_sigaction=initialize_vector; action.sa_flags=SA_SIGINFO;
    if (sigaction(SIGILL,&action,&old)) return 1;
    context_initial_probe();
    if (sigaction(SIGILL,&old,0) || adapter_fail) return 1;
    for (int j=0; j<32; j++) if (context_initial_output[j]!=(uint8_t)(j+1)) return 1;
    return 0;
}
#endif

int main(void) {
    unsigned saved, selected;
    unsigned short saved_x87, selected_x87;
    int fail = 0;
#ifdef __linux__
    if (initial_sigreturn()) { puts("FAIL initially inactive YMM sigreturn"); fail++; }
#endif
    avxemu_force_install();
    avxemu_test_ud2 = 1;
    avxemu_regfile untouched, before;
    memset(&untouched,0x5a,sizeof untouched); before=untouched;
    if (avxemu_context_read(0,&untouched,1) || memcmp(&before,&untouched,sizeof before) ||
        avxemu_context_write(0,&untouched,1)) fail++;
    struct sigaction action; memset(&action,0,sizeof action);
    action.sa_sigaction=inspect; action.sa_flags=SA_SIGINFO; sigemptyset(&action.sa_mask);
    if (sigaction(SIGUSR2,&action,0) || raise(SIGUSR2) || adapter_fail) fail++;
    for (int i=0;i<16;i++) {
        context_input.gpr[i] = 0x123456789abc0000ull + i;
        for (int j=0;j<32;j++) context_input.ymm[i][j] = (uint8_t)(i*37+j*3+1);
    }
    context_input.gpr[0] = 0x12345678;
    context_input.gpr[2] = 3;
    __asm__ volatile("stmxcsr %0" : "=m"(saved));
    selected = (saved & ~0x6000u) | 0x2000u | 0x20u;
    __asm__ volatile("ldmxcsr %0" :: "m"(selected));
    __asm__ volatile("fnstcw %0" : "=m"(saved_x87));
    selected_x87=(saved_x87 & ~0xc00u) | 0x800u;
    __asm__ volatile("fldcw %0" :: "m"(selected_x87));
    context_probe();
    __asm__ volatile("fldcw %0" :: "m"(saved_x87));
    __asm__ volatile("ldmxcsr %0" :: "m"(saved));
    context_input.gpr[0] <<= 3;
    for (int i=0;i<8;i++) {
        uint32_t a,b;
        memcpy(&a,context_input.ymm[0]+4*i,4);
        memcpy(&b,context_input.ymm[1]+4*i,4);
        a+=b; memcpy(context_input.ymm[0]+4*i,&a,4);
    }
    for (int i=0;i<16;i++) {
        if (memcmp(context_input.ymm[i],context_output.ymm[i],32)) {
            printf("FAIL ymm%d (including upper half)\n",i); fail++;
        }
        if (context_input.gpr[i]!=context_output.gpr[i]) {
            printf("FAIL gpr%d\n",i); fail++;
        }
    }
    for(int i=0;i<16;i++) if(context_redzone[i]!=0x5a5a) fail++;
    if ((context_output.rflags & 0x8d5)!=0x45 || context_mxcsr!=selected || context_x87!=selected_x87 || context_resumed!=1) fail++;
    printf("context-contract: %s (GPRs, all YMM halves, flags, resume, red zone, MXCSR, x87)\n",fail?"FAIL":"PASS");
    return fail?1:0;
}
