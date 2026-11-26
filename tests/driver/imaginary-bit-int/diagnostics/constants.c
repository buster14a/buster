// #2219: imaginary bit-precise constants in every spelling clang takes, of 2 to 129 bits, in constant contexts and as
// operands. C23 takes them silently; before it, each is clang's -Wc23-extensions, as a real bit-precise constant is. The
// array's size is GNU's folding in either mode.
typedef __typeof__(5wbi) C4;
static C4 signed_constants[] = { 5wbi, 5iwb, 5WBJ, 5Iwb, 0wbi, -7wbi };
static __typeof__(5uwbi) unsigned_constants[] = { 5uwbi, 5wbui, 5iuwb, 5uiwb, 5wbiu, 0uwbi };
static __typeof__(0x10000000000000000wbi) wide = 0x10000000000000000wbi;
static __typeof__(0x1ffffffffffffffffffffffffffffffffwbi) wider = 0x1ffffffffffffffffffffffffffffffffwbi * 2;
_Static_assert(__imag__ 0x5wbi == 5 && __imag__ 0b101wbi == 5 && __imag__ 07wbi == 7 && __real__ 7wbi == 0, "radixes");
_Static_assert(sizeof(5wbi) == 2 && sizeof(0x10000000000000000wbi) == 32 && sizeof(0x1ffffffffffffffffffffffffffffffffwbi) == 48, "sizes");
enum { E = __imag__ 7wbi, F = (int)(5wbi * 2) };
int sizes[__imag__ 5wbi];
int f(int n)
{
    switch (n)
    {
        case __imag__ 3wbi:
            break;
    }
    int k = 5wbi + n;
    _Bool b = 5wbi;
    double d = 0x10000000000000000wbi;
    __typeof__(5wbi * 2wbi) z = 5wbi * 2wbi;
    z += 1wbi;
    return k + b + (int)d + (int)__imag__ z;
}
