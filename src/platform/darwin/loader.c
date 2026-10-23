#define _XOPEN_SOURCE 700
#include "platform/loader.h"
#include "platform/darwin/image.h"
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <mach/mach.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>

/*
 * The genuine libc sigaction. dlsym is unusable: dyld applies our own
 * interposition to *every* symbol resolution (RTLD_NEXT and explicit handles
 * alike) and hands back avxemu_sigaction, which would recurse forever. The real
 * sigaction *code* is untouched, though — we find its address by reading
 * libsystem_c.dylib's own symbol table (n_value + slide), immune to interpose.
 */
typedef int (*sigaction_fn)(int, const struct sigaction *, struct sigaction *);

static void *real_sym(const char *img, const char *name){
    for (uint32_t i = 0; i < _dyld_image_count(); i++) {
        const char *nm = _dyld_get_image_name(i);
        if (!nm || !strstr(nm, img)) continue;
        const struct mach_header_64 *mh = (const struct mach_header_64 *)_dyld_get_image_header(i);
        if (!mh || mh->magic != MH_MAGIC_64) continue;
        intptr_t slide = _dyld_get_image_vmaddr_slide(i);
        uint64_t le_vm = 0, le_off = 0; uint32_t symoff = 0, nsyms = 0, stroff = 0;
        const struct load_command *lc = (const struct load_command *)(mh + 1);
        for (uint32_t c = 0; c < mh->ncmds; c++) {
            if (lc->cmd == LC_SEGMENT_64) {
                const struct segment_command_64 *sg = (const struct segment_command_64 *)lc;
                if (!strcmp(sg->segname, "__LINKEDIT")) { le_vm = sg->vmaddr; le_off = sg->fileoff; }
            } else if (lc->cmd == LC_SYMTAB) {
                const struct symtab_command *st = (const struct symtab_command *)lc;
                symoff = st->symoff; nsyms = st->nsyms; stroff = st->stroff;
            }
            lc = (const struct load_command *)((const char *)lc + lc->cmdsize);
        }
        if (!symoff || !le_vm) return 0;
        const struct nlist_64 *syms = (const struct nlist_64 *)(le_vm + slide + (symoff - le_off));
        const char *strs = (const char *)(le_vm + slide + (stroff - le_off));
        for (uint32_t s = 0; s < nsyms; s++) {
            if (syms[s].n_un.n_strx == 0 || syms[s].n_value == 0) continue;
            if (!strcmp(strs + syms[s].n_un.n_strx, name)) return (void *)(syms[s].n_value + slide);
        }
    }
    return 0;
}

static sigaction_fn genuine_sigaction;
int avxemu_loader_resolve(void) {
    if (__sync_val_compare_and_swap(&genuine_sigaction,0,0)) return 1;
    sigaction_fn resolved=(sigaction_fn)real_sym("libsystem_c.dylib", "_sigaction");
    if (!resolved) return 0;
    __sync_bool_compare_and_swap(&genuine_sigaction,0,resolved);
    return 1;
}
int avxemu_real_sigaction(int sig, const struct sigaction *act, struct sigaction *old) {
    if (!avxemu_loader_resolve()) { errno=ENOSYS; return -1; }
    sigaction_fn genuine=__sync_val_compare_and_swap(&genuine_sigaction,0,0);
    return genuine(sig,act,old);
}

__attribute__((used)) static const struct { const void *r, *o; }
  _interpose_sigaction __attribute__((section("__DATA,__interpose"))) =
    { (const void*)avxemu_sigaction, (const void*)sigaction },
  _interpose_signal __attribute__((section("__DATA,__interpose"))) =
    { (const void*)avxemu_signal, (const void*)signal };

/*
 * Fallback interposition, for when dyld won't do it for us.
 *
 * 10.9's dyld registers __DATA,__interpose ONLY for DYLD_INSERT_LIBRARIES
 * images. Baked into a binary as a plain LC_LOAD_DYLIB — the tidier way to
 * attach the emulator, since it then cannot leak into child processes — the
 * overrides above are silently ignored: the runtime's crash reporter takes
 * SIGILL and the first emulated instruction becomes an "illegal instruction"
 * crash report. (Observed exactly that with Claude Code: Bun's crash banner,
 * then a hang.)
 *
 * So produce dyld's end state by hand. For each image, walk the lazy and
 * non-lazy symbol pointer sections through the indirect symbol table and
 * repoint the sigaction/signal slots at ours. Idempotent, so it costs nothing
 * when dyld did interpose — those slots already hold our addresses and are
 * skipped. AVXEMU_NO_REBIND=1 opts out.
 *
 * Deliberately not touched: our own image, and the system libraries under
 * /usr/lib and /System. The runtime and whatever it bundles are what install
 * crash handlers; rewriting libsystem's own call sites buys nothing and is a
 * good way to find a reentrancy problem.
 */
/* Our own image's mach header, found by asking which loaded image contains one
 * of our own functions.
 *
 * Not _mh_dylib_header: the linker defines that only for MH_DYLIB output, so a
 * reference to it fails to link every test target that pulls handler.o into an
 * executable ("Undefined symbols: __mh_dylib_header"). Neither weak_import nor
 * weak rescues it -- both still need the symbol to exist somewhere. dladdr()
 * would be the obvious alternative, but it and Dl_info are Darwin extensions
 * that this file's strict _XOPEN_SOURCE hides.
 * The dyld image APIs are already in use here and are not hidden, so use those. */
static const struct mach_header_64 *avxemu_self_header(void) {
    static const struct mach_header_64 *self;
    static int done;
    if (!done) { self = avxemu_darwin_self_image(); done = 1; }
    return self;
}

static int rebind_sym_in_image(const struct mach_header_64 *mh, intptr_t slide,
                               const char *want, const void *repl) {
    const struct load_command *lc = (const struct load_command *)(mh + 1);
    const struct symtab_command *st = 0;
    const struct dysymtab_command *dy = 0;
    uintptr_t le_base = 0;

    for (uint32_t c = 0; c < mh->ncmds; c++) {
        if (lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *sg = (const struct segment_command_64 *)lc;
            if (!strcmp(sg->segname, "__LINKEDIT"))
                le_base = (uintptr_t)slide + (uintptr_t)sg->vmaddr - (uintptr_t)sg->fileoff;
        } else if (lc->cmd == LC_SYMTAB) {
            st = (const struct symtab_command *)lc;
        } else if (lc->cmd == LC_DYSYMTAB) {
            dy = (const struct dysymtab_command *)lc;
        }
        lc = (const struct load_command *)((const char *)lc + lc->cmdsize);
    }
    if (!st || !dy || !le_base || !dy->nindirectsyms) return 0;

    const struct nlist_64 *syms = (const struct nlist_64 *)(le_base + st->symoff);
    const char *strs = (const char *)(le_base + st->stroff);
    const uint32_t *indirect = (const uint32_t *)(le_base + dy->indirectsymoff);

    int n = 0;
    lc = (const struct load_command *)(mh + 1);
    for (uint32_t c = 0; c < mh->ncmds; c++) {
        if (lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *sg = (const struct segment_command_64 *)lc;
            const struct section_64 *sect = (const struct section_64 *)(sg + 1);
            for (uint32_t s = 0; s < sg->nsects; s++) {
                uint32_t type = sect[s].flags & SECTION_TYPE;
                if (type != S_LAZY_SYMBOL_POINTERS && type != S_NON_LAZY_SYMBOL_POINTERS)
                    continue;
                const void **slot = (const void **)((uintptr_t)slide + (uintptr_t)sect[s].addr);
                uint32_t nslots = (uint32_t)(sect[s].size / sizeof(void *));
                for (uint32_t k = 0; k < nslots; k++) {
                    uint32_t si = indirect[sect[s].reserved1 + k];
                    if (si >= dy->nindirectsyms + dy->indirectsymoff) continue;
                    if (si & (INDIRECT_SYMBOL_ABS | INDIRECT_SYMBOL_LOCAL)) continue;
                    if (si >= st->nsyms) continue;
                    const char *nm = strs + syms[si].n_un.n_strx;
                    if (strcmp(nm, want)) continue;
                    if (slot[k] == repl) continue;          /* dyld already did it */
                    uintptr_t page = (uintptr_t)&slot[k] & ~(uintptr_t)(PAGE_SIZE - 1);
                    if (mprotect((void *)page, PAGE_SIZE, PROT_READ | PROT_WRITE) != 0)
                        continue;
                    slot[k] = repl;
                    n++;
                }
            }
        }
        lc = (const struct load_command *)((const char *)lc + lc->cmdsize);
    }
    return n;
}

/* Pointers the rebind actually rewrote. Test hook (linkhook): with the rebind
 * off it must stay 0, which tells "dyld interposed us itself" (it does, for
 * linked images, on macOS newer than 10.9) apart from "the rebind ran anyway". */
int avxemu_rebind_count;

static void avxemu_rebind_image(const struct mach_header *mhp, intptr_t slide) {
    const struct mach_header_64 *mh = (const struct mach_header_64 *)mhp;
    if (!mh || mh->magic != MH_MAGIC_64) return;
    if (mh == avxemu_self_header()) return;                 /* never ourselves */

    for (uint32_t i = 0; i < _dyld_image_count(); i++) {
        if ((const struct mach_header_64 *)_dyld_get_image_header(i) != mh) continue;
        const char *nm = _dyld_get_image_name(i);
        if (nm && (!strncmp(nm, "/usr/lib/", 9) || !strncmp(nm, "/System/", 8))) return;
        break;
    }
    avxemu_rebind_count += rebind_sym_in_image(mh, slide, "_sigaction", (const void *)avxemu_sigaction);
    avxemu_rebind_count += rebind_sym_in_image(mh, slide, "_signal",    (const void *)avxemu_signal);
}


int avxemu_loader_init(void) {
    if (!avxemu_loader_resolve()) return 0;
    static int initialized;
    if (!initialized) {
        initialized=1;
        if (!getenv("AVXEMU_NO_REBIND")) _dyld_register_func_for_add_image(avxemu_rebind_image);
    }
    return 1;
}
