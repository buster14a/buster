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

// A constant array bound is not a variable-length array, and the C lowering
// skips the VLA layout for this shape rather than folding it: the parameter
// decays to a pointer, and every answer below has to come from that pointer
// type instead of from a runtime layout the skip no longer builds.
static int check_constant_bound_parameter(unsigned char slots[2], unsigned char matrix[2][3])
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
        int constant_bound = check_constant_bound_parameter(slots, grid);
        if (constant_bound != 0)
        {
            return 50 + constant_bound;
        }
    }
    return 0;
}
