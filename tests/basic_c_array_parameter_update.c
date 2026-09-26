// C17 6.7.6.3p7: a parameter declared as an array is adjusted to a pointer to
// the element type, so it is a modifiable lvalue and `++`/`--` step it by one
// element (6.5.2.4, 6.5.3.1). The parameter entity keeps its declared array
// spelling, and the constraint pass read that spelling as an array object and
// refused every update as "not a modifiable place" -- while `+=` and `= p + 1`
// on the same parameter were accepted. `char *argv[]` with `argv++` is the
// ordinary command-line loop (lmdb's `mdb_copy`, sbase's ARGBEGIN), and
// mbedtls's `des_setkey` writes its key schedule through `uint32_t SK[32]` as
// `*SK++ = ...`.
//
// Every update runs and the element it lands on is checked, including a
// multidimensional parameter whose step is a whole row.

typedef int Quad[4];

static int count_options(int argc, char* argv[])
{
    int options = 0;
    for (; argc > 1 && argv[1][0] == '-'; argc--, argv++)
    {
        options += 1;
    }
    return options * 10 + argc;
}

static void schedule(unsigned int SK[4], unsigned int seed)
{
    for (int index = 0; index < 4; index += 1)
    {
        *SK++ = seed + (unsigned int)index;
    }
}

static int prefix_and_postfix(int values[])
{
    int first = *values++;
    int second = *++values;
    --values;
    int middle = *values--;
    return first * 100 + second * 10 + middle + (values[0] == first ? 1000 : 0);
}

static int bounded(int values[static 3])
{
    values++;
    return values[1];
}

static int variable(int n, int values[n])
{
    values += n - 1;
    values--;
    return values[0];
}

static int rows(int matrix[][3])
{
    matrix++;
    return matrix[0][2];
}

static int typedef_parameter(Quad quad)
{
    ++quad;
    return quad[0];
}

static int qualified_elements(const char* names[])
{
    int total = 0;
    while (*names)
    {
        total += (*names)[0];
        (names)++;
    }
    return total;
}

// `const` inside the brackets makes the pointer const; its elements are not.
static int bracket_const(int values[const 2])
{
    values[1]++;
    return values[0] * 10 + values[1];
}

static int unevaluated(int values[])
{
    // The operand of sizeof is not evaluated: `values` is not stepped.
    int size = (int)sizeof(values++);
    return size == (int)sizeof(int*) ? values[0] : -1;
}

int main(void)
{
    char program[] = "tool";
    char flag_a[] = "-a";
    char flag_b[] = "-b";
    char operand[] = "file";
    char* argv[] = {program, flag_a, flag_b, operand, 0};
    if (count_options(4, argv) != 22)
    {
        return 1;
    }

    unsigned int keys[5] = {0, 0, 0, 0, 99};
    schedule(keys, 40);
    if (keys[0] != 40 || keys[3] != 43 || keys[4] != 99)
    {
        return 2;
    }

    int values[4] = {1, 2, 3, 4};
    if (prefix_and_postfix(values) != 1132)
    {
        return 3;
    }
    if (bounded(values) != 3)
    {
        return 4;
    }
    if (variable(4, values) != 3)
    {
        return 5;
    }

    int matrix[2][3] = {{1, 2, 3}, {4, 5, 6}};
    if (rows(matrix) != 6)
    {
        return 6;
    }

    Quad quad = {10, 20, 30, 40};
    if (typedef_parameter(quad) != 20)
    {
        return 7;
    }

    char const* names[] = {"a", "b", 0};
    if (qualified_elements(names) != 'a' + 'b')
    {
        return 8;
    }

    if (unevaluated(values) != 1)
    {
        return 9;
    }

    int pair[2] = {4, 5};
    if (bracket_const(pair) != 46 || pair[1] != 6)
    {
        return 10;
    }
    return 0;
}
