// #3338: a one-bit bit-field takes a system header's `true` without -Wsingle-bit-bitfield-constant-conversion
// (AnalyzeBitFieldAssignment's isInSystemMacro and findMacroSpelling), assigned or initialized; a literal 1, a user's
// `TRUE` and a system header's other macro of 1 warn, and a wider bit-field's truncation warns at the expansion.
# 1 "system-bool.h" 3
#define true 1
#define SYSTEM_ONE 1
#define SYSTEM_SEVEN 7
# 8 "bitfields.c"
#define TRUE 1
struct Flags
{
    int bit : 1;
    int pair : 2;
};
void flag(struct Flags *flags)
{
    flags->bit = true;
    flags->bit = 1;
    flags->bit = TRUE;
    flags->bit = SYSTEM_ONE;
    flags->pair = SYSTEM_SEVEN;
    struct Flags local = { true, SYSTEM_SEVEN };
    struct Flags other = { SYSTEM_ONE, 1 };
    (void)local;
    (void)other;
}
