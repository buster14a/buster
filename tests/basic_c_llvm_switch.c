/* This translation unit is emitted as Buster bitcode. The oracle is compiled
   separately by Clang so incorrectly routed expectations cannot hide a bug. */
int llvm_switch_dense(int value)
{
    int result;
    switch (value)
    {
    case 0: result = 11; break;
    case 1: result = 23; break;
    case 2: result = 37; break;
    default: result = 53; break;
    }
    return result;
}

int llvm_switch_grouped(int value)
{
    int result = 3;
    switch (value)
    {
    case 0:
    case 1:
        result += 4;
        /* fall through */
    case 2:
        result += 8;
        break;
    default:
        result = 31;
        break;
    case 4:
        result = 47;
        break;
    }
    return result;
}

int llvm_switch_no_default(int value)
{
    int result = 17;
    switch (value)
    {
    case -7: result = 19; break;
    case 5: result = 23; break;
    }
    return result;
}

int llvm_switch_default_only(int value)
{
    int result;
    switch (value)
    {
    default: result = 59; break;
    }
    return result;
}

int llvm_switch_nested(int outer, int inner)
{
    int result;
    switch (outer)
    {
    case 1:
        switch (inner)
        {
        case -3: result = 61; break;
        case 4: result = 67; break;
        default: result = 71; break;
        }
        break;
    case -2: result = 73; break;
    default: result = 79; break;
    }
    return result;
}

int llvm_switch_wide_unsigned(unsigned long long value)
{
    int result;
    switch (value)
    {
    case 0x10000000007ULL: result = 83; break;
    case 0x20000000011ULL: result = 89; break;
    default: result = 97; break;
    }
    return result;
}

int llvm_switch_wide_signed(long long value)
{
    int result;
    switch (value)
    {
    case -0x10000000007LL: result = 101; break;
    case 0x20000000011LL: result = 103; break;
    default: result = 107; break;
    }
    return result;
}
