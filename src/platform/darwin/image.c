#define _DARWIN_C_SOURCE
#include "image.h"
#include "../../image.h"
#include <mach-o/dyld.h>
#include <stdlib.h>
#include <string.h>

static avxemu_image g_image;
static int g_ready;

static int loaded_address(uint64_t vaddr, intptr_t slide, uintptr_t *address) {
    if (vaddr > UINTPTR_MAX) return 0;
    if (slide >= 0) {
        if (vaddr > UINTPTR_MAX - (uintptr_t)slide) return 0;
        *address = (uintptr_t)vaddr + (uintptr_t)slide;
    } else {
        uintptr_t amount = (uintptr_t)(-(slide + 1)) + 1;
        if (vaddr < amount) return 0;
        *address = (uintptr_t)vaddr - amount;
    }
    return 1;
}

/* The loader owns these mappings. Copy command bytes only after checking their
 * readable extent, then bound every command and section against that copy. */
static int readable_span(uintptr_t p, size_t n) {
    if (n > UINTPTR_MAX - p) return 0;
    while (n) {
        avxemu_mapping m;
        if (avxemu_memory_query(p, &m) != 1 || !(m.prot & AVXEMU_PROT_R) || m.end <= p)
            return 0;
        size_t part = m.end - p;
        if (part >= n) return 1;
        p += part; n -= part;
    }
    return 1;
}

static uint8_t *commands(const struct mach_header_64 *mh, struct mach_header_64 *hdr) {
    if (avxemu_memory_read(hdr, (uintptr_t)mh, sizeof *hdr) != sizeof *hdr ||
        hdr->magic != MH_MAGIC_64 || hdr->ncmds > hdr->sizeofcmds / sizeof(struct load_command))
        return 0;
    uintptr_t start = (uintptr_t)mh;
    if (sizeof *hdr > UINTPTR_MAX - start) return 0;
    start += sizeof *hdr;
    if (!readable_span(start, hdr->sizeofcmds)) return 0;
    uint8_t *data = malloc(hdr->sizeofcmds ? hdr->sizeofcmds : 1);
    if (!data) return 0;
    if (avxemu_memory_read(data, start, hdr->sizeofcmds) != hdr->sizeofcmds) goto fail;
    size_t off = 0;
    for (uint32_t i = 0; i < hdr->ncmds; i++) {
        if (hdr->sizeofcmds - off < sizeof(struct load_command)) goto fail;
        const struct load_command *lc = (const void *)(data + off);
        if (lc->cmdsize < sizeof *lc || lc->cmdsize > hdr->sizeofcmds - off || lc->cmdsize % 8)
            goto fail;
        if (lc->cmd == LC_SEGMENT_64) {
            if (lc->cmdsize < sizeof(struct segment_command_64)) goto fail;
            const struct segment_command_64 *sg = (const void *)lc;
            if (sg->nsects > (lc->cmdsize - sizeof *sg) / sizeof(struct section_64) ||
                sg->vmsize > UINT64_MAX - sg->vmaddr ||
                sg->filesize > UINT64_MAX - sg->fileoff) goto fail;
        } else if ((lc->cmd == LC_FUNCTION_STARTS || lc->cmd == LC_DATA_IN_CODE) && lc->cmdsize < sizeof(struct linkedit_data_command))
            goto fail;
        else if (lc->cmd == LC_UUID && lc->cmdsize < sizeof(struct uuid_command)) goto fail;
        off += lc->cmdsize;
    }
    if (off != hdr->sizeofcmds) goto fail;
    return data;
fail:
    free(data); return 0;
}

/* MH_EXECUTE, rather than dyld image zero: inserted dylibs can occupy zero
 * while their constructors run on newer dyld versions. */
static const struct mach_header_64 *select_image(int self, intptr_t *slide,
                                                const char **path) {
    uintptr_t me = (uintptr_t)&select_image;
    for (uint32_t i = 0; i < _dyld_image_count(); i++) {
        const struct mach_header_64 *mh = (const void *)_dyld_get_image_header(i);
        struct mach_header_64 hdr;
        if (!mh) continue;
        uint8_t *data = commands(mh, &hdr);
        if (!data) continue;
        intptr_t bias = _dyld_get_image_vmaddr_slide(i);
        int found = !self && hdr.filetype == MH_EXECUTE;
        size_t off = 0;
        for (uint32_t j = 0; self && j < hdr.ncmds; j++) {
            const struct load_command *lc = (const void *)(data + off);
            if (lc->cmd == LC_SEGMENT_64) {
                const struct segment_command_64 *sg = (const void *)lc;
                uintptr_t base;
                if (loaded_address(sg->vmaddr, bias, &base) &&
                    sg->vmsize <= UINTPTR_MAX - base && me >= base && me - base < sg->vmsize)
                    found = 1;
            }
            off += lc->cmdsize;
        }
        free(data);
        if (found) {
            if (slide) *slide = bias;
            if (path) *path = _dyld_get_image_name(i);
            return mh;
        }
    }
    return 0;
}

const struct mach_header_64 *avxemu_main_image(intptr_t *slide, const char **name) {
    if (!avxemu_image_init()) return 0;
    return select_image(0, slide, name);
}

const struct mach_header_64 *avxemu_darwin_self_image(void) {
    return select_image(1, 0, 0);
}

int avxemu_darwin_image_uuid(int self, uint8_t uuid[16], const char **path) {
    const struct mach_header_64 *mh = select_image(self, 0, path);
    struct mach_header_64 hdr;
    if (!mh) return 0;
    uint8_t *data = commands(mh, &hdr);
    if (!data) return 0;
    size_t off = 0; int found = 0;
    for (uint32_t i = 0; i < hdr.ncmds; i++) {
        const struct load_command *lc = (const void *)(data + off);
        if (lc->cmd == LC_UUID) {
            if (found) { free(data); return 0; }
            memcpy(uuid, ((const struct uuid_command *)lc)->uuid, 16);
            found = 1;
        }
        off += lc->cmdsize;
    }
    free(data);
    return found;
}

static int uleb(const uint8_t **p, const uint8_t *end, uint64_t *value) {
    uint64_t v = 0;
    for (unsigned shift = 0; shift < 64; shift += 7) {
        if (*p == end) return 0;
        uint8_t b = *(*p)++;
        if (shift == 63 && (b & 0x7e)) return 0;
        v |= (uint64_t)(b & 0x7f) << shift;
        if (!(b & 0x80)) { *value = v; return 1; }
    }
    return 0;
}

/* Count, then fill the same validated stream. Starts outside __text delimit
 * its last function but never become patchable ranges (raw/test sections). */
static int functions(const uint8_t *data, size_t size, uint64_t textseg,
                     uint64_t segsize, const avxemu_code_range *r,
                     avxemu_function *out, size_t *count) {
    const uint8_t *p = data, *end = data + size;
    uint64_t addr = textseg, prev = 0; size_t n = 0; int terminated = 0;
    while (p < end) {
        uint64_t delta;
        if (!uleb(&p, end, &delta)) return 0;
        if (!delta) { terminated = 1; break; }
        if (delta > UINT64_MAX - addr) return 0;
        addr += delta;
        if (addr - textseg >= segsize) return 0;
        if (prev >= r->vaddr && prev - r->vaddr < r->size) {
            size_t begin = (size_t)(prev - r->vaddr);
            size_t finish = addr - r->vaddr < r->size ? (size_t)(addr - r->vaddr) : r->size;
            if (n == UINT32_MAX) return 0;
            if (out) out[n] = (avxemu_function){0, begin, finish};
            n++;
        }
        prev = addr;
    }
    if (!terminated) return 0;
    while (p < end) if (*p++) return 0;
    if (prev >= r->vaddr && prev - r->vaddr < r->size) {
        if (n == UINT32_MAX) return 0;
        if (out) out[n] = (avxemu_function){0, (size_t)(prev - r->vaddr), r->size};
        n++;
    }
    *count = n; return 1;
}

int avxemu_darwin_data_spans(const void *records, size_t size, uint64_t file_begin,
                            size_t text_size, avxemu_data_span *out, size_t *count) {
    if(!count || (size && !records) || size%sizeof(struct data_in_code_entry)) return 0;
    size_t n=size/sizeof(struct data_in_code_entry);
    if(n>UINT32_MAX) return 0;
    uint64_t previous=file_begin;
    for(size_t i=0;i<n;i++) {
        struct data_in_code_entry d; memcpy(&d,(const uint8_t *)records+i*sizeof d,sizeof d);
        if(!d.length || d.kind<1 || d.kind>5 || d.offset<file_begin ||
            (uint64_t)d.offset-file_begin>text_size ||
            d.length>text_size-((uint64_t)d.offset-file_begin) || d.offset<previous ||
            (d.kind==3 && d.length%2) || (d.kind>=4 && d.length%4)) return 0;
        previous=(uint64_t)d.offset+d.length;
    }
    if(out) for(size_t i=0;i<n;i++) {
        struct data_in_code_entry d; memcpy(&d,(const uint8_t *)records+i*sizeof d,sizeof d);
        size_t begin=(size_t)((uint64_t)d.offset-file_begin);
        out[i]=(avxemu_data_span){0,begin,begin+d.length,d.kind==4?AVXEMU_DATA_JUMP32:AVXEMU_DATA_OPAQUE};
    }
    *count=n; return 1;
}
static uint8_t *linkedit_bytes(const struct linkedit_data_command *cmd,
                              const struct segment_command_64 *segment, intptr_t slide) {
    if(!segment || cmd->dataoff<segment->fileoff ||
        cmd->dataoff-segment->fileoff>segment->filesize ||
        cmd->datasize>segment->filesize-(cmd->dataoff-segment->fileoff) ||
        cmd->dataoff-segment->fileoff>segment->vmsize ||
        cmd->datasize>segment->vmsize-(cmd->dataoff-segment->fileoff)) return 0;
    uintptr_t base;
    size_t offset=(size_t)(cmd->dataoff-segment->fileoff);
    if(!loaded_address(segment->vmaddr,slide,&base) || offset>UINTPTR_MAX-base ||
        !readable_span(base+offset,cmd->datasize)) return 0;
    uint8_t *bytes=malloc(cmd->datasize?cmd->datasize:1);
    if(bytes && avxemu_memory_read(bytes,base+offset,cmd->datasize)==cmd->datasize) return bytes;
    free(bytes); return 0;
}

int avxemu_image_init(void) {
    if (g_ready) return 1;
    intptr_t slide; const char *path;
    const struct mach_header_64 *mh = select_image(0, &slide, &path);
    if (!mh) return 0;
    struct mach_header_64 hdr;
    uint8_t *data = commands(mh, &hdr), *starts = 0, *data_records = 0;
    void *storage = 0;
    if (!data) return 0;
    const struct segment_command_64 *textseg = 0, *linkedit = 0;
    const struct section_64 *text = 0;
    const struct linkedit_data_command *fs = 0, *dc = 0;
    size_t off = 0;
    for (uint32_t i = 0; i < hdr.ncmds; i++) {
        const struct load_command *lc = (const void *)(data + off);
        if (lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *sg = (const void *)lc;
            if (!strncmp(sg->segname, "__TEXT", 16)) {
                if (textseg) goto fail;
                textseg = sg;
                const struct section_64 *sc = (const void *)(sg + 1);
                for (uint32_t j = 0; j < sg->nsects; j++) {
                    if (!strncmp(sc[j].sectname, "__text", 16) && !strncmp(sc[j].segname, "__TEXT", 16)) {
                        if (text) goto fail;
                        text = &sc[j];
                    }
                }
            } else if (!strncmp(sg->segname, "__LINKEDIT", 16)) {
                if (linkedit) goto fail;
                linkedit = sg;
            }
        } else if (lc->cmd == LC_FUNCTION_STARTS) {
            if (fs) goto fail;
            fs = (const void *)lc;
        } else if(lc->cmd==LC_DATA_IN_CODE) {
            if(dc) goto fail;
            dc=(const void *)lc;
        }
        off += lc->cmdsize;
    }
    if (!textseg || !text || !text->size || text->addr < textseg->vmaddr ||
        text->addr - textseg->vmaddr > textseg->vmsize ||
        text->size > textseg->vmsize - (text->addr - textseg->vmaddr) ||
        (textseg->initprot & (AVXEMU_PROT_R | AVXEMU_PROT_X)) != (AVXEMU_PROT_R | AVXEMU_PROT_X)) goto fail;
    uintptr_t textbase;
    if (!loaded_address(text->addr, slide, &textbase)) goto fail;
    avxemu_code_range range = {
        (uint8_t *)textbase, text->addr,
        (size_t)text->size, (size_t)(textseg->vmsize - (text->addr - textseg->vmaddr)),
        (unsigned)textseg->initprot & (AVXEMU_PROT_R | AVXEMU_PROT_W | AVXEMU_PROT_X)
    };
    if (!readable_span((uintptr_t)range.base, range.readable)) goto fail;
    if(text->offset<textseg->fileoff || text->offset-textseg->fileoff>textseg->filesize ||
        text->size>textseg->filesize-(text->offset-textseg->fileoff) ||
        text->offset-textseg->fileoff!=text->addr-textseg->vmaddr) goto fail;
    size_t nfunctions = 0, ndata = 0;
    if (fs && fs->datasize) {
        starts=linkedit_bytes(fs,linkedit,slide);
        if(!starts || !functions(starts,fs->datasize,textseg->vmaddr,textseg->vmsize,&range,0,&nfunctions)) goto fail;
    }
    if(dc && dc->datasize) {
        data_records=linkedit_bytes(dc,linkedit,slide);
        if(!data_records || !avxemu_darwin_data_spans(data_records,dc->datasize,text->offset,
            range.size,0,&ndata)) goto fail;
    }
    if (nfunctions > (SIZE_MAX - sizeof range) / sizeof(avxemu_function)) goto fail;
    size_t storage_size=sizeof range+nfunctions*sizeof(avxemu_function);
    if(ndata>(SIZE_MAX-storage_size)/sizeof(avxemu_data_span)) goto fail;
    storage = malloc(storage_size+ndata*sizeof(avxemu_data_span));
    if (!storage) goto fail;
    memcpy(storage, &range, sizeof range);
    avxemu_function *funcs = (void *)((uint8_t *)storage + sizeof range);
    if (starts && !functions(starts, fs->datasize, textseg->vmaddr, textseg->vmsize, &range, funcs, &nfunctions)) goto fail;
    avxemu_data_span *spans=(void *)((uint8_t *)storage+storage_size);
    if(data_records && !avxemu_darwin_data_spans(data_records,dc->datasize,text->offset,
        range.size,spans,&ndata)) goto fail;
    avxemu_image candidate={storage,1,funcs,nfunctions,path,spans,ndata,AVXEMU_SCAN_LEGACY};
    if(!avxemu_image_data_valid(&candidate)) goto fail;
    g_image=candidate;
    g_ready = 1;
    free(data_records); free(starts); free(data); return 1;
fail:
    free(storage); free(data_records); free(starts); free(data); return 0;
}

const avxemu_image *avxemu_image_main(void) {
    return g_ready ? &g_image : 0;
}
