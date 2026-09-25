/*
 * cachetest.c — avxemu's load-time analysis cache (lcache.c).
 *
 * avxemu is linked INTO this program, so its load-time passes act on our own
 * __text: a cpuid, an lzcnt and a trampolinable AVX2 run (cachetest.s). A driver
 * runs fresh copies of us against a private cache directory; each child reports
 * a fingerprint of its patched __text, the code pool it used, and whether the
 * patched code computes correctly. The driver checks:
 *   1. miss writes the cache; the next launch hits it
 *   2. analysed and replayed launches patch __text identically and use the same
 *      pool -- the replay built the same thunks -- and both compute correctly
 *   3. a corrupted file is not used, and is recorded afresh
 *   4. a well-formed file that is WRONG (a journal byte that is not what is in
 *      __text, with a valid checksum) is caught by replay validation, removed,
 *      and the launch still ends up correct and identical
 *   5. 8 simultaneous cold starts all succeed and leave one good cache
 *
 * On a CPU that already has everything avxemu emulates, it must instead stay
 * out of the way: no analysis, no cache file, no pool. That is checked first;
 * then the suite runs with AVXEMU_FORCEPATCH/FORCETRAMP, which make avxemu
 * analyse and patch anyway, so the cache logic is exercised on that CPU and OS
 * too. There a trampoline may not be placeable at all -- on hosts that put the
 * pool beyond rel32 reach of __text the jmp is never written -- and the cache
 * rightly records none; the trampoline checks then apply only if one landed.
 * On the CPU avxemu is for, they are mandatory.
 *
 *   AVXEMU_NOCACHE=1 cachetest     (the driver's own launch must not use a cache)
 */
#include <dirent.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <mach-o/dyld.h>
#include <mach-o/getsect.h>
#include <mach-o/ldsyms.h>
#include <crt_externs.h>
#include <mach-o/loader.h>
#include "lcache.h"

extern uint32_t ct_cpuid7_ebx(void);
extern uint32_t ct_lzcnt(uint32_t);
extern void     ct_quadruple(uint32_t v[8]);
extern size_t   avxemu_pool_used(void);
extern void     avxemu_cpuid_raw(uint32_t leaf, uint32_t sub, uint32_t r[4]);

static uint64_t fnv(uint64_t h, const void *p, size_t n) {
    const uint8_t *b = p; for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 0x100000001b3ull; } return h;
}

static int child(void) {
    unsigned long sz;
    const uint8_t *text = getsectiondata((const struct mach_header_64 *)_NSGetMachExecuteHeader(),
                                         "__TEXT", "__text", &sz);
    uint32_t v[8] __attribute__((aligned(32))) = {1, 2, 3, 4, 5, 6, 7, 8};
    ct_quadruple(v);
    int ok = (ct_cpuid7_ebx() & (1u << 5)) != 0      /* advertised AVX2: cpuid site patched */
          && ct_lzcnt(1) == 31 && ct_lzcnt(0) == 32  /* lzcnt, not bsr */
          && v[0] == 4 && v[7] == 32;                 /* the AVX2 run */
    printf("fp=%016llx pool=%zu ok=%d\n", (unsigned long long)fnv(0xcbf29ce484222325ull, text, sz),
           avxemu_pool_used(), ok);
    return 0;
}

static char g_dir[512], g_self[1024];
typedef struct { char fp[17]; size_t pool; int ok; char stat[64]; } result;

static int run(result *r) {
    int pfd[2]; if (pipe(pfd)) return 0;
    pid_t pid = fork();
    if (pid == 0) {
        dup2(pfd[1], 1); dup2(pfd[1], 2); close(pfd[0]);
        unsetenv("AVXEMU_NOCACHE");
        setenv("AVXEMU_CACHE_DIR", g_dir, 1); setenv("AVXEMU_CACHE_STATS", "1", 1);
        setenv("CACHETEST_CHILD", "1", 1);
        execl(g_self, g_self, (char *)0); _exit(127);
    }
    close(pfd[1]);
    char buf[4096]; size_t n = 0; ssize_t k;
    while ((k = read(pfd[0], buf + n, sizeof buf - 1 - n)) > 0) n += (size_t)k;
    buf[n] = 0; close(pfd[0]);
    int st; waitpid(pid, &st, 0);
    memset(r, 0, sizeof *r);
    char *s = strstr(buf, "avxemu cache: ");
    if (s) { s += 14; size_t i = 0; while (s[i] && s[i] != '(' && i < sizeof r->stat - 1) { r->stat[i] = s[i]; i++; }
             while (i && r->stat[i-1] == ' ') r->stat[--i] = 0; }
    char *f = strstr(buf, "fp=");
    if (!f || sscanf(f, "fp=%16s pool=%zu ok=%d", r->fp, &r->pool, &r->ok) != 3) {
        printf("  child produced no result (exit %d): %s\n", st, buf); return 0;
    }
    return WIFEXITED(st) && WEXITSTATUS(st) == 0;
}

static int nfail;
static void expect(int cond, const char *what) {
    printf("  %-58s %s\n", what, cond ? "ok" : "FAIL"); if (!cond) nfail++;
}

static int cache_file(char *out, size_t n) {
    DIR *d = opendir(g_dir); if (!d) return 0;
    struct dirent *e; int found = 0;
    while ((e = readdir(d))) {
        size_t l = strlen(e->d_name);
        if (l > 5 && !strcmp(e->d_name + l - 5, ".avxc")) { snprintf(out, n, "%s/%s", g_dir, e->d_name); found++; }
    }
    closedir(d);
    return found;
}

static int rewrite(const char *path, void (*mutate)(uint8_t *, size_t), int fix_sum) {
    int fd = open(path, O_RDWR); if (fd < 0) return 0;
    struct stat st; fstat(fd, &st);
    uint8_t *b = malloc((size_t)st.st_size);
    if (read(fd, b, (size_t)st.st_size) != st.st_size) return 0;
    mutate(b, (size_t)st.st_size);
    if (fix_sum) {
        lc_hdr *h = (lc_hdr *)b;
        h->body_sum = (uint32_t)fnv(0xcbf29ce484222325ull, b + sizeof(lc_hdr), (size_t)st.st_size - sizeof(lc_hdr));
    }
    lseek(fd, 0, SEEK_SET);
    int ok = write(fd, b, (size_t)st.st_size) == st.st_size;
    close(fd); free(b);
    return ok;
}
static void flip_last(uint8_t *b, size_t n) { b[n - 1] ^= 0x5A; }
static void wrong_old_byte(uint8_t *b, size_t n) {
    lc_hdr *h = (lc_hdr *)b; (void)n;
    lc_byte *j = (lc_byte *)(b + sizeof(lc_hdr) + (size_t)h->n_cpuid * 4);
    j[0].old ^= 0xFF;
}

int main(void) {
    if (getenv("CACHETEST_CHILD")) return child();
    if (!getenv("AVXEMU_NOCACHE")) { printf("run as: AVXEMU_NOCACHE=1 cachetest\n"); return 2; }
    uint32_t sz = sizeof g_self;
    if (_NSGetExecutablePath(g_self, &sz)) return 2;
    snprintf(g_dir, sizeof g_dir, "/tmp/avxemu-cachetest.%d", (int)getpid());
    mkdir(g_dir, 0700);

    uint32_t cr[4]; avxemu_cpuid_raw(7, 0, cr);
    int capable = (cr[1] & (1u << 5)) != 0;
    result a, b, c;
    char path[700];
    if (capable) {
        printf("== CPU has AVX2: linked avxemu must stay out of the way ==\n");
        run(&a);
        expect(!a.stat[0] && cache_file(path, sizeof path) == 0 && a.pool == 0,
               "no analysis, no cache file, no pool");
        expect(a.ok, "cpuid, lzcnt and AVX2 run correct, natively");
        setenv("AVXEMU_FORCEPATCH", "1", 1);          /* the children inherit these */
        setenv("AVXEMU_FORCETRAMP", "1", 1);
        printf("== ...and forced on, the cache on this CPU and OS ==\n");
    } else {
        printf("== load-time analysis cache ==\n");
    }
    run(&a); run(&b);
    expect(!strcmp(a.stat, "miss, written"), "cold start analyses and writes the cache");
    expect(!strcmp(b.stat, "hit"), "next start replays it");
    expect(a.ok && b.ok, "patched cpuid, lzcnt and AVX2 run compute correctly");
    cache_file(path, sizeof path);
    lc_hdr h; int fd = open(path, O_RDONLY); read(fd, &h, sizeof h); close(fd);
    int runs = h.n_runs >= 1;
    if (!runs && capable) printf("  (no trampoline placed on this host: pool beyond rel32 reach)\n");
    expect(!strcmp(a.fp, b.fp) && (!runs || (a.pool == b.pool && a.pool > 0)),
           runs ? "replayed __text and pool identical to analysed" : "replayed __text identical to analysed");
    expect(h.n_cpuid >= 1 && h.n_bytes >= 1 && (runs || capable),
           runs ? "recording holds a site of each kind" : "recording holds cpuid and lzcnt sites");

    rewrite(path, flip_last, 0);
    run(&c);
    expect(!strcmp(c.stat, "miss, written") && c.ok && !strcmp(c.fp, a.fp), "corrupt file ignored and recorded afresh");

    rewrite(path, wrong_old_byte, 1);
    run(&c);
    struct stat st;
    expect(!strcmp(c.stat, "invalid, removed") && stat(path, &st) != 0, "well-formed but wrong file caught and removed");
    expect(c.ok && !strcmp(c.fp, a.fp), "...and that launch still analysed to the same result");

    unlink(path);
    pid_t kids[8];
    for (int i = 0; i < 8; i++) if ((kids[i] = fork()) == 0) { result r; _exit(run(&r) && r.ok && !strcmp(r.fp, a.fp) ? 0 : 1); }
    int all = 1;
    for (int i = 0; i < 8; i++) { int st2; waitpid(kids[i], &st2, 0); all &= WIFEXITED(st2) && WEXITSTATUS(st2) == 0; }
    run(&c);
    expect(all && cache_file(path, sizeof path) == 1 && !strcmp(c.stat, "hit"), "8 simultaneous cold starts: all correct, one good cache");

    char cmd[600]; snprintf(cmd, sizeof cmd, "rm -rf '%s'", g_dir); system(cmd);
    printf("\nCACHETEST TOTAL: %d failure(s)\n", nfail);
    return nfail ? 1 : 0;
}
