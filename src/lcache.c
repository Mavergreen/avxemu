/*
 * lcache.c — cache of avxemu's load-time analysis.
 *
 * At every process start avxemu's constructor makes three passes over the main
 * image's __text: cpuid sites become ud2 (so we can advertise AVX2), lzcnt/tzcnt
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
 *                         taken by diffing __text around that pass
 *   - trampoline runs:    (site, instruction count, resume) -- replay re-decodes
 *                         those few instructions and builds the thunk exactly as
 *                         the analysis would have
 *
 * Offsets are relative to __text, so ASLR does not matter; the in-memory writes
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
#include <crt_externs.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>

extern int avxemu_get_cpuid(uint32_t leaf, uint32_t sub, uint32_t r[4]);   /* handler.c */

#include "lcache.h"

int avxemu_lc_mode = LC_OFF;

static lc_key    g_key;
static char      g_path[1024], g_dir[1024];
static int       g_stats, g_failed;
static uint32_t *g_cpuid;  static uint32_t g_ncpuid, g_capcpuid;
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

static int image_uuid(const struct mach_header_64 *mh, uint8_t out[16]) {
    const struct load_command *lc = (const struct load_command *)(mh + 1);
    for (uint32_t i = 0; i < mh->ncmds; i++) {
        if (lc->cmd == LC_UUID) { memcpy(out, ((const struct uuid_command *)lc)->uuid, 16); return 1; }
        lc = (const struct load_command *)((const char *)lc + lc->cmdsize);
    }
    return 0;
}

/* The image this code is in: avxemu's dylib, or the program when linked into it. */
static const struct mach_header_64 *self_image(void) {
    const uintptr_t me = (uintptr_t)&self_image;
    for (uint32_t i = 0; i < _dyld_image_count(); i++) {
        const struct mach_header_64 *mh = (const struct mach_header_64 *)_dyld_get_image_header(i);
        if (!mh || mh->magic != MH_MAGIC_64) continue;
        intptr_t slide = _dyld_get_image_vmaddr_slide(i);
        const struct load_command *lc = (const struct load_command *)(mh + 1);
        for (uint32_t c = 0; c < mh->ncmds; c++) {
            if (lc->cmd == LC_SEGMENT_64) {
                const struct segment_command_64 *sg = (const struct segment_command_64 *)lc;
                uintptr_t lo = (uintptr_t)sg->vmaddr + (uintptr_t)slide;
                if (me >= lo && me < lo + (uintptr_t)sg->vmsize) return mh;
            }
            lc = (const struct load_command *)((const char *)lc + lc->cmdsize);
        }
    }
    return 0;
}

static int settings_that_do_not_matter(const char *e) {
    return !strncmp(e, "AVXEMU_CACHE_DIR=", 17) || !strncmp(e, "AVXEMU_CACHE_STATS=", 19);
}

static int make_key(uint64_t text_vmaddr, uint64_t text_size) {
    memset(&g_key, 0, sizeof g_key);
    const struct mach_header_64 *self = self_image();
    const struct mach_header_64 *exe = (const struct mach_header_64 *)_dyld_get_image_header(0);
    if (!self || !exe || exe->magic != MH_MAGIC_64) return 0;
    if (!image_uuid(self, g_key.self_uuid)) return 0;
    /* The program's own UUID is a bonus, not a requirement: the Mavericks
     * wrapper's change_dylib -strip-lc removes LC_UUID from Claude Code to make
     * header room. The file's identity below (device, inode, size, mtime to the
     * nanosecond) is what pins the program; replay re-checks every site anyway. */
    (void)image_uuid(exe, g_key.exe_uuid);
    const char *name = _dyld_get_image_name(0);
    struct stat st;
    if (!name || stat(name, &st) != 0) return 0;
    g_key.exe_dev = (uint64_t)st.st_dev;   g_key.exe_ino = (uint64_t)st.st_ino;
    g_key.exe_size = (uint64_t)st.st_size;
    g_key.exe_mtime_s = (uint64_t)st.st_mtimespec.tv_sec;
    g_key.exe_mtime_ns = (uint64_t)st.st_mtimespec.tv_nsec;
    g_key.text_vmaddr = text_vmaddr;       g_key.text_size = text_size;
    uint32_t r[4] = {0};
    if (avxemu_get_cpuid(1, 0, r)) g_key.cpu_l1ecx = r[2];
    r[1] = 0; if (avxemu_get_cpuid(7, 0, r)) g_key.cpu_l7ebx = r[1];
    r[2] = 0; if (avxemu_get_cpuid(0x80000001u, 0, r)) g_key.cpu_x1ecx = r[2];
    uint64_t h = FNV0;
    for (char **e = *_NSGetEnviron(); e && *e; e++)
        if (!strncmp(*e, "AVXEMU_", 7) && !settings_that_do_not_matter(*e))
            h = fnv64(h, *e, strlen(*e) + 1);
    g_key.env_hash = h;
    return 1;
}

static int cache_dir(void) {
    const char *d = getenv("AVXEMU_CACHE_DIR");
    if (d && *d) {
        if (snprintf(g_dir, sizeof g_dir, "%s", d) >= (int)sizeof g_dir) return 0;
        mkdir(g_dir, 0700);
        return 1;
    }
    const char *home = getenv("HOME");
    if (!home || !*home) return 0;
    char caches[1024];
    if (snprintf(caches, sizeof caches, "%s/Library/Caches", home) >= (int)sizeof caches) return 0;
    if (snprintf(g_dir, sizeof g_dir, "%s/avxemu", caches) >= (int)sizeof g_dir) return 0;
    mkdir(caches, 0700);
    mkdir(g_dir, 0700);
    return 1;
}

static uint32_t body_sum(const void *body, size_t n) { return (uint32_t)fnv64(FNV0, body, n); }

/* Load and check the whole file; on any doubt, it is not a hit. */
static int load(void) {
    int fd = open(g_path, O_RDONLY);
    if (fd < 0) return 0;
    struct stat st;
    int ok = 0;
    if (fstat(fd, &st) == 0 && st.st_size >= (off_t)sizeof(lc_hdr) && st.st_size < (64 << 20)) {
        g_blob = malloc((size_t)st.st_size);
        if (g_blob && read(fd, g_blob, (size_t)st.st_size) == st.st_size) {
            const lc_hdr *h = g_blob;
            size_t body = (size_t)h->n_cpuid * 4 + (size_t)h->n_bytes * sizeof(lc_byte)
                        + (size_t)h->n_runs * sizeof(lc_run);
            if (!memcmp(h->magic, LC_MAGIC, 8) && h->version == LC_VERSION &&
                h->hdr_size == sizeof(lc_hdr) && !memcmp(&h->key, &g_key, sizeof g_key) &&
                h->n_cpuid < (1u << 20) && h->n_bytes < (1u << 24) && h->n_runs < (1u << 24) &&
                sizeof(lc_hdr) + body == (size_t)st.st_size &&
                body_sum((const uint8_t *)g_blob + sizeof(lc_hdr), body) == h->body_sum) {
                const uint8_t *p = (const uint8_t *)g_blob + sizeof(lc_hdr);
                g_cpuid = (uint32_t *)p;  g_ncpuid = h->n_cpuid;  p += (size_t)h->n_cpuid * 4;
                g_bytes = (lc_byte *)p;   g_nbytes = h->n_bytes;  p += (size_t)h->n_bytes * sizeof(lc_byte);
                g_runs  = (lc_run *)p;    g_nruns  = h->n_runs;
                ok = 1;
            }
        }
    }
    close(fd);
    if (!ok) { free(g_blob); g_blob = 0; }
    return ok;
}

/* The main image's __text, unslid: part of the key. */
static int main_text(uint64_t *vmaddr, uint64_t *size) {
    const struct mach_header_64 *mh = (const struct mach_header_64 *)_dyld_get_image_header(0);
    if (!mh || mh->magic != MH_MAGIC_64) return 0;
    const struct load_command *lc = (const struct load_command *)(mh + 1);
    for (uint32_t i = 0; i < mh->ncmds; i++) {
        if (lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *sg = (const struct segment_command_64 *)lc;
            const struct section_64 *sc = (const struct section_64 *)(sg + 1);
            for (uint32_t j = 0; j < sg->nsects; j++)
                if (!strcmp(sc[j].segname, "__TEXT") && !strcmp(sc[j].sectname, "__text")) {
                    *vmaddr = sc[j].addr; *size = sc[j].size; return 1;
                }
        }
        lc = (const struct load_command *)((const char *)lc + lc->cmdsize);
    }
    return 0;
}

/* Called first thing in the constructor, before any pass. Sets avxemu_lc_mode. */
void avxemu_lc_begin(void) {
    const char *s = getenv("AVXEMU_CACHE_STATS");
    g_stats = s && *s && *s != '0';
    avxemu_lc_mode = LC_OFF;
    if (getenv("AVXEMU_NOCACHE")) return;
    uint64_t text_vmaddr, text_size;
    if (!main_text(&text_vmaddr, &text_size)) return;
    if (!make_key(text_vmaddr, text_size) || !cache_dir()) return;
    uint64_t h = fnv64(FNV0, &g_key, sizeof g_key);
    if (snprintf(g_path, sizeof g_path, "%s/%016llx.avxc", g_dir, (unsigned long long)h) >= (int)sizeof g_path) return;
    if (load()) { avxemu_lc_mode = LC_REPLAY; return; }
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
#define GROW(arr, n, cap, T) do { if ((n) == (cap)) { uint32_t nc = (cap) ? (cap) * 2 : 256; \
    T *na = realloc((arr), (size_t)nc * sizeof(T)); if (!na) { g_failed = 1; return; } (arr) = na; (cap) = nc; } } while (0)

void avxemu_lc_rec_cpuid(uint32_t off) {
    if (avxemu_lc_mode != LC_RECORD) return;
    GROW(g_cpuid, g_ncpuid, g_capcpuid, uint32_t);
    g_cpuid[g_ncpuid++] = off;
}
void avxemu_lc_rec_byte(uint32_t off, uint8_t old, uint8_t neu) {
    if (avxemu_lc_mode != LC_RECORD) return;
    GROW(g_bytes, g_nbytes, g_capbytes, lc_byte);
    lc_byte b = { off, old, neu, 0 };
    g_bytes[g_nbytes++] = b;
}
void avxemu_lc_rec_run(uint32_t site, uint32_t n, uint32_t resume) {
    if (avxemu_lc_mode != LC_RECORD) return;
    GROW(g_runs, g_nruns, g_capruns, lc_run);
    lc_run r = { site, n, resume };
    g_runs[g_nruns++] = r;
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
        avxemu_lc_mode = LC_OFF;
        return;
    }
    if (avxemu_lc_mode != LC_RECORD) return;
    if (g_failed) { say("miss, not written (incomplete)"); return; }
    size_t body = (size_t)g_ncpuid * 4 + (size_t)g_nbytes * sizeof(lc_byte) + (size_t)g_nruns * sizeof(lc_run);
    uint8_t *buf = malloc(sizeof(lc_hdr) + body);
    if (!buf) return;
    lc_hdr *h = (lc_hdr *)buf;
    memset(h, 0, sizeof *h);
    memcpy(h->magic, LC_MAGIC, 8);
    h->version = LC_VERSION; h->hdr_size = sizeof(lc_hdr); h->key = g_key;
    h->n_cpuid = g_ncpuid; h->n_bytes = g_nbytes; h->n_runs = g_nruns;
    uint8_t *p = buf + sizeof(lc_hdr);
    if (g_ncpuid) memcpy(p, g_cpuid, (size_t)g_ncpuid * 4);                  p += (size_t)g_ncpuid * 4;
    if (g_nbytes) memcpy(p, g_bytes, (size_t)g_nbytes * sizeof(lc_byte));   p += (size_t)g_nbytes * sizeof(lc_byte);
    if (g_nruns)  memcpy(p, g_runs,  (size_t)g_nruns * sizeof(lc_run));
    h->body_sum = body_sum(buf + sizeof(lc_hdr), body);

    /* Many processes can miss at once (parallel greps); each writes its own
     * temporary and renames it into place, and the contents are identical. */
    char tmp[1100];
    snprintf(tmp, sizeof tmp, "%s.%d.tmp", g_path, (int)getpid());
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd >= 0) {
        size_t want = sizeof(lc_hdr) + body;
        int ok = write(fd, buf, want) == (ssize_t)want;
        ok = (close(fd) == 0) && ok;
        if (ok && rename(tmp, g_path) == 0) { say("miss, written"); prune(); }
        else unlink(tmp);
    }
    free(buf);
    free(g_cpuid); free(g_bytes); free(g_runs);
    g_cpuid = 0; g_bytes = 0; g_runs = 0;
    avxemu_lc_mode = LC_OFF;
}

/* ---- replay accessors ---- */
uint32_t avxemu_lc_cpuid(const uint32_t **offs)  { *offs = g_cpuid; return g_ncpuid; }
uint32_t avxemu_lc_bytes(const lc_byte **ent)     { *ent = g_bytes; return g_nbytes; }
uint32_t avxemu_lc_runs(const lc_run **runs)      { *runs = g_runs; return g_nruns; }
