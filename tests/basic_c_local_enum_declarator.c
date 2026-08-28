// `run-test262.c` opens load_config with a block-scope anonymous enum that
// carries a declarator: `enum { SECTION_NONE = 0, SECTION_CONFIG, ... }
// section = SECTION_NONE;`.  The scan that finds a local declaration's
// initializer counted brackets and parentheses but not braces, so the '='
// inside the enum body read as the object's initializer and the rest of the
// body was lowered as its value -- the enumerators after the first bound to
// nothing.
static int classify(int value)
{
    enum
    {
        SECTION_NONE = 0,
        SECTION_CONFIG,
        SECTION_EXCLUDE,
        SECTION_FEATURES = 10,
        SECTION_TESTS,
    } section = SECTION_NONE;

    if (value == 1) section = SECTION_CONFIG;
    if (value == 2) section = SECTION_EXCLUDE;
    if (value == 3) section = SECTION_FEATURES;
    if (value == 4) section = SECTION_TESTS;
    switch (section)
    {
    case SECTION_NONE: return 0;
    case SECTION_CONFIG: return 1;
    case SECTION_EXCLUDE: return 2;
    case SECTION_FEATURES: return 10;
    case SECTION_TESTS: return 11;
    }
    return -1;
}

int main(void)
{
    // A named enum, an initialized declarator and a trailing bit-width-free
    // member all take the same path.
    enum shift { SHIFT_ONE = 1, SHIFT_TWO } named = SHIFT_TWO;
    enum { INNER_A = 3, INNER_B } inner;
    inner = INNER_B;
    if (classify(0) != 0) return 1;
    if (classify(1) != 1) return 2;
    if (classify(2) != 2) return 3;
    if (classify(3) != 10) return 4;
    if (classify(4) != 11) return 5;
    if ((int)named != 2) return 6;
    if ((int)inner != 4) return 7;
    return 0;
}
