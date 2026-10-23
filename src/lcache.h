/*
 * lcache.h — the load-time analysis cache (lcache.c): record what the three
 * load-time passes decided, replay it on the next launch of the same binary.
 */
#ifndef AVXEMU_LCACHE_H
#define AVXEMU_LCACHE_H
#include <stdint.h>
#include "image.h"
#include "platform/host.h"

enum { LC_OFF = 0, LC_RECORD = 1, LC_REPLAY = 2 };
extern int avxemu_lc_mode;

typedef struct { uint32_t range, reserved; uint64_t off; } lc_site;
typedef struct { lc_site site; uint8_t old, neu; uint8_t reserved[6]; } lc_byte;
typedef struct { lc_site site, resume; uint32_t n, reserved; } lc_run;

/* Fixed-width, explicitly zero-reserved records. The header is followed by
 * n_cpuid lc_site, n_bytes lc_byte, and n_runs lc_run records. body_sum is
 * FNV-1a 64 over that body, truncated to 32 bits. */
#define LC_MAGIC   "AVXEMUC"
#define LC_VERSION 2u
typedef struct {
    avxemu_file_identity self, exe;
    uint64_t layout_hash;       /* ordered link-time ranges; never load addresses */
    uint32_t cpu_l1ecx, cpu_l7ebx, cpu_x1ecx, reserved;
    uint64_t env_hash;          /* sorted AVXEMU_* settings that affect analysis */
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

void avxemu_lc_rec_cpuid(lc_site site);
void avxemu_lc_rec_byte(lc_site site, uint8_t old, uint8_t neu);
void avxemu_lc_rec_run(lc_site site, uint32_t n, lc_site resume);
void avxemu_lc_rec_fail(void);     /* the recording would be incomplete: write nothing */

uint32_t avxemu_lc_cpuid(const lc_site **sites);
uint32_t avxemu_lc_bytes(const lc_byte **ent);
uint32_t avxemu_lc_runs(const lc_run **runs);
/* need == 0 permits a range-end resume address; other sites are half-open.
 * Failure returns NULL without reading image bytes. */
uint8_t *avxemu_lc_resolve(const avxemu_image *image, lc_site site, size_t need);
#endif
