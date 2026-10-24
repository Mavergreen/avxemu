#ifndef AVXEMU_LINUX_IMAGE_H
#define AVXEMU_LINUX_IMAGE_H
#include <stdint.h>
/* Initialization only. self selects the emulator rather than the executable.
 * Verified metadata owns the borrowed read-only fd.
 * Build IDs include format/domain and length in their normalization.
 * A missing build ID is all zeroes; failure leaves both outputs unchanged. */
int avxemu_linux_image_file(int self, int *fd, uint8_t build_id[32]);
#endif
