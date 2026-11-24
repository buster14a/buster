#define NEG (-1)
#define GT >
#define COMPARE(a,b) ((a) > (b))
#if NEG GT 0u
macro_left
#endif
#if COMPARE(0u, -2)
macro_right
#endif
#if 0
#if -3 > 0u
skipped
#endif
#elif 0u < -4
elif_taken
#elif -5 > 0u
skipped_elif
#endif
