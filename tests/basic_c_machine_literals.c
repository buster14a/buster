// Array and aggregate literal construction on the machine path: the shapes
// the AArch64 selector's literal port covers, each built from runtime
// operands so nothing folds before selection. Scalar members store sized at
// their offsets, aggregate members copy slot to slot through the member's
// address, unions zero-fill their tail past the one initialized member, and
// bit-field siblings merge into one storage-unit image before it stores
// once. Every function is self-checking through main's exit status, so the
// register-allocator-mode differential (NONE as the oracle) needs only the
// exit codes to agree.

typedef struct Point
{
    long x;
    long y;
} Point;

typedef struct Mixed
{
    char tag;
    int count;
    Point inner;
    long tail;
} Mixed;

typedef union Overlay
{
    char head;
    long words[4];
} Overlay;

typedef struct Flags
{
    unsigned read : 1;
    unsigned write : 1;
    unsigned mode : 3;
    unsigned rest : 27;
} Flags;

static long array_literal(long a, long b)
{
    long values[5] = {a, b, a + b, a * b, 9};
    return values[0] + values[1] * 10 + values[2] * 100 + values[3] * 1000 + values[4];
}

static long nested_aggregate(long a, long b)
{
    Mixed m = {.tag = (char)a, .count = (int)b, .inner = {.x = a * 2, .y = b * 3}, .tail = a ^ b};
    return m.tag + m.count * 10 + m.inner.x * 100 + m.inner.y * 1000 + m.tail;
}

static long array_of_aggregates(long a, long b)
{
    Point points[3] = {{.x = a, .y = b}, {.x = b, .y = a}, {.x = a + b, .y = a - b}};
    long total = 0;
    for (int index = 0; index < 3; index += 1)
    {
        total += points[index].x * 2 + points[index].y;
    }
    return total;
}

// The tail bytes past the one initialized union member are zero under
// buster and clang; gcc leaves them unspecified, so this fixture is not a
// gcc oracle. The zero-fill is the behavior under test (see union_tail in
// machine_test.c for the regression it guards).
static long union_zero_tail(long a)
{
    Overlay o = {(char)(a & 0)};
    return o.words[1] + o.words[2] + o.words[3] + o.head;
}

static long bit_field_literal(long a, long b)
{
    Flags f = {.read = (unsigned)a & 1u, .write = (unsigned)b & 1u, .mode = (unsigned)(a + b) & 7u, .rest = 0u};
    return (long)(f.read + f.write * 10u + f.mode * 100u);
}

int main(void)
{
    int failures = 0;
    failures += array_literal(3, 4) != 3 + 40 + 700 + 12000 + 9;
    failures += nested_aggregate(5, 6) != 5 + 60 + 1000 + 18000 + (5 ^ 6);
    failures += array_of_aggregates(7, 2) != (7 * 2 + 2) + (2 * 2 + 7) + (9 * 2 + 5);
    failures += union_zero_tail(123) != 0;
    failures += bit_field_literal(1, 3) != 1 + 10 + 400;
    return failures;
}
