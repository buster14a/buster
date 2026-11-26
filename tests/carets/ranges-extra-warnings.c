// Operand ranges on common warnings, and a following declaration note with no range (#2376).
int unused_and_bounds(int x)
{
    int values[2];
    x + 1;
    values[3] = 4;
    return x;
}
char *stack_address(void)
{
    char bytes[2];
    return bytes;
}
