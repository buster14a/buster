// A parameter's outermost suffix size — the whole object's — is never read, so
// the C lowering stops building it: an array parameter is a pointer, and
// `sizeof values` below is that pointer's rather than the runtime layout's.
// One dimension is where that walk now ends, which makes `sizeof(values[0])`
// the innermost suffix it still has to build; the subscripts either side of it
// say that dropping the outer product left the element size alone.
static int check_one_dimensional_parameter(int count, int values[count])
{
    if (sizeof(values) != sizeof(int*))
    {
        return 3;
    }
    if (sizeof(values[0]) != sizeof(int))
    {
        return 4;
    }
    if (values[0] != 7 || values[count - 1] != 11)
    {
        return 5;
    }
    values[count - 1] = 23;
    if (values[count - 1] != 23 || values + count - values != count)
    {
        return 6;
    }
    return 0;
}

static int check(int count)
{
    int values[count];
    values[0] = 7;
    values[count - 1] = 11;
    if (sizeof(values) != (unsigned long long)count * sizeof(int))
    {
        return 1;
    }
    {
        int parameter = check_one_dimensional_parameter(count, values);
        if (parameter != 0)
        {
            return parameter;
        }
    }
    values[count - 1] = 11;
    return values[0] + values[count - 1] == 18 ? 0 : 2;
}

static int check_loop_lifetime(int count)
{
    int total = 0;
    for (int index = 0; index < 256; index += 1)
    {
        {
            int values[count];
            values[count - 1] = index;
            if ((index & 1) != 0)
            {
                continue;
            }
            total += values[count - 1];
        }
    }
    return total == 16256 ? 0 : 1;
}

static int check_parameter(int rows, int columns, int values[static rows][columns])
{
    values[rows - 1][columns - 1] = 29;
    if (sizeof(values) != sizeof(int*))
    {
        return 1;
    }
    if (sizeof(values[0]) != (unsigned long long)columns * sizeof(int))
    {
        return 2;
    }
    if (values[rows - 1][columns - 1] != 29)
    {
        return 3;
    }
    return 0;
}

static long bound_global = 1;
enum { bound_enumerator = 2 };

// This helper is named only by the function-designator expression in the
// parameter below.  Binding that token must also keep the internal function in
// the emitted module even though no body call reaches it.
static long bound_designator_helper(void)
{
    return 0;
}

// Names in an array parameter's bound live in the declarator rather than in the
// body.  The object, function designator, and enumerator all occur in the
// second dimension here, where lowering needs their bound entities to compute
// the row stride.
static int check_identifier_bound_parameters(int rows, int columns, int object_values[rows][bound_global + columns],
                                             int function_values[rows][(long)&bound_designator_helper - (long)&bound_designator_helper + columns],
                                             int enum_values[rows][bound_enumerator + columns])
{
    if (sizeof(object_values[0]) != (unsigned long long)(bound_global + columns) * sizeof(int))
    {
        return 1;
    }
    object_values[0][bound_global + columns - 1] = 17;
    if (object_values[0][bound_global + columns - 1] != 17)
    {
        return 2;
    }
    if (sizeof(function_values[0]) != (unsigned long long)columns * sizeof(int))
    {
        return 3;
    }
    function_values[0][columns - 1] = 19;
    if (function_values[0][columns - 1] != 19)
    {
        return 4;
    }
    if (sizeof(enum_values[0]) != (unsigned long long)(bound_enumerator + columns) * sizeof(int))
    {
        return 5;
    }
    enum_values[0][bound_enumerator + columns - 1] = 23;
    return enum_values[0][bound_enumerator + columns - 1] == 23 ? 0 : 6;
}

// Neither a constant array bound nor an absent one is a variable-length array,
// and the C lowering skips the VLA layout for both shapes rather than folding
// them: the parameter decays to a pointer, and every answer below has to come
// from that pointer type instead of from a runtime layout the skip no longer
// builds.  `open[]` is main's argv/envp spelling, and `rows[][3]` is the
// two-dimensional absent bound, which is a pointer to a constant-bound array
// and was always skipped.
static int check_constant_bound_parameter(unsigned char slots[2], unsigned char matrix[2][3], unsigned char open[], unsigned char rows[][3])
{
    if (sizeof(slots) != sizeof(unsigned char*))
    {
        return 1;
    }
    if (sizeof(slots[0]) != 1)
    {
        return 2;
    }
    if (sizeof(slots[1]) != 1)
    {
        return 3;
    }
    if (slots[1] - slots[0] != 1)
    {
        return 4;
    }
    if (slots + 2 - slots != 2)
    {
        return 5;
    }
    if (sizeof(matrix) != sizeof(unsigned char*))
    {
        return 6;
    }
    if (sizeof(matrix[0]) != 3)
    {
        return 7;
    }
    if (matrix[1][2] != 6)
    {
        return 8;
    }
    if (sizeof(open) != sizeof(unsigned char*))
    {
        return 9;
    }
    if (sizeof(open[0]) != 1)
    {
        return 10;
    }
    if (open[1] - open[0] != 1)
    {
        return 11;
    }
    if (open + 2 - open != 2)
    {
        return 12;
    }
    if (sizeof(rows) != sizeof(unsigned char*))
    {
        return 13;
    }
    if (sizeof(rows[0]) != 3)
    {
        return 14;
    }
    if (rows[1][2] != 6)
    {
        return 15;
    }
    return 0;
}

// The absent outermost bound keeps its layout whenever an inner bound is a real
// runtime value: `cells[][columns]` still needs the row stride to index
// through, so the one-dimensional skip above must not reach it.
static int check_unspecified_bound_over_vla(int columns, unsigned char cells[][columns])
{
    if (sizeof(cells) != sizeof(unsigned char*))
    {
        return 1;
    }
    if (sizeof(cells[0]) != (unsigned long long)columns)
    {
        return 2;
    }
    cells[1][2] = 23;
    if (cells[1][2] != 23)
    {
        return 3;
    }
    return 0;
}

static int bound_calls = 0;

static long counted_bound(long value)
{
    bound_calls += 1;
    return value;
}

// A parameter's array bound is an expression the callee evaluates on entry,
// once per call, and its tokens live in the declarator instead of in the body.
// A call there is prepared and lowered like any other call, into the entry
// block, before the body runs; it used to index the body's token-to-call table
// out of range and abort the compiler.  The outermost dimension is evaluated
// too, even though the parameter decays to a pointer and nothing ever reads
// that count -- clang evaluates it, so this fixture counts it.  counted_bound
// is static and named nowhere but this declarator, so it also covers the
// reachability pass that decides which internal functions get emitted.
static int check_call_bound_parameter(long rows, long columns, int values[counted_bound(rows)][counted_bound(columns)])
{
    if (sizeof(values) != sizeof(int*))
    {
        return 1;
    }
    if (sizeof(values[0]) != (unsigned long long)columns * sizeof(int))
    {
        return 2;
    }
    values[rows - 1][columns - 1] = 23;
    if (values[rows - 1][columns - 1] != 23)
    {
        return 3;
    }
    return 0;
}

static int check_nested(int rows, int columns)
{
    int values[rows][columns];
    values[0][0] = 13;
    values[rows - 1][columns - 1] = 17;
    if (sizeof(values) != (unsigned long long)rows * columns * sizeof(int))
    {
        return 1;
    }
    if (sizeof(values[0]) != (unsigned long long)columns * sizeof(int))
    {
        return 2;
    }
    {
        int parameter = check_parameter(rows, columns, values);
        if (parameter != 0)
        {
            return 3 + parameter;
        }
    }
    return values[0][0] + values[rows - 1][columns - 1] == 42 ? 0 : 7;
}

int main(void)
{
    int count = 4;
    int values[count++];
    values[0] = 3;
    values[3] = 5;
    if (count != 5)
    {
        return 3;
    }
    if (sizeof(values) != 4 * sizeof(int))
    {
        return 4;
    }
    if (values[0] + values[3] != 8)
    {
        return 5;
    }
    {
        int small = check(7);
        if (small != 0)
        {
            return 10 + small;
        }
    }
    {
        int large = check(16384);
        if (large != 0)
        {
            return 20 + large;
        }
    }
    {
        int lifetime = check_loop_lifetime(16384);
        if (lifetime != 0)
        {
            return 30 + lifetime;
        }
    }
    {
        int nested = check_nested(5, 7);
        if (nested != 0)
        {
            return 40 + nested;
        }
    }
    {
        unsigned char slots[2] = {4, 5};
        unsigned char grid[2][3] = {{1, 2, 3}, {4, 5, 6}};
        int constant_bound = check_constant_bound_parameter(slots, grid, slots, grid);
        if (constant_bound != 0)
        {
            return 50 + constant_bound;
        }
    }
    {
        unsigned char cells[2][3] = {{0, 0, 0}, {0, 0, 0}};
        int over_vla = check_unspecified_bound_over_vla(3, cells);
        if (over_vla != 0)
        {
            return 70 + over_vla;
        }
    }
    {
        int object_values[2][5] = {{0}};
        int function_values[2][3] = {{0}};
        int enum_values[2][5] = {{0}};
        int identifier_bound = check_identifier_bound_parameters(2, 3, object_values, function_values, enum_values);
        if (identifier_bound != 0)
        {
            return 100 + identifier_bound;
        }
    }
    {
        int grid[3][4];
        int call_bound = check_call_bound_parameter(3, 4, grid);
        if (call_bound != 0)
        {
            return 80 + call_bound;
        }
        if (bound_calls != 2)
        {
            return 84;
        }
        call_bound = check_call_bound_parameter(3, 4, grid);
        if (call_bound != 0)
        {
            return 90 + call_bound;
        }
        if (bound_calls != 4)
        {
            return 94;
        }
    }
    return 0;
}
