/* Preserve both limbs through canonical joins, mixed loop parameters,
   parallel swap/rotation cycles, repeated switch targets and computed gotos.
   Pointer signatures keep this independent of platform i128 call ABIs. */
typedef unsigned __int128 U128;
typedef __int128 I128;

void wide_choose(U128* out, U128 const* in, int n)
{
    *out = n ? in[0] : in[1];
}

void wide_signed(U128* out, U128 const* in, int n)
{
    I128 value = n ? (I128)in[0] : (I128)in[1];
    *out = (U128)value;
}

void wide_nested(U128* out, U128 const* in, int n)
{
    *out = (n & 1) ? ((n & 2) ? in[0] : in[1]) : ((n & 2) ? in[2] : in[3]);
}

void wide_swap(U128* out, U128 const* in, int n)
{
    U128 a = in[0];
    U128 b = in[1];
    while (n > 0)
    {
        U128 temporary = a;
        a = b;
        b = temporary;
        n -= 1;
    }
    out[0] = a;
    out[1] = b;
}

void wide_rotate(U128* out, U128 const* in, int n)
{
    U128 a = in[0];
    U128 b = in[1];
    U128 c = in[2];
    int count = 0;
    while (count < n)
    {
        U128 temporary = a;
        a = b;
        b = c;
        c = temporary;
        count += 1;
    }
    out[0] = a;
    out[1] = b;
    out[2] = c;
    out[3] = (U128)count;
}

void wide_switch(U128* out, U128 const* in, int n)
{
    U128 value = in[0];
    switch (n)
    {
    case 0:
    case 2: value = in[1]; break;
    case 1:
    case 3: value = in[2]; break;
    default: break;
    }
    *out = value;
}

void wide_indirect(U128* out, U128 const* in, int n)
{
    U128 value = in[0];
    void* destination = n ? &&replace : &&done;
    goto *destination;
replace:
    value = in[1];
done:
    /* Exercise a conditional value after the indirect branch. */
    *out = (n & 2) ? value : in[2];
}

int main(void)
{
    U128 in[4];
    in[0] = ((U128)0x123456789abcdef0ULL << 64) | 0xfedcba9876543210ULL;
    in[1] = ((U128)0x8070605040302010ULL << 64) | 0x0102030405060708ULL;
    in[2] = (U128)1 << 127;
    in[3] = ((U128)0xffffffffffffffffULL << 64) | 0xffffffffffffffffULL;
    U128 out[4];
    int failed = 0;
    for (int n = 0; n < 18; n += 1)
    {
        wide_choose(out, in, n);
        failed |= out[0] != in[n ? 0 : 1];
        wide_signed(out, in, n);
        failed |= out[0] != in[n ? 0 : 1];
        wide_nested(out, in, n);
        int selected = (n & 1) ? ((n & 2) ? 0 : 1) : ((n & 2) ? 2 : 3);
        failed |= out[0] != in[selected];
        wide_swap(out, in, n);
        failed |= out[0] != in[n & 1] || out[1] != in[(n & 1) ^ 1];
        wide_rotate(out, in, n);
        failed |= out[0] != in[n % 3] || out[1] != in[(n + 1) % 3] || out[2] != in[(n + 2) % 3];
        failed |= out[3] != (U128)n;
        wide_switch(out, in, n);
        failed |= out[0] != in[n < 4 ? 1 + (n & 1) : 0];
        wide_indirect(out, in, n);
        failed |= out[0] != in[(n & 2) ? (n ? 1 : 0) : 2];
    }
    return failed;
}
