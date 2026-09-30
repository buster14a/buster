/* Identity casts for aggregate comma results must copy the complete value,
 * including partial eightbytes and the tail of an indirect ABI argument. */
struct Small { unsigned char a, b, c; };
struct Big { long long a, b, c; };
union BigUnion { long long words[3]; unsigned char bytes[24]; };
union OddUnion { unsigned char bytes[13]; };
static volatile int effects;
static int side(void) { effects += 1; return 0; }

static struct Small small_result(void)
{
    return (side(), (struct Small){3, 5, 7});
}

static struct Big big_result(struct Big value)
{
    return (side(), value);
}

static long long big_sum(struct Big value)
{
    struct Big copy = (side(), value);
    return copy.a + copy.b + copy.c;
}

static long long union_tail(union BigUnion value)
{
    union BigUnion copy = (side(), value);
    return copy.words[2];
}

static union BigUnion union_result(union BigUnion value)
{
    return (side(), value);
}

static int odd_tail(union OddUnion value)
{
    union OddUnion copy = (side(), value);
    return copy.bytes[0] + copy.bytes[7] + copy.bytes[12];
}

int main(void)
{
    struct Small small = small_result();
    struct Big original = {11, 23, 47};
    struct Big big = big_result(original);
    union BigUnion original_union = {.words = {13, 29, 61}};
    union BigUnion large_union = union_result(original_union);
    union OddUnion odd_union = {.bytes = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13}};
    long long sum = big_sum((side(), original));
    long long literal_sum = big_sum((side(), (struct Big){2, 3, 5}));
    long long tail = union_tail((side(), large_union));
    int odd = odd_tail((side(), odd_union));
    return small.a != 3 || small.b != 5 || small.c != 7 ||
           big.a != 11 || big.b != 23 || big.c != 47 ||
           sum != 81 || literal_sum != 10 || tail != 61 || odd != 22 || effects != 11;
}
