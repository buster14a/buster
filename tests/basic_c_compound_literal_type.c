// An aggregate defined inside a type name rather than named by one. The type
// exists only where it is written -- inside a parenthesis, as the type of a
// compound literal, a cast, or a sizeof operand -- so no declaration ever
// registers it, and a compiler that only looks aggregates up by tag has
// nothing to find. It is how a libc's math library spells type punning:
// musl's asuint/asdouble are exactly the four macros below.

typedef unsigned int punning_u32;
typedef unsigned long long punning_u64;

#define asuint(f) ((union {float _f; punning_u32 _i;}){f})._i
#define asfloat(i) ((union {punning_u32 _i; float _f;}){i})._f
#define asuint64(f) ((union {double _f; punning_u64 _i;}){f})._i
#define asdouble(i) ((union {punning_u64 _i; double _f;}){i})._f

static int failures;

static void check(int condition)
{
    if (!condition)
    {
        failures += 1;
    }
}

// A tag on an inline definition names the type as well as defining it, and a
// later mention of that tag in the same scope has to reach the same type.
static int tagged_inline(int value)
{
    int second = ((struct CompoundLiteralPair {int first; int second;}){value, value + 1}).second;
    struct CompoundLiteralPair reused = {second, second * 2};
    return reused.second;
}

static int in_local_initializer(float value)
{
    // The initializer of a local declaration is the position where the members
    // would otherwise be read as uses of undeclared names, because the
    // declaration path consumes the whole statement.
    punning_u32 bits = asuint(value);
    return (int)(bits >> 23);
}

static unsigned long anonymous_sizeof(void)
{
    return sizeof(struct {int x; char c;});
}

static int through_cast(void* storage)
{
    return ((struct {int x;}*)storage)->x;
}

int main(void)
{
    int storage = 77;

    check(asuint(1.0f) == 0x3f800000u);
    check(asfloat(0x40000000u) == 2.0f);
    check(asuint64(1.0) == 0x3ff0000000000000ull);
    check(asdouble(0x4000000000000000ull) == 2.0);
    check(asdouble(asuint64(-0.5)) == -0.5);

    check(tagged_inline(4) == 10);
    check(in_local_initializer(1.0f) == 127);
    check(anonymous_sizeof() == 8);
    check(through_cast(&storage) == 77);

    return failures;
}
