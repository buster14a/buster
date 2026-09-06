// C11 6.4.4.4p10: a plain single-character constant has the value a plain
// char object with that byte would have. Exercise both target choices:
// System V x86-64 uses signed char while AAPCS64 uses unsigned char.
// Multi-character constants keep the concatenated spelling, and prefixed
// forms keep their own types.
#include <limits.h>

enum opcode
{
    PROTO = '\x80',
    STOP = '.',
};

int main(void)
{
    char byte = (char)0x80;
#if CHAR_MIN == 0
    if ((int)'\x80' != 128 || (int)'\xff' != 255 || (int)'\x7f' != 127)
    {
        return 1;
    }
#if '\x80' < 0
    return 4;
#endif
#else
    if ((int)'\x80' != -128 || (int)'\xff' != -1 || (int)'\x7f' != 127)
    {
        return 1;
    }
#if '\x80' >= 0
    return 4;
#endif
#endif
    if ((enum opcode)byte != PROTO)
    {
        return 2;
    }
    if ((int)'ab' != 24930 || (int)L'\x80' != 128)
    {
        return 3;
    }
    switch ((enum opcode)byte)
    {
    case PROTO:
        return 0;
    default:
        return 5;
    }
}
