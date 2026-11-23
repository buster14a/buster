// Moves __COUNTER__, and leaves a diagnostic pragma's push open with a default warning ignored.
#include "probe.h"
int counters_a = __COUNTER__;
int counters_b = __COUNTER__ + __COUNTER__;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wshift-count-overflow"
int counters_quiet = 1 << 41;
#pragma GCC diagnostic push
#pragma GCC diagnostic error "-Wshift-count-overflow"
