// Synthetic high-reuse control, not a representative workload.
// Distinct stored values and mixed types must survive shared topology analysis.
static int left = 17;
static int right = 29;

static int repeated(int condition)
{
#define DECLARE(n) int v##n = (n) + 1;
    DECLARE(0) DECLARE(1) DECLARE(2) DECLARE(3)
    DECLARE(4) DECLARE(5) DECLARE(6) DECLARE(7)
    DECLARE(8) DECLARE(9) DECLARE(10) DECLARE(11)
    DECLARE(12) DECLARE(13) DECLARE(14) DECLARE(15)
#undef DECLARE
    if (condition)
    {
#define ASSIGN(n) v##n = 3 * (n) + 2;
        ASSIGN(0) ASSIGN(1) ASSIGN(2) ASSIGN(3)
        ASSIGN(4) ASSIGN(5) ASSIGN(6) ASSIGN(7)
        ASSIGN(8) ASSIGN(9) ASSIGN(10) ASSIGN(11)
        ASSIGN(12) ASSIGN(13) ASSIGN(14) ASSIGN(15)
#undef ASSIGN
    }
    return v0 + v1 + v2 + v3 + v4 + v5 + v6 + v7 + v8 + v9 + v10 + v11 + v12 + v13 + v14 + v15;
}

static int mixed(int condition)
{
    int integer = 3;
    double floating = 5.0;
    int const* pointer = &left;
    if (condition)
    {
        integer = 7;
        floating = 11.0;
        pointer = &right;
    }
    return integer + (int)floating + *pointer;
}

int main(void)
{
    int failed = repeated(0) != 136 || repeated(1) != 392 || mixed(0) != 25 || mixed(1) != 47;
    return failed;
}
