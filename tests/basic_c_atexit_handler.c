// glibc splits its C library in two: libc.so.6 exports the shared symbols and
// libc_nonshared.a a handful of stubs every program links statically.
// `atexit` is one of those stubs -- the shared object exports only
// `__cxa_atexit` -- so a program that registers an exit handler links against
// the ELF writers here and then dies in the loader with an undefined symbol.
// SQLite's shell registers one on the first line of main.
extern int atexit(void (*handler)(void));
extern int at_quick_exit(void (*handler)(void));

static int ran;

static void first(void) { ran += 1; }
static void second(void) { ran += 2; }
static void quick(void) { ran += 4; }

int main(void)
{
    if (atexit(first) != 0) return 1;
    if (atexit(second) != 0) return 2;
    if (at_quick_exit(quick) != 0) return 3;
    if (ran != 0) return 4;
    return 0;
}
