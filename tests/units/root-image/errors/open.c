// Ends with a conditional and an invocation open: an error, and stacks that aren't empty at its end.
#include "../probe.h"
#define OPEN_CALL(a, b) a + b
int open_before = 1;
#if 1
#if __STDC_HOSTED__
int open_inside = OPEN_CALL(1,
