#ifndef AVXEMU_PLATFORM_CONTEXT_H
#define AVXEMU_PLATFORM_CONTEXT_H
#include "regfile.h"
uintptr_t avxemu_context_ip(const void *context);
void avxemu_context_set_ip(void *context, uintptr_t ip);
/* vectors=0 needs only integer state. Failures leave destinations untouched;
 * vector writes preserve x87, MXCSR and unrelated extended state. No allocation. */
int avxemu_context_read(const void *context, avxemu_regfile *rf, int vectors);
int avxemu_context_write(void *context, const avxemu_regfile *rf, int vectors);
#endif
