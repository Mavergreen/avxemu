#define _GNU_SOURCE
#include "maps.h"
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

/* Retain only the bounded address/permissions/identity prefix, never a path.
 * A failed or truncated read cannot establish a gap. */

int avxemu_maps_open(maps_reader *reader) {
    *reader = (maps_reader){.fd = open("/proc/self/maps", O_RDONLY | O_CLOEXEC)};
    return reader->fd >= 0;
}

static int hex_address(const char **cursor, uintptr_t *value) {
    const char *p = *cursor;
    uintptr_t result = 0;
    unsigned digits = 0;
    for (;;) {
        unsigned digit;
        if (*p >= '0' && *p <= '9') digit = (unsigned)(*p - '0');
        else if (*p >= 'a' && *p <= 'f') digit = (unsigned)(*p - 'a' + 10);
        else break;
        if (++digits > sizeof(uintptr_t) * 2 || result > (UINTPTR_MAX - digit) / 16) return 0;
        result = result * 16 + digit;
        p++;
    }
    if (!digits) return 0;
    *cursor = p;
    *value = result;
    return 1;
}

int avxemu_maps_next(maps_reader *reader, avxemu_mapping *mapping, avxemu_map_file *file) {
    char prefix[160] = {0};
    size_t length = 0;
    int any = 0;
    for (;;) {
        if (reader->pos == reader->count) {
            ssize_t n;
            do { n = read(reader->fd, reader->bytes, sizeof reader->bytes); } while (n < 0 && errno == EINTR);
            if (n < 0 || (n == 0 && any)) return -1;
            /* A live process always has mappings. Empty procfs data is
             * unknown, rather than evidence that its whole space is free. */
            if (!n) return reader->previous_end ? 0 : -1;
            if ((size_t)n > 64 * 1024 * 1024 - reader->total) return -1;
            reader->total += (size_t)n;
            reader->pos = 0;
            reader->count = (size_t)n;
        }
        char c = reader->bytes[reader->pos++];
        any = 1;
        if (c == '\n') break;
        if (length < sizeof prefix - 1) prefix[length++] = c;
    }
    const char *p = prefix;
    avxemu_mapping result = {0};
    if (!hex_address(&p, &result.begin) || *p++ != '-' ||
        !hex_address(&p, &result.end) || *p++ != ' ' ||
        result.begin >= result.end || result.begin < reader->previous_end) return -1;
    if (p[0] != 'r' && p[0] != '-') return -1;
    if (p[1] != 'w' && p[1] != '-') return -1;
    if (p[2] != 'x' && p[2] != '-') return -1;
    if ((p[3] != 'p' && p[3] != 's') || p[4] != ' ') return -1;
    if (p[0] == 'r') result.prot |= AVXEMU_PROT_R;
    if (p[1] == 'w') result.prot |= AVXEMU_PROT_W;
    if (p[2] == 'x') result.prot |= AVXEMU_PROT_X;
    avxemu_map_file identity;
    p += 5;
    if (!hex_address(&p, &identity.offset) || *p++ != ' ' ||
        !hex_address(&p, &identity.major) || *p++ != ':' ||
        !hex_address(&p, &identity.minor) || *p++ != ' ') return -1;
    identity.ino = 0;
    unsigned digits = 0;
    while (*p >= '0' && *p <= '9') {
        unsigned digit = (unsigned)(*p++ - '0');
        if (++digits > 20 || identity.ino > (UINT64_MAX - digit) / 10) return -1;
        identity.ino = identity.ino * 10 + digit;
    }
    if (!digits || (*p && *p != ' ')) return -1;
    if (file) *file = identity;
    reader->previous_end = result.end;
    *mapping = result;
    return 1;
}
