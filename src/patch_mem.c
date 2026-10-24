/* In-memory LZCNT/TZCNT prefix edits, scoped to executable image ranges. */
#include "lde.h"
#include "scan.h"
#include "decode.h"
#include "avxemu_cpuid.h"
#include "lcache.h"
#include "image.h"
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <limits.h>

static void restore(const avxemu_mapping *saved) {
    if (!avxemu_memory_patch_end(saved)) {
        static const char msg[] = "avxemu: fatal: cannot restore patch page permissions\n";
        (void)!write(2, msg, sizeof msg - 1); _exit(127);
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

/* Required-encoding helpers. Strict discovery below proves candidate coverage
 * within validated functions; unknown bytes require conservative absence proof.
 * Legacy discovery uses the separately documented compatibility policy. */
static int zcnt_prefix(unsigned b) {
    return (b>=0x40 && b<=0x4f) || b==0x66 || b==0x67 || b==0xf3 ||
        b==0xf2 || b==0xf0 || b==0x26 || b==0x2e || b==0x36 ||
        b==0x3e || b==0x64 || b==0x65;
}
static int possible_zcnt(const uint8_t *p, size_t n, unsigned required) {
    if (!n || *p != 0xf3) return 0;
    size_t i = 1;
    while (i < n && i < 13) {
        unsigned b = p[i];
        if (zcnt_prefix(b)) i++;
        else break;
    }
    if (n - i < 2) return i == n || p[i] == 0x0f;
    return p[i] == 0x0f &&
        ((p[i + 1] == 0xbd && (required & LDE_LZCNT)) ||
         (p[i + 1] == 0xbc && (required & LDE_TZCNT)));
}
enum { ZCNT_PREFIX=1, ZCNT_DATA=2, ZCNT_UNREACHABLE=3 };
/* An overlapping entry can share an existing trap without authorizing a new
 * edit. Require a complete <=15-byte candidate and identical handler decoding
 * after changing only its selected F3 prefix. This covers F3 operand bytes
 * immediately before a real F3 instruction, not arbitrary instruction interiors. */
static int candidate_rewrite(const uint8_t *p, size_t n, unsigned required, size_t *site) {
    int op,prefix; size_t bound=n<15?n:15;
    int len=x86_len(p,p+bound,&op,&prefix);
    if(len<=0 || len>15 || !((unsigned)op&required) || prefix<0 || p[prefix]!=0xf3) return 0;
    /* Conflicting group1 prefix precedence is not part of this proof. */
    for(int i=0;i<len && zcnt_prefix(p[i]);i++) if(p[i]==0xf0 || p[i]==0xf2) return 0;
    uint8_t copy[32]={0}; memcpy(copy,p,(size_t)len);
    decoded before,after;
    if(decode(copy,&before)!=len ||
        before.op!=(op==LDE_LZCNT?BMI_LZCNT:BMI_TZCNT)) return 0;
    copy[prefix]=0xf0;
    if(decode(copy,&after)!=len || memcmp(&before,&after,sizeof before)) return 0;
    *site=(size_t)prefix; return 1;
}
static int candidate_covered(const uint8_t *p, size_t n, unsigned required,
                              const uint8_t *proven) {
    size_t site;
    return candidate_rewrite(p,n,required,&site) && proven[site]==ZCNT_PREFIX;
}
struct avxemu_zcnt_plan {
    const avxemu_image *image;
    lc_site *sites;
    size_t count, capacity;
    unsigned required;
    int from_replay;
};
typedef struct avxemu_zcnt_plan zcnt_plan;
static int plan_add(zcnt_plan *plan, uint32_t range, size_t offset) {
    if (plan->count == plan->capacity) {
        size_t next = plan->capacity ? plan->capacity * 2 : 64;
        if (next < plan->capacity || next > SIZE_MAX / sizeof(lc_site) || next > LONG_MAX) return 0;
        lc_site *sites = realloc(plan->sites, next * sizeof *sites);
        if (!sites) return 0;
        plan->sites = sites; plan->capacity = next;
    }
    lc_site site = {range, 0, offset};
    plan->sites[plan->count++] = site;
    return 1;
}
static int contains_zcnt(const uint8_t *bytes, size_t n, unsigned required) {
    for (size_t i = 0; i < n; i++)
        if (possible_zcnt(bytes + i, n - i, required)) return 1;
    return 0;
}
/* Historical linear-first discovery is a compatibility policy, not a proof
 * that every selected byte is reachable. Keep its range mop-up, but never edit
 * declared data or islands excluded by a complete direct-control-flow map. */
static int discover_legacy(const avxemu_image *image, unsigned required, zcnt_plan *plan) {
    for(size_t ri=0;ri<image->nranges;ri++) {
        const avxemu_code_range *r=&image->ranges[ri];
        if(!r->base || !r->size || r->readable<r->size) return 0;
        if(!contains_zcnt(r->base,r->size,required)) continue;
        uint8_t *excluded=calloc(r->size,1),*selected=calloc(r->size,1);
        if(!excluded || !selected) { free(excluded);free(selected);return 0; }
        for(size_t di=0;di<image->ndata;di++) {
            const avxemu_data_span *d=&image->data[di];
            if(d->range==ri) memset(excluded+d->begin,1,d->end-d->begin);
        }
        for(size_t fi=0;fi<image->nfunctions;fi++) {
            const avxemu_function *f=&image->functions[fi];
            if(f->range!=ri) continue;
            size_t n=f->end-f->begin;
            if(!contains_zcnt(r->base+f->begin,n,required)) continue;
            uint8_t *code=calloc(n,1),*covered=calloc(n,1);
            int indirect=1,complete=code && covered && lde_rd_map_direct(r->base,f->begin,f->end,
                r->readable,code,covered,&indirect,avxemu_cpuid_trap,excluded+f->begin) && !indirect;
            if(complete) {
                memset(covered,0,n);size_t previous=0;
                for(size_t q=0;q<n;q++) if(code[q]) {
                    int op,prefix,len=x86_len(r->base+f->begin+q,r->base+f->end,&op,&prefix);
                    if(len<=0 || q<previous) { complete=0;break; }
                    previous=q+(size_t)len;memset(covered+q,1,(size_t)len);
                }
                if(complete) for(size_t q=0;q<n;q++) if(!covered[q]) excluded[f->begin+q]=1;
            }
            free(code);free(covered);
            long count=lde_collect_zcnt(r->base,f->begin,f->end,r->readable,required,0,
                excluded,selected,0,avxemu_cpuid_trap);
            if(count<0) {
                memset(selected+f->begin,0,n);
                count=lde_collect_zcnt(r->base,f->begin,f->end,r->readable,required,1,
                    excluded,selected,0,avxemu_cpuid_trap);
                if(count<0) memset(selected+f->begin,0,n);
            }
        }
        /* As in the established range mop-up, retain sites collected before a
         * length-decoder failure. The completed plan is validated before writes. */
        (void)lde_collect_zcnt(r->base,0,r->size,r->readable,required,0,
            excluded,selected,1,avxemu_cpuid_trap);
        int valid=1;
        for(size_t i=0;i<r->size && valid;i++) if(selected[i] && !excluded[i])
            valid=plan_add(plan,(uint32_t)ri,i);
        free(excluded);free(selected);
        if(!valid) return 0;
    }
    return 1;
}

/* Fresh discovery uses the backend-selected strategy. Strict replay also
 * compares every record with this complete canonical plan. */
static int discover_zcnt(const avxemu_image *image, unsigned required, zcnt_plan *plan) {
    if (!required) return 1;
    if (!image || image->nranges > UINT32_MAX || !avxemu_image_data_valid(image)) return 0;
    for (size_t fi = 0; fi < image->nfunctions; fi++) {
        const avxemu_function *f = &image->functions[fi];
        if (f->range >= image->nranges || f->begin >= f->end ||
            f->end > image->ranges[f->range].size) return 0;
        if (fi && (f->range < image->functions[fi-1].range ||
            (f->range == image->functions[fi-1].range && f->begin < image->functions[fi-1].end))) return 0;
    }
    if(image->strategy==AVXEMU_SCAN_LEGACY) return discover_legacy(image,required,plan);
    int any_candidate=0;
    for(size_t ri=0;ri<image->nranges;ri++) {
        const avxemu_code_range *r=&image->ranges[ri];
        if(!r->base || !r->size || r->readable<r->size) return 0;
        if(contains_zcnt(r->base,r->size,required)) any_candidate=1;
    }
    if(!image->ndata && !any_candidate) return 1;
    avxemu_scan *scan=avxemu_scan_prepare(image,avxemu_cpuid_trap,AVXEMU_SCAN_WORK_LIMIT);
    if(!scan) return 0;
    int valid=1;
    for (size_t ri = 0; ri < image->nranges && valid; ri++) {
        const avxemu_code_range *r = &image->ranges[ri];
        uint8_t *proven = calloc(r->size, 1);
        if (!proven) { valid=0;break; }
        for(size_t di=0;di<image->ndata;di++) {
            const avxemu_data_span *d=&image->data[di];
            if(d->range==ri) memset(proven+d->begin,ZCNT_DATA,d->end-d->begin);
        }
        for (size_t fi = 0; fi < image->nfunctions && valid; fi++) {
            const avxemu_function *f = &image->functions[fi];
            if (f->range != ri) continue;
            const avxemu_scan_map *map=avxemu_scan_function(scan,fi);
            for(size_t off=0;off<f->end-f->begin && valid;off++) if(map->code[off]) {
                int op,prefix;
                x86_len(r->base+f->begin+off,r->base+f->end,&op,&prefix);
                if(((unsigned)op & required) && prefix>=0) {
                    size_t site=f->begin+off+(size_t)prefix;
                    if(!plan_add(plan,(uint32_t)ri,site)) valid=0;
                    else proven[site]=ZCNT_PREFIX;
                }
            }
            /* The image-wide closure includes every proven external entry.
             * Only then can complete direct flow exclude skipped bytes. */
            if(!map->indirect) for(size_t i=f->begin;i<f->end;i++)
                if(!proven[i]) proven[i]=ZCNT_UNREACHABLE;
        }
        for (size_t i = 0; i < r->size && valid; i++)
            if (!proven[i] && possible_zcnt(r->base + i, r->size - i, required) &&
                !candidate_covered(r->base+i,r->size-i,required,proven+i)) valid=0;
        free(proven);
    }
    avxemu_scan_free(scan);return valid;
}

/* Legacy cache records retain the established identity/integrity trust model.
 * Local decoding checks each edit, not global boundaries or completeness. Strict
 * images never use this shortcut: their complete canonical plan is rediscovered. */
static int legacy_replay_plan(const avxemu_image *image, unsigned required, zcnt_plan *plan) {
    if(!avxemu_image_data_valid(image)) return 0;
    const lc_byte *bytes;
    uint32_t count=avxemu_lc_bytes(&bytes);
    for(uint32_t i=0;i<count;i++) {
        const lc_byte *b=&bytes[i];
        uint8_t *p=avxemu_lc_resolve(image,b->site,1);
        if(!p || b->old!=0xf3 || b->neu!=0xf0 || *p!=0xf3 ||
            (i && (b->site.range<bytes[i-1].site.range ||
             (b->site.range==bytes[i-1].site.range && b->site.off<=bytes[i-1].site.off)))) return 0;
        const avxemu_code_range *r=&image->ranges[b->site.range];
        size_t n=r->size-(size_t)b->site.off;
        int op,prefix,len=x86_len(p,p+(n<15?n:15),&op,&prefix);
        if(len<=0 || !((unsigned)op&required) || prefix!=0 ||
            avxemu_image_data_overlap(image,b->site.range,(size_t)b->site.off,(size_t)len) ||
            !plan_add(plan,b->site.range,(size_t)b->site.off)) return 0;
    }
    plan->from_replay=1;
    return 1;
}
avxemu_zcnt_plan *avxemu_zcnt_prepare(const avxemu_image *image, unsigned required) {
    zcnt_plan *plan=calloc(1,sizeof *plan);
    if(!plan) return 0;
    plan->image=image; plan->required=required;
    if(image && image->strategy==AVXEMU_SCAN_LEGACY && avxemu_lc_mode==LC_REPLAY) {
        if(legacy_replay_plan(image,required,plan)) return plan;
        avxemu_lc_invalid();
        free(plan->sites); plan->sites=0; plan->count=plan->capacity=0;
    }
    if(!discover_zcnt(image,required,plan)) { avxemu_zcnt_discard(plan); return 0; }
    return plan;
}
void avxemu_zcnt_discard(avxemu_zcnt_plan *plan) {
    if(plan) { free(plan->sites); free(plan); }
}
int avxemu_zcnt_preflight(const avxemu_image *image, unsigned required) {
    zcnt_plan plan={0};
    int valid=discover_zcnt(image,required,&plan);
    free(plan.sites);
    return valid;
}
long avxemu_zcnt_apply(avxemu_zcnt_plan *plan) {
    if(!plan) return -1;
    const avxemu_image *image=plan->image;
    if(plan->from_replay && avxemu_lc_mode!=LC_REPLAY) {
        /* A prerequisite pass rejected the cache after preparation. */
        free(plan->sites); plan->sites=0; plan->count=plan->capacity=0;
        plan->from_replay=0;
        if(!discover_zcnt(image,plan->required,plan)) { avxemu_zcnt_discard(plan); return -1; }
    }
    /* The intervening CPUID pass may only change its owned instructions. Check
     * all planned originals before writing any prefix, including on fresh runs. */
    for(size_t i=0;i<plan->count;i++) {
        lc_site site=plan->sites[i];
        if(image->ranges[site.range].base[site.off]!=0xf3) {
            avxemu_lc_rec_fail(); avxemu_zcnt_discard(plan); return -1;
        }
    }
    if (avxemu_lc_mode == LC_REPLAY) {
        const lc_byte *bytes;
        uint32_t count = avxemu_lc_bytes(&bytes);
        int valid = count == plan->count;
        for (uint32_t i = 0; i < count && valid; i++)
            valid = bytes[i].site.range == plan->sites[i].range &&
                bytes[i].site.off == plan->sites[i].off && bytes[i].old == 0xf3 && bytes[i].neu == 0xf0;
        if (!valid) avxemu_lc_invalid();
    }
    /* Discovery and complete replay validation precede the first write. Even a
     * rejected cache falls back only to the current strategy's freshly collected plan. */
    size_t i = 0;
    while (i < plan->count) {
        uint32_t ri = plan->sites[i].range;
        const avxemu_code_range *range = &image->ranges[ri];
        size_t nsaved;
        avxemu_mapping *saved = avxemu_patch_write_begin((uintptr_t)range->base, range->size, &nsaved);
        if (!saved) { avxemu_lc_rec_fail(); avxemu_zcnt_discard(plan); return -1; }
        while (i < plan->count && plan->sites[i].range == ri) {
            lc_site site = plan->sites[i++];
            range->base[site.off] = 0xf0;
            if (avxemu_lc_mode == LC_RECORD) avxemu_lc_rec_byte(site, 0xf3, 0xf0);
        }
        avxemu_patch_write_end(saved, nsaved);
    }
    long result = (long)plan->count;
    avxemu_zcnt_discard(plan);
    return result;
}

long avxemu_patch_zcnt_image(const avxemu_image *image, unsigned required) {
    return avxemu_zcnt_apply(avxemu_zcnt_prepare(image,required));
}

long avxemu_patch_lzcnt(void) {
    uint32_t r7[4] = {0}, r8[4] = {0};
    (void)avxemu_get_cpuid(7, 0, r7);
    (void)avxemu_get_cpuid(0x80000001u, 0, r8);
    unsigned required = avxemu_zcnt_required(r7[1], r8[2], getenv("AVXEMU_FORCEPATCH") != 0);
    return avxemu_patch_zcnt_image(avxemu_image_main(), required);
}
