#define A 2
#if A == 1
one
#elif A == 2
#ifdef A
two
#else
not
#endif
#elif 1/0
three
#else
four
#endif
#ifndef B
b
#elifdef A
no
#endif
