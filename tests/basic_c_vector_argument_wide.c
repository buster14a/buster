// Vectors wider than the model's widest register, crossing the Win64 call
// boundary by value in both directions. Win64 legalizes such a bare vector
// argument into one indirect reference per register-sized piece -- each piece
// its own argument slot, registers first and stack eightbytes after in the
// same call -- and returns such a value directly in up to four consecutive
// vector registers, through the caller's hidden pointer past that. The piece
// width follows the CPU model (16 bytes at baseline, 32 with AVX, 64 with
// AVX-512), so the same 128-byte vector here is eight references at baseline
// and two on znver5, and its return flips from the hidden pointer to zmm0-1.
//
// This fixture is Windows-only: no other convention accepts a vector
// signature type past 64 bytes yet, so the driver builds it solely for
// x86_64-pc-windows-msvc -- under wine at the host's model, and compile-only
// at the pinned models whose piece counts differ.

typedef unsigned char u8;
typedef double F64x16 __attribute__((vector_size(128)));
typedef u8 Byte256 __attribute__((vector_size(256)));

static F64x16 vector_make(double base)
{
    F64x16 value;
    for (int lane = 0; lane < 16; lane += 1)
    {
        value[lane] = base + (double)lane;
    }
    return value;
}

static F64x16 vector_identity(F64x16 value)
{
    return value;
}

// Two leading slots force the pieces to start mid-file; at baseline the tail
// pieces and the trailing scalar continue on the stack.
static double vector_straddle(int first, long long second, F64x16 value, int third)
{
    double sum = (double)first + (double)second + (double)third;
    for (int lane = 0; lane < 16; lane += 1)
    {
        sum += value[lane];
    }
    return sum;
}

// Three wide arguments exhaust the register file however wide the pieces
// are, so the last vector's references all travel as stack eightbytes.
static double vector_third(F64x16 a, F64x16 b, F64x16 c)
{
    double sum = 0;
    for (int lane = 0; lane < 16; lane += 1)
    {
        sum += c[lane] - a[lane] - b[lane];
    }
    return sum;
}

static Byte256 bytes_make(unsigned base)
{
    Byte256 value;
    for (int lane = 0; lane < 256; lane += 1)
    {
        value[lane] = (u8)(base + (unsigned)lane);
    }
    return value;
}

// A 256-byte result stays behind the hidden pointer below AVX-512 and comes
// back in zmm0-3 with it; the leading scalar keeps the slot file offset by
// one in the argument direction.
static Byte256 bytes_shift(int amount, Byte256 value)
{
    for (int lane = 0; lane < 256; lane += 1)
    {
        value[lane] = (u8)(value[lane] + (u8)amount);
    }
    return value;
}

int main(void)
{
    F64x16 identity = vector_identity(vector_make(1.0));
    for (int lane = 0; lane < 16; lane += 1)
    {
        if (identity[lane] != 1.0 + (double)lane)
        {
            return 1;
        }
    }

    // 11 + 22 + 33 + sum(2..17) = 66 + 152
    if (vector_straddle(11, 22, vector_make(2.0), 33) != 218.0)
    {
        return 2;
    }

    // sum over lanes of (5+l) - (3+l) - (4+l) = 16 * -2 - sum(0..15)
    if (vector_third(vector_make(3.0), vector_make(4.0), vector_make(5.0)) != -152.0)
    {
        return 3;
    }

    Byte256 shifted = bytes_shift(5, bytes_make(9));
    for (int lane = 0; lane < 256; lane += 1)
    {
        if (shifted[lane] != (u8)(14 + (unsigned)lane))
        {
            return 4;
        }
    }

    return 0;
}
