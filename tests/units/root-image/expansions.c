// Fills the stacks: nested invocations whose arguments are invocations, pastes and strings, a _Pragma, and an argument
// list that directives interrupt.
#include "probe.h"
#define E0(x) x
#define E1(x) E0(x) + E0(x)
#define E2(x) E1(x) + E1(E1(x))
#define E3(x) E2(E2(x)) + E2(x)
#define E4(x) E3(E3(x)) + E3(E2(x))
#define EXPANSIONS_NAME(a, b) a##b##_##a
#define EXPANSIONS_TEXT(...) #__VA_ARGS__
#define EXPANSIONS_PRAGMA(x) _Pragma(#x)
long expansions_value = E4(1);
long EXPANSIONS_NAME(expansions, value) = E3(E0(E0(2)));
const char* expansions_text = EXPANSIONS_TEXT(E4, (1), "a" L"b", 'c', ??=, <::>, a ## b);
EXPANSIONS_PRAGMA(pack(push, 2))
struct ExpansionsPacked
{
    char c;
    int i;
};
EXPANSIONS_PRAGMA(pack(pop))
long expansions_arguments = E1(
#define EXPANSIONS_INSIDE 5
    EXPANSIONS_INSIDE
#ifdef EXPANSIONS_INSIDE
    + 6
#endif
);
