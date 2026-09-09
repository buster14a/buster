unsigned long long switch_u32(unsigned long long x)
{
    unsigned long long result = 0;
    switch ((unsigned)x)
    {
        case 0x7fffffffu: result = 1; break;
        case 0x80000000u: result = 2; break;
        case 0xb9000000u: result = 3; break;
        case 0xf9400000u: result = 4; break;
        case 0xffffffffu: result = 5; break;
        default: break;
    }
    return result;
}

unsigned long long switch_u64(unsigned long long x)
{
    unsigned long long result = 0;
    switch (x)
    {
        case 0x7fffffffull: result = 1; break;
        case 0x80000000ull: result = 2; break;
        case 0xb9000000ull: result = 3; break;
        case 0xf9400000ull: result = 4; break;
        case 0xffffffffull: result = 5; break;
        case 0xffffffff80000000ull: result = 6; break;
        case 0xffffffffffffffffull: result = 7; break;
        default: break;
    }
    return result;
}
