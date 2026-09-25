/*
 * lcache.h — the load-time analysis cache (lcache.c): record what the three
 * load-time passes decided, replay it on the next launch of the same binary.
 */
#ifndef AVXEMU_LCACHE_H
#define AVXEMU_LCACHE_H
#include <stdint.h>

enum { LC_OFF = 0, LC_RECORD = 1, LC_REPLAY = 2 };
extern int avxemu_lc_mode;

typedef struct { uint32_t off; uint8_t old, neu; uint16_t pad; } lc_byte;   /* one prefix edit */
typedef struct { uint32_t site, n, resume; } lc_run;                         /* one trampoline */

/* The file: lc_hdr, then n_cpuid uint32 offsets, n_bytes lc_byte, n_runs lc_run.
 * body_sum is FNV-1a 64 over the body, truncated. Here (not in lcache.c) so the
 * test can build a well-formed file that is wrong in content. */
#define LC_MAGIC   "AVXEMUC"
#define LC_VERSION 1u
typedef struct {
    uint8_t  self_uuid[16];     /* avxemu's own build */
    uint8_t  exe_uuid[16];      /* the program's, or zeros if it has none */
    uint64_t exe_dev, exe_ino, exe_size, exe_mtime_s, exe_mtime_ns;
    uint64_t text_vmaddr, text_size;
    uint32_t cpu_l1ecx, cpu_l7ebx, cpu_x1ecx;
    uint32_t pad;
    uint64_t env_hash;          /* AVXEMU_* settings that can change the answer */
} lc_key;
typedef struct {
    char     magic[8];
    uint32_t version, hdr_size;
    lc_key   key;
    uint32_t n_cpuid, n_bytes, n_runs, body_sum;
} lc_hdr;

void avxemu_lc_begin(void);        /* constructor, before the passes: sets the mode */
void avxemu_lc_end(void);          /* constructor, after them: writes a recording */
void avxemu_lc_invalid(void);      /* a replay disagreed with the program: drop the file */

void avxemu_lc_rec_cpuid(uint32_t off);
void avxemu_lc_rec_byte(uint32_t off, uint8_t old, uint8_t neu);
void avxemu_lc_rec_run(uint32_t site, uint32_t n, uint32_t resume);
void avxemu_lc_rec_fail(void);     /* the recording would be incomplete: write nothing */

uint32_t avxemu_lc_cpuid(const uint32_t **offs);   /* offsets are relative to __text */
uint32_t avxemu_lc_bytes(const lc_byte **ent);
uint32_t avxemu_lc_runs(const lc_run **runs);
#endif
