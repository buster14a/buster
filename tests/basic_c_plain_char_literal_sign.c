// C11 6.4.4.4p10: a plain single-character constant has the value a plain
// char object with that byte would have -- signed on this target, so
// '\x80' is -128.  pickle's opcode enum spells `PROTO = '\x80'` and the
// unpickler switches a signed char over it: the unsigned reading made
// every protocol-2+ stream "invalid load key".  Multi-character constants
// keep the concatenated spelling, and the prefixed forms keep their own
// types.

enum opcode
{
    PROTO = '\x80',
    STOP = '.',
};

int main(void)
{
    char byte = (char)0x80;
    if ((int)'\x80' != -128 || (int)'\xff' != -1 || (int)'\x7f' != 127)
    {
        return 1;
    }
    if ((enum opcode)byte != PROTO)
    {
        return 2;
    }
    if ((int)'ab' != 24930 || (int)L'\x80' != 128)
    {
        return 3;
    }
#if '\x80' >= 0
    return 4;
#endif
    switch ((enum opcode)byte)
    {
    case PROTO:
        return 0;
    default:
        return 5;
    }
}
