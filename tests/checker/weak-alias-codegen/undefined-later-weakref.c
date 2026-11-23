static int target(void);
#pragma weak alias = target
static int target(void) __attribute__((weakref("missing")));
