// C requires the width of a *named* bit-field to be at least one bit
// (C23 6.7.3.2p4).  Zero is reserved for the unnamed `int : 0;`, which
// declares no member and only moves the next one to its type's boundary, and
// both reference compilers refuse the named spelling: "named bit-field 'b' has
// zero width" (Clang), "zero width for bit-field 'b'" (GCC).  Accepted, the
// member was laid out as the zero-width member it is not allowed to be -- the
// struct below measured 4/4 -- and `v.b = 0;` followed by a read of `v.b`
// compiled and answered zero.
//
// This fixture must fail to compile, so it is never linked or run; the
// declarations below are the whole test.  The width is written as a constant
// expression as well as a literal because the parse folds only the literal
// spelling and the width the layout uses is folded while the type is lowered.
struct named_zero_width
{
    char c;
    int b : 0;
};

struct named_zero_width_expression
{
    char c;
    int b : 1 - 1;
};

// The unnamed spelling stays legal; `struct __attribute__((packed))
// zero_width_bits` in tests/basic_c_packed_layout.c covers what it lays out.
struct unnamed_zero_width
{
    int a : 3;
    int : 0;
    int b : 3;
};

int main(void)
{
    return 0;
}
