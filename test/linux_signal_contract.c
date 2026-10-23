#define _XOPEN_SOURCE 700
#include "platform/loader.h"
#include <assert.h>
#include <stdint.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef __USE_MISC
#error This test must use the glibc strict-source signal redirect
#endif
extern void avxemu_force_install(void);
extern int avxemu_test_ud2;
extern uint64_t e2e_shlx(uint64_t, uint64_t);
extern void (*sysv_signal(int, void (*)(int)))(int);
extern void (*bsd_signal(int, void (*)(int)))(int);
extern void (*ssignal(int, void (*)(int)))(int);
static volatile sig_atomic_t calls, bad;
static int oneshot;
static void caught(int sig) {
    sigset_t mask;
    struct sigaction current;
    if (sig!=SIGILL || sigprocmask(SIG_SETMASK,0,&mask) ||
        sigismember(&mask,SIGILL)==oneshot ||
        sigaction(SIGILL,0,&current) ||
        (oneshot && current.sa_handler!=SIG_DFL)) bad=1;
    calls++;
}
static int child(int mode, int die) {
    typedef void (*handler)(int);
    typedef handler (*register_handler)(int, handler);
    register_handler functions[]={signal,sysv_signal,bsd_signal,ssignal};
    register_handler install=functions[mode];
    oneshot=mode<2;
    avxemu_force_install();
    struct sigaction application, kernel;
    if (install(SIGILL,caught)==SIG_ERR || sigaction(SIGILL,0,&application) ||
        avxemu_real_sigaction(SIGILL,0,&kernel) || application.sa_handler!=caught ||
        kernel.sa_handler==caught) return 10;
    if (!!(application.sa_flags&SA_RESETHAND)!=oneshot ||
        !!(application.sa_flags&SA_NODEFER)!=oneshot ||
        !!(application.sa_flags&SA_RESTART)==oneshot) return 11;
    errno=0;
    if (install(SIGILL,SIG_ERR)!=SIG_ERR || errno!=EINVAL) return 12;
    if (sigaction(SIGILL,0,&application) || application.sa_handler!=caught) return 13;
    avxemu_test_ud2=1;
    if (e2e_shlx(7,3)!=56 || calls) return 14;
    raise(SIGILL);
    if (calls!=1 || bad || sigaction(SIGILL,0,&application) ||
        application.sa_handler!=(oneshot?SIG_DFL:caught)) return 15;
    if (die || !oneshot) raise(SIGILL);
    return die ? 16 : (calls!=(oneshot?1:2) || bad);
}
int main(void) {
    int fail=0;
    for (int mode=0; mode<4; mode++) for (int die=0; die<(mode<2?2:1); die++) {
        pid_t pid=fork(); int status=0;
        assert(pid>=0);
        if (!pid) { alarm(10); _exit(child(mode,die)); }
        assert(waitpid(pid,&status,0)==pid);
        int ok=die ? WIFSIGNALED(status)&&WTERMSIG(status)==SIGILL : WIFEXITED(status)&&!WEXITSTATUS(status);
        if (!ok) { printf("FAIL signal alias %d death %d status %x\n",mode,die,status); fail++; }
    }
    printf("linux-signal-contract: %s (strict signal, System V and BSD aliases)\n",fail?"FAIL":"PASS");
    return !!fail;
}
