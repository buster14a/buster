// Calls while a VLA is live need shadow space and stack arguments below it.
// The aggregate copy must remain private even when the callee writes to it.
typedef unsigned long long U64;
struct Triple { U64 a; U64 b; U64 c; };

static U64 consume(U64 a, U64 b, U64 c, U64 d, U64 e, U64 f, struct Triple value, U64 last)
{
    U64 result = a + 3*b + 5*c + 7*d + 11*e + 13*f + value.a + 17*value.b + 19*value.c + 23*last;
    value.a = 0;
    return result;
}

static int exercise(U64 count)
{
    unsigned char bytes[count];
    struct Triple value = {31, 37, 41};
    for (U64 index = 0; index < count; index += 1)
    {
        bytes[index] = (unsigned char)(index * 7 + 3);
    }
    U64 sum = consume(2, 3, 5, 7, 11, 13, value, 17);
    int result = sum != 2205 || value.a != 31 || value.b != 37 || value.c != 41;
    for (U64 index = 0; index < count; index += 1)
    {
        result |= bytes[index] != (unsigned char)(index * 7 + 3);
    }
    {
        unsigned char nested[count + 19];
        nested[0] = 43;
        nested[count + 18] = 47;
        U64 (*call)(U64, U64, U64, U64, U64, U64, struct Triple, U64) = consume;
        result |= call(2, 3, 5, 7, 11, 13, value, 17) != 2205;
        result |= nested[0] != 43 || nested[count + 18] != 47;
    }
    for (U64 index = 0; index < count; index += 1)
    {
        result |= bytes[index] != (unsigned char)(index * 7 + 3);
    }
    return result;
}

int main(void)
{
    return exercise(1) | exercise(31) | exercise(8207);
}
