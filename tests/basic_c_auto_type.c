static int side_effect_count;

static int produce_scalar(void)
{
    side_effect_count += 1;
    return 23;
}

static int called_function(void)
{
    return 31;
}

struct AutoPair
{
    int left;
    int right;
};

int main(void)
{
    __auto_type scalar = produce_scalar();
    if (scalar != 23 || side_effect_count != 1)
    {
        return 1;
    }

    __auto_type floating = 1.5f;
    if (floating != 1.5f)
    {
        return 8;
    }

    int outer = 41;
    int other = 43;
    {
        __auto_type outer = outer;
        if (outer != 41)
        {
            return 2;
        }
    }

    int array_values[2] = {5, 11};
    __auto_type array = array_values;
    if (array[1] != 11)
    {
        return 3;
    }

    __auto_type function = called_function;
    if (function() != 31)
    {
        return 4;
    }

    __auto_type pointer = &outer;
    if (*pointer != 41)
    {
        return 5;
    }

    __auto_type conditional = 1 ? &outer : &other;
    if (*conditional != 41)
    {
        return 6;
    }

    __auto_type aggregate = (struct AutoPair){7, 9};
    if (aggregate.left + aggregate.right != 16)
    {
        return 7;
    }
    return side_effect_count != 1;
}
