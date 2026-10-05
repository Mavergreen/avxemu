/*
 * threadtest.c — runtime relocation under concurrency.
 *
 * avxemu relocates a hot faulting site from inside the SIGILL handler, i.e.
 * while every other thread keeps running. This drives exactly that: N
 * threads run the same hot loops in lockstep, so each loop's relocation lands
 * while the others are executing it, faulting on it, or sitting just past it.
 *
 * The loop body (threadtest.s) is copied NFUNCS times into an r-x buffer
 * near the pool and registered as the patch-safety layout, the same way
 * reloctest does it. Each trial is a fresh fork, so each one relocates every
 * site again from scratch. A trial fails if a thread computes the wrong answer
 * or the child dies (SIGBUS from a page that lost EXECUTE, SIGSEGV/SIGILL from
 * a thread resuming inside the half-written or freshly-written jmp).
 *
 * Linked against libavxemu.dylib (not inserted): the production configuration.
 *
 *   threadtest [trials [threads]]   exit 0 iff every trial is clean
 *
 * threads=1 is the control: same code, same relocations, nobody to race.
 */
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <mach-o/dyld.h>
#include "cpu.h"

extern const uint8_t tt_body_start[], tt_body_site[], tt_body_end[];
extern void  *avxemu_pool_base(void);
extern void   avxemu_patch_safe_test_region(uint8_t *base, size_t size);

#define MAXTHREADS 64
#define NFUNCS   256
#define ITERS    400        /* > HOT_THRESHOLD, so every site relocates mid-run */
#define STRIDE   65     /* odd: the sites walk through every cache-line offset */

typedef void (*body_fn)(uint64_t, uint32_t *);
static uint8_t *g_buf;
static volatile int g_bad;
static int g_nthreads = 12;

/* 10.9 has no pthread_barrier_t; a counter is enough for a one-shot start line. */
static volatile int g_ready, g_go;

static void *worker(void *arg) {
    (void)arg;
    __sync_fetch_and_add(&g_ready, 1);
    while (!g_go) ;
    uint32_t out[9] __attribute__((aligned(32)));
    for (int f = 0; f < NFUNCS; f++) {
        memset(out, 0, sizeof out);
        ((body_fn)(g_buf + (size_t)f * STRIDE))(ITERS, out);
        for (int k = 0; k < 9; k++)
            if (out[k] != ITERS) { __sync_fetch_and_add(&g_bad, 1); break; }
    }
    return 0;
}

static int trial(void) {
    size_t len = (size_t)(tt_body_end - tt_body_start);
    if (len > STRIDE) { fprintf(stderr, "body %zu > stride\n", len); return 2; }

    /* Within rel32 of the pool, which the constructor placed near __text. */
    uint8_t *pool = avxemu_pool_base();
    if (!pool) { fprintf(stderr, "no pool: avxemu not emulating here?\n"); return 2; }
    size_t sz = (size_t)NFUNCS * STRIDE;
    void *hint = pool + (128u << 20);
    g_buf = mmap(hint, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (g_buf == MAP_FAILED) { perror("mmap"); return 2; }
    int64_t d = (int64_t)(g_buf - pool);
    if (d > INT32_MAX / 2 || d < INT32_MIN / 2) { fprintf(stderr, "buffer out of rel32 reach\n"); return 2; }
    memset(g_buf, 0xCC, sz);
    for (int f = 0; f < NFUNCS; f++) memcpy(g_buf + (size_t)f * STRIDE, tt_body_start, len);
    if (mprotect(g_buf, sz, PROT_READ | PROT_EXEC) != 0) { perror("mprotect"); return 2; }
    avxemu_patch_safe_test_region(g_buf, sz);

    pthread_t th[MAXTHREADS];
    for (int i = 0; i < g_nthreads; i++) pthread_create(&th[i], 0, worker, 0);
    while (g_ready < g_nthreads) ;
    g_go = 1;
    for (int i = 0; i < g_nthreads; i++) pthread_join(th[i], 0);
    if (g_bad) { fprintf(stderr, "%d wrong results\n", g_bad); return 1; }

    /* Clean only counts if the race was actually run: every site must now be
     * our jmp. (A patcher that declined everything would also be "clean".) The
     * exception is a site whose 5 bytes cross a cache line: no single store can
     * write it atomically, so it must be declined and left as it was. */
    size_t so = (size_t)(tt_body_site - tt_body_start);
    int patched = 0, want = 0;
    for (int f = 0; f < NFUNCS; f++) {
        const uint8_t *site = g_buf + (size_t)f * STRIDE + so;
        int straddles = ((uintptr_t)site & 63) + 5 > 64;
        want += !straddles;
        if (straddles ? *site != 0xC5 : *site != 0xE9) {
            fprintf(stderr, "site %d (line offset %d): 0x%02x\n", f, (int)((uintptr_t)site & 63), *site);
            return 3;
        }
        patched += !straddles;
    }
    if (patched != want || want == 0) { fprintf(stderr, "only %d/%d sites relocated\n", patched, want); return 3; }
    return 0;
}

/* The site's own instruction, once, for the probe below. */
static void probe_site(void) {
    __asm__ volatile(".byte 0xC5, 0xFD, 0xFA, 0xC1\n\tvzeroupper" ::: "xmm0", "xmm1");   /* vpsubd ymm1,ymm0,ymm0 */
}

/* Does the site fault here? Not inferred from a feature bit: a CPU, or a translator, decides
 * what it runs. Asked by running it once in a child with avxemu disabled, where a #UD can
 * only kill it. */
static int site_faults(void) {
    char self[1024]; uint32_t sz = sizeof self;
    if (_NSGetExecutablePath(self, &sz)) return -1;
    fflush(0);
    pid_t p = fork();
    if (p == 0) {
        setenv("AVXEMU_DISABLE", "1", 1);
        execl(self, self, "--probe", (char *)0);
        _exit(127);
    }
    int st = 0;
    if (p < 0 || waitpid(p, &st, 0) != p) return -1;
    if (WIFSIGNALED(st) && WTERMSIG(st) == SIGILL) return 1;
    if (WIFEXITED(st) && WEXITSTATUS(st) == 0) return 0;
    return -1;
}

int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "--probe")) { probe_site(); return 0; }
    /* Where avxemu stays inert there is no pool and no handler; where the loop never faults,
     * nothing relocates. Either way there is no race to run. */
    if (cpu_avxemu_capable()) { printf("threadtest: skipped, avxemu stays inert on this CPU\n"); return 77; }
    int f = site_faults();
    if (f < 0) { fprintf(stderr, "threadtest: the fault probe failed\n"); return 2; }
    if (!f) { printf("threadtest: skipped, the site does not fault here, so nothing relocates\n"); return 77; }

    int trials = argc > 1 ? atoi(argv[1]) : 20;
    if (argc > 2) g_nthreads = atoi(argv[2]);
    if (g_nthreads < 1 || g_nthreads > MAXTHREADS) { fprintf(stderr, "threads: 1..%d\n", MAXTHREADS); return 2; }
    int clean = 0, wrong = 0, died = 0;
    for (int t = 0; t < trials; t++) {
        fflush(0);
        pid_t p = fork();
        if (p == 0) _exit(trial());
        int st = 0;
        waitpid(p, &st, 0);
        if (WIFEXITED(st) && WEXITSTATUS(st) == 0) clean++;
        else if (WIFEXITED(st) && WEXITSTATUS(st) == 1) wrong++;
        else if (WIFEXITED(st)) { fprintf(stderr, "trial %d: exit %d (setup, or nothing relocated)\n", t, WEXITSTATUS(st)); return 2; }
        else { died++; fprintf(stderr, "trial %d: signal %d\n", t, WTERMSIG(st)); }
    }
    printf("threadtest: %d/%d clean, %d wrong-result, %d died (%d threads x %d sites)\n",
           clean, trials, wrong, died, g_nthreads, NFUNCS);
    return clean == trials ? 0 : 1;
}
