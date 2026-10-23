#ifndef AVXEMU_LINUX_ELF_H
#define AVXEMU_LINUX_ELF_H
#include <stddef.h>
#include <stdint.h>
#include <elf.h>

typedef struct { const uint8_t *data; size_t size; uint64_t vaddr; } avxemu_elf_view;
typedef int (*avxemu_elf_function_sink)(uint64_t begin, uint64_t end, void *arg);
/* Malformed input never reaches the sink. A sink failure stops publication. */
int avxemu_elf_functions(const avxemu_elf_view *file,
                         avxemu_elf_function_sink sink, void *arg);

typedef struct { uint64_t begin, end, offset; unsigned prot; } avxemu_elf_range;
typedef struct { uint64_t begin, end; } avxemu_elf_bound;
typedef struct {
    Elf64_Ehdr header;
    Elf64_Phdr *loads;
    size_t nloads;
    avxemu_elf_range *ranges;
    size_t nranges;
    avxemu_elf_bound *functions;
    size_t nfunctions;
    uint8_t build_id[32];
} avxemu_elf;
/* Owns allocated metadata only; never borrows the input view. */
int avxemu_elf_parse(const avxemu_elf_view *file, avxemu_elf *result);
void avxemu_elf_destroy(avxemu_elf *elf);
#endif
