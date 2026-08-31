// CPython's interpreter trampoline spells every code unit
// `{ .op.code = NOP, .op.arg = 0 }`: two dotted designators into the same
// union member.  Designating a union member clears the union -- correct
// when the designator switches members, but both of these subobjects
// belong to one active member, so the second cleared the first and every
// trampoline opcode read back as CACHE; returning from the first Python
// frame executed the zeroed slot and aborted the interpreter.  The
// switching cases must still clear, and later same-subfield designators
// still override.
#include <stdio.h>

struct pair
{
    unsigned char code;
    unsigned char arg;
};

union unit
{
    unsigned short bits;
    struct pair op;
};

static const union unit trampoline[] = {
    {.op.code = 9, .op.arg = 0},
    {.op.code = 88, .op.arg = 0},
    {.op.code = 149, .op.arg = 3},
};

static const union unit switch_to_op = {.bits = 0xffff, .op.code = 9};
static const union unit switch_to_bits = {.op.code = 9, .bits = 0xff00};
static const union unit override_same = {.op.code = 9, .op.arg = 3, .op.code = 7};

int main(void)
{
    if (trampoline[0].bits != 0x0009 || trampoline[1].bits != 0x0058 || trampoline[2].bits != 0x0395)
    {
        return 1;
    }
    if (switch_to_op.bits != 0x0009 || switch_to_bits.bits != 0xff00 || override_same.bits != 0x0307)
    {
        return 2;
    }
    printf("union designator merge ok\n");
    return 0;
}
