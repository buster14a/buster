// Executable control for the opt-in canonical CSE pass (#3011), compiled by
// driver_fast_test.c with -fcanonical-fast-cse. Exactly three arithmetic rows
// are commoned (b+a, b&a, b*a, each onto its commuted twin); a-b and b-a are
// ordered and stay distinct. Kept apart from basic_c_canonical_fast.c, whose
// bytes the native-retirement support ledger freezes.
static unsigned common(unsigned a, unsigned b)
{
    unsigned x = (a + b) * (b + a);
    unsigned y = (a - b) ^ (b - a);
    unsigned z = (a & b) | (b & a);
    return x + y + z + (a * b) + (b * a) + (a * 5u) + (b * 5u);
}
volatile unsigned left = 7u;
volatile unsigned right = 3u;
int main(void)
{
    // 100 + (4 ^ 0xfffffffc) + 3 + 21 + 21 + 35 + 15 wraps to 187.
    return common(left, right) != 187u;
}
