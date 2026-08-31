// GNU's transparent_union: a parameter of the union type accepts an
// argument of any member's type, passed as that member's own bits.  glibc's
// accept4 takes __SOCKADDR_ARG this way, and CPython's socketmodule hands
// it a struct sockaddr* -- both member spellings are called and the family
// read back through the union.
struct sockaddr_like { unsigned short family; };
struct sockaddr_in_like { unsigned short family; unsigned short port; };
typedef union {
    struct sockaddr_like* base;
    struct sockaddr_in_like* in;
} sockaddr_arg __attribute__((__transparent_union__));

static unsigned short read_family(sockaddr_arg arg)
{
    return arg.base->family;
}

int main(void)
{
    struct sockaddr_in_like in = {7, 80};
    struct sockaddr_like* base = (struct sockaddr_like*)&in;
    if (read_family(base) != 7) { return 1; }
    if (read_family(&in) != 7) { return 2; }
    return 0;
}
