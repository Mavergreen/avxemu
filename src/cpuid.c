/* CPU feature policy and owned CPUID trap sites. */
#include "avxemu_cpuid.h"
#include "asm.h"
#include "lde.h"
#include "scan.h"
#include "lcache.h"
#include <stdlib.h>
#include <string.h>

static uint64_t g_cpuid_addrs[16384];
static size_t g_cpuid_count;
static uint32_t g_l1ecx_set, g_l1ecx_clr, g_l7ebx_set, g_l7ebx_clr;

/*
 * avxemu's own cpuid, kept out of __text on purpose. avxemu_patch_cpuid turns
 * every cpuid in the main image's __text into ud2, and when avxemu is linked
 * INTO the main image (the test programs are) that includes ours: the handler
 * would fault on the very cpuid it emulates cpuid with, forever, and feature
 * detection would see the AVX2 we advertise instead of the CPU it runs on.
 * A section of its own in __TEXT is executable and invisible to that scan.
 */
AVXEMU_RAW_ATTR
void avxemu_cpuid_raw(uint32_t leaf, uint32_t sub, uint32_t r[4]) {
    __asm__ volatile("cpuid" : "=a"(r[0]), "=b"(r[1]), "=c"(r[2]), "=d"(r[3]) : "a"(leaf), "c"(sub));
}
int avxemu_cpu_baseline(void) {
    uint32_t r[4];
    if (!avxemu_get_cpuid(1, 0, r)) return 0;
    const uint32_t needed = (1u << 20) | (1u << 27) | (1u << 28);
    if ((r[2] & needed) != needed) return 0;
    uint32_t lo, hi;
    __asm__ volatile("xgetbv" : "=a"(lo), "=d"(hi) : "c"(0));
    return (lo & 6) == 6;
}
/* __get_cpuid's contract: 0 if `leaf` is past the CPU's maximum. */
int avxemu_get_cpuid(uint32_t leaf, uint32_t sub, uint32_t r[4]) {
    uint32_t m[4];
    avxemu_cpuid_raw(leaf & 0x80000000u, 0, m);
    if (m[0] < leaf) return 0;
    avxemu_cpuid_raw(leaf, sub, r);
    return 1;
}

/* Map a feature token to its cpuid bit and OR it into the set or clr mask. */
static void cpuid_feat(const char *name, int set){
    uint32_t *l1 = set ? &g_l1ecx_set : &g_l1ecx_clr;
    uint32_t *l7 = set ? &g_l7ebx_set : &g_l7ebx_clr;
    if      (!strcmp(name,"avx"))   *l1 |= (1u<<28);
    else if (!strcmp(name,"fma"))   *l1 |= (1u<<12);
    else if (!strcmp(name,"movbe")) *l1 |= (1u<<22);
    else if (!strcmp(name,"f16c"))  *l1 |= (1u<<29);
    else if (!strcmp(name,"avx2"))  *l7 |= (1u<<5);
    else if (!strcmp(name,"bmi1"))  *l7 |= (1u<<3);
    else if (!strcmp(name,"bmi2"))  *l7 |= (1u<<8);
    else if (!strcmp(name,"bmi"))   *l7 |= (1u<<3)|(1u<<8);
    else if (!strcmp(name,"all")) { *l1 |= (1u<<28)|(1u<<12)|(1u<<22)|(1u<<29); *l7 |= (1u<<5)|(1u<<3)|(1u<<8); }
}
static void cpuid_parse(const char *spec, int set){
    if (!spec) return;
    char buf[64]; int bi=0;
    for (const char *s=spec; ; s++){
        if (*s==',' || *s=='\0'){ buf[bi]=0; if (bi) cpuid_feat(buf,set); bi=0; if (!*s) break; }
        else if (bi < (int)sizeof buf-1) buf[bi++]=*s;
    }
}

void avxemu_cpuid_configure(void) {
    g_l7ebx_set |= (1u << 5) | (1u << 3) | (1u << 8);
    const char *fc = getenv("AVXEMU_FAKE_CPUID");
    if (fc && *fc) cpuid_parse("all", fc[1] == 'f' ? 0 : 1);
    cpuid_parse(getenv("AVXEMU_CPUID_SET"), 1);
    cpuid_parse(getenv("AVXEMU_CPUID_CLR"), 0);
}

unsigned avxemu_zcnt_required(uint32_t leaf7_ebx, uint32_t ext1_ecx, int force) {
    return force ? 3u : ((ext1_ecx & (1u << 5) ? 0u : 1u) |
                         (leaf7_ebx & (1u << 3) ? 0u : 2u));
}

int avxemu_cpuid_trap(uintptr_t address) {
    size_t lo = 0, hi = g_cpuid_count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (g_cpuid_addrs[mid] < address) lo = mid + 1;
        else hi = mid;
    }
    return lo < g_cpuid_count && g_cpuid_addrs[lo] == address;
}

int avxemu_cpuid_handle(avxemu_regfile *rf) {
    if (avxemu_cpuid_trap((uintptr_t)rf->rip)) {
        uint32_t leaf = (uint32_t)rf->gpr[0], sub = (uint32_t)rf->gpr[1], r[4];
        avxemu_cpuid_raw(leaf, sub, r);
        if (leaf == 1) r[2] = (r[2] | g_l1ecx_set) & ~g_l1ecx_clr;
        else if (leaf == 7 && sub == 0) r[1] = (r[1] | g_l7ebx_set) & ~g_l7ebx_clr;
        rf->gpr[0] = r[0]; rf->gpr[3] = r[1];
        rf->gpr[1] = r[2]; rf->gpr[2] = r[3]; rf->rip += 2;
        return 1;
    }
    return 0;
}

long avxemu_cpuid_patch_image(const avxemu_image *image, int recursive) {
    if (!image || !avxemu_image_data_valid(image)) return 0;
    size_t available = sizeof g_cpuid_addrs / sizeof g_cpuid_addrs[0] - g_cpuid_count;
    size_t *sites = malloc((available ? available : 1) * sizeof *sites);
    if (!sites) { avxemu_lc_rec_fail(); return 0; }
    avxemu_scan *scan=image->strategy==AVXEMU_SCAN_STRICT ?
        avxemu_scan_prepare(image,avxemu_cpuid_trap,AVXEMU_SCAN_WORK_LIMIT):0;
    if(image->strategy==AVXEMU_SCAN_STRICT && !scan) {
        if(avxemu_lc_mode==LC_REPLAY) avxemu_lc_invalid();
        avxemu_lc_rec_fail();free(sites);return 0;
    }
    if (avxemu_lc_mode == LC_REPLAY) {
        const lc_site *cached; uint32_t n = avxemu_lc_cpuid(&cached);
        int valid = n <= available;
        for (uint32_t i = 0; i < n && valid; i++) {
            uint8_t *p = avxemu_lc_resolve(image, cached[i], 2);
            valid = p && p[0] == 0x0f && p[1] == 0xa2 &&
                (image->strategy!=AVXEMU_SCAN_STRICT ||
                 avxemu_scan_span(scan,(uintptr_t)p,2));
        }
        if (!valid) avxemu_lc_invalid();
    }
    long total = 0;
    for (size_t ri = 0; ri < image->nranges; ri++) {
        const avxemu_code_range *range = &image->ranges[ri];
        size_t count = 0;
        int ok = 1;
        if (avxemu_lc_mode == LC_REPLAY) {
            const lc_site *cached; uint32_t n = avxemu_lc_cpuid(&cached);
            for (uint32_t i = 0; i < n; i++)
                if (cached[i].range == ri) sites[count++] = (size_t)cached[i].off;
        } else ok = 0;
        if (!ok) {
            if (!recursive && image->strategy!=AVXEMU_SCAN_STRICT) {
                long n = lde_scan_sites(range->base, 0, range->size, range->readable,
                                        LDE_CPUID, 0, sites, available, avxemu_cpuid_trap);
                if (n < 0) { avxemu_lc_rec_fail(); continue; }
                count = (size_t)n;
            } else {
                for (size_t fi = 0; fi < image->nfunctions; fi++) {
                    const avxemu_function *f = &image->functions[fi];
                    if (f->range != ri) continue;
                    /* The raw helper must remain callable even when linked
                     * into the main ELF executable's load segment. */
                    uintptr_t raw = (uintptr_t)avxemu_cpuid_raw;
                    if (raw >= (uintptr_t)range->base + f->begin &&
                        raw < (uintptr_t)range->base + f->end) continue;
                    long n;
                    if(image->strategy==AVXEMU_SCAN_STRICT) {
                        size_t size=f->end-f->begin;
                        const avxemu_scan_map *map=avxemu_scan_function(scan,fi);
                        n=0;
                        for(size_t off=0;off+1<size;off++) if(map->code[off] &&
                            range->base[f->begin+off]==0x0f && range->base[f->begin+off+1]==0xa2) {
                            if(count+(size_t)n==available) { n=-1;break; }
                            sites[count+(size_t)n++]=f->begin+off;
                        }
                    } else n = lde_scan_sites(range->base, f->begin, f->end, range->readable,
                                            LDE_CPUID, 1, sites + count, available - count, avxemu_cpuid_trap);
                    if (n < 0) { ok = -1; break; }
                    count += (size_t)n;
                }
                if (ok == -1) { avxemu_lc_rec_fail(); continue; }
            }
        }
        size_t kept=0;
        for(size_t i=0;i<count;i++) if(!avxemu_image_data_overlap(image,(uint32_t)ri,sites[i],2))
            sites[kept++]=sites[i];
        count=kept;
        /* Reserve ownership before publishing each UD2. A full table declines
         * the whole range; it can never leave an unowned trap behind. */
        if (!count) continue;
        size_t nsaved;
        avxemu_mapping *saved = avxemu_patch_write_begin((uintptr_t)range->base, range->size, &nsaved);
        if (!saved) { avxemu_lc_rec_fail(); continue; }
        for (size_t i = 0; i < count; i++) {
            uint8_t *p = range->base + sites[i];
            size_t at = g_cpuid_count;
            while (at && g_cpuid_addrs[at - 1] > (uintptr_t)p) {
                g_cpuid_addrs[at] = g_cpuid_addrs[at - 1]; at--;
            }
            g_cpuid_addrs[at] = (uintptr_t)p;
            g_cpuid_count++;
            __asm__ volatile("" ::: "memory"); /* ownership precedes the trap */
            p[1] = 0x0b;
            lc_site site = {(uint32_t)ri, 0, sites[i]};
            avxemu_lc_rec_cpuid(site);
            total++;
        }
        avxemu_patch_write_end(saved, nsaved);
        available = sizeof g_cpuid_addrs / sizeof g_cpuid_addrs[0] - g_cpuid_count;
    }
    avxemu_scan_free(scan);free(sites);
    return total;
}

void avxemu_patch_cpuid(void) {
    const avxemu_image *image=avxemu_image_main();
    if (image && (g_l1ecx_set | g_l1ecx_clr | g_l7ebx_set | g_l7ebx_clr))
        (void)avxemu_cpuid_patch_image(image,image->strategy==AVXEMU_SCAN_STRICT);
}
