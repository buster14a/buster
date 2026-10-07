// A `goto` inside a GNU statement expression may name a label outside it, in
// the enclosing function, either side of the expression. Labels have function
// scope, so the jump must find the label and the rest of the expression must
// still lower: it never runs, but its value is still part of the expression.

static int forward_unconditional(int x)
{
    int z = x;
    z = ({ goto out; 5; });
    z = 99;
out:
    return z;
}

static int forward_conditional(int x)
{
    int z = ({ if (x > 3) goto big; x * 2; });
    return z;
big:
    return -1;
}

static int forward_void_expression(int x)
{
    int z = 0;
    ({ if (x) goto skip; });
    z = 1;
skip:
    return z + 10;
}

static int backward(int limit)
{
    int count = 0;
again:
    count += 1;
    (void)({ if (count < limit) goto again; 0; });
    return count;
}

static int in_loop(int n)
{
    int sum = 0;
    for (int i = 0; i < n; i += 1)
    {
        sum += ({ if (i == 3) goto done; i; });
    }
done:
    return sum;
}

int main(void)
{
    if (forward_unconditional(7) != 7)
    {
        return 1;
    }
    if (forward_conditional(2) != 4 || forward_conditional(9) != -1)
    {
        return 2;
    }
    if (forward_void_expression(0) != 11 || forward_void_expression(1) != 10)
    {
        return 3;
    }
    if (backward(5) != 5)
    {
        return 4;
    }
    if (in_loop(10) != 0 + 1 + 2)
    {
        return 5;
    }
    return 0;
}
