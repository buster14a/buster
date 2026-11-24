// Read as many times as it is included: each time counts.
#ifndef REPEATED_COUNT
#define REPEATED_COUNT 1
#elif REPEATED_COUNT == 1
#undef REPEATED_COUNT
#define REPEATED_COUNT 2
#else
#undef REPEATED_COUNT
#define REPEATED_COUNT 3
#endif
