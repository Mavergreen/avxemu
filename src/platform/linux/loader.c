#define _GNU_SOURCE
#include "platform/loader.h"
#include <dlfcn.h>
#include <errno.h>
#include <stdatomic.h>
#include <string.h>

/* Linux assigns nonpositive codes to kill/queue/tkill and other software
 * sources. SI_KERNEL and instruction exception codes are positive. */
int avxemu_signal_generated(const siginfo_t *info) { return info && info->si_code<=0; }

typedef int (*sigaction_fn)(int, const struct sigaction *, struct sigaction *);
static _Atomic(sigaction_fn) genuine_sigaction;
/* dlvsym is declared leaf by glibc. Volatile also keeps the guard observable
 * to a signal or an interposed resolver that reenters during initialization. */
static _Thread_local volatile sig_atomic_t resolving;

int avxemu_loader_resolve(void) {
    if (atomic_load_explicit(&genuine_sigaction,memory_order_acquire)) return 1;
    if (resolving) return 0;
    resolving=1;
    /* glibc x86-64's original symbol version excludes our unversioned ELF
     * wrapper. RTLD_NEXT skips this object's definition as well. */
    sigaction_fn resolved=(sigaction_fn)dlvsym(RTLD_NEXT,"sigaction","GLIBC_2.2.5");
    resolving=0;
    if (!resolved || resolved==sigaction || resolved==avxemu_sigaction || resolved==avxemu_real_sigaction) return 0;
    atomic_store_explicit(&genuine_sigaction,resolved,memory_order_release);
    return 1;
}
int avxemu_loader_init(void) { return avxemu_loader_resolve(); }
int avxemu_real_sigaction(int sig, const struct sigaction *act, struct sigaction *old) {
    sigaction_fn genuine=atomic_load_explicit(&genuine_sigaction,memory_order_acquire);
    if (!genuine) {
        if (!avxemu_loader_resolve()) { errno=ENOSYS; return -1; }
        genuine=atomic_load_explicit(&genuine_sigaction,memory_order_acquire);
    }
    return genuine(sig,act,old);
}
int sigaction(int sig, const struct sigaction *act, struct sigaction *old) {
    return avxemu_sigaction(sig,act,old);
}
void (*signal(int sig, void (*handler)(int)))(int) {
    if (handler==SIG_ERR) { errno=EINVAL; return SIG_ERR; }
    return avxemu_signal(sig,handler);
}

/* glibc signal.h redirects strict-C/XOPEN callers to __sysv_signal. Both
 * System V names must enter the same ownership policy as sigaction. */
void (*__sysv_signal(int sig, void (*handler)(int)))(int) {
    struct sigaction action, old;
    if (handler==SIG_ERR) { errno=EINVAL; return SIG_ERR; }
    memset(&action,0,sizeof action);
    action.sa_handler=handler;
    action.sa_flags=SA_RESETHAND|SA_NODEFER|SA_INTERRUPT;
    sigemptyset(&action.sa_mask);
    if (avxemu_sigaction(sig,&action,&old)) return SIG_ERR;
    return old.sa_handler;
}
void (*sysv_signal(int sig, void (*handler)(int)))(int) {
    return __sysv_signal(sig,handler);
}
void (*bsd_signal(int sig, void (*handler)(int)))(int) {
    return signal(sig,handler);
}
void (*ssignal(int sig, void (*handler)(int)))(int) {
    return signal(sig,handler);
}
