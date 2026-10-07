typedef struct BfPair BfPair;
struct BfPair
{
    int tag;
    unsigned low : 3;
    unsigned mid : 12;
    unsigned high : 17;
};

typedef struct BfSmall BfSmall;
struct BfSmall
{
    unsigned first : 5;
    unsigned second : 11;
};

typedef union BfUnion BfUnion;
union BfUnion
{
    long long wide;
    int narrow;
    double real;
};

typedef struct BfNested BfNested;
struct BfNested
{
    union
    {
        long long wide;
        int narrow;
    };
    int tail;
    unsigned flag : 1;
};

extern unsigned llvm_bf_copy(const BfPair* source);
extern BfPair llvm_bf_return(int tag, unsigned low, unsigned mid, unsigned high);
extern BfPair llvm_bf_forward(BfPair pair);
extern unsigned llvm_bf_pass(BfPair pair, BfSmall small);
extern BfSmall llvm_bf_small(unsigned first, unsigned second);
extern unsigned llvm_bf_call_clang(void);
extern BfUnion llvm_union_init(long long wide);
extern BfUnion llvm_union_zero(void);
extern BfNested llvm_nested_zero(void);
extern long long llvm_union_pass(BfUnion value, BfNested nested);
extern long long llvm_union_call_clang(void);

unsigned clang_bf_pass(BfPair pair, BfSmall small)
{
    return pair.low + pair.mid * 10u + pair.high * 100u + (unsigned)pair.tag + small.first * 7u + small.second * 11u;
}

BfPair clang_bf_return(int tag)
{
    BfPair result = {tag, 3, 2000, 70000};
    return result;
}

long long clang_union_pass(BfUnion value, BfNested nested)
{
    return value.wide + nested.wide * 3 + nested.tail * 5 + nested.flag * 7;
}

BfUnion clang_union_return(long long wide)
{
    BfUnion value = {wide};
    return value;
}

int main(void)
{
    BfPair pair = {-3, 5, 1234, 99999};
    BfSmall small = {17, 1500};
    BfNested nested = {9, 4, 1};
    BfUnion value = {123456789012345LL};
    BfPair made = llvm_bf_return(11, 6, 4000, 100000);
    BfPair forwarded = llvm_bf_forward(pair);
    BfSmall small_made = llvm_bf_small(31, 2047);
    BfNested zero = llvm_nested_zero();
    unsigned expected_call = clang_bf_pass(pair, small) + 3 + 2000 + 70000 + (unsigned)40;
    int failures[] = {
        llvm_bf_copy(&pair) != 5u + 12340u + 9999900u - 3u,
        made.tag != 11 || made.low != 6 || made.mid != 4000 || made.high != 100000,
        forwarded.tag != -3 || forwarded.low != 5 || forwarded.mid != 1234 || forwarded.high != 99999,
        llvm_bf_pass(pair, small) != clang_bf_pass(pair, small),
        small_made.first != 31 || small_made.second != 2047,
        llvm_bf_call_clang() != expected_call,
        llvm_union_init(987654321987LL).wide != 987654321987LL,
        llvm_union_zero().wide != 0,
        zero.wide != 0 || zero.tail != 0 || zero.flag != 0,
        llvm_union_pass(value, nested) != clang_union_pass(value, nested),
        llvm_union_call_clang() != clang_union_pass(value, nested) + 77,
    };
    int result = 0;
    for (unsigned index = 0; index < sizeof(failures) / sizeof(failures[0]); index += 1)
    {
        if (!result && failures[index])
        {
            result = (int)index + 1;
        }
    }
    return result;
}
