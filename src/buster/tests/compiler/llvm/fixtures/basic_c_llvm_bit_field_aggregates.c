// Bit-field structs and unions as whole values in the LLVM backend: copy,
// return, pass by value, member reads after a copy, and calls to Clang.
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

extern unsigned clang_bf_pass(BfPair pair, BfSmall small);
extern BfPair clang_bf_return(int tag);
extern long long clang_union_pass(BfUnion value, BfNested nested);
extern BfUnion clang_union_return(long long wide);

unsigned llvm_bf_copy(const BfPair* source)
{
    BfPair copy = *source;
    return copy.low + copy.mid * 10u + copy.high * 100u + (unsigned)copy.tag;
}

BfPair llvm_bf_return(int tag, unsigned low, unsigned mid, unsigned high)
{
    BfPair result = {tag, low, mid, high};
    return result;
}

BfPair llvm_bf_forward(BfPair pair)
{
    BfPair copy = pair;
    return copy;
}

unsigned llvm_bf_pass(BfPair pair, BfSmall small)
{
    return pair.low + pair.mid * 10u + pair.high * 100u + (unsigned)pair.tag + small.first * 7u + small.second * 11u;
}

BfSmall llvm_bf_small(unsigned first, unsigned second)
{
    BfSmall small = {first, second};
    return small;
}

unsigned llvm_bf_call_clang(void)
{
    BfPair pair = {-3, 5, 1234, 99999};
    BfSmall small = {17, 1500};
    BfPair returned = clang_bf_return(40);
    BfPair copy = returned;
    return clang_bf_pass(pair, small) + copy.low + copy.mid + copy.high + (unsigned)copy.tag;
}

BfUnion llvm_union_init(long long wide)
{
    BfUnion value = {wide};
    return value;
}

BfUnion llvm_union_zero(void)
{
    BfUnion value = {0};
    return value;
}

BfNested llvm_nested_zero(void)
{
    BfNested value = {0};
    return value;
}

long long llvm_union_pass(BfUnion value, BfNested nested)
{
    BfUnion copy = value;
    return copy.wide + nested.wide * 3 + nested.tail * 5 + nested.flag * 7;
}

long long llvm_union_call_clang(void)
{
    BfUnion value = {123456789012345LL};
    BfNested nested = {9, 4, 1};
    BfUnion returned = clang_union_return(77);
    BfUnion copy = returned;
    return clang_union_pass(value, nested) + copy.wide;
}
