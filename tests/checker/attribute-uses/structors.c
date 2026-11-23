#pragma clang diagnostic warning "-Wunused-function"
// A constructor's or destructor's definition is emitted, so it is never unused (clang's DeclMustBeEmitted).
__attribute__((constructor)) static void init(void) { }
__attribute__((destructor)) static void fini(void) { }
__attribute__((constructor(101))) static void init_priority(void) { }
__attribute__((destructor(102))) static void fini_priority(void) { }
static void declared_first(void) __attribute__((constructor));
static void declared_first(void) { }
// An attribute after the definition is ignored: the function is unused.
static void attribute_after(void) { }
static void attribute_after(void) __attribute__((destructor));
// A constructor that is never defined is unused.
static void never_defined(void) __attribute__((constructor));
static void plain(void) { }
