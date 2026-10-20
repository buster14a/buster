// The out-of-line definition of inline.c's again, which the linker takes.
int again(int n);

int again(int n)
{
    (void)n;
    return 1;
}
