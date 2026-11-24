// A fatal error, the last unit in the command's order: a file that isn't there, included inside a conditional, after
// a macro of its own.
#include "probe.h"
#define MISSING_VALUE 1
#if MISSING_VALUE
int missing_before = MISSING_VALUE;
#include "not-there.h"
int missing_after;
#endif
