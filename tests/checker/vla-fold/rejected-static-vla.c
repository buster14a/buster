void check(int n)
{
    int array[2];
    array[3] = 0;
    static int bad[(2, n)][((void)3, n)][(4, n)];
    static _Thread_local int thread_local_vla[(5, n)];
    array[4] = 0;
}

void check_controls(int n)
{
    int automatic[(1, n)];
    static int (*pointer)[(6, n)];
    static int folded[(7, 2)];
    (void)automatic;
    (void)folded;
    (void)pointer;
}

void check_initialized(int n)
{
    static int bad[(8, n)] = { 0 };
}
