#ifndef AVXEMU_LDE_H
#define AVXEMU_LDE_H

#include <stdint.h>
#include <stddef.h>

/*
 * Minimal x86-64 length disassembler — just enough to walk instruction
 * boundaries and locate lzcnt/tzcnt. We need lengths (not semantics) so we can
 * find `F3 [REX] 0F BD/BC` precisely and never mistake a mid-instruction or
 * data byte for one. Validated against otool over the whole binary.
 */

typedef int (*lde_trap_lookup)(uintptr_t address);

enum { LDE_LZCNT = 1, LDE_TZCNT = 2, LDE_CPUID = 4 };
/* Discover selected operations without editing. Offsets are relative to text.
 * Return -1 if storage is insufficient or discovery cannot be completed; no
 * caller may edit any sites in that case. recursive skips unreachable bytes.
 * traps optionally identifies owned UD2 sites which resume by falling through. */
long lde_scan_sites(const uint8_t *text, size_t begin, size_t end,
                    size_t readable, unsigned required, int recursive,
                    size_t *out, size_t capacity, lde_trap_lookup traps);
/* Shared masked collector for legacy function scans and partial range mop-up.
 * excluded/selected are indexed by text offsets. On failure selected may be
 * partial; callers discard a failed function attempt before recursive fallback. */
long lde_collect_zcnt(const uint8_t *text, size_t begin, size_t end,
    size_t readable, unsigned required, int recursive, const uint8_t *excluded,
    uint8_t *selected, int partial, lde_trap_lookup traps);
long lde_scan_zcnt_mask(uint8_t *text, size_t n, int patch, unsigned required);
long lde_scan_func_mask(uint8_t *text, size_t begin, size_t end, size_t readable,
                        int patch, unsigned required, int recursive,
                        size_t *out, int capacity, lde_trap_lookup traps);

/* Decode one instruction at p (bounded by end). Returns its length, or 0 on a
 * decode error / truncation. *zk = 1 for lzcnt, 2 for tzcnt, 0 otherwise;
 * *pfx_off = byte offset (within the instruction) of the F3 prefix to patch. */
int x86_len(const uint8_t *p, const uint8_t *end, int *zk, int *pfx_off);

/* Walk [text, text+n) instruction by instruction. For each lzcnt/tzcnt, if
 * patch != 0, rewrite its F3 prefix to F0 (lock) so it faults (#UD). Returns
 * the number of lzcnt/tzcnt seen, or -1 on a decode desync. */
long lde_scan_zcnt(uint8_t *text, size_t n, int patch);

/* Recursive-descent scan of one function [fstart, fend) (offsets into text):
 * follows control flow so embedded jump-table data is never decoded as code.
 * Records the offsets of lzcnt/tzcnt in reachable code into out[] (up to
 * maxout). Returns the count, or -1 if capacity is insufficient. Used for
 * functions that don't decode cleanly linearly. */
long lde_rd_zcnt(const uint8_t *text, size_t fstart, size_t fend, size_t readable,
                 size_t *out, int maxout);

/*
 * Scan one function [fstart, fend) for lzcnt/tzcnt. If the function decodes
 * cleanly to its boundary, uses fast linear scan; otherwise falls back to
 * recursive descent (skips embedded jump-table data, resolves switch tables).
 * Records instruction-start offsets in out[]. If patch != 0, also rewrites each
 * one's F3 prefix to F0 (lock) so it faults. `readable` bounds table reads.
 */
long lde_scan_func(uint8_t *text, size_t fstart, size_t fend, size_t readable,
                   int patch, size_t *out, int maxout);

/* Classify the instruction at p for the trampoline scanner (see lde.c). */
void lde_cflow(const uint8_t *p, const uint8_t *end, int len, long insn_off,
               int *term, long *tgt, int *indirect);

/*
 * Recursive-descent reachability map of one function [fstart, fend) for the
 * trampoline scanner. Follows control flow (and resolves switch tables) so
 * embedded jump-table data is never treated as code. Marks every reachable
 * instruction-start offset (relative to fstart) in code[] and every direct or
 * resolved-switch branch-target offset in tgt[] (both caller-allocated, length
 * fend-fstart). Sets *has_indirect if an indirect jmp had no resolvable table,
 * so unknown targets may remain and the caller must stay conservative.
 * `readable` bounds table reads. Returns 1, or 0 on allocation/decode failure.
 */
/* Owned nonterminating UD2 traps are distinguished from genuine UD2. The
 * callback is read-only and optional; the legacy map wrapper passes NULL. */
/* Required-operation proof: follow only direct edges/call fallthrough,
 * never heuristic switch entries. has_indirect covers every indirect CALL/JMP;
 * callers must prove candidate coverage before treating unmapped bytes as safe.
 * Optional data[fend-fstart] marks authoritative non-code bytes. Fallthrough
 * stops there; an entry, decoded instruction or direct target into data fails. */
int lde_rd_map_direct(const uint8_t *text, size_t fstart, size_t fend, size_t readable,
                      uint8_t *code, uint8_t *tgt, int *has_indirect,
                      lde_trap_lookup traps, const uint8_t *data);
/* Additional independently established direct-entry roots, function-relative. */
int lde_rd_map_direct_roots(const uint8_t *text, size_t fstart, size_t fend, size_t readable,
    uint8_t *code, uint8_t *tgt, int *has_indirect, lde_trap_lookup traps,
    const uint8_t *data, const uint8_t *entries);
int lde_rd_map_traps(const uint8_t *text, size_t fstart, size_t fend, size_t readable,
                     uint8_t *code, uint8_t *tgt, int *has_indirect,
                     lde_trap_lookup traps);
int lde_rd_map(const uint8_t *text, size_t fstart, size_t fend, size_t readable,
               uint8_t *code, uint8_t *tgt, int *has_indirect);

#endif /* AVXEMU_LDE_H */
