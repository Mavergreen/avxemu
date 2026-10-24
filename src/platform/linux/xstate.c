#include "xstate.h"
#include "platform/memory.h"
#include <asm/sigcontext.h>
#include <string.h>

/* Linux UAPI asm/sigcontext.h specifies the standard XSAVE layout. Never
 * interpret compacted components at standard offsets. Later components remain
 * opaque: only the legacy XMM area, header and fixed YMMH component are used. */
static int metadata(const void *bytes, size_t size, struct _fpx_sw_bytes *sw,
                    uint64_t *active) {
    struct _header header;
    if (!bytes || size < sizeof(struct _xstate)) return 0;
    memcpy(sw, (const uint8_t *)bytes + offsetof(struct _fpstate_64, sw_reserved), sizeof *sw);
    memcpy(&header, (const uint8_t *)bytes + offsetof(struct _xstate, xstate_hdr), sizeof header);
    if (sw->magic1 != FP_XSTATE_MAGIC1 || (sw->xfeatures & 7) != 7 ||
        sw->xstate_size < sizeof(struct _xstate) ||
        sw->xstate_size > UINT32_MAX - FP_XSTATE_MAGIC2_SIZE ||
        sw->extended_size != sw->xstate_size + FP_XSTATE_MAGIC2_SIZE ||
        (header.xfeatures & ~sw->xfeatures)) return 0;
    /* The first reserved word is XCOMP_BV on CPUs with XSAVEC/XSAVES. Linux
     * signal frames use the standard format, whose complete reserved header
     * must be zero for XRSTOR. Software-reserved legacy bytes are preserved. */
    for (size_t i=0; i<2; i++) if (header.reserved1[i]) return 0;
    for (size_t i=0; i<5; i++) if (header.reserved2[i]) return 0;
    *active=header.xfeatures;
    return 1;
}

int avxemu_xstate_view(void *bytes, size_t size, avxemu_xstate *view) {
    struct _fpx_sw_bytes sw;
    uint64_t active;
    uint32_t trailer;
    if (!view || !metadata(bytes,size,&sw,&active) || sw.extended_size > size) return 0;
    memcpy(&trailer,(uint8_t *)bytes+sw.xstate_size,sizeof trailer);
    if (trailer != FP_XSTATE_MAGIC2) return 0;
    avxemu_xstate result = {
        (uint8_t *)bytes + offsetof(struct _fpstate_64, xmm_space),
        (uint8_t *)bytes + offsetof(struct _xstate, ymmh),
        (uint8_t *)bytes + offsetof(struct _xstate, xstate_hdr), active
    };
    *view=result;
    return 1;
}

int avxemu_xstate_live(void *bytes, avxemu_xstate *view) {
    uint8_t prefix[sizeof(struct _xstate)], scratch[1024];
    struct _fpx_sw_bytes sw;
    uint64_t active;
    uintptr_t address=(uintptr_t)bytes;
    if (!view || avxemu_memory_read(prefix,address,sizeof prefix)!=sizeof prefix ||
        !metadata(prefix,sizeof prefix,&sw,&active) || sw.extended_size > UINTPTR_MAX-address) return 0;
    /* A claimed size does not establish readable storage. Check the entire
     * claimed extent through the fault-tolerant reader before dereferencing
     * the kernel-created frame. Storage and reads remain bounded. */
    for (size_t offset=sizeof prefix; offset<sw.extended_size;) {
        size_t count=sw.extended_size-offset;
        if (count>sizeof scratch) count=sizeof scratch;
        if (avxemu_memory_read(scratch,address+offset,count)!=count) return 0;
        offset+=count;
    }
    return avxemu_xstate_view(bytes,sw.extended_size,view);
}
