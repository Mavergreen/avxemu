#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <sys/syscall.h>
extern int pending_unblock(const uint64_t *);
static volatile sig_atomic_t calls;
static void handler(int sig) { if(sig==SIGILL) calls++; }
int main(void) {
    struct sigaction sa={0}; sa.sa_handler=handler; sigemptyset(&sa.sa_mask);
    if(sigaction(SIGILL,&sa,0)) return 2;
    sigset_t set; sigemptyset(&set); sigaddset(&set,SIGILL);
    if(sigprocmask(SIG_BLOCK,&set,0)) return 3;
    if(syscall(SYS_tgkill,getpid(),syscall(SYS_gettid),SIGILL)) return 4;
    uint64_t mask=UINT64_C(1)<<(SIGILL-1);
    int result=pending_unblock(&mask);
    printf("application SIGILL calls=%d, tzcnt result=%d\n",calls,result);
    return calls!=1;
}
