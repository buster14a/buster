// #3338: a literal's warnings at a system header's macro expanded in user code are given at the expansion (an unknown
// escape, a multi-character constant, an unknown attribute); a format call's extra argument a system header's macro begins
// is quiet (CheckFormatArguments' isInSystemMacro), and one a user macro gives warns.
# 1 "system-literals.h" 3
#define SYSTEM_ESCAPE '\q'
#define SYSTEM_STRING "\q"
#define SYSTEM_PAIR 'ab'
#define SYSTEM_FOUR 'abcd'
#define SYSTEM_ATTRIBUTE __attribute__((unknown_system_attribute))
#define SYSTEM_TWO 1, 2
#define SYSTEM_FORMAT "%d %d\n"
# 12 "literals.c"
#pragma clang diagnostic warning "-Wfour-char-constants"
#define USER_TWO 1, 2
int printf(const char *, ...);
int literal(void) SYSTEM_ATTRIBUTE;
int literal(void)
{
    int i = SYSTEM_ESCAPE;
    i += SYSTEM_PAIR;
    i += SYSTEM_FOUR;
    const char *s = SYSTEM_STRING;
    printf("%d\n", SYSTEM_TWO);
    printf("%d\n", USER_TWO);
    printf(SYSTEM_FORMAT, 1);
    return i + *s;
}
