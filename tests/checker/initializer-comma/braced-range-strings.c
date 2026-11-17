struct Source { int value; } source;
char file_strings[3][1] = { [0 ... 2] = "abc" };

void local_strings(void)
{
    char later_strings[3][1] = { { source }, [1 ... 2] = "abc" };
    char braced_strings[3][1] = { [0 ... 2] = { "abc" } };
    int repeated_lists[3][1] = { [0 ... 2] = { 0, 1 } };
}
