// QuickJS spells the interpreter's arithmetic right shift as
// `(int)JS_VALUE_GET_INT(op1) >> v2` with `uint32_t v2`.  A shift takes no
// common type: C promotes each operand on its own and the result has the
// promoted left operand's type (C11 6.5.7p3).  Merging the two made the
// unsigned count decide the shift, so `-4 >> 1` answered 2147483646 and every
// JavaScript `>>` behaved like `>>>`.
static int shift_by_unsigned(int value, unsigned count)
{
    return value >> count;
}

static int shift_by_unsigned_long(int value, unsigned long count)
{
    return value >> count;
}

static long long shift_wide_by_unsigned_char(long long value, unsigned char count)
{
    return value >> count;
}

static unsigned shift_unsigned_by_signed(unsigned value, int count)
{
    return value >> count;
}

static int shift_left_by_unsigned(int value, unsigned count)
{
    return value << count;
}

int main(void)
{
    if (shift_by_unsigned(-4, 1) != -2) return 1;
    if (shift_by_unsigned(-1, 3) != -1) return 2;
    if (shift_by_unsigned(4, 1) != 2) return 3;
    if (shift_by_unsigned_long(-4, 1) != -2) return 4;
    if (shift_wide_by_unsigned_char(-16, 2) != -4) return 5;
    if (shift_unsigned_by_signed(0xfffffffcu, 1) != 0x7ffffffeu) return 6;
    if (shift_left_by_unsigned(-4, 1) != -8) return 7;
    // The count's own type never widens the result either.
    unsigned long long wide_count = 1;
    int value = -4;
    if ((value >> wide_count) != -2) return 8;
    if (sizeof(value >> wide_count) != sizeof(int)) return 9;
    unsigned short narrow = 0xfffc;
    if ((narrow >> 1) != 32766) return 10;
    return 0;
}
