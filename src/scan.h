#ifndef AVXEMU_SCAN_H
#define AVXEMU_SCAN_H
#include "image.h"
#include "lde.h"
/* Owned direct-entry closure. No indirect/table guesses. Inputs must remain
 * unchanged for its lifetime; free after the current analysis/publication stage.
 * Work counts function bytes revisited; exhaustion fails before publication. */
#define AVXEMU_SCAN_WORK_LIMIT ((size_t)256*1024*1024)
typedef struct avxemu_scan avxemu_scan;
typedef struct {
    uint8_t *code, *targets;
    int indirect;
} avxemu_scan_map;
avxemu_scan *avxemu_scan_prepare(const avxemu_image *image, lde_trap_lookup traps,
    size_t work_limit);
void avxemu_scan_free(avxemu_scan *scan);
const avxemu_scan_map *avxemu_scan_function(const avxemu_scan *scan, size_t index);
/* The span must consist of whole reachable instructions in one function. */
int avxemu_scan_span(const avxemu_scan *scan, uintptr_t address, size_t size);
#endif
