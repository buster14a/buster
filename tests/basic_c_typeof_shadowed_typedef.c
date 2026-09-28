// A lone identifier a block's scope binds to an object or parameter is that
// object inside typeof, whatever typedef shares the spelling further out; a
// block-local typedef shadowing the outer one still names a type (#1669).
typedef char T;

static long local_typeof(void)
{
    long T = 1;
    typeof(T) y = 0x12345;
    return sizeof(y) == sizeof(long) && y == 0x12345 && T == 1;
}

static long local_gnu_typeof(void)
{
    long T = 2;
    __typeof__(T) y = 0x12345;
    __typeof__(T)* p = &T;
    return sizeof(y) == sizeof(long) && y == 0x12345 && *p == 2;
}

static long local_typeof_unqual(void)
{
    const long T = 3;
    typeof_unqual(T) y = 0;
    y = 0x12345;
    return sizeof(y) == sizeof(long) && y == 0x12345 && T == 3;
}

static long parameter_typeof(long T)
{
    typeof(T) y = T;
    __typeof__(T) z = 0x12345;
    return sizeof(y) == sizeof(long) && sizeof(z) == sizeof(long) && z == 0x12345;
}

static long statement_expression_typeof(void)
{
    long r = ({
        long T = 4;
        typeof(T) y = 0x12345;
        __typeof__(T) z = T;
        typeof_unqual(T) w = 0x6789a;
        sizeof(y) == sizeof(long) && y == 0x12345 && z == 4 && w == 0x6789a;
    });
    return r;
}

static long block_typedef_typeof(void)
{
    typedef long T;
    long outer = 0;
    {
        typedef char T;
        typeof(T) y = 0;
        outer = (long)sizeof(y);
    }
    typeof(T) wide = 0x12345;
    return outer == 1 && sizeof(wide) == sizeof(long) && wide == 0x12345;
}

int main(void)
{
    T c = 0;
    return !(local_typeof() && local_gnu_typeof() && local_typeof_unqual() && parameter_typeof(5) && statement_expression_typeof() &&
             block_typedef_typeof() && sizeof(c) == 1);
}
