typedef unsigned long Size;

enum : Size
{
    FIXED_ENUM_NEGATIVE_ONE = -1UL,
    FIXED_ENUM_NEGATIVE_TWO = -2UL,
};

enum FixedByte : unsigned char
{
    FIXED_BYTE_ZERO,
    FIXED_BYTE_ONE,
};

int main(void)
{
    return !(
        sizeof(enum FixedByte) == 1 &&
        (unsigned long)FIXED_ENUM_NEGATIVE_ONE ==
            (unsigned long)-1);
}
