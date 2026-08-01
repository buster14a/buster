static double add(double left, double right)
{
    return left + right;
}

static double multiply_add(double addend, double left, double right)
{
    return addend + left * right;
}

struct Mixed
{
    double value;
    int count;
};

struct FloatPair
{
    double left;
    double right;
};

struct SmallPair
{
    int left;
    int right;
};

typedef void* va_list;

static struct Mixed make_mixed(double value, int count)
{
    return (struct Mixed){value, count};
}

static int mixed_is_expected(struct Mixed value)
{
    return value.value == 2.25 && value.count == 3;
}

static double sum_mixed(struct Mixed value)
{
    return value.value + value.count;
}

static struct FloatPair make_float_pair(double left, double right)
{
    return (struct FloatPair){left, right};
}

static double sum_float_pair(struct FloatPair value)
{
    return value.left + value.right;
}

static struct SmallPair make_small_pair(int left, int right)
{
    return (struct SmallPair){left, right};
}

static int sum_small_pair(struct SmallPair value)
{
    return value.left + value.right;
}

static double mixed_after_integer_registers(int first, int second, int third, int fourth, int fifth, int sixth, struct Mixed value)
{
    return first + second + third + fourth + fifth + sixth + value.value + value.count;
}

static double float_pair_after_float_registers(double first, double second, double third, double fourth, double fifth, double sixth, double seventh,
                                               double eighth, struct FloatPair value)
{
    return first + second + third + fourth + fifth + sixth + seventh + eighth + value.left + value.right;
}

static double variadic_mixed_sum(int marker, ...)
{
    va_list arguments;
    struct Mixed value;
    __builtin_va_start(arguments, marker);
    value = __builtin_va_arg(arguments, struct Mixed);
    __builtin_va_end(arguments);
    return value.value + value.count;
}

static double variadic_mixed_after_integer_registers(int first, int second, int third, int fourth, int fifth, int sixth, ...)
{
    va_list arguments;
    struct Mixed value;
    __builtin_va_start(arguments, sixth);
    value = __builtin_va_arg(arguments, struct Mixed);
    __builtin_va_end(arguments);
    return first + second + third + fourth + fifth + sixth + value.value + value.count;
}

static double variadic_float_pair_after_float_registers(double first, double second, double third, double fourth, double fifth, double sixth, double seventh,
                                                        double eighth, ...)
{
    va_list arguments;
    struct FloatPair value;
    __builtin_va_start(arguments, eighth);
    value = __builtin_va_arg(arguments, struct FloatPair);
    __builtin_va_end(arguments);
    return first + second + third + fourth + fifth + sixth + seventh + eighth + value.left + value.right;
}

int main(void)
{
    if (add(2.25, 3.0) != 5.25)
    {
        return 1;
    }
    if (multiply_add(1.25, 2.0, 3.0) != 7.25)
    {
        return 12;
    }
    if (!mixed_is_expected((struct Mixed){2.25, 3}))
    {
        return 2;
    }
    if (!mixed_is_expected(make_mixed(2.25, 3)))
    {
        return 4;
    }
    if (sum_mixed(make_mixed(2.25, 3)) != 5.25)
    {
        return 6;
    }
    if (sum_float_pair(make_float_pair(1.5, 2.75)) != 4.25)
    {
        return 3;
    }
    if (sum_small_pair(make_small_pair(7, 11)) != 18)
    {
        return 5;
    }
    if (mixed_after_integer_registers(1, 2, 3, 4, 5, 6, (struct Mixed){2.25, 3}) != 26.25)
    {
        return 7;
    }
    if (float_pair_after_float_registers(1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, (struct FloatPair){1.5, 2.75}) != 40.25)
    {
        return 8;
    }
    if (variadic_mixed_sum(0, (struct Mixed){2.25, 3}) != 5.25)
    {
        return 9;
    }
    if (variadic_mixed_after_integer_registers(1, 2, 3, 4, 5, 6, (struct Mixed){2.25, 3}) != 26.25)
    {
        return 10;
    }
    if (variadic_float_pair_after_float_registers(1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, (struct FloatPair){1.5, 2.75}) != 40.25)
    {
        return 11;
    }
    return 0;
}
