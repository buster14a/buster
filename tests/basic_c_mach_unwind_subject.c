static int adjust(int value)
{
    volatile int saved = value + 5;
    return saved * 3;
}

int mach_unwind_value(int value)
{
    return adjust(value) + 7;
}
