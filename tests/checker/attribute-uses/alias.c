#pragma clang diagnostic warning "-Wunused-function"
#pragma clang diagnostic warning "-Wunused-variable"
#pragma clang diagnostic warning "-Wunused-const-variable"
static void target(void) { }
void named(void) __attribute__((alias("target")));
// Declared before the alias and defined after it: the whole entity is used.
static void declared_target(void);
void named_declared(void) __attribute__((alias("declared_target")));
static void declared_target(void) { }
// A target declared after the alias isn't found where the attribute is: it is unused.
void named_late(void) __attribute__((alias("target_late")));
static void target_late(void) { }
static int variable;
extern int variable_alias __attribute__((alias("variable")));
static const int constant = 1;
extern const int constant_alias __attribute__((alias("constant")));
// An asm label's symbol is another spelling than the declaration's name: the function isn't named.
static void labelled(void) __asm__("labelled_symbol");
static void labelled(void) { }
void by_label(void) __attribute__((alias("labelled_symbol")));
// weakref and #pragma weak name their targets without marking them used.
static void weakref_target(void) { }
static void reference(void) __attribute__((weakref("weakref_target")));
static void pragma_target(void) { }
#pragma weak pragma_alias = pragma_target
