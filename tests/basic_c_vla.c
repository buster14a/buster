static int check(int count)
{
    int values[count];
    values[0] = 7;
    values[count - 1] = 11;
    if (sizeof(values) !=
        (unsigned long long)count * sizeof(int))
    {
        return 1;
    }
    return values[0] + values[count - 1] == 18 ?
        0 : 2;
}

static int check_loop_lifetime(int count)
{
    int total = 0;
    for (int index = 0; index < 256;
        index += 1)
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

static int check_parameter(
    int rows,
    int columns,
    int values[static rows][columns])
{
    values[rows - 1][columns - 1] = 29;
    if (sizeof(values) != sizeof(int*))
    {
        return 1;
    }
    if (sizeof(values[0]) !=
        (unsigned long long)columns * sizeof(int))
    {
        return 2;
    }
    if (values[rows - 1][columns - 1] != 29)
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
    if (sizeof(values) !=
        (unsigned long long)rows * columns *
            sizeof(int))
    {
        return 1;
    }
    if (sizeof(values[0]) !=
        (unsigned long long)columns * sizeof(int))
    {
        return 2;
    }
    {
        int parameter = check_parameter(
            rows,
            columns,
            values);
        if (parameter != 0)
        {
            return 3 + parameter;
        }
    }
    return values[0][0] +
            values[rows - 1][columns - 1] ==
        42 ? 0 : 7;
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
    return 0;
}
