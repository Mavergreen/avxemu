#ifndef AVXEMU_LINUX_XSTATE_H
#define AVXEMU_LINUX_XSTATE_H

#include <stddef.h>
#include <stdint.h>

typedef struct { uint8_t *xmm, *ymmh, *header; uint64_t active; } avxemu_xstate;
int avxemu_xstate_view(void *bytes, size_t size, avxemu_xstate *view);
int avxemu_xstate_live(void *bytes, avxemu_xstate *view);

#endif
