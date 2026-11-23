#pragma clang diagnostic warning "-Wunused-function"
#pragma clang diagnostic warning "-Wunused-variable"
static int implementation(void) { return 1; }
static int (*resolver(void))(void) { return implementation; }
int resolved(void) __attribute__((ifunc("resolver")));
// A resolver declared after the ifunc isn't found where the attribute is: it is unused.
int resolved_late(void) __attribute__((ifunc("resolver_late")));
static int (*resolver_late(void))(void) { return implementation; }
// ifunc only applies to a function; on a variable it is ignored, and its resolver stays unused.
static int (*resolver_ignored(void))(void) { return implementation; }
static int ignored_subject __attribute__((ifunc("resolver_ignored")));
