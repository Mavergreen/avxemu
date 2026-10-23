/* In-memory LZCNT/TZCNT prefix edits, scoped to executable image ranges. */
#include "lde.h"
#include "avxemu_cpuid.h"
#include "lcache.h"
#include "image.h"
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

static void restore(const avxemu_mapping *saved) {
    if (!avxemu_memory_patch_end(saved)) {
        static const char msg[] = "avxemu: fatal: cannot restore patch page permissions\n";
        (void)write(2, msg, sizeof msg - 1); _exit(127);
    }
}

/* Startup-only write spans. Split at mapping boundaries and retain every
 * original permission, including when a later mapping refuses the change. */
void avxemu_patch_write_end(avxemu_mapping *saved, size_t count) {
    while (count) restore(&saved[--count]);
    free(saved);
}

avxemu_mapping *avxemu_patch_write_begin(uintptr_t address, size_t size, size_t *count) {
    if (!size || size > UINTPTR_MAX - address) return 0;
    avxemu_mapping *saved = 0;
    size_t used = 0, capacity = 0;
    while (size) {
        avxemu_mapping mapping;
        if (avxemu_memory_query(address, &mapping) != 1 ||
            mapping.end <= address) goto fail;
        size_t chunk = mapping.end - address;
        if (chunk > size) chunk = size;
        if (used == capacity) {
            if (capacity > SIZE_MAX / 2 / sizeof *saved) goto fail;
            size_t next = capacity ? capacity * 2 : 4;
            avxemu_mapping *p = realloc(saved, next * sizeof *p);
            if (!p) goto fail;
            saved = p; capacity = next;
        }
        if (!avxemu_memory_patch_begin(address, chunk, &saved[used])) goto fail;
        used++; address += chunk; size -= chunk;
    }
    *count = used;
    return saved;
fail:
    avxemu_patch_write_end(saved, used);
    return 0;
}

/* Individual functions never cross executable range boundaries. */
long avxemu_patch_zcnt_image(const avxemu_image *image, unsigned required, int recursive) {
    if (!image || !required) return 0;
    long patched = 0;
    for (size_t ri = 0; ri < image->nranges; ri++) {
        const avxemu_code_range *range = &image->ranges[ri];
        uint8_t *text = range->base;
        size_t nsaved;
        avxemu_mapping *saved = avxemu_patch_write_begin((uintptr_t)text, range->size, &nsaved);
        if (!saved) {
            avxemu_lc_rec_fail(); continue;
        }
        if (avxemu_lc_mode == LC_REPLAY && image->nranges == 1) {
            const lc_byte *b; uint32_t n = avxemu_lc_bytes(&b);
            int ok = 1;
            for (uint32_t i = 0; i < n && ok; i++)
                ok = b[i].off < range->size && text[b[i].off] == b[i].old;
            if (ok) {
                for (uint32_t i = 0; i < n; i++) text[b[i].off] = b[i].neu;
                patched += (long)n;
                avxemu_patch_write_end(saved, nsaved); continue;
            }
            avxemu_lc_invalid();
        }
        uint8_t *before = 0;
        if (avxemu_lc_mode == LC_RECORD && image->nranges == 1) {
            before = malloc(range->size);
            if (before) memcpy(before, text, range->size);
            else avxemu_lc_rec_fail();
        }
        for (size_t fi = 0; fi < image->nfunctions; fi++) {
            const avxemu_function *f = &image->functions[fi];
            if (f->range != ri || f->begin >= f->end || f->end > range->size) continue;
            size_t capacity = (f->end - f->begin) / 2 + 1;
            if (capacity > SIZE_MAX / sizeof(size_t) || capacity > INT32_MAX) {
                avxemu_lc_rec_fail(); continue;
            }
            size_t *sites = malloc(capacity * sizeof *sites);
            if (!sites) { avxemu_lc_rec_fail(); continue; }
            long n = lde_scan_func_mask(text, f->begin, f->end, range->readable,
                                        1, required, recursive, sites, (int)capacity, avxemu_cpuid_trap);
            if (n > 0) patched += n;
            else if (n < 0) avxemu_lc_rec_fail();
            free(sites);
        }
        /* Preserve Darwin's existing cold-code mop-up. ELF executable segments
         * may contain arbitrary data; only reachable function code is eligible. */
        if (!recursive) {
            long n = lde_scan_zcnt_mask(text, range->size, 1, required);
            if (n > 0) patched += n;
        }
        if (before) {
            for (size_t i = 0; i < range->size; i++)
                if (before[i] != text[i]) avxemu_lc_rec_byte((uint32_t)i, before[i], text[i]);
            free(before);
        }
        avxemu_patch_write_end(saved, nsaved);
    }
    return patched;
}

long avxemu_patch_lzcnt(void) {
    uint32_t r7[4] = {0}, r8[4] = {0};
    (void)avxemu_get_cpuid(7, 0, r7);
    (void)avxemu_get_cpuid(0x80000001u, 0, r8);
    unsigned required = avxemu_zcnt_required(r7[1], r8[2], getenv("AVXEMU_FORCEPATCH") != 0);
    return avxemu_patch_zcnt_image(avxemu_image_main(), required, AVXEMU_SCAN_RECURSIVE);
}
