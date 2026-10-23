/* Shared strict authorization for required patches and optional cold/replay
 * scans. Propagate only direct entries, including from candidate-free sources. */
#include "scan.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

struct avxemu_scan {
    const avxemu_image *image;
    avxemu_scan_map *maps;
};
void avxemu_scan_free(avxemu_scan *scan) {
    if(!scan) return;
    if(scan->maps) for(size_t i=0;i<scan->image->nfunctions;i++) {
        free(scan->maps[i].code);free(scan->maps[i].targets);
    }
    free(scan->maps);free(scan);
}
const avxemu_scan_map *avxemu_scan_function(const avxemu_scan *scan, size_t index) {
    return scan && index<scan->image->nfunctions?&scan->maps[index]:0;
}

avxemu_scan *avxemu_scan_prepare(const avxemu_image *image, lde_trap_lookup traps,
    size_t work_limit) {
    if(!avxemu_image_data_valid(image)) return 0;
    for(size_t ri=0;ri<image->nranges;ri++) {
        const avxemu_code_range *r=&image->ranges[ri];
        if(!r->base || !r->size || r->size>r->readable ||
            (uintptr_t)r->base>(uintptr_t)LONG_MAX-INT32_MAX-15 ||
            r->size>(uintptr_t)LONG_MAX-INT32_MAX-15-(uintptr_t)r->base) return 0;
    }
    size_t nf=image->nfunctions,total=0;
    for(size_t i=0;i<nf;i++) {
        const avxemu_function *f=&image->functions[i];
        if(f->range>=image->nranges || f->begin>=f->end || f->end>image->ranges[f->range].size ||
            (i && (f->range<image->functions[i-1].range ||
            (f->range==image->functions[i-1].range && f->begin<image->functions[i-1].end)))) return 0;
        if(f->end-f->begin>work_limit-total) return 0;
        total+=f->end-f->begin;
    }
    if(nf>SIZE_MAX/sizeof(size_t) || nf>SIZE_MAX/sizeof(avxemu_scan_map)) return 0;
    avxemu_scan *scan=calloc(1,sizeof *scan);
    uint8_t **entries=calloc(nf?nf:1,sizeof *entries),*queued=calloc(nf?nf:1,1);
    size_t *queue=calloc(nf?nf:1,sizeof *queue);
    if(scan) { scan->image=image;scan->maps=calloc(nf?nf:1,sizeof *scan->maps); }
    if(!scan || !scan->maps || !entries || !queued || !queue) goto fail;
    for(size_t i=0;i<nf;i++) {
        size_t n=image->functions[i].end-image->functions[i].begin;
        scan->maps[i].code=calloc(n,1);scan->maps[i].targets=calloc(n,1);
        entries[i]=calloc(n,1);
        if(!scan->maps[i].code || !scan->maps[i].targets || !entries[i]) goto fail;
        entries[i][0]=1;queue[i]=i;queued[i]=1;
    }
    size_t head=0,tail=0,pending=nf;
    while(pending) {
        size_t fi=queue[head];head=(head+1)%nf;pending--;queued[fi]=0;
        const avxemu_function *f=&image->functions[fi];
        const avxemu_code_range *r=&image->ranges[f->range];
        size_t n=f->end-f->begin;
        if(n>work_limit) goto fail;
        work_limit-=n;
        avxemu_scan_map *map=&scan->maps[fi];
        uint8_t *data=calloc(n,1);
        if(!data) goto fail;
        for(size_t di=0;di<image->ndata;di++) {
            const avxemu_data_span *d=&image->data[di];
            if(d->range!=f->range || d->end<=f->begin || d->begin>=f->end) continue;
            size_t begin=d->begin>f->begin?d->begin:f->begin;
            size_t end=d->end<f->end?d->end:f->end;
            memset(data+begin-f->begin,1,end-begin);
        }
        memset(map->code,0,n);memset(map->targets,0,n);
        int valid=lde_rd_map_direct_roots(r->base,f->begin,f->end,r->readable,
            map->code,map->targets,&map->indirect,traps,data,entries[fi]);
        free(data);
        if(!valid) goto fail;
        for(size_t off=0;off<n;off++) if(entries[fi][off]) map->targets[off]=1;
        size_t previous=0;
        for(size_t off=0;off<n;off++) if(map->code[off]) {
            int op,prefix,term,ind;long target;
            const uint8_t *p=r->base+f->begin+off;
            int len=x86_len(p,r->base+f->end,&op,&prefix);
            if(len<=0 || len>15 || off<previous) goto fail;
            previous=off+(size_t)len;
            lde_cflow(p,r->base+f->end,len,(long)(uintptr_t)p,&term,&target,&ind);
            uint32_t ri;size_t at;
            if(target<0 || !avxemu_image_find(image,(uintptr_t)target,1,&ri,&at)) continue;
            if(avxemu_image_data_overlap(image,ri,at,1)) goto fail;
            const avxemu_function *dest=avxemu_image_function(image,(uintptr_t)target);
            if(!dest) goto fail; /* A direct entry into an unknown gap has no safe bound. */
            if(dest==f) continue;
            size_t di=(size_t)(dest-image->functions),entry=at-dest->begin;
            if(entries[di][entry]) continue;
            entries[di][entry]=1;
            if(!queued[di]) {
                queue[tail]=di;tail=(tail+1)%nf;pending++;queued[di]=1;
            }
        }
    }
    for(size_t i=0;i<nf;i++) free(entries[i]);
    free(entries);free(queued);free(queue);return scan;
fail:
    if(entries) for(size_t i=0;i<nf;i++) free(entries[i]);
    free(entries);free(queued);free(queue);avxemu_scan_free(scan);return 0;
}

int avxemu_scan_span(const avxemu_scan *scan, uintptr_t address, size_t size) {
    if(!scan || !size) return 0;
    const avxemu_image *image=scan->image;
    const avxemu_function *f=avxemu_image_function(image,address);
    if(!f) return 0;
    const avxemu_code_range *r=&image->ranges[f->range];
    size_t begin=address-(uintptr_t)r->base;
    if(size>f->end-begin) return 0;
    const avxemu_scan_map *map=&scan->maps[f-image->functions];
    for(size_t off=begin;off<begin+size;) {
        int op,prefix,len=x86_len(r->base+off,r->base+f->end,&op,&prefix);
        if(!map->code[off-f->begin] || len<=0 || (size_t)len>begin+size-off) return 0;
        off+=(size_t)len;
    }
    return 1;
}
