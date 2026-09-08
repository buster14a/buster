#include <stdarg.h>

struct Bits
{
    unsigned int one : 1;
    unsigned int three : 3;
    unsigned int thirty_one : 31;
    unsigned int full : 32;
    signed int signed_three : 3;
};

struct Nested
{
    struct { unsigned int promoted : 3; };
};

static int promoted_argument(int ignored, ...)
{
    va_list arguments;
    va_start(arguments, ignored);
    int value = va_arg(arguments, int);
    va_end(arguments);
    return value;
}

static int check(struct Bits *p, struct Nested *nested)
{
    int result = 0;
    result |= p->one <= -1;
    result |= p->three <= -1;
    result |= p->thirty_one <= -1;
    result |= -p->three >= 0;
    result |= ~p->three != -8;
    result |= p->three / -1 != -7;
    result |= p->three % -2 != 1;
    result |= (p->three << 1) != 14;
    result |= (p->three >> 1) != 3;
    result |= (p->three & -1) != 7;
    result |= (p->three ^ -1) != -8;
    result |= (p->three | -1) != -1;
    result |= _Generic(+p->one, int: 1, default: 0) != 1;
    result |= _Generic(+p->three, int: 1, default: 0) != 1;
    result |= _Generic(+p->thirty_one, int: 1, default: 0) != 1;
    result |= _Generic(+p->full, unsigned int: 1, default: 0) != 1;
    result |= _Generic(+p->signed_three, int: 1, default: 0) != 1;
    result |= _Generic(+(nested->promoted), int: 1, default: 0) != 1;
    result |= _Generic(+((unsigned int)p->three), unsigned int: 1, default: 0) != 1;
    result |= p->full != 0xffffffffU;
    result |= ((unsigned int)p->three > -1) != 0;
    result |= promoted_argument(0, p->three) != 7;
    result |= nested->promoted <= -1;
    result |= (p->one ? p->three : -1) <= -1;
    result |= _Generic(p->one ? p->three : p->one, int: 1, default: 0) != 1;
    result |= _Generic(+(0, p->three), int: 1, default: 0) != 1;
    result |= (p->three = 7) <= -1;
    result |= (p->three += 1) != 0;
    p->three = 7;
    result |= ++p->three != 0;
    p->three = 7;
    p->three /= -1;
    result |= p->three != 1;
    return result;
}

int main(void)
{
    struct Bits bits = {1, 7, 2147483647U, 0xffffffffU, -3};
    struct Nested nested = {{5}};
    return check(&bits, &nested);
}
