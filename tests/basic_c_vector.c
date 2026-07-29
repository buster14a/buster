typedef float Float4
    __attribute__((vector_size(16)));

typedef int Int4
    __attribute__((vector_size(16)));

typedef unsigned int UInt4
    __attribute__((vector_size(16)));

typedef struct VectorPair
{
    Float4 left;
    Float4 right;
} VectorPair;

typedef union VectorStorage
{
    Float4 vector;
    float lanes[4];
} VectorStorage;

typedef union IntegerVectorStorage
{
    Int4 vector;
    int lanes[4];
} IntegerVectorStorage;

typedef union UnsignedVectorStorage
{
    UInt4 vector;
    unsigned int lanes[4];
} UnsignedVectorStorage;

Float4 vector_identity(Float4 value)
{
    return value;
}

VectorPair vector_pair_identity(VectorPair value)
{
    return value;
}

VectorPair vector_pair_stack_identity(
    Float4 first,
    Float4 second,
    Float4 third,
    Float4 fourth,
    Float4 fifth,
    Float4 sixth,
    Float4 seventh,
    Float4 eighth,
    VectorPair value)
{
    return value;
}

int main(void)
{
    VectorStorage input = { 0 };
    VectorStorage output = { 0 };
    VectorStorage identity_output = { 0 };
    VectorPair pair = { 0 };
    VectorPair pair_output = { 0 };
    VectorPair pair_stack_output = { 0 };
    VectorStorage arithmetic = { 0 };
    VectorStorage negated = { 0 };
    IntegerVectorStorage integers = { 0 };
    IntegerVectorStorage integer_result = { 0 };
    IntegerVectorStorage comparison = { 0 };
    IntegerVectorStorage float_comparison = { 0 };
    IntegerVectorStorage integer_math = { 0 };
    IntegerVectorStorage integer_shift = { 0 };
    IntegerVectorStorage integer_negate = { 0 };
    IntegerVectorStorage integer_not = { 0 };
    UnsignedVectorStorage unsigned_values = { 0 };
    IntegerVectorStorage unsigned_comparison = { 0 };
    input.lanes[0] = 1.25f;
    input.lanes[1] = 2.5f;
    input.lanes[2] = 3.75f;
    input.lanes[3] = 5.0f;
    identity_output.vector =
        vector_identity(input.vector);
    pair.left = input.vector;
    pair.right = identity_output.vector;
    pair_output = vector_pair_identity(pair);
    pair_stack_output = vector_pair_stack_identity(
        input.vector,
        input.vector,
        input.vector,
        input.vector,
        input.vector,
        input.vector,
        input.vector,
        input.vector,
        pair_output);
    output.vector = pair_stack_output.right;
    arithmetic.vector =
        (input.vector + 1.0f) * 2.0f -
        input.vector / 2.0f;
    negated.vector = -input.vector;
    integers.lanes[0] = 3;
    integers.lanes[1] = 6;
    integers.lanes[2] = 9;
    integers.lanes[3] = 12;
    integer_result.vector =
        (integers.vector + 1) ^ 3;
    comparison.vector = integers.vector > 6;
    float_comparison.vector = input.vector > 2.5f;
    integer_math.vector =
        (integers.vector * 2) / 2 % 5;
    integer_shift.vector =
        (integers.vector << 2) >> 2;
    integer_negate.vector = -integers.vector;
    integer_not.vector = ~integers.vector;
    unsigned_values.lanes[0] = 0;
    unsigned_values.lanes[1] = 1;
    unsigned_values.lanes[2] = 0x80000000u;
    unsigned_values.lanes[3] = 0xffffffffu;
    unsigned_comparison.vector =
        unsigned_values.vector > 1u;
    float indexed = input.vector[2];
    input.vector[2] = indexed + 1.0f;
    int result = 0;
    result |= identity_output.lanes[0] != 1.25f ? 1 : 0;
    result |= identity_output.lanes[1] != 2.5f ? 2 : 0;
    result |= identity_output.lanes[2] != 3.75f ? 4 : 0;
    result |= identity_output.lanes[3] != 5.0f ? 8 : 0;
    result |= output.lanes[0] != 1.25f ? 16 : 0;
    result |= output.lanes[1] != 2.5f ? 32 : 0;
    result |= output.lanes[2] != 3.75f ? 64 : 0;
    result |= output.lanes[3] != 5.0f ? 128 : 0;
    if (arithmetic.lanes[0] != 3.875f) return 1;
    if (arithmetic.lanes[1] != 5.75f) return 2;
    if (arithmetic.lanes[2] != 7.625f) return 3;
    if (arithmetic.lanes[3] != 9.5f) return 4;
    if (negated.lanes[2] != -3.75f) return 5;
    if (integer_result.lanes[0] != 7) return 6;
    if (integer_result.lanes[1] != 4) return 7;
    if (integer_result.lanes[2] != 9) return 8;
    if (integer_result.lanes[3] != 14) return 9;
    if (comparison.lanes[0] != 0) return 10;
    if (comparison.lanes[1] != 0) return 11;
    if (comparison.lanes[2] != -1) return 12;
    if (comparison.lanes[3] != -1) return 13;
    if (float_comparison.lanes[0] != 0) return 14;
    if (float_comparison.lanes[1] != 0) return 15;
    if (float_comparison.lanes[2] != -1) return 16;
    if (float_comparison.lanes[3] != -1) return 17;
    if (integer_math.lanes[0] != 3) return 18;
    if (integer_math.lanes[1] != 1) return 19;
    if (integer_math.lanes[2] != 4) return 20;
    if (integer_math.lanes[3] != 2) return 21;
    if (integer_shift.lanes[2] != 9) return 22;
    if (integer_negate.lanes[3] != -12) return 23;
    if (integer_not.lanes[0] != -4) return 24;
    if (unsigned_comparison.lanes[0] != 0) return 25;
    if (unsigned_comparison.lanes[1] != 0) return 26;
    if (unsigned_comparison.lanes[2] != -1) return 27;
    if (unsigned_comparison.lanes[3] != -1) return 28;
    if (indexed != 3.75f) return 29;
    if (input.vector[2] != 4.75f) return 30;
    return result;
}
