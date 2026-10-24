#define _XOPEN_SOURCE 700
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#if defined(__linux__) && !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE
#endif
#include "platform/context.h"
#include "platform/loader.h"
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach/mach_time.h>
#endif

#ifdef __linux__
#define public_sigaction sigaction
#define public_signal signal
#else
#define public_sigaction avxemu_sigaction
#define public_signal avxemu_signal
#endif

extern void avxemu_force_install(void);
extern int avxemu_test_ud2;
extern uint64_t e2e_shlx(uint64_t, uint64_t);
extern void guard_operand(const void *);
static int expected_signal=SIGILL, mask_faults;
static volatile sig_atomic_t calls, bad, nodefer, oneshot;
static void simple(int sig) { if(sig!=expected_signal) bad=1; calls++; }
static void detailed(int sig, siginfo_t *info, void *context) {
    sigset_t mask;
    sigprocmask(SIG_SETMASK,0,&mask);
    if(sig!=SIGILL || !info || info->si_signo!=sig || !context ||
       !sigismember(&mask,SIGUSR1) || sigismember(&mask,SIGILL)==nodefer) bad=1;
    if(mask_faults && (!sigismember(&mask,SIGBUS) || !sigismember(&mask,SIGSEGV))) bad=1;
    calls++;
    if(oneshot) {
        struct sigaction current;
        if(avxemu_sigaction(SIGILL,0,&current) || current.sa_handler!=SIG_DFL) bad=1;
    }
    if(nodefer && calls==1) raise(SIGILL);
}
static void fault(int sig, siginfo_t *info, void *context) {
    uintptr_t ip=avxemu_context_ip(context);
    if(sig!=SIGILL || !info || !context || info->si_addr!=(void *)ip ||
       ((const unsigned char *)ip)[0]!=0x0f || ((const unsigned char *)ip)[1]!=0x0b) bad=1;
    calls++;
    avxemu_context_set_ip(context,avxemu_context_ip(context)+2);
}
/* Real monotonic deadlines remain finite when the host is busy or a sleep is
 * interrupted. Mavericks predates clock_gettime. */
static uint64_t monotonic_ms(void) {
#ifdef __APPLE__
    mach_timebase_info_data_t scale;
    if(mach_timebase_info(&scale)!=KERN_SUCCESS) _exit(70);
    return (uint64_t)((long double)mach_absolute_time()*scale.numer/scale.denom/1000000);
#else
    struct timespec now;
    if(clock_gettime(CLOCK_MONOTONIC,&now)) _exit(70);
    return (uint64_t)now.tv_sec*1000+(uint64_t)now.tv_nsec/1000000;
#endif
}
/* Parent-side SIGKILL deadlines also cover a child that has blocked every
 * catchable signal while waiting for a broken policy lock. */
static int wait_child(pid_t child, int *status, int milliseconds) {
    uint64_t deadline=monotonic_ms()+(unsigned)milliseconds;
    struct timespec tick={0,1000000};
    do {
        pid_t result=waitpid(child,status,WNOHANG);
        if(result==child) return 1;
        if(result<0 && errno!=EINTR) return 0;
        nanosleep(&tick,0);
    } while(monotonic_ms()<deadline);
    kill(child,SIGKILL);
    while(waitpid(child,status,0)<0 && errno==EINTR) {}
    return 0;
}
static volatile sig_atomic_t loader_ack=-1;
static void acknowledge(int sig) {
    int saved=errno;
    simple(sig);
    if(write(loader_ack,"a",1)!=1) bad=1;
    errno=saved;
}
/* A handshake publishes the acknowledgment FD before delivery. The peer
 * supplies data only after the handler acknowledges. A successful read in the
 * non-restart case can mean the signal arrived before read entered the kernel:
 * that attempt is inconclusive, so retry until EINTR or the real deadline.
 * Deliberately force the first attempt early to exercise that retry. */
static int interrupted_read(int restart) {
    uint64_t deadline=monotonic_ms()+10000;
    for(int attempt=0;monotonic_ms()<deadline;attempt++) {
        int data[2],ack[2],ready[2],status;char byte=0;
        if(pipe(data)||pipe(ack)||pipe(ready)) return 20;
        loader_ack=ack[1];
        pid_t peer=fork();
        if(peer<0) return 21;
        if(!peer) {
            close(ack[1]);close(data[0]);close(ready[1]);
            if(read(ready[0],&byte,1)!=1 || kill(getppid(),SIGILL) ||
                read(ack[0],&byte,1)!=1 || write(data[1],"x",1)!=1) _exit(1);
            _exit(0);
        }
        close(ack[0]);close(data[1]);close(ready[0]);
        if(write(ready[1],"r",1)!=1) return 22;
        close(ready[1]);
        int reaped=0;
        if(!attempt) { /* Known early delivery: never counts as contract evidence. */
            if(!wait_child(peer,&status,2000)) return 23;
            reaped=1;
        }
        errno=0;
        ssize_t n=read(data[0],&byte,1);int error=errno;
        if(n==-1 && error==EINTR) {
            ssize_t drained;
            do { drained=read(data[0],&byte,1); } while(drained<0 && errno==EINTR);
            if(drained!=1) return 24;
        }
        close(data[0]);close(ack[1]);loader_ack=-1;
        if(!reaped && !wait_child(peer,&status,2000)) return 23;
        if(!WIFEXITED(status) || WEXITSTATUS(status)) return 23;
        if(!attempt) continue;
        if(restart) return !(n==1 && byte=='x');
        if(n==-1 && error==EINTR) return 0;
        if(n!=1 || byte!='x') return 25;
    }
    return 26; /* No conclusive interrupt before the deadline. */
}

/* Registration and queries race on four application threads. The coordinator
 * checks the quiescent kernel/virtual pair after every batch, then releases the
 * next batch. No test mutex is held while invoking the signal policy. */
enum { RACE_THREADS=4, RACE_ROUNDS=400 };
static pthread_mutex_t race_mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t race_cond=PTHREAD_COND_INITIALIZER;
static unsigned race_arrivals, race_generation;
static volatile int race_bad, race_stop, race_signals;
static pthread_t race_threads[RACE_THREADS];
static void rendezvous(unsigned participants) {
    pthread_mutex_lock(&race_mutex);
    unsigned generation=race_generation;
    if(++race_arrivals==participants) {
        race_arrivals=0; race_generation++;
        pthread_cond_broadcast(&race_cond);
    } else while(generation==race_generation) pthread_cond_wait(&race_cond,&race_mutex);
    pthread_mutex_unlock(&race_mutex);
}
static void register_a(int sig) { (void)sig; }
static void register_b(int sig) { (void)sig; }
static void race_action(struct sigaction *a, int id) {
    memset(a,0,sizeof *a); sigemptyset(&a->sa_mask);
    a->sa_handler=id&1?register_a:register_b;
    a->sa_flags=id&1?SA_RESTART:SA_NODEFER;
    sigaddset(&a->sa_mask,id&1?SIGUSR1:SIGUSR2);
}
static int action_valid(const struct sigaction *a) {
    int id=a->sa_handler==register_a;
    return (id || a->sa_handler==register_b) && a->sa_flags==(id?SA_RESTART:SA_NODEFER) &&
        sigismember(&a->sa_mask,SIGUSR1)==id && sigismember(&a->sa_mask,SIGUSR2)==!id;
}
static void query_from_signal(int sig) {
    struct sigaction a;
    if(sig!=SIGUSR1 || avxemu_sigaction(SIGILL,0,&a) || !action_valid(&a))
        __sync_lock_test_and_set(&race_bad,1);
    __sync_add_and_fetch(&race_signals,1);
}
static void *registration_sender(void *argument) {
    (void)argument;
    while(!__sync_fetch_and_add(&race_stop,0)) {
        for(int i=0;i<RACE_THREADS;i++) pthread_kill(race_threads[i],SIGUSR1);
        struct timespec tick={0,1000000}; nanosleep(&tick,0);
    }
    return 0;
}
static void *registration_worker(void *argument) {
    int id=(int)(intptr_t)argument;
    struct sigaction a, old;
    race_action(&a,id);
    for(int round=0;round<RACE_ROUNDS;round++) {
        rendezvous(RACE_THREADS+1);
        for(int repeat=0;repeat<8;repeat++) {
            if(avxemu_sigaction(SIGILL,&a,&old) || !action_valid(&old) ||
               avxemu_sigaction(SIGILL,0,&old) || !action_valid(&old))
                __sync_lock_test_and_set(&race_bad,1);
        }
        rendezvous(RACE_THREADS+1);
        rendezvous(RACE_THREADS+1);
    }
    return 0;
}
static int registration_race(void) {
    pthread_t sender;
    struct sigaction initial, application, kernel, query;
    memset(&query,0,sizeof query); sigemptyset(&query.sa_mask);
    query.sa_handler=query_from_signal;
    if(avxemu_sigaction(SIGUSR1,&query,0)) return 41;
    race_action(&initial,0);
    if(avxemu_sigaction(SIGILL,&initial,0)) return 30;
    for(int i=0;i<RACE_THREADS;i++)
        if(pthread_create(&race_threads[i],0,registration_worker,(void *)(intptr_t)i)) return 31;
    if(pthread_create(&sender,0,registration_sender,0)) return 42;
    for(int round=0;round<RACE_ROUNDS;round++) {
        rendezvous(RACE_THREADS+1);
        rendezvous(RACE_THREADS+1);
        if(avxemu_sigaction(SIGILL,0,&application) || avxemu_real_sigaction(SIGILL,0,&kernel) ||
           !action_valid(&application) ||
           (application.sa_flags&(SA_RESTART|SA_NODEFER))!=(kernel.sa_flags&(SA_RESTART|SA_NODEFER)) ||
           sigismember(&application.sa_mask,SIGUSR1)!=sigismember(&kernel.sa_mask,SIGUSR1) ||
           sigismember(&application.sa_mask,SIGUSR2)!=sigismember(&kernel.sa_mask,SIGUSR2)) race_bad=1;
        rendezvous(RACE_THREADS+1);
    }
    __sync_lock_test_and_set(&race_stop,1);
    pthread_join(sender,0);
    for(int i=0;i<RACE_THREADS;i++) pthread_join(race_threads[i],0);
    return race_bad||!race_signals?32:0;
}

/* Two simultaneous deliveries must claim a one-shot handler at most once.
 * Its first invocation installs a persistent replacement from inside the
 * callback; a pending reset must never overwrite that replacement. */
static volatile int claim_calls, replacement_calls;
static void replacement(int sig) {
    if(sig!=SIGILL) _exit(33);
    __sync_add_and_fetch(&replacement_calls,1);
}
static void claim_once(int sig) {
    if(sig!=SIGILL || __sync_add_and_fetch(&claim_calls,1)!=1) _exit(34);
    struct sigaction a; memset(&a,0,sizeof a);
    a.sa_handler=replacement; a.sa_flags=SA_RESTART; sigemptyset(&a.sa_mask);
    if(avxemu_sigaction(SIGILL,&a,0)) _exit(35);
}
static void *delivery_worker(void *argument) {
    (void)argument;
    rendezvous(3);
    /* Target each thread explicitly: process-directed standard signals may
     * coalesce, which cannot establish two separate one-shot claims. */
    if(pthread_kill(pthread_self(),SIGILL)) _exit(51);
    return 0;
}
static int oneshot_race(void) {
    /* Each attempt is isolated: the second delivery can legitimately claim
     * SIG_DFL before the first callback installs its replacement. */
    for(int attempt=0;attempt<100;attempt++) {
        pid_t p=fork(); int status=0;
        if(!p) {
            pthread_t threads[2]; struct sigaction a, current; memset(&current,0,sizeof current);
            memset(&a,0,sizeof a); a.sa_handler=claim_once;
            a.sa_flags=SA_RESETHAND; sigemptyset(&a.sa_mask);
            if(avxemu_sigaction(SIGILL,&a,0) ||
               pthread_create(&threads[0],0,delivery_worker,0) ||
               pthread_create(&threads[1],0,delivery_worker,0)) _exit(36);
            rendezvous(3);
            pthread_join(threads[0],0); pthread_join(threads[1],0);
            if(claim_calls!=1 || replacement_calls!=1 || avxemu_sigaction(SIGILL,0,&current) ||
               current.sa_handler!=replacement || !(current.sa_flags&SA_RESTART)) {
                fprintf(stderr,"FAIL one-shot counts %d/%d handler %p flags %x\n",claim_calls,replacement_calls,(void *)current.sa_handler,current.sa_flags);
                _exit(37);
            }
            raise(SIGILL);
            _exit(replacement_calls==2?0:38);
        }
        if(p<0 || !wait_child(p,&status,5000)) return 39;
        if(!((WIFSIGNALED(status)&&WTERMSIG(status)==SIGILL) ||
             (WIFEXITED(status)&&WEXITSTATUS(status)==0))) {
            fprintf(stderr,"FAIL one-shot attempt %d status %x\n",attempt,status);
            return 40;
        }
    }
    return 0;
}

/* Fork while other threads repeatedly update kernel/virtual state. The child
 * must recover a complete committed disposition without an inherited lock. */
static void *fork_registration_worker(void *argument) {
    struct sigaction a; race_action(&a,(int)(intptr_t)argument);
    while(!__sync_fetch_and_add(&race_stop,0))
        if(avxemu_sigaction(SIGILL,&a,0)) __sync_lock_test_and_set(&race_bad,1);
    return 0;
}
static volatile int recovery_arrivals;
static void *child_recovery_worker(void *argument) {
    (void)argument;
    struct sigaction a;
    __sync_add_and_fetch(&recovery_arrivals,1);
    while(__sync_fetch_and_add(&recovery_arrivals,0)!=RACE_THREADS) __asm__ volatile("pause");
    if(avxemu_sigaction(SIGILL,0,&a) || !action_valid(&a)) __sync_lock_test_and_set(&race_bad,1);
    return 0;
}
static int fork_registration_race(void) {
    pthread_t threads[RACE_THREADS]; struct sigaction a;
    race_action(&a,0);
    if(avxemu_sigaction(SIGILL,&a,0)) return 43;
    for(int i=0;i<RACE_THREADS;i++)
        if(pthread_create(&threads[i],0,fork_registration_worker,(void *)(intptr_t)i)) return 44;
    int fail=0;
    for(int attempt=0;attempt<100;attempt++) {
        pid_t p=fork(); int status=0;
        if(!p) {
            struct sigaction app,kernel;
            pthread_t recovery[RACE_THREADS];
            alarm(5);
            /* Race the child's first policy entries as well as the fork. */
            for(int i=0;i<RACE_THREADS;i++)
                if(pthread_create(&recovery[i],0,child_recovery_worker,0)) _exit(49);
            for(int i=0;i<RACE_THREADS;i++) pthread_join(recovery[i],0);
            if(race_bad) _exit(50);
            if(avxemu_sigaction(SIGILL,0,&app) || avxemu_real_sigaction(SIGILL,0,&kernel) ||
               !action_valid(&app) || (app.sa_flags&(SA_RESTART|SA_NODEFER))!=(kernel.sa_flags&(SA_RESTART|SA_NODEFER)) ||
               sigismember(&app.sa_mask,SIGUSR1)!=sigismember(&kernel.sa_mask,SIGUSR1) ||
               sigismember(&app.sa_mask,SIGUSR2)!=sigismember(&kernel.sa_mask,SIGUSR2)) _exit(45);
            raise(SIGILL);
            a.sa_handler=simple;
            if(avxemu_sigaction(SIGILL,&a,0)) _exit(46);
            raise(SIGILL);
            if(calls!=1 || bad) {
                sigset_t mask; sigprocmask(SIG_SETMASK,0,&mask);
                fprintf(stderr,"FAIL fork delivery calls %d bad %d blocked %d\n",calls,bad,sigismember(&mask,SIGILL));
                _exit(47);
            }
            _exit(0);
        }
        if(p<0 || !wait_child(p,&status,5000) || !WIFEXITED(status) || WEXITSTATUS(status)) {
            fprintf(stderr,"FAIL fork attempt %d status %x\n",attempt,status);
            fail=1; break;
        }
    }
    __sync_lock_test_and_set(&race_stop,1);
    for(int i=0;i<RACE_THREADS;i++) pthread_join(threads[i],0);
    return fail||race_bad?48:0;
}

static int child(int mode) {
    struct sigaction a,o,k;
    memset(&a,0,sizeof a); sigemptyset(&a.sa_mask);
    a.sa_handler=simple; a.sa_flags=SA_RESTART;
    if(mode>=19) {
        a.sa_flags=SA_RESTART|SA_NODEFER;
        a.sa_handler=mode==19?SIG_DFL:SIG_IGN;
        sigaddset(&a.sa_mask,SIGBUS); sigaddset(&a.sa_mask,SIGSEGV);
        if(mode>=21) {
            mask_faults=1; a.sa_sigaction=detailed; a.sa_flags=SA_SIGINFO|SA_RESTART;
            sigaddset(&a.sa_mask,SIGUSR1);
        }
        /* Capture the kernel's canonical record (glibc adds SA_RESTORER)
         * before activation; ownership must preserve that exact query. */
        if(avxemu_sigaction(SIGILL,&a,0) || avxemu_sigaction(SIGILL,0,&a)) return 60;
    }
    if(mode==0 && avxemu_sigaction(SIGILL,&a,0)) return 10;
    setenv("AVXEMU_FORCEPATCH","1",1);
    avxemu_force_install();
    if(mode>=19) {
        if(avxemu_sigaction(SIGILL,0,&o) || o.sa_flags!=a.sa_flags ||
           o.sa_handler!=a.sa_handler || !sigismember(&o.sa_mask,SIGBUS) ||
           !sigismember(&o.sa_mask,SIGSEGV)) return 61;
        avxemu_test_ud2=1;
        if(e2e_shlx(7,3)!=56 || calls) return 62;
        if(mode>=21) { raise(SIGILL); if(calls!=1 || bad) return 63; }
        if(mode==22) {
            /* A caller-blocked pending fault signal must stay pending through
             * recognized emulation, even with a callback mask installed. */
            sigset_t blocked, saved, pending;
            struct sigaction bus; memset(&bus,0,sizeof bus);
            sigemptyset(&bus.sa_mask); bus.sa_handler=simple;
            sigemptyset(&blocked); sigaddset(&blocked,SIGBUS);
            expected_signal=SIGBUS; calls=0;
            if(avxemu_sigaction(SIGBUS,&bus,0) ||
               sigprocmask(SIG_BLOCK,&blocked,&saved) || raise(SIGBUS) ||
               e2e_shlx(9,2)!=36 || calls || sigpending(&pending) ||
               !sigismember(&pending,SIGBUS)) return 66;
            if(sigprocmask(SIG_SETMASK,&saved,0) || calls!=1 || bad) return 67;
            return 0;
        }
        size_t page=(size_t)sysconf(_SC_PAGESIZE);
        unsigned char *mem=mmap(0,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
        if(mem==MAP_FAILED || mprotect(mem+page,page,PROT_NONE)) return 64;
        guard_operand(mem+page-16);
        return 65; /* genuine operand fault must terminate this child */
    }
    if(mode==16) return registration_race();
    if(mode==17) return oneshot_race();
    if(mode==18) return fork_registration_race();
    if(mode==0) {
        if(avxemu_sigaction(SIGILL,0,&o) || o.sa_handler!=simple || !(o.sa_flags&SA_RESTART)) return 11;
        raise(SIGILL); return calls==1&&!bad?0:12;
    }
    if(mode==1 || mode==7 || mode==8) {
        a.sa_sigaction=detailed; a.sa_flags=SA_SIGINFO|SA_RESTART;
        sigaddset(&a.sa_mask,SIGUSR1);
        if(mode==7) { a.sa_flags|=SA_NODEFER; nodefer=1; }
        if(mode==8) { a.sa_flags|=SA_RESETHAND; oneshot=1; }
    }
    if(mode==2) a.sa_handler=SIG_IGN;
    if(mode==3) a.sa_handler=SIG_DFL;
    if(mode==4 || mode==15) a.sa_flags=SA_RESETHAND;
    if(mode==5) { a.sa_sigaction=fault; a.sa_flags=SA_SIGINFO; }
    if(mode==9 || mode==10) { a.sa_handler=acknowledge; a.sa_flags=mode==9?SA_RESTART:0; }
    if(public_sigaction(SIGILL,&a,&o) || public_sigaction(SIGILL,0,&o)) return 13;
    if(o.sa_handler!=a.sa_handler || o.sa_flags!=a.sa_flags || sigismember(&o.sa_mask,SIGUSR1)!=sigismember(&a.sa_mask,SIGUSR1)) return 14;
    if(avxemu_real_sigaction(SIGILL,0,&k) || k.sa_handler==a.sa_handler ||
       (k.sa_flags&SA_RESTART)!=((mode==2 || mode==3)?0:(a.sa_flags&SA_RESTART))) return 15;
    if(mode==9 || mode==10) return interrupted_read(mode==9)||bad;
    if(mode==11) {
        if(public_signal(SIGILL,simple)!=simple || avxemu_sigaction(SIGILL,0,&o) || !(o.sa_flags&SA_RESTART)) return 19;
        raise(SIGILL); raise(SIGILL); return calls==2&&!bad?0:18;
    }
    if(mode==13 || mode==14) {
        expected_signal=mode==13?SIGSEGV:SIGBUS;
        if(avxemu_sigaction(expected_signal,&a,0) || avxemu_sigaction(expected_signal,0,&o) ||
           o.sa_handler!=simple || avxemu_real_sigaction(expected_signal,0,&k) ||
           k.sa_handler==simple) return 24;
        raise(expected_signal); return calls==1&&!bad?0:18;
    }
    if(mode==15) {
        avxemu_test_ud2=1;
        if(e2e_shlx(7,3)!=56 || calls || avxemu_sigaction(SIGILL,0,&o) ||
           o.sa_handler!=simple || !(o.sa_flags&SA_RESETHAND)) return 25;
        raise(SIGILL);
        if(avxemu_sigaction(SIGILL,0,&o) || o.sa_handler!=SIG_DFL) return 26;
        return calls==1&&!bad?0:18;
    }
    if(mode==12) {
        if(avxemu_sigaction(SIGILL,&a,&a) || a.sa_handler!=simple) return 22;
    }
    if(mode==3 || mode==5) __asm__ volatile("ud2"); else raise(SIGILL);
    if(mode==4) { if(avxemu_sigaction(SIGILL,0,&o)||o.sa_handler!=SIG_DFL) return 16; raise(SIGILL); }
    if(mode==2) return calls?17:0;
    return calls==(mode==7?2:1)&&!bad?0:18;
}
int main(void) {
    int fail=0;
    unsetenv("AVXEMU_ASSUME_CAPABLE");
    static const char *names[]={"pre-activation", "siginfo-mask", "ignore-raised", "default-ud2",
        "one-shot-death", "siginfo-ud2", "simple", "nodefer-recursion", "one-shot-query",
        "restart-read", "interrupt-read", "signal-wrapper", "aliased-query",
        "segv-ownership", "bus-ownership", "emulation-keeps-one-shot", "registration-race", "one-shot-race", "fork-registration-race", "default-mask-operand", "ignored-mask-operand", "callback-mask-operand", "caller-mask-pending"};
    for(int mode=0;mode<23;mode++) {
        pid_t p=fork(); int status=0;
        if(!p) _exit(child(mode));
        if(p<0 || !wait_child(p,&status,(mode==17 || mode==18)?120000:20000)) {
            printf("FAIL loader %s timed out or could not be waited for\n",names[mode]);
            fail++; continue;
        }
        int ok=mode>=19 && mode<22 ? WIFSIGNALED(status)&&(WTERMSIG(status)==SIGSEGV||WTERMSIG(status)==SIGBUS) : (mode==3||mode==4)?WIFSIGNALED(status)&&WTERMSIG(status)==SIGILL:WIFEXITED(status)&&WEXITSTATUS(status)==0;
        if(!ok) { printf("FAIL loader %s status %x\n",names[mode],status); fail++; }
    }
    printf("loader-contract: %s (23 isolated delivery cases)\n",fail?"FAIL":"PASS"); return !!fail;
}
