// Block-scope function declarators without a storage-class specifier link like
// their `extern` forms and must not become stack objects in the bitcode; taking
// their address references the file-scope function symbol.
typedef char T;

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

static int address_extern(void)
{
    extern long add(long, long);
    long (*fp)(long, long) = add;
    return (int)fp(1L, 3L) - 4;
}

static int address_plain(void)
{
    long add(long, long);
    long (*fp)(long, long) = add;
    return (int)fp(1L, 3L) - 4;
}

static int address_of_later(void)
{
    extern long later(long, long);
    long (*fp)(long, long) = &later;
    return (int)fp(7L, 3L) - 4;
}

long later(long a, long b)
{
    return a - b;
}

// The declarator's T has block scope; the file-scope typedef is T again below.
static int shadow(void)
{
    double T(void);
    return 0;
}

static int typedef_after_shadow(void)
{
    T c = 0;
    return (int)sizeof(c) - 1;
}

int main(void)
{
    return prototyped() | unprototyped() | unreferenced() | list() | mixed() | shadow() | typedef_after_shadow() | address_extern() | address_plain() | address_of_later();
}
