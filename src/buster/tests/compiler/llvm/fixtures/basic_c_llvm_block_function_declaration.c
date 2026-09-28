// Block-scope function declarators without a storage-class specifier link like
// their `extern` forms and must not become stack objects in the bitcode.
long add(long a, long b)
{
    return a + b;
}

long sub(long a, long b)
{
    return a - b;
}

static int prototyped(void)
{
    long add(long, long);
    return (int)add(2L, 3L) - 5;
}

static int unprototyped(void)
{
    long add();
    return (int)add(4L, 5L) - 9;
}

static int unreferenced(void)
{
    long add(long, long);
    return 0;
}

static int list(void)
{
    long add(long, long), sub(long, long);
    return (int)(add(2L, 3L) - sub(8L, 3L));
}

static int mixed(void)
{
    long x = 2, add(long, long);
    return (int)add(x, 3L) - 5;
}

int main(void)
{
    return prototyped() | unprototyped() | unreferenced() | list() | mixed();
}
