// A constant carries only the bits its own type is wide, so a negative one
// that initializes a wider object has to replay its sign bit.  Every
// initializer below spells its value in a type narrower than the object it
// initializes; before the constant cast sign-extended, each stored the
// unsigned reading of the narrow bits -- `(long long)-1` became 4294967295 --
// while an explicitly `LL`-suffixed literal was already wide and stayed
// correct.

struct fields
{
    long long wide;
    unsigned long long unsigned_wide;
};

static const long long plain[] = {-1};
static const long long converted[] = {(long long)-1};
static const long long suffixed[] = {-1LL};
static long long writable[] = {-1};
static const long long scalar = -1;
static const unsigned long long unsigned_scalar = -1;
// The unsigned source is the control: converting it widens by zero extension,
// so this one must keep the value the sign-extending path would destroy.
static const long long from_unsigned = (unsigned)-1;
static const signed char narrowed = -1;
static const struct fields grouped = {-1, -1};
static const long long nested[2][2] = {{-1, 2}, {-3, 4}};
static const __int128 wide_pair = -1;

// Read the images through pointers stored in writable globals.  A direct read
// of a `const` object can be folded back into its initializer expression,
// which would check the constant evaluator twice and never look at the bytes
// the object writer emitted.
static const long long *observed_plain = plain;
static const long long *observed_scalar = &scalar;
static const struct fields *observed_grouped = &grouped;
static const __int128 *observed_wide_pair = &wide_pair;

int main(void)
{
    if (plain[0] != -1 || converted[0] != -1 || suffixed[0] != -1 || writable[0] != -1) return 1;
    if (scalar != -1 || unsigned_scalar != 0xffffffffffffffffull) return 2;
    if (from_unsigned != 4294967295LL || narrowed != -1) return 3;
    if (grouped.wide != -1 || grouped.unsigned_wide != 0xffffffffffffffffull) return 4;
    if (nested[0][0] != -1 || nested[0][1] != 2 || nested[1][0] != -3 || nested[1][1] != 4) return 5;
    if ((long long)wide_pair != -1 || (long long)(wide_pair >> 64) != -1) return 6;
    if (observed_plain[0] != -1 || *observed_scalar != -1) return 7;
    if (observed_grouped->wide != -1 || observed_grouped->unsigned_wide != 0xffffffffffffffffull) return 8;
    return (long long)*observed_wide_pair == -1 && (long long)(*observed_wide_pair >> 64) == -1 ? 0 : 9;
}
