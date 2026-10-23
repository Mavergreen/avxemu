/*
 * lcache.c — cache of avxemu's load-time analysis.
 *
 * At every process start avxemu's constructor makes three passes over the main
 * image's executable ranges: cpuid sites become ud2 (so we can advertise AVX2), lzcnt/tzcnt
 * get a faulting prefix (so they cannot silently run as bsr/bsf), and runs of
 * faulting instructions get trampolines. For Claude Code that is ~2.2 s on an
 * Ivy Bridge, before main, in every process -- including each grep/find shim
 * re-exec -- and nearly all of it is ANALYSIS: decoding 64 MB of code, recovering
 * control flow, proving patch safety. The answer is a pure function of the
 * binary, of avxemu itself, of the CPU and of the AVXEMU_* settings, so we record
 * it once and replay it:
 *
 *   - cpuid sites:        offsets of `0F A2`, rewritten to `0F 0B`
 *   - byte journal:       (offset, old, new) for the lzcnt/tzcnt prefix edits,
 *                         taken by diffing each range around that pass
 *   - trampoline runs:    (site, instruction count, resume) -- replay re-decodes
 *                         those few instructions and builds the thunk exactly as
 *                         the analysis would have
 *
 * Sites are relative to their sorted executable ranges, so ASLR does not matter; the in-memory writes
 * and the thunks are still made fresh in every process.
 *
 * Correctness does not rest on the key alone. Before replay writes anything,
 * every recorded site is checked against the bytes actually there (and each
 * trampoline run is re-decoded and must still be the same faulting run). Any
 * mismatch deletes the file and the full analysis runs instead -- always possible,
 * because validation precedes every write of its stage.
 *
 * The file lives in ~/Library/Caches/avxemu (AVXEMU_CACHE_DIR overrides;
 * AVXEMU_NOCACHE=1 disables). It decides what gets patched into the program, so
 * it is exactly as trusted as the program binary: both are the user's files.
 * AVXEMU_CACHE_STATS=1 reports hit / miss / invalid on stderr.
 */
#define _DARWIN_C_SOURCE
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <time.h>
#include <sys/stat.h>

#include "avxemu_cpuid.h"

#include "lcache.h"
#include "image.h"
#include "platform/host.h"
#include <errno.h>

_Static_assert(sizeof(lc_site) == 16 && sizeof(lc_byte) == 24 && sizeof(lc_run) == 40,
               "cache records must have explicit padding");
_Static_assert(sizeof(lc_key) == 176 && sizeof(lc_hdr) == 208,
               "cache header must have explicit padding");

int avxemu_lc_mode = LC_OFF;

static lc_key    g_key;
static char      g_path[1024], g_dir[1024];
static int       g_stats, g_failed;
static lc_site  *g_cpuid;  static uint32_t g_ncpuid, g_capcpuid;
static lc_byte  *g_bytes;  static uint32_t g_nbytes, g_capbytes;
static lc_run   *g_runs;   static uint32_t g_nruns,  g_capruns;
static void     *g_blob;   /* replay: the file's contents; the arrays point into it */

static uint64_t fnv64(uint64_t h, const void *p, size_t n) {
    const uint8_t *b = p;
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 0x100000001b3ull; }
    return h;
}
#define FNV0 0xcbf29ce484222325ull

static void say(const char *what) {
    if (!g_stats) return;
    char b[1300];
    int n = snprintf(b, sizeof b, "avxemu cache: %s (%u cpuid, %u bytes, %u runs) %s\n",
                     what, g_ncpuid, g_nbytes, g_nruns, g_path);
    if (n > 0) (void)write(2, b, (size_t)n);
}

static int settings_that_do_not_matter(const char *e) {
    return !strncmp(e, "AVXEMU_CACHE_DIR=", 17) || !strncmp(e, "AVXEMU_CACHE_STATS=", 19);
}

static int setting_order(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static int make_key(const avxemu_image *image) {
    memset(&g_key, 0, sizeof g_key);
    if (!avxemu_host_identity(1, &g_key.self) ||
        !avxemu_host_identity(0, &g_key.exe)) return 0;
    uint64_t h = FNV0, count = image->nranges;
    h = fnv64(h, &count, sizeof count);
    for (size_t i = 0; i < image->nranges; i++) {
        const avxemu_code_range *r = &image->ranges[i];
        uint64_t size = r->size;
        if (!size || r->readable < size || size > UINT64_MAX - r->vaddr ||
            (i && image->ranges[i - 1].vaddr + image->ranges[i - 1].size > r->vaddr)) return 0;
        h = fnv64(h, &r->vaddr, sizeof r->vaddr);
        h = fnv64(h, &size, sizeof size);
    }
    g_key.layout_hash = h;
    uint32_t r[4] = {0};
    if (avxemu_get_cpuid(1, 0, r)) g_key.cpu_l1ecx = r[2];
    r[1] = 0; if (avxemu_get_cpuid(7, 0, r)) g_key.cpu_l7ebx = r[1];
    r[2] = 0; if (avxemu_get_cpuid(0x80000001u, 0, r)) g_key.cpu_x1ecx = r[2];
    size_t n = 0;
    char **env = avxemu_host_environ();
    for (char **e = env; e && *e; e++)
        if (!strncmp(*e, "AVXEMU_", 7) && !settings_that_do_not_matter(*e)) n++;
    if (n > SIZE_MAX / sizeof(char *)) return 0;
    const char **settings = n ? malloc(n * sizeof *settings) : 0;
    if (n && !settings) return 0;
    n = 0;
    for (char **e = env; e && *e; e++)
        if (!strncmp(*e, "AVXEMU_", 7) && !settings_that_do_not_matter(*e)) settings[n++] = *e;
    if (n) qsort(settings, n, sizeof *settings, setting_order);
    h = FNV0;
    for (size_t i = 0; i < n; i++) h = fnv64(h, settings[i], strlen(settings[i]) + 1);
    free(settings);
    g_key.env_hash = h;
    return 1;
}

static int ensure_dir(const char *path) {
    if (mkdir(path, 0700) == 0) return 1;
    struct stat st;
    return errno == EEXIST && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int cache_dir(void) {
    if (!avxemu_host_cache_dir(g_dir, sizeof g_dir)) return 0;
    const char *end = memchr(g_dir, 0, sizeof g_dir);
    size_t length = end ? (size_t)(end - g_dir) : sizeof g_dir;
    if (!length || length == sizeof g_dir) return 0;
    char parent[sizeof g_dir];
    memcpy(parent, g_dir, length + 1);
    for (char *p = parent + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            int ok = ensure_dir(parent);
            *p = '/';
            if (!ok) return 0;
        }
    }
    return ensure_dir(g_dir);
}

static uint32_t body_sum(const void *body, size_t n) { return (uint32_t)fnv64(FNV0, body, n); }

/* A zero-size lookup admits the one-past-end address used by a resume. */
uint8_t *avxemu_lc_resolve(const avxemu_image *image, lc_site site, size_t need) {
    if (!image || !image->ranges || site.reserved || site.range >= image->nranges) return 0;
    const avxemu_code_range *r = &image->ranges[site.range];
    if (site.off > r->size || need > r->size - site.off || r->readable < r->size ||
        r->size > UINTPTR_MAX - (uintptr_t)r->base) return 0;
    uint8_t *p = r->base + (size_t)site.off;
    uint32_t range; size_t off;
    if (need && (!avxemu_image_find(image, (uintptr_t)p, need, &range, &off) ||
                 range != site.range || off != site.off)) return 0;
    return p;
}

static int ordered(lc_site a, lc_site b) {
    return a.range < b.range || (a.range == b.range && a.off < b.off);
}

/* Bounds and ordering are independent of the pass's prerequisite edits. Check
 * those here; each pass also checks all of its expected bytes before writing. */
static int records_valid(const avxemu_image *image) {
    for (uint32_t i = 0; i < g_ncpuid; i++)
        if (!avxemu_lc_resolve(image, g_cpuid[i], 2) ||
            (i && (!ordered(g_cpuid[i - 1], g_cpuid[i]) ||
                   (g_cpuid[i - 1].range == g_cpuid[i].range &&
                    g_cpuid[i].off - g_cpuid[i - 1].off < 2)))) return 0;
    for (uint32_t i = 0; i < g_nbytes; i++) {
        const lc_byte *b = &g_bytes[i];
        static const uint8_t zero[6] = {0};
        if (memcmp(b->reserved, zero, sizeof zero) || !avxemu_lc_resolve(image, b->site, 1) ||
            (i && !ordered(g_bytes[i - 1].site, b->site))) return 0;
    }
    for (uint32_t i = 0; i < g_nruns; i++) {
        const lc_run *r = &g_runs[i];
        if (r->reserved || !r->n || !avxemu_lc_resolve(image, r->site, 5) ||
            !avxemu_lc_resolve(image, r->resume, 0) || r->site.range != r->resume.range ||
            r->resume.off < r->site.off || r->resume.off - r->site.off < 5 ||
            (i && (!ordered(g_runs[i - 1].site, r->site) ||
                   (g_runs[i - 1].site.range == r->site.range &&
                    g_runs[i - 1].resume.off > r->site.off)))) return 0;
    }
    return 1;
}

/* Counts are bounded by the remaining file, before multiplication or addition. */
static int body_size(uint32_t nc, uint32_t nb, uint32_t nr, size_t limit, size_t *out) {
    size_t n = 0;
    if (nc >= (1u << 20) || nb >= (1u << 24) || nr >= (1u << 24)) return 0;
#define ADD_RECORDS(count, type) do { \
    if ((count) > (limit - n) / sizeof(type)) return 0; \
    n += (size_t)(count) * sizeof(type); \
} while (0)
    ADD_RECORDS(nc, lc_site); ADD_RECORDS(nb, lc_byte); ADD_RECORDS(nr, lc_run);
#undef ADD_RECORDS
    *out = n; return 1;
}

/* Load and check the whole file; on any doubt, it is not a hit. */
static int load(const avxemu_image *image) {
    int fd = open(g_path, O_RDONLY);
    if (fd < 0) return 0;
    struct stat st;
    int ok = 0;
    if (fstat(fd, &st) == 0 && st.st_size >= (off_t)sizeof(lc_hdr) && st.st_size < (64 << 20)) {
        g_blob = malloc((size_t)st.st_size);
        if (g_blob && read(fd, g_blob, (size_t)st.st_size) == st.st_size) {
            const lc_hdr *h = g_blob;
            size_t body, available = (size_t)st.st_size - sizeof(lc_hdr);
            if (!memcmp(h->magic, LC_MAGIC, 8) && h->version == LC_VERSION &&
                h->hdr_size == sizeof(lc_hdr) && !memcmp(&h->key, &g_key, sizeof g_key) &&
                body_size(h->n_cpuid, h->n_bytes, h->n_runs, available, &body) &&
                body == available && body_sum((const uint8_t *)g_blob + sizeof(lc_hdr), body) == h->body_sum) {
                uint8_t *p = (uint8_t *)g_blob + sizeof(lc_hdr);
                g_cpuid = (lc_site *)p; g_ncpuid = h->n_cpuid; p += (size_t)h->n_cpuid * sizeof(lc_site);
                g_bytes = (lc_byte *)p; g_nbytes = h->n_bytes; p += (size_t)h->n_bytes * sizeof(lc_byte);
                g_runs = (lc_run *)p; g_nruns = h->n_runs;
                ok = records_valid(image);
            }
        }
    }
    close(fd);
    if (!ok) {
        free(g_blob); g_blob = 0; g_cpuid = 0; g_bytes = 0; g_runs = 0;
        g_ncpuid = g_nbytes = g_nruns = 0;
    }
    return ok;
}

/* Called first thing in the constructor, before any pass. Sets avxemu_lc_mode. */
void avxemu_lc_begin(void) {
    const char *s = getenv("AVXEMU_CACHE_STATS");
    g_stats = s && *s && *s != '0';
    avxemu_lc_mode = LC_OFF;
    g_failed = 0;
    if (getenv("AVXEMU_NOCACHE")) return;
    const avxemu_image *image = avxemu_image_main();
    if (!image || !image->ranges || !image->nranges || image->nranges > UINT32_MAX) return;
    if (!make_key(image) || !cache_dir()) return;
    uint64_t h = fnv64(FNV0, &g_key, sizeof g_key);
    if (snprintf(g_path, sizeof g_path, "%s/%016llx.avxc", g_dir, (unsigned long long)h) >= (int)sizeof g_path) return;
    if (load(image)) { avxemu_lc_mode = LC_REPLAY; return; }
    avxemu_lc_mode = LC_RECORD;
}

/* A replay found the file disagreeing with the program: drop it, record afresh
 * next time. This process falls back to analysis for the stage that failed. */
void avxemu_lc_invalid(void) {
    say("invalid, removed");
    unlink(g_path);
    free(g_blob); g_blob = 0; g_cpuid = 0; g_bytes = 0; g_runs = 0;
    g_ncpuid = g_nbytes = g_nruns = 0;
    avxemu_lc_mode = LC_OFF;
}

/* ---- recording ---- */
#define GROW(arr, n, cap, T) do { if ((n) == (cap)) { \
    if ((cap) > UINT32_MAX / 2 || (size_t)(cap) > SIZE_MAX / 2 / sizeof(T)) { g_failed = 1; return; } \
    uint32_t nc = (cap) ? (cap) * 2 : 256; \
    T *na = realloc((arr), (size_t)nc * sizeof(T)); if (!na) { g_failed = 1; return; } (arr) = na; (cap) = nc; } } while (0)

void avxemu_lc_rec_cpuid(lc_site site) {
    if (avxemu_lc_mode != LC_RECORD) return;
    GROW(g_cpuid, g_ncpuid, g_capcpuid, lc_site);
    site.reserved = 0; g_cpuid[g_ncpuid++] = site;
}
void avxemu_lc_rec_byte(lc_site site, uint8_t old, uint8_t neu) {
    if (avxemu_lc_mode != LC_RECORD) return;
    GROW(g_bytes, g_nbytes, g_capbytes, lc_byte);
    lc_byte b; memset(&b, 0, sizeof b); site.reserved = 0;
    b.site = site; b.old = old; b.neu = neu; g_bytes[g_nbytes++] = b;
}
void avxemu_lc_rec_run(lc_site site, uint32_t n, lc_site resume) {
    if (avxemu_lc_mode != LC_RECORD) return;
    GROW(g_runs, g_nruns, g_capruns, lc_run);
    lc_run r; memset(&r, 0, sizeof r); site.reserved = resume.reserved = 0;
    r.site = site; r.n = n; r.resume = resume; g_runs[g_nruns++] = r;
}
/* Something the recording depends on did not happen (allocation, a pass that
 * could not run): write nothing rather than a partial answer. */
void avxemu_lc_rec_fail(void) { g_failed = 1; }

/* Old cache files, from Claude Code versions long gone: drop anything a month
 * untouched. Our own was just written, so it is never old. */
static void prune(void) {
    DIR *d = opendir(g_dir);
    if (!d) return;
    time_t now = time(0);
    struct dirent *e;
    while ((e = readdir(d))) {
        size_t n = strlen(e->d_name);
        if (n < 6 || strcmp(e->d_name + n - 5, ".avxc")) continue;
        char p[1300];
        if (snprintf(p, sizeof p, "%s/%s", g_dir, e->d_name) >= (int)sizeof p) continue;
        struct stat st;
        if (stat(p, &st) == 0 && now - st.st_mtime > 30 * 24 * 3600) unlink(p);
    }
    closedir(d);
}

/* Called last in the constructor. Writes a complete recording atomically. */
void avxemu_lc_end(void) {
    if (avxemu_lc_mode == LC_REPLAY) {
        say("hit");
        free(g_blob); g_blob = 0; g_cpuid = 0; g_bytes = 0; g_runs = 0;
        g_ncpuid = g_nbytes = g_nruns = 0;
        avxemu_lc_mode = LC_OFF;
        return;
    }
    if (avxemu_lc_mode != LC_RECORD) return;
    size_t body;
    if (g_failed || !body_size(g_ncpuid, g_nbytes, g_nruns, (64u << 20) - sizeof(lc_hdr), &body)) {
        say("miss, not written (incomplete)"); goto cleanup;
    }
    uint8_t *buf = malloc(sizeof(lc_hdr) + body);
    if (!buf) goto cleanup;
    lc_hdr *h = (lc_hdr *)buf;
    memset(h, 0, sizeof *h);
    memcpy(h->magic, LC_MAGIC, 8);
    h->version = LC_VERSION; h->hdr_size = sizeof(lc_hdr); h->key = g_key;
    h->n_cpuid = g_ncpuid; h->n_bytes = g_nbytes; h->n_runs = g_nruns;
    uint8_t *p = buf + sizeof(lc_hdr);
    if (g_ncpuid) memcpy(p, g_cpuid, (size_t)g_ncpuid * sizeof(lc_site));
    p += (size_t)g_ncpuid * sizeof(lc_site);
    if (g_nbytes) memcpy(p, g_bytes, (size_t)g_nbytes * sizeof(lc_byte));
    p += (size_t)g_nbytes * sizeof(lc_byte);
    if (g_nruns)  memcpy(p, g_runs,  (size_t)g_nruns * sizeof(lc_run));
    h->body_sum = body_sum(buf + sizeof(lc_hdr), body);

    /* Many processes can miss at once (parallel greps); each writes its own
     * temporary and renames it into place, and the contents are identical. */
    char tmp[1100];
    int length = snprintf(tmp, sizeof tmp, "%s.%d.tmp", g_path, (int)getpid());
    if (length < 0 || (size_t)length >= sizeof tmp) { free(buf); goto cleanup; }
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd >= 0) {
        size_t want = sizeof(lc_hdr) + body;
        int ok = write(fd, buf, want) == (ssize_t)want;
        ok = (close(fd) == 0) && ok;
        if (ok && rename(tmp, g_path) == 0) { say("miss, written"); prune(); }
        else unlink(tmp);
    }
    free(buf);
cleanup:
    free(g_cpuid); free(g_bytes); free(g_runs);
    g_cpuid = 0; g_bytes = 0; g_runs = 0;
    g_ncpuid = g_nbytes = g_nruns = g_capcpuid = g_capbytes = g_capruns = 0;
    avxemu_lc_mode = LC_OFF;
}

/* ---- replay accessors ---- */
uint32_t avxemu_lc_cpuid(const lc_site **sites) { *sites = g_cpuid; return g_ncpuid; }
uint32_t avxemu_lc_bytes(const lc_byte **ent)     { *ent = g_bytes; return g_nbytes; }
uint32_t avxemu_lc_runs(const lc_run **runs)      { *runs = g_runs; return g_nruns; }
