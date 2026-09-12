extern int dwarf_first(int count);
extern int dwarf_second(int count);

int dwarf_step(int value)
{
    return value * 2;
}

int main(void)
{
    return dwarf_first(3) != 9 || dwarf_second(5) != 25;
}
