// C23 constexpr leaves and composite subobjects share the same type checks.
// Pointees are not subobjects: a null pointer to a volatile object is valid.
struct ConstexprPair
{
    int first;
    int second;
};

struct ConstexprNested
{
    struct ConstexprPair pair;
    int values[3];
};

union ConstexprUnion
{
    int integer;
    unsigned other;
};

constexpr int width = 3;
constexpr double fraction = 1.5;
constexpr volatile int* no_object = nullptr;
constexpr int fixed[width] = {2, 3, 5};
constexpr int inferred[] = {7, 11};
constexpr struct ConstexprNested nested = {{13, 17}, {19, 23, 29}};
constexpr union ConstexprUnion selected = {31};

static_assert(width == 3);
static_assert(sizeof(inferred) / sizeof(inferred[0]) == 2);

static int scoped_constants(void)
{
    int value = width;
    {
        constexpr int width = 6;
        int local[width];
        local[0] = width;
        value += local[0];
    }
    return value + width;
}

int main(void)
{
    int failed = 0;
    failed |= width != 3 || fraction != 1.5 || no_object != nullptr;
    failed |= fixed[0] + fixed[1] + fixed[2] != 10;
    failed |= inferred[0] + inferred[1] != 18;
    failed |= nested.pair.first != 13 || nested.pair.second != 17;
    failed |= nested.values[0] + nested.values[1] + nested.values[2] != 71;
    failed |= selected.integer != 31;
    failed |= scoped_constants() != 12;
    return failed;
}
