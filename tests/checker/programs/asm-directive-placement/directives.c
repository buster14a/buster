// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #2351: template bytes, alignment and fixups remain correct when placed inside a function with surrounding code.
// The placement test separately requires eligible templates to have no helper: helper execution also passes here.

static int buster2351_data = 71;
extern const unsigned char buster2351_trailing[];
extern const unsigned char buster2351_only[];

static int aligned(int n)
{
    unsigned long first;
    unsigned long second;
    __asm__ volatile(".byte 0x90\n\t.p2align 6\n1:\n\tleaq 1b(%%rip), %0\n\t.byte 0x90\n\t.balign 16\n2:\n\tleaq 2b(%%rip), %1"
                     : "=&r"(first), "=r"(second));
    return n + ((first & 63) == 0) * 101 + ((second & 15) == 0) * 103;
}

static int data_bytes(int n)
{
    static const unsigned char expected[] = {
        0x23,
        0x45,
        0x89,
        0x67,
        0x01,
        0xef,
        0xcd,
        0xab,
        0x80,
        0x70,
        0x60,
        0x50,
        0x40,
        0x30,
        0x20,
        0x10,
        'A',
        'Z',
        'q',
        0,
        'r',
        's',
        0,
        0,
        0,
        0x5a,
        0x5a,
        0xa5,
    };
    const unsigned char* bytes;
    __asm__("jmp 2f\n1:\n\t.byte 0x23, 0x45\n\t.word 0x6789\n\t.long 0xabcdef01\n\t.quad 0x1020304050607080\n"
            "\t.ascii \"AZ\"\n\t.asciz \"q\"\n\t.string \"rs\"\n\t.zero 2\n\t.space 2, 0x5a\n\t.skip 1, 0xa5\n2:\n\tleaq 1b(%%rip), %0"
            : "=r"(bytes));
    int matched = 0;
    for (unsigned i = 0; i < sizeof(expected); i += 1)
    {
        matched += bytes[i] == expected[i];
    }
    return n + matched * 107;
}

static int relocated(int n)
{
    const unsigned long* table;
    __asm__("jmp 3f\n\t.p2align 3\n1:\n\t.quad buster2351_data\n2:\n\t.long 1b - .\n3:\n\tleaq 1b(%%rip), %0" : "=r"(table));
    const int* value = (const int*)table[0];
    const int distance = ((const int*)(table + 1))[0];
    return n + (value == &buster2351_data) * 109 + *value + (distance == -8) * 113;
}

static int short_branches(int n)
{
    long count = (n & 7) + 1;
    int value = n;
    __asm__("1:\n\taddl $3, %0\n\tjmp 2f\n\t.byte 0x90\n2:\n\tloop 1b" : "+r"(value), "+c"(count) : : "cc");
    const long available = n & 3;
    __asm__("jrcxz 2f\n\tjmp 1f\n1:\n\taddl $9, %0\n2:" : "+r"(value) : "c"(available) : "cc");
    return value + (int)count * 127;
}

static int address32(int n)
{
    int value;
    __asm__("leal 5(%1,%1,2), %0" : "=r"(value) : "r"(n));
    return value;
}

static int fill(int n)
{
    __asm__ volatile(".nops 5\n\t.nops 4, 1\n\t.byte 0x90\n\taddl $17, %0" : "+r"(n) : : "cc");
    return n;
}

static int section_data(int n)
{
    const unsigned* value;
    __asm__(".pushsection .rodata.buster2351, \"a\"\n\t.p2align 2\n1:\n\t.long 0x12345678\n\t.popsection\n\tleaq 1b(%%rip), %0" : "=r"(value));
    return n + (*value == 0x12345678) * 139;
}

static int trailing_alignment(int n)
{
    __asm__ volatile("nop\n\t.p2align 6");
    __asm__ volatile("buster2351_trailing:\n\tnop" : : : "memory");
    return n + (((unsigned long)buster2351_trailing & 63) == 0) * 131;
}

static int alignment_only(int n)
{
    __asm__ volatile(".p2align 6");
    __asm__ volatile("buster2351_only:\n\tnop" : : : "memory");
    return n + (((unsigned long)buster2351_only & 63) == 0) * 137;
}

static int stack_input(int n)
{
    register unsigned long sp __asm__("rsp");
    __asm__("" : "=r"(sp));
    const unsigned long before = sp;
    unsigned long copied;
    __asm__ volatile(".p2align 5\n\tmovq %1, %0" : "=r"(copied) : "r"(sp));
    return n + (copied == before) * 149;
}

typedef float v4sf __attribute__((vector_size(16)));

static int vector_data(int n)
{
    v4sf value = { (float)n, 1.5f, -2.0f, 3.0f };
    __asm__(".p2align 4\n\taddps %0, %0" : "+x"(value));
    return (int)(value[0] + value[1] * 3 + value[2] * 5 + value[3] * 7);
}

static int unreachable(int n)
{
    if (0)
    {
        __asm__ volatile(".p2align 6\n\t.byte 0x90");
        goto dropped;
    }
    return n + 151;
dropped:
    __asm__ volatile(".p2align 5\n\t.byte 0x90");
    return n + 157;
}

int tested(int n)
{
    int result = n * 3;
    for (int i = 0; i < 5; i += 1)
    {
        result += aligned(n + i) + data_bytes(n - i) + relocated(n + i) + short_branches(n - i) + address32(n + i) + fill(n - i) +
                  section_data(n + i) + trailing_alignment(n + i) + alignment_only(n - i) + stack_input(n + i) + vector_data(n - i) + unreachable(n + i);
    }
    return result;
}
