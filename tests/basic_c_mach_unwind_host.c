int mach_unwind_value(int value);

int main(void)
{
    return mach_unwind_value(29) != 109 || mach_unwind_value(-5) != 7;
}
