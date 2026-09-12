// Compile twice with different public names to exercise merged debug contributions.
#ifndef DWARF_FUNCTION
#define DWARF_FUNCTION dwarf_first
#endif

extern int dwarf_step(int value);

__attribute__((noinline)) static int dwarf_helper(int value)
{
    return dwarf_step(value) + 1;
}

int DWARF_FUNCTION(int count)
{
    int sum = 0;
    for (int index = 0; index < count; index += 1)
    {
        int value = dwarf_helper(index);
        sum += value;
    }
    return sum;
}
