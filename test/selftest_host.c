/*
 * selftest_host.c -- a program of our own for the selftest ctest to insert avxemu into. Not
 * /usr/bin/true: SIP strips DYLD_INSERT_LIBRARIES from system binaries on macOS newer than
 * 10.9, so the self-test would silently never load there.
 */
int main(void) { return 0; }
