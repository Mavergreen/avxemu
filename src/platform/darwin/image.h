/* Private Mach-O adapter for loader integration and legacy scanner consumers. */
#ifndef AVXEMU_DARWIN_IMAGE_H
#define AVXEMU_DARWIN_IMAGE_H
#include <stdint.h>
#include "../../image.h"
#include <mach-o/loader.h>
const struct mach_header_64 *avxemu_main_image(intptr_t *slide, const char **name);
const struct mach_header_64 *avxemu_darwin_self_image(void);
int avxemu_darwin_image_uuid(int self, uint8_t uuid[16], const char **path);
/* Bounded LC_DATA_IN_CODE normalization, also used by metadata contracts. */
int avxemu_darwin_data_spans(const void *records, size_t size, uint64_t file_begin,
                            size_t text_size, avxemu_data_span *out, size_t *count);
#endif
