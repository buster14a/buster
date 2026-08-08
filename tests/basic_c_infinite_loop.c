// Loops whose only exit is a return inside the body: the loop-exit block is
// unreachable and the function has no trailing return statement.
static int probe_for(int value)
{
    for (;;)
    {
        if (value > 3)
        {
            return value;
        }
        value = value + 1;
    }
}

static int probe_while(int value)
{
    while (1)
    {
        if (value > 30)
        {
            return value;
        }
        value = value + 10;
    }
}

static int probe_break(int value)
{
    for (;;)
    {
        if (value > 3)
        {
            break;
        }
        value = value + 1;
    }
    return value;
}

static int probe_do(int value)
{
    do
    {
        value = value + 1;
    } while (0);
    return value;
}

int main(void)
{
    if (probe_for(0) != 4)
    {
        return 1;
    }
    if (probe_while(5) != 35)
    {
        return 2;
    }
    if (probe_break(0) != 4)
    {
        return 3;
    }
    if (probe_do(41) != 42)
    {
        return 4;
    }
    return 0;
}
