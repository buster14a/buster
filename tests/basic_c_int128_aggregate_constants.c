// Pin bytes independently of the compiler's wide-integer expression lowering.
typedef __int128 i128;
typedef unsigned __int128 u128;
static i128 signed_array[] = {((i128)1 << 100), (i128)0xffffffffffffffffULL, -((i128)1 << 100), -((i128)1 << 126) * 2};
static u128 unsigned_array[] = {((u128)1 << 100), (u128)0xffffffffffffffffULL, (u128)-1, (u128)1 << 127};
struct signed_members { i128 a, b, c, d; };
struct unsigned_members { u128 a, b, c, d; };
static struct signed_members signed_struct = {((i128)1 << 100), (i128)0xffffffffffffffffULL, -((i128)1 << 100), -((i128)1 << 126) * 2};
static struct unsigned_members unsigned_struct = {((u128)1 << 100), (u128)0xffffffffffffffffULL, (u128)-1, (u128)1 << 127};

static const unsigned char expected_signed[64] = {
    0,0,0,0,0,0,0,0,0,0,0,0,0x10,0,0,0,
    255,255,255,255,255,255,255,255,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0xf0,255,255,255,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0x80,
};
static const unsigned char expected_unsigned[64] = {
    0,0,0,0,0,0,0,0,0,0,0,0,0x10,0,0,0,
    255,255,255,255,255,255,255,255,0,0,0,0,0,0,0,0,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0x80,
};

int main(void)
{
    unsigned char *sa = (unsigned char *)signed_array;
    unsigned char *ua = (unsigned char *)unsigned_array;
    unsigned char *ss = (unsigned char *)&signed_struct;
    unsigned char *us = (unsigned char *)&unsigned_struct;
    int result = 0;
    for (int i = 0; i < 64; i += 1)
    {
        if (sa[i] != expected_signed[i] || ss[i] != expected_signed[i]) result = 1;
        if (ua[i] != expected_unsigned[i] || us[i] != expected_unsigned[i]) result = 2;
    }
    return result;
}
