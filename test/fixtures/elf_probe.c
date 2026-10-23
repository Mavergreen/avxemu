#include <stdio.h>
#include <stdint.h>
extern int elf_asm_probe(int), elf_other_probe(int);
extern char elf_asm_end[], elf_data_island[], elf_raw_probe[], elf_test_probe[];
__attribute__((noinline)) int elf_c_probe(int x) { return x * 3 + 2; }
int main(void) {
    if (elf_asm_probe(1) != 8 || elf_other_probe(1) != 10 || elf_c_probe(1) != 5) return 1;
    printf("%llx %llx %llx %llx %llx\n", (unsigned long long)(uintptr_t)elf_asm_probe,
           (unsigned long long)(uintptr_t)elf_asm_end, (unsigned long long)(uintptr_t)elf_data_island,
           (unsigned long long)(uintptr_t)elf_raw_probe, (unsigned long long)(uintptr_t)elf_test_probe);
    return 0;
}
