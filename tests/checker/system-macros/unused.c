// #3338: a discarded value written in a macro's body, or in a system header's macro, is quiet (DiagnoseUnusedExprResult's
// ShouldSuppress), as a comparison a macro's body writes is (DiagnoseUnusedComparison); a macro's argument still warns.
# 1 "system-unused.h" 3
#define SYSTEM_COMPARE(a) ((a) == 1)
#define SYSTEM_BARE(a) (a) == 1
#define SYSTEM_ADD(a) ((a) + 1)
#define SYSTEM_VALUE(a) (a)
#define SYSTEM_NOT(a) (!(a) == 1)
#define SYSTEM_COMMA(a) ((a), 0)
# 9 "unused.c"
#define USER_COMPARE(a) ((a) == 1)
#define USER_BARE(a) (a) == 1
#define USER_ADD(a) ((a) + 1)
#define USER_VALUE(a) (a)
void discard(int i, int *p)
{
    SYSTEM_COMPARE(i);
    SYSTEM_BARE(i);
    SYSTEM_ADD(i);
    SYSTEM_VALUE(i + 2);
    SYSTEM_VALUE(i == 2);
    SYSTEM_NOT(i);
    SYSTEM_COMMA(i);
    USER_COMPARE(i);
    USER_BARE(i);
    USER_ADD(i);
    USER_VALUE(i + 3);
    USER_VALUE(i == 3);
    i == 4;
    i + 5;
    (i, *p);
    (void)i;
}
