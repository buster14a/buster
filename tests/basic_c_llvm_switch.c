// Switch destinations must stay paired with their keys through bitcode emission.
int switch_distinct(int value)
{
    int result;
    switch (value)
    {
    case 0: result = 11; break;
    case 1: result = 23; break;
    default: result = 37; break;
    }
    return result;
}

int switch_grouped(int value)
{
    int result = 3;
    switch (value)
    {
    case -3:
    case 2: result += 7; break;
    case 4: result += 11; // fall through
    case 5: result += 13; break;
    default: result += 17; break;
    }
    return result;
}

int switch_no_default(int value)
{
    int result = 41;
    switch (value)
    {
    case 2: result = 43; break;
    case 7: result = 47; break;
    }
    return result;
}

int switch_default_only(int value)
{
    int result;
    switch (value)
    {
    default: result = 53; break;
    }
    return result;
}

int switch_empty(int value)
{
    switch (value++)
    {
    }
    return value;
}

int switch_nested(int outer, int inner)
{
    int result;
    switch (outer)
    {
    case -1:
        switch (inner)
        {
        case -2: result = 59; break;
        case 3: result = 61; break;
        default: result = 67; break;
        }
        break;
    case 4: result = 71; break;
    default: result = 73; break;
    }
    return result;
}

int switch_signed(long long value)
{
    int result;
    switch (value)
    {
    case -1099511627776LL: result = 79; break;
    case -1: result = 83; break;
    case 1099511627776LL: result = 89; break;
    default: result = 97; break;
    }
    return result;
}

int switch_unsigned(unsigned long long value)
{
    int result;
    switch (value)
    {
    case 0: result = 101; break;
    case 9223372036854775808ULL: result = 103; break;
    case 18446744073709551615ULL: result = 107; break;
    default: result = 109; break;
    }
    return result;
}
