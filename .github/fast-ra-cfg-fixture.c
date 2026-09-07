// All-mode allocator regression: calls under pressure, fixed-register divide
// and shifts, cyclic loop assignments, joins/critical edges, and switch fanout.
// Unsigned arithmetic makes every wrap and shift defined. The golden checksum
// is checked against independent Clang -O0/-O2 and GCC builds in the audit.
typedef unsigned long long U;
static volatile U inputs[24];
static volatile U calls;

__attribute__((noinline)) static U call_mix(U a, U b, U c)
{
    calls += 1;
    return (a * 0x9e3779b97f4a7c15ULL) ^ (b + (c << 7));
}

__attribute__((noinline)) static U pressure(U seed)
{
    U v0 = inputs[0] + seed * 1ULL;
    U v1 = inputs[1] + seed * 2ULL;
    U v2 = inputs[2] + seed * 3ULL;
    U v3 = inputs[3] + seed * 4ULL;
    U v4 = inputs[4] + seed * 5ULL;
    U v5 = inputs[5] + seed * 6ULL;
    U v6 = inputs[6] + seed * 7ULL;
    U v7 = inputs[7] + seed * 8ULL;
    U v8 = inputs[8] + seed * 9ULL;
    U v9 = inputs[9] + seed * 10ULL;
    U v10 = inputs[10] + seed * 11ULL;
    U v11 = inputs[11] + seed * 12ULL;
    U v12 = inputs[12] + seed * 13ULL;
    U v13 = inputs[13] + seed * 14ULL;
    U v14 = inputs[14] + seed * 15ULL;
    U v15 = inputs[15] + seed * 16ULL;
    U v16 = inputs[16] + seed * 17ULL;
    U v17 = inputs[17] + seed * 18ULL;
    U v18 = inputs[18] + seed * 19ULL;
    U v19 = inputs[19] + seed * 20ULL;
    U v20 = inputs[20] + seed * 21ULL;
    U v21 = inputs[21] + seed * 22ULL;
    U v22 = inputs[22] + seed * 23ULL;
    U v23 = inputs[23] + seed * 24ULL;
    U result = call_mix(seed, v0, v23);
    result = (result ^ (v0 << 0)) + v0 / ((seed & 15ULL) + 1ULL);
    result = (result ^ (v1 << 1)) + v1 / ((seed & 15ULL) + 1ULL);
    result = (result ^ (v2 << 2)) + v2 / ((seed & 15ULL) + 1ULL);
    result = (result ^ (v3 << 3)) + v3 / ((seed & 15ULL) + 1ULL);
    result = (result ^ (v4 << 4)) + v4 / ((seed & 15ULL) + 1ULL);
    result = (result ^ (v5 << 5)) + v5 / ((seed & 15ULL) + 1ULL);
    result = (result ^ (v6 << 6)) + v6 / ((seed & 15ULL) + 1ULL);
    result = (result ^ (v7 << 7)) + v7 / ((seed & 15ULL) + 1ULL);
    result = (result ^ (v8 << 8)) + v8 / ((seed & 15ULL) + 1ULL);
    result = (result ^ (v9 << 9)) + v9 / ((seed & 15ULL) + 1ULL);
    result = (result ^ (v10 << 10)) + v10 / ((seed & 15ULL) + 1ULL);
    result = (result ^ (v11 << 0)) + v11 / ((seed & 15ULL) + 1ULL);
    result = (result ^ (v12 << 1)) + v12 / ((seed & 15ULL) + 1ULL);
    result = (result ^ (v13 << 2)) + v13 / ((seed & 15ULL) + 1ULL);
    result = (result ^ (v14 << 3)) + v14 / ((seed & 15ULL) + 1ULL);
    result = (result ^ (v15 << 4)) + v15 / ((seed & 15ULL) + 1ULL);
    result = (result ^ (v16 << 5)) + v16 / ((seed & 15ULL) + 1ULL);
    result = (result ^ (v17 << 6)) + v17 / ((seed & 15ULL) + 1ULL);
    result = (result ^ (v18 << 7)) + v18 / ((seed & 15ULL) + 1ULL);
    result = (result ^ (v19 << 8)) + v19 / ((seed & 15ULL) + 1ULL);
    result = (result ^ (v20 << 9)) + v20 / ((seed & 15ULL) + 1ULL);
    result = (result ^ (v21 << 10)) + v21 / ((seed & 15ULL) + 1ULL);
    result = (result ^ (v22 << 0)) + v22 / ((seed & 15ULL) + 1ULL);
    result = (result ^ (v23 << 1)) + v23 / ((seed & 15ULL) + 1ULL);
    return result;
}

__attribute__((noinline)) static U cfg(U n, U salt)
{
    U a = salt + n, b = salt ^ (n + 17ULL);
    U slots[4] = {n, salt, n + salt, n ^ salt};
    U* p = slots;
    for (unsigned i = 0; i < 11; i += 1)
    {
        if ((a >> (i & 7u)) & 1ULL)
        {
            a ^= b;
            p = slots + 1;
            if (a & 4ULL) goto join;
            b += n;
        }
        else
        {
            p = slots + 2;
            b ^= a + i;
        }
join:
        {
            U old_a = a;
            a = b;
            b = old_a + i + *p;
        }
        if (b & 8ULL) continue;
        a += call_mix(a, b, salt);
    }
    return a ^ (b >> (n & 63ULL));
}

__attribute__((noinline)) static U dispatch(U n, U value)
{
    switch (n & 31ULL)
    {
    case 0: value = (value + 3ULL) ^ (value >> 1); break;
    case 1: value = (value + 20ULL) ^ (value >> 2); break;
    case 2: value = (value + 37ULL) ^ (value >> 3); break;
    case 3: value = (value + 54ULL) ^ (value >> 4); break;
    case 4: value = (value + 71ULL) ^ (value >> 5); break;
    case 5: value = (value + 88ULL) ^ (value >> 6); break;
    case 6: value = (value + 105ULL) ^ (value >> 7); break;
    case 7: value = (value + 122ULL) ^ (value >> 1); break;
    case 8: value = (value + 139ULL) ^ (value >> 2); break;
    case 9: value = (value + 156ULL) ^ (value >> 3); break;
    case 10: value = (value + 173ULL) ^ (value >> 4); break;
    case 11: value = (value + 190ULL) ^ (value >> 5); break;
    case 12: value = (value + 207ULL) ^ (value >> 6); break;
    case 13: value = (value + 224ULL) ^ (value >> 7); break;
    case 14: value = (value + 241ULL) ^ (value >> 1); break;
    case 15: value = (value + 258ULL) ^ (value >> 2); break;
    case 16: value = (value + 275ULL) ^ (value >> 3); break;
    case 17: value = (value + 292ULL) ^ (value >> 4); break;
    case 18: value = (value + 309ULL) ^ (value >> 5); break;
    case 19: value = (value + 326ULL) ^ (value >> 6); break;
    case 20: value = (value + 343ULL) ^ (value >> 7); break;
    case 21: value = (value + 360ULL) ^ (value >> 1); break;
    case 22: value = (value + 377ULL) ^ (value >> 2); break;
    case 23: value = (value + 394ULL) ^ (value >> 3); break;
    default: value ^= 0x3141592653589793ULL; break;
    }
    return value;
}

__attribute__((noinline)) static U split_work(U seed)
{
    U a = seed + 3, b = seed ^ 13;
    for (unsigned i = 0; i < 31; i += 1)
    {
        a += b ^ i;
        b = (b << 1) ^ (a >> 2);
    }
    U boundary = call_mix(a, b, seed);
    for (unsigned i = 0; i < 9; i += 1)
    {
        a ^= boundary + b;
        b += a / ((boundary & 7ULL) + 1ULL);
    }
    return a + b + boundary;
}

int main(void)
{
    U checksum = 0xcbf29ce484222325ULL;
    for (unsigned i = 0; i < 24; i += 1) inputs[i] = 0x123456789abcdef0ULL ^ (i * 7919ULL);
    for (U n = 0; n < 32; n += 1)
    {
        U value = pressure(n + 1);
        value ^= cfg(n, value);
        value += dispatch(n, value);
        value ^= split_work(value);
        checksum = (checksum ^ value) * 0x100000001b3ULL;
    }
    return checksum != 10881790925525684737ULL || calls != 246ULL;
}
