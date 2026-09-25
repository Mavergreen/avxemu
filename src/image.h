/*
 * image.h — which loaded image is the program avxemu is patching.
 */
#ifndef AVXEMU_IMAGE_H
#define AVXEMU_IMAGE_H
#include <stdint.h>
#include <mach-o/loader.h>

/* The main executable (MH_EXECUTE), its slide, and its path (either may be
 * NULL). Not dyld image 0: on dyld newer than 10.9's, while an INSERTED
 * library's initializers run, image 0 is that library -- avxemu would patch
 * itself and leave the program untouched. NULL if there is none. (handler.c) */
const struct mach_header_64 *avxemu_main_image(intptr_t *slide, const char **name);
#endif
