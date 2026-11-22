static int first, second, kept;

static int unused(void)
{
    return first + second;
}

int live(int value)
{
    {
        int local = value;
        kept = local;
        return kept;
    }
}
