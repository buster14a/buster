// The x86-64 byte rows of the two atomic read-modify-write forms. `xchg r/m8,
// r8` is opcode 0x86 and `cmpxchg r/m8, r8` is 0x0F 0xB0 -- each a different
// metadata form from its 0x87 / 0x0F 0xB1 sibling rather than an operand-size
// variant of it -- so a width-1 sequentially consistent atomic store or
// compare-exchange selected on the machine path and then failed to encode,
// falling back to the canonical emitter (issue #806). The driver test asserts
// the fallback count is zero for this file under every register allocator;
// running it is what says the byte encodings are also correct.
//
// The many-argument shapes are here on purpose: a byte source in SIL, DIL, SPL
// or BPL needs a REX prefix that the low four registers do not, and an
// allocator is free to pick one.

_Atomic char atomic_char;
_Atomic signed char atomic_signed_char;
_Atomic unsigned char atomic_unsigned_char;
_Atomic short atomic_short;
_Atomic int atomic_int;

void store_char(char value)
{
    atomic_char = value;
}

char exchange_char(char value)
{
    return __c11_atomic_exchange(&atomic_char, value, __ATOMIC_SEQ_CST);
}

// Six by-value arguments push the later ones through the registers whose byte
// halves need a REX prefix.
char store_from_sixth(char a, char b, char c, char d, char e, char f)
{
    atomic_char = f;
    atomic_signed_char = (signed char)e;
    atomic_unsigned_char = (unsigned char)d;
    return (char)(a + b + c);
}

int main(void)
{
    for (int value = -128; value < 128; value += 1)
    {
        store_char((char)value);
        if ((char)atomic_char != (char)value)
        {
            return 1;
        }
    }
    atomic_char = 5;
    if (exchange_char(9) != 5 || (char)atomic_char != 9)
    {
        return 2;
    }
    {
        char expected = 9;
        if (!__c11_atomic_compare_exchange_strong(&atomic_char, &expected, (char)11, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
        {
            return 3;
        }
    }
    {
        char expected = 3;
        if (__c11_atomic_compare_exchange_strong(&atomic_char, &expected, (char)1, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST) || expected != 11)
        {
            return 4;
        }
    }
    if (__c11_atomic_fetch_add(&atomic_char, (char)2, __ATOMIC_SEQ_CST) != 11 || (char)atomic_char != 13)
    {
        return 5;
    }
    if (store_from_sixth(1, 2, 3, (char)200, (char)-7, (char)42) != 6)
    {
        return 6;
    }
    if ((char)atomic_char != 42 || (signed char)atomic_signed_char != -7 || (unsigned char)atomic_unsigned_char != 200u)
    {
        return 7;
    }
    // The wider widths keep using the sibling rows, and must still work.
    atomic_short = 300;
    {
        short expected = 300;
        if (!__c11_atomic_compare_exchange_strong(&atomic_short, &expected, (short)-1, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST) || (short)atomic_short != -1)
        {
            return 8;
        }
    }
    atomic_int = 70000;
    {
        int expected = 70000;
        if (!__c11_atomic_compare_exchange_strong(&atomic_int, &expected, 1, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST) || (int)atomic_int != 1)
        {
            return 9;
        }
    }
    return 0;
}
