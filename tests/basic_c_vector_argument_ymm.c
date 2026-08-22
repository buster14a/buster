// 256-bit vectors crossing a call boundary by value: SystemV classifies each
// as one 32-byte vector part, and the canonical emitter moves that part with
// the AVX form of VMOVDQU. The driver test builds this fixture with the
// default pipeline and again with -fno-register-allocator, because the
// canonical path is where the move's feature set was wrong: the emitter
// offered only "avx2" for an instruction whose metadata rows are AVX, so
// every 32-byte vector argument failed codegen whenever a function fell
// back to the canonical emitter, for every element type.
typedef int Int8 __attribute__((vector_size(32)));
typedef signed char Byte32 __attribute__((vector_size(32)));
typedef float Float8 __attribute__((vector_size(32)));
typedef double Double4 __attribute__((vector_size(32)));

typedef union Int8Storage
{
    Int8 vector;
    int lanes[8];
} Int8Storage;

typedef union Byte32Storage
{
    Byte32 vector;
    signed char lanes[32];
} Byte32Storage;

typedef union Float8Storage
{
    Float8 vector;
    float lanes[8];
} Float8Storage;

typedef union Double4Storage
{
    Double4 vector;
    double lanes[4];
} Double4Storage;

static Int8 int8_combine(Int8 left, Int8 right)
{
    return left + right;
}

static Byte32 byte32_double(Byte32 value)
{
    return value + value;
}

static Float8 float8_scale(Float8 value, Float8 scale)
{
    return value * scale;
}

static Double4 double4_pass(Double4 value)
{
    return value;
}

int main(void)
{
    int result = 0;
    unsigned int front_canary = 0x51C0FFEEu;
    Int8Storage ints_a;
    Int8Storage ints_b;
    unsigned int middle_canary = 0x0DDF00D5u;
    for (int lane = 0; lane < 8; lane += 1)
    {
        ints_a.lanes[lane] = lane + 1;
        ints_b.lanes[lane] = 10 * (lane + 1);
    }
    Int8Storage ints_sum;
    ints_sum.vector = int8_combine(ints_a.vector, ints_b.vector);
    for (int lane = 0; lane < 8 && !result; lane += 1)
    {
        if (ints_sum.lanes[lane] != 11 * (lane + 1))
        {
            result = 1;
        }
    }
    Byte32Storage bytes;
    for (int lane = 0; lane < 32; lane += 1)
    {
        bytes.lanes[lane] = (signed char)(lane % 40 - 20);
    }
    Byte32Storage bytes_doubled;
    bytes_doubled.vector = byte32_double(bytes.vector);
    for (int lane = 0; lane < 32 && !result; lane += 1)
    {
        if (bytes_doubled.lanes[lane] != (signed char)(2 * (lane % 40 - 20)))
        {
            result = 2;
        }
    }
    Float8Storage floats;
    Float8Storage scales;
    for (int lane = 0; lane < 8; lane += 1)
    {
        floats.lanes[lane] = (float)(lane + 1);
        scales.lanes[lane] = 3.0f;
    }
    Float8Storage floats_scaled;
    floats_scaled.vector = float8_scale(floats.vector, scales.vector);
    for (int lane = 0; lane < 8 && !result; lane += 1)
    {
        if (floats_scaled.lanes[lane] != 3.0f * (float)(lane + 1))
        {
            result = 3;
        }
    }
    Double4Storage doubles;
    for (int lane = 0; lane < 4; lane += 1)
    {
        doubles.lanes[lane] = (double)(100 + lane);
    }
    Double4Storage doubles_passed;
    doubles_passed.vector = double4_pass(doubles.vector);
    for (int lane = 0; lane < 4 && !result; lane += 1)
    {
        if (doubles_passed.lanes[lane] != (double)(100 + lane))
        {
            result = 4;
        }
    }
    if (!result && (front_canary != 0x51C0FFEEu || middle_canary != 0x0DDF00D5u))
    {
        result = 5;
    }
    return result;
}
