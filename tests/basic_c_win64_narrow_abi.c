// The Win64 call shapes that clang picks for values narrower than one vector
// register, crossing real call boundaries in both directions. MSVC has no
// generic vector extension and no __int128, so clang's answers are the
// de-facto ABI here; each was measured against clang 22 with
// --target=x86_64-pc-windows-msvc before being implemented:
//
//   - A single-lane vector travels exactly like its scalar element: integer
//     lanes ride the positional GPR and come back in RAX, float lanes ride
//     the positional XMM register and come back in XMM0. Buster used to pass
//     every Win64 vector by reference, so these four families disagreed with
//     clang in both directions.
//   - A multi-lane vector under eight bytes is an indirect argument and a
//     direct XMM0 result. The result half is what buster used to refuse: it
//     returned anything narrower than eight bytes by reference.
//   - __int128 is an indirect argument and an XMM0 result. Buster used to
//     return it through the caller's hidden pointer.
//
// The fixture is deliberately callee-in-another-function: a round trip
// through one identity function cannot tell an argument-side disagreement
// from a return-side one, so every family checks the two halves separately.
// tests/c_abi_* pairs the same shapes against clang objects for real; this
// fixture is the in-tree regression that runs on every platform.
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;

typedef u8 Vector1U8 __attribute__((vector_size(1)));
typedef u16 Vector1U16 __attribute__((vector_size(2)));
typedef u32 Vector1U32 __attribute__((vector_size(4)));
typedef u64 Vector1U64 __attribute__((vector_size(8)));
typedef float Vector1F32 __attribute__((vector_size(4)));
typedef double Vector1F64 __attribute__((vector_size(8)));
typedef u8 Vector2U8 __attribute__((vector_size(2)));
typedef u8 Vector4U8 __attribute__((vector_size(4)));
typedef u16 Vector2U16 __attribute__((vector_size(4)));
typedef float Vector2F32 __attribute__((vector_size(8)));

// Each `take_` callee reads an argument the caller placed; each `make_`
// callee produces a result the caller reads back. A second scalar parameter
// follows the vector so a family that miscounts registers or eightbytes
// shifts the scalar and is caught even when the vector lanes survive.
static int take_vector_1_u8(Vector1U8 value, int tag)
{
    return value[0] == 3 && tag == 11;
}

static Vector1U8 make_vector_1_u8(void)
{
    return (Vector1U8){5};
}

static int take_vector_1_u16(Vector1U16 value, int tag)
{
    return value[0] == 0x1234 && tag == 12;
}

static Vector1U16 make_vector_1_u16(void)
{
    return (Vector1U16){0x5678};
}

static int take_vector_1_u32(Vector1U32 value, int tag)
{
    return value[0] == 0x11223344u && tag == 13;
}

static Vector1U32 make_vector_1_u32(void)
{
    return (Vector1U32){0x55667788u};
}

static int take_vector_1_u64(Vector1U64 value, int tag)
{
    return value[0] == 0x1122334455667788ull && tag == 14;
}

static Vector1U64 make_vector_1_u64(void)
{
    return (Vector1U64){0x99aabbccddeeff00ull};
}

static int take_vector_1_f32(Vector1F32 value, int tag)
{
    return value[0] == 2.5f && tag == 15;
}

static Vector1F32 make_vector_1_f32(void)
{
    return (Vector1F32){7.25f};
}

static int take_vector_1_f64(Vector1F64 value, int tag)
{
    return value[0] == 2.5 && tag == 16;
}

static Vector1F64 make_vector_1_f64(void)
{
    return (Vector1F64){7.25};
}

static int take_vector_2_u8(Vector2U8 value, int tag)
{
    return value[0] == 1 && value[1] == 2 && tag == 17;
}

static Vector2U8 make_vector_2_u8(void)
{
    return (Vector2U8){3, 4};
}

static int take_vector_4_u8(Vector4U8 value, int tag)
{
    return value[0] == 1 && value[1] == 2 && value[2] == 3 && value[3] == 4 && tag == 18;
}

static Vector4U8 make_vector_4_u8(void)
{
    return (Vector4U8){5, 6, 7, 8};
}

static int take_vector_2_u16(Vector2U16 value, int tag)
{
    return value[0] == 0x1111 && value[1] == 0x2222 && tag == 19;
}

static Vector2U16 make_vector_2_u16(void)
{
    return (Vector2U16){0x3333, 0x4444};
}

static int take_vector_2_f32(Vector2F32 value, int tag)
{
    return value[0] == 1.5f && value[1] == 2.5f && tag == 20;
}

static Vector2F32 make_vector_2_f32(void)
{
    return (Vector2F32){3.5f, 4.5f};
}

static int take_int128(__int128 value, int tag)
{
    return (u64)value == 0x0123456789abcdefull && (u64)(value >> 64) == 0x1122334455667788ull && tag == 21;
}

static __int128 make_int128(void)
{
    return ((__int128)0x99aabbccddeeff00ull << 64) | (__int128)0x0f1e2d3c4b5a6978ull;
}

// Two 128-bit arguments in a row: the second one is what catches a callee
// that read its indirect reference from the wrong register.
static int take_int128_pair(__int128 first, __int128 second, int tag)
{
    return (u64)first == 1 && (u64)(first >> 64) == 2 && (u64)second == 3 && (u64)(second >> 64) == 4 && tag == 22;
}

int main(void)
{
    if (!take_vector_1_u8((Vector1U8){3}, 11) || make_vector_1_u8()[0] != 5)
    {
        return 1;
    }
    if (!take_vector_1_u16((Vector1U16){0x1234}, 12) || make_vector_1_u16()[0] != 0x5678)
    {
        return 2;
    }
    if (!take_vector_1_u32((Vector1U32){0x11223344u}, 13) || make_vector_1_u32()[0] != 0x55667788u)
    {
        return 3;
    }
    if (!take_vector_1_u64((Vector1U64){0x1122334455667788ull}, 14) || make_vector_1_u64()[0] != 0x99aabbccddeeff00ull)
    {
        return 4;
    }
    if (!take_vector_1_f32((Vector1F32){2.5f}, 15) || make_vector_1_f32()[0] != 7.25f)
    {
        return 5;
    }
    if (!take_vector_1_f64((Vector1F64){2.5}, 16) || make_vector_1_f64()[0] != 7.25)
    {
        return 6;
    }
    Vector2U8 two_bytes = make_vector_2_u8();
    if (!take_vector_2_u8((Vector2U8){1, 2}, 17) || two_bytes[0] != 3 || two_bytes[1] != 4)
    {
        return 7;
    }
    Vector4U8 four_bytes = make_vector_4_u8();
    if (!take_vector_4_u8((Vector4U8){1, 2, 3, 4}, 18) || four_bytes[0] != 5 || four_bytes[1] != 6 || four_bytes[2] != 7 ||
        four_bytes[3] != 8)
    {
        return 8;
    }
    Vector2U16 two_shorts = make_vector_2_u16();
    if (!take_vector_2_u16((Vector2U16){0x1111, 0x2222}, 19) || two_shorts[0] != 0x3333 || two_shorts[1] != 0x4444)
    {
        return 9;
    }
    Vector2F32 two_floats = make_vector_2_f32();
    if (!take_vector_2_f32((Vector2F32){1.5f, 2.5f}, 20) || two_floats[0] != 3.5f || two_floats[1] != 4.5f)
    {
        return 10;
    }
    __int128 wide_argument = ((__int128)0x1122334455667788ull << 64) | (__int128)0x0123456789abcdefull;
    if (!take_int128(wide_argument, 21))
    {
        return 11;
    }
    __int128 wide_result = make_int128();
    if ((u64)wide_result != 0x0f1e2d3c4b5a6978ull || (u64)(wide_result >> 64) != 0x99aabbccddeeff00ull)
    {
        return 12;
    }
    __int128 first = ((__int128)2 << 64) | (__int128)1;
    __int128 second = ((__int128)4 << 64) | (__int128)3;
    if (!take_int128_pair(first, second, 22))
    {
        return 13;
    }
    return 0;
}
