// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Tier 1's rematerialization (#178) through the C driver's --tier1. An original fixture's loop keeps more addresses and
// constants live than there are registers, each recipe among them: statics' addresses from rip, a static array's
// member's at an addend, extern variables' from their GOT slots, frame objects' addresses and a member's of one, and
// constants of each encoding (a zero, a 32-bit one, a sign-extended and a 64-bit one), each a value the C driver's typed
// stream converts (its pointers are 64-bit integers). A second fixture's loop divides by constants and by a register
// while such values stay live across the divisions, whose sequences take rax and rdx (#141). Clang's build of the same
// source is each one's oracle. Included by c_checker_driver_test.c after its ELF readers.

// The fixture: tier1_remat is the function under test, main prints its results.
static const char check_test_tier1_remat_source[] =
    "#include <stdio.h>\n"
    "#include <unistd.h>\n"
    "extern char** environ;\n"
    "static long s0, s1, s2, s3, s4, s5;\n"
    "static long sa[8];\n"
    "long tier1_remat(long n)\n"
    "{\n"
    "    long a0[2], a1[2], a2[2], a3[2], a4[2], a5[4];\n"
    "    long *p0 = a0, *p1 = a1, *p2 = a2, *p3 = a3, *p4 = a4, *p5 = &a5[1];\n"
    "    long *q0 = &s0, *q1 = &s1, *q2 = &s2, *q3 = &s3, *q4 = &s4, *q5 = &s5, *q6 = &sa[3];\n"
    "    char*** r0 = &environ;\n"
    "    int* r1 = &opterr;\n"
    "    int* r2 = &optind;\n"
    "    const long zero = 0;\n"
    "    const long middle = 0x87654321l;\n"
    "    const long wide = 0x123456789abcdefl;\n"
    "    const long negative = -0x76543210l;\n"
    "    long total = 0;\n"
    "    for (long k = 0; k < 2; k += 1)\n"
    "    {\n"
    "        p0[k] = k; p1[k] = k + 1; p2[k] = k + 2; p3[k] = k + 3; p4[k] = k + 4; p5[k] = k + 5;\n"
    "    }\n"
    "    a5[0] = 0;\n"
    "    a5[3] = 0;\n"
    "    for (long i = 0; i < n; i += 1)\n"
    "    {\n"
    "        const long k = i & 1;\n"
    "        p0[k] += i; p1[k] ^= i; p2[k] -= i; p3[k] += i * 3; p4[k] ^= i + 7; p5[k] += i ^ 5;\n"
    "        q0[0] += i; q1[0] ^= i; q2[0] -= i; q3[0] += i * 5; q4[0] ^= i + 3; q5[0] += i ^ 9; q6[k] += i;\n"
    "        total += (*r0 != 0) + *r1 + *r2;\n"
    "        total = total * 31 + wide;\n"
    "        total += middle;\n"
    "        p5[k ^ 1] = zero;\n"
    "        q6[k ^ 1] = negative;\n"
    "    }\n"
    "    return total + p0[0] + p0[1] + p1[0] + p1[1] + p2[0] + p2[1] + p3[0] + p3[1] + p4[0] + p4[1] + a5[0] + a5[1] + a5[2] + a5[3] +\n"
    "           s0 + s1 + s2 + s3 + s4 + s5 + sa[3] + sa[4];\n"
    "}\n"
    "int main(void)\n"
    "{\n"
    "    printf(\"%ld %ld %ld\\n\", tier1_remat(0), tier1_remat(7), tier1_remat(1000));\n"
    "    return 0;\n"
    "}\n";

// The division fixture: tier1_divide is the function under test. Its loop divides by eight constants, each a reciprocal's
// sequence of another shape (64-bit signed by 10 and its remainder by 641, unsigned remainder by 1000003, the add
// sequence's division by 7 and the even divisor's pre-shift by 14; 32-bit signed by -7, unsigned remainder by 10 and
// division by 7), and by a register, while statics' and frame objects' addresses and constants stay live. main's calls
// leave nothing live across them, so the saves its build reports are tier1_divide's (check_test_tier1_divide_main).
static const char check_test_tier1_divide_source[] =
    "#include <stdio.h>\n"
    "static long s0, s1, s2, s3;\n"
    "static unsigned long su[4];\n"
    "unsigned long tier1_divide(long n, long d)\n"
    "{\n"
    "    long a0[2], a1[2], a2[2];\n"
    "    long *p0 = a0, *p1 = a1, *p2 = a2;\n"
    "    long *q0 = &s0, *q1 = &s1, *q2 = &s2, *q3 = &s3;\n"
    "    unsigned long* u = &su[1];\n"
    "    const unsigned long wide = 0x123456789abcdefull;\n"
    "    const unsigned long middle = 0x87654321ull;\n"
    "    unsigned long total = 0;\n"
    "    for (long k = 0; k < 2; k += 1)\n"
    "    {\n"
    "        p0[k] = k; p1[k] = k + 1; p2[k] = k + 2;\n"
    "    }\n"
    "    for (long i = 1; i <= n; i += 1)\n"
    "    {\n"
    "        const long k = i & 1;\n"
    "        const unsigned long x = (unsigned long)i * wide;\n"
    "        p0[k] += i / 10;\n"
    "        p1[k] ^= (long)(x % 1000003u);\n"
    "        p2[k] -= (int)i / -7;\n"
    "        q0[0] += (unsigned)x % 10u;\n"
    "        q1[0] ^= i / d;\n"
    "        q2[0] += i % 641;\n"
    "        q3[0] ^= (long)(x / 7u);\n"
    "        u[k] += x / 14u + (unsigned)x / 7u;\n"
    "        total = total * 31u + wide + middle + (unsigned long)p0[k];\n"
    "    }\n"
    "    return total + (unsigned long)(p0[0] + p0[1] + p1[0] + p1[1] + p2[0] + p2[1]) + (unsigned long)(s0 + s1 + s2 + s3) + su[1] + su[2];\n"
    "}\n"
    "int main(void)\n"
    "{\n"
    "    printf(\"%lu\\n\", tier1_divide(0, 3));\n"
    "    printf(\"%lu\\n\", tier1_divide(7, 3));\n"
    "    printf(\"%lu\\n\", tier1_divide(1000, 7));\n"
    "    return 0;\n"
    "}\n";

// The division fixture's main alone, whose build must save nothing.
static const char check_test_tier1_divide_main[] =
    "#include <stdio.h>\n"
    "unsigned long tier1_divide(long n, long d);\n"
    "int main(void)\n"
    "{\n"
    "    printf(\"%lu\\n\", tier1_divide(0, 3));\n"
    "    printf(\"%lu\\n\", tier1_divide(7, 3));\n"
    "    printf(\"%lu\\n\", tier1_divide(1000, 7));\n"
    "    return 0;\n"
    "}\n";

// The labels fixture (#789): every function in it has labels and indirect jumps, each one's shape another.
// tier1_interpret's computed gotos go through its dispatch block, which the lowering puts last, with values live across
// it in a loop; tier1_classify's switch is a jump table, with values live across it; tier1_pick jumps once through a ?:
// of two labels' addresses; tier1_both enters a label directly and through its table, where a phi meets values that stay
// live apart, so that each edge into it has moves and the indirect jump's has a stub, where the label's symbol goes;
// tier1_once loads its target from its table; tier1_single can jump to one label alone.
static const char check_test_tier1_labels_source[] =
    "#include <stdio.h>\n"
    "static long tier1_interpret(const unsigned char* code, long n)\n"
    "{\n"
    "    static void* const table[] = { &&op_add, &&op_mul, &&op_sub, &&op_halt };\n"
    "    long acc = 1;\n"
    "    long steps = 0;\n"
    "    const unsigned char* pc = code;\n"
    "    goto *table[*pc];\n"
    "op_add:\n"
    "    acc += n + steps;\n"
    "    steps += 1;\n"
    "    goto *table[*++pc];\n"
    "op_mul:\n"
    "    acc *= 3;\n"
    "    steps += 1;\n"
    "    goto *table[*++pc];\n"
    "op_sub:\n"
    "    acc -= n;\n"
    "    steps += 1;\n"
    "    goto *table[*++pc];\n"
    "op_halt:\n"
    "    return acc * 1000 + steps;\n"
    "}\n"
    "static long tier1_classify(long x, long y)\n"
    "{\n"
    "    long r = y;\n"
    "    long q = x ^ y;\n"
    "    for (long i = 0; i < x; i += 1)\n"
    "    {\n"
    "        switch ((i * 7 + y) % 12)\n"
    "        {\n"
    "        case 0: r += 3; break;\n"
    "        case 1: r ^= i; break;\n"
    "        case 2: r -= 5; q += r; break;\n"
    "        case 3: r *= 2; break;\n"
    "        case 4: r += i * i; break;\n"
    "        case 5: r >>= 1; break;\n"
    "        case 6: r += 11; q ^= i; break;\n"
    "        case 7: r -= i; break;\n"
    "        case 8: r |= 4; break;\n"
    "        case 9: r &= 0xffff; break;\n"
    "        case 10: r += 100; break;\n"
    "        default: r -= 1; break;\n"
    "        }\n"
    "    }\n"
    "    return r * 31 + q;\n"
    "}\n"
    "static int tier1_pick(int p)\n"
    "{\n"
    "    void* t = p ? &&a : &&b;\n"
    "    goto *t;\n"
    "a:\n"
    "    return 10;\n"
    "b:\n"
    "    return 20;\n"
    "}\n"
    "static long tier1_both(long x)\n"
    "{\n"
    "    static void* const targets[] = { &&first, &&second };\n"
    "    long a = x * 3;\n"
    "    long b = x * 5 + 1;\n"
    "    long r;\n"
    "    if (x & 1)\n"
    "    {\n"
    "        r = a;\n"
    "        goto first;\n"
    "    }\n"
    "    r = b;\n"
    "    goto *targets[(x >> 1) & 1];\n"
    "first:\n"
    "    return r * 7 + (a ^ b);\n"
    "second:\n"
    "    return r - a;\n"
    "}\n"
    "static int tier1_once(int i)\n"
    "{\n"
    "    static void* const table[] = { &&a, &&b, &&c };\n"
    "    goto *table[i];\n"
    "a:\n"
    "    return 1;\n"
    "b:\n"
    "    return 2;\n"
    "c:\n"
    "    return 3;\n"
    "}\n"
    "static int tier1_single(int x)\n"
    "{\n"
    "    void* t = &&only;\n"
    "    goto *t;\n"
    "only:\n"
    "    return x + 1;\n"
    "}\n"
    "int main(void)\n"
    "{\n"
    "    static const unsigned char program[] = { 0, 1, 2, 0, 1, 1, 2, 0, 3 };\n"
    "    printf(\"%ld %ld %ld\\n\", tier1_interpret(program, 5), tier1_classify(1000, 3), tier1_classify(77, 9));\n"
    "    printf(\"%d %d\\n\", tier1_pick(1), tier1_pick(0));\n"
    "    printf(\"%ld %ld %ld %ld\\n\", tier1_both(5), tier1_both(6), tier1_both(8), tier1_both(-3));\n"
    "    printf(\"%d %d %d %d\\n\", tier1_once(0), tier1_once(1), tier1_once(2), tier1_single(41));\n"
    "    return 0;\n"
    "}\n";

// The variadic definitions' fixture (#789): va_start, va_arg of integers, pointers and an int, va_copy, unnamed arguments
// past the registers (in the overflow area), five named ones before them, a va_list handed to vsnprintf, a variadic
// function that reads none, and one whose result crosses a call after it read its overflow area, which takes a
// callee-saved register its prologue pushes (#1060's review). main's calls pass more than six arguments, which Tier 1
// doesn't take yet: it is Tier 0's.
static const char check_test_tier1_variadic_source[] =
    "#include <stdarg.h>\n"
    "#include <stdio.h>\n"
    "static long tier1_sum(int count, ...)\n"
    "{\n"
    "    va_list ap;\n"
    "    va_start(ap, count);\n"
    "    long total = 0;\n"
    "    for (int i = 0; i < count; i += 1)\n"
    "    {\n"
    "        total = total * 31 + va_arg(ap, long);\n"
    "    }\n"
    "    va_end(ap);\n"
    "    return total;\n"
    "}\n"
    "static long tier1_mixed(long first, ...)\n"
    "{\n"
    "    va_list ap;\n"
    "    va_start(ap, first);\n"
    "    const char* s = va_arg(ap, const char*);\n"
    "    int i = va_arg(ap, int);\n"
    "    long* p = va_arg(ap, long*);\n"
    "    va_end(ap);\n"
    "    return first + (long)s[1] * 3 + i * 5 + *p * 7;\n"
    "}\n"
    "static long tier1_copy(int count, ...)\n"
    "{\n"
    "    va_list ap;\n"
    "    va_list again;\n"
    "    va_start(ap, count);\n"
    "    va_copy(again, ap);\n"
    "    long a = 0;\n"
    "    for (int i = 0; i < count; i += 1)\n"
    "    {\n"
    "        a += va_arg(ap, long);\n"
    "    }\n"
    "    long b = 0;\n"
    "    for (int i = 0; i < count; i += 1)\n"
    "    {\n"
    "        b ^= va_arg(again, long) << i;\n"
    "    }\n"
    "    va_end(again);\n"
    "    va_end(ap);\n"
    "    return a * 1000003 + b;\n"
    "}\n"
    "static long tier1_tail(long a, long b, long c, long d, long e, ...)\n"
    "{\n"
    "    va_list ap;\n"
    "    va_start(ap, e);\n"
    "    long x = va_arg(ap, long);\n"
    "    long y = va_arg(ap, long);\n"
    "    long z = va_arg(ap, long);\n"
    "    va_end(ap);\n"
    "    return a + b * 2 + c * 3 + d * 4 + e * 5 + x * 6 + y * 7 + z * 8;\n"
    "}\n"
    "static int tier1_format(char* buffer, unsigned long size, const char* format, ...)\n"
    "{\n"
    "    va_list ap;\n"
    "    va_start(ap, format);\n"
    "    int n = vsnprintf(buffer, size, format, ap);\n"
    "    va_end(ap);\n"
    "    return n;\n"
    "}\n"
    "static long tier1_bump(long x)\n"
    "{\n"
    "    return x * 5 + 1;\n"
    "}\n"
    "static long tier1_late(int count, ...)\n"
    "{\n"
    "    va_list ap;\n"
    "    va_start(ap, count);\n"
    "    long total = 0;\n"
    "    for (int i = 0; i < count; i += 1)\n"
    "    {\n"
    "        total = total * 3 + va_arg(ap, long);\n"
    "    }\n"
    "    va_end(ap);\n"
    "    long r = tier1_bump(total);\n"
    "    return r + total;\n"
    "}\n"
    "static long tier1_ignore(long x, ...)\n"
    "{\n"
    "    return x * 2 + 1;\n"
    "}\n"
    "int main(void)\n"
    "{\n"
    "    long value = 11;\n"
    "    char buffer[64];\n"
    "    printf(\"%ld %ld %ld\\n\", tier1_sum(9, 1L, 2L, 3L, 4L, 5L, 6L, 7L, 8L, 9L), tier1_sum(0), tier1_sum(2, -5L, 7L));\n"
    "    printf(\"%ld %ld\\n\", tier1_mixed(10L, \"abc\", 7, &value), tier1_copy(7, 10L, 20L, 30L, 40L, 50L, 60L, 70L));\n"
    "    printf(\"%ld %ld\\n\", tier1_tail(1, 2, 3, 4, 5, 6L, 7L, 8L), tier1_ignore(20, 1L, 2L));\n"
    "    int n = tier1_format(buffer, sizeof(buffer), \"%s-%d-%ld-%c\", \"x\", 42, 123456789012L, 'q');\n"
    "    printf(\"%d %s\\n\", n, buffer);\n"
    "    printf(\"%ld %ld\\n\", tier1_late(9, 1L, 2L, 3L, 4L, 5L, 6L, 7L, 8L, 9L), tier1_late(7, 11L, 12L, 13L, 14L, 15L, 16L, 17L));\n"
    "    return 0;\n"
    "}\n";

// The opmask fixture (#179): tier1_masks is the function under test, every _k* builtin's row of its two words' masks (the
// words truncated to each width, as the builtins' prototypes convert them), folded into one hash; main prints the hash
// over pairs of words around each width's edges.
static const char check_test_tier1_masks_source[] =
    "#include <stdio.h>\n"
    "typedef unsigned long long u64;\n"
    "#define B(name) h = h * 31 + __builtin_ia32_##name(a, b);\n"
    "#define U(name) h = h * 31 + __builtin_ia32_##name(a);\n"
    "#define S(name) h = h * 31 + __builtin_ia32_##name(a, 0); h = h * 31 + __builtin_ia32_##name(a, 1); h = h * 31 + __builtin_ia32_##name(a, 7); \\\n"
    "    h = h * 31 + __builtin_ia32_##name(a, 8); h = h * 31 + __builtin_ia32_##name(a, 31); h = h * 31 + __builtin_ia32_##name(a, 64);\n"
    "u64 tier1_masks(u64 a, u64 b)\n"
    "{\n"
    "    u64 h = 0;\n"
    "    B(kandqi) B(kandhi) B(kandsi) B(kanddi) B(kandnqi) B(kandnhi) B(kandnsi) B(kandndi)\n"
    "    B(korqi) B(korhi) B(korsi) B(kordi) B(kxorqi) B(kxorhi) B(kxorsi) B(kxordi)\n"
    "    B(kxnorqi) B(kxnorhi) B(kxnorsi) B(kxnordi) B(kaddqi) B(kaddhi) B(kaddsi) B(kadddi)\n"
    "    B(kunpckhi) B(kunpcksi) B(kunpckdi)\n"
    "    B(kortestzqi) B(kortestzhi) B(kortestzsi) B(kortestzdi) B(kortestcqi) B(kortestchi) B(kortestcsi) B(kortestcdi)\n"
    "    B(ktestzqi) B(ktestzhi) B(ktestzsi) B(ktestzdi) B(ktestcqi) B(ktestchi) B(ktestcsi) B(ktestcdi)\n"
    "    U(knotqi) U(knothi) U(knotsi) U(knotdi) U(kmovb) U(kmovw) U(kmovd) U(kmovq)\n"
    "    S(kshiftliqi) S(kshiftlihi) S(kshiftlisi) S(kshiftlidi) S(kshiftriqi) S(kshiftrihi) S(kshiftrisi) S(kshiftridi)\n"
    "    return h;\n"
    "}\n"
    "int main(void)\n"
    "{\n"
    "    static const u64 words[] = { 0, 1, 0x80, 0xff, 0x8000, 0xffff, 0x80000000ull, 0xffffffffull, 0x8000000000000000ull, ~0ull,\n"
    "                                 0x0123456789abcdefull, 0x5555aaaa33330f0full };\n"
    "    u64 h = 0;\n"
    "    for (unsigned i = 0; i < 12; i += 1)\n"
    "    {\n"
    "        for (unsigned j = 0; j < 12; j += 1)\n"
    "        {\n"
    "            h = h * 1000003 + tier1_masks(words[i], words[j]);\n"
    "        }\n"
    "    }\n"
    "    printf(\"%llx\\n\", h);\n"
    "    return 0;\n"
    "}\n";

// The narrow arguments' fixture (#141): a caller unit whose functions pass chars, shorts and a _Bool, by the driver's
// --tier1, and a callee unit by clang -O2, whose functions trust their callers' extension to 32 bits (signed or
// unsigned by each parameter's type: `movl %edi, %eax` for a signed char's return), linked together. A narrow parameter
// of the caller unit's own, which it truncates, is called there too. tier1_call_cross_signedness passes a signed char
// value to an unsigned char parameter and an unsigned char value to a signed char parameter, so the extension must be
// the parameter's type's, not the argument expression's. clang_calls_narrow, in the clang -O2 unit, calls the caller
// unit's tier1_narrow_entry through a prototype of ints, with bits set above each parameter's width: the callee must
// truncate as clang -O0's own callee does.
static const char check_test_tier1_narrow_caller_source[] =
    "#include <stdio.h>\n"
    "int tier1_take_s8(signed char c);\n"
    "int tier1_take_u8(unsigned char c);\n"
    "int tier1_take_s16(short s);\n"
    "int tier1_take_u16(unsigned short s);\n"
    "int tier1_take_bool(_Bool b);\n"
    "long tier1_take_mixed(signed char a, unsigned short b, short c, unsigned char d);\n"
    "int clang_calls_narrow(int x);\n"
    "static int tier1_call_narrow(int x)\n"
    "{\n"
    "    signed char a = (signed char)(x * 37);\n"
    "    unsigned char b = (unsigned char)(x * 53);\n"
    "    short c = (short)(x * 4099);\n"
    "    unsigned short d = (unsigned short)(x * 7919);\n"
    "    _Bool e = (x & 4) != 0;\n"
    "    return tier1_take_s8(a) + tier1_take_u8(b) * 3 + tier1_take_s16(c) * 5 + tier1_take_u16(d) * 7 + tier1_take_bool(e) * 11;\n"
    "}\n"
    "static long tier1_call_mixed(int x)\n"
    "{\n"
    "    return tier1_take_mixed((signed char)(x * 3 - 200), (unsigned short)(x * 60001), (short)(x * -12345), (unsigned char)(x + 250));\n"
    "}\n"
    "static int tier1_call_cross_signedness(int x)\n"
    "{\n"
    "    signed char negative = (signed char)(0x80 + (x & 1));\n"
    "    unsigned char high = (unsigned char)(0xff - (x & 1));\n"
    "    return tier1_take_u8(negative) * 13 + tier1_take_s8(high) * 17;\n"
    "}\n"
    "static __attribute__((noinline)) int tier1_narrow_parameters(signed char a, unsigned short b, _Bool c)\n"
    "{\n"
    "    return a * 100000 + b + (c ? 7 : 0);\n"
    "}\n"
    "int tier1_narrow_entry(signed char a, unsigned short b, _Bool c)\n"
    "{\n"
    "    return a * 100000 + b + (c ? 7 : 0);\n"
    "}\n"
    "int main(void)\n"
    "{\n"
    "    unsigned long total = 0;\n"
    "    for (int x = -300; x < 300; x += 7)\n"
    "    {\n"
    "        total = total * 31 + (unsigned long)tier1_call_narrow(x) + (unsigned long)tier1_call_mixed(x) + (unsigned long)tier1_call_cross_signedness(x) + (unsigned long)tier1_narrow_parameters((signed char)x, (unsigned short)(x * 3), x > 0);\n"
    "    }\n"
    "    for (int x = -4; x < 4; x += 1)\n"
    "    {\n"
    "        total = total * 31 + (unsigned long)clang_calls_narrow(x);\n"
    "    }\n"
    "    printf(\"%lu\\n\", total);\n"
    "    return 0;\n"
    "}\n";

static const char check_test_tier1_narrow_callee_source[] =
    "int tier1_take_s8(signed char c) { return c; }\n"
    "int tier1_take_u8(unsigned char c) { return c; }\n"
    "int tier1_take_s16(short s) { return s; }\n"
    "int tier1_take_u16(unsigned short s) { return s; }\n"
    "int tier1_take_bool(_Bool b) { return b ? 7 : 3; }\n"
    "long tier1_take_mixed(signed char a, unsigned short b, short c, unsigned char d) { return a * 1000000L + b * 1000L + c * 10L + d; }\n"
    "int tier1_narrow_entry(int a, int b, int c);\n"
    "int clang_calls_narrow(int x) { return tier1_narrow_entry(0x123456f0 + x, 0x7fff0000 | (x & 0xffff), 0x5a5a0000 | (x & 1)); }\n";

// The value of `name` (as "spill_stores") in --tier1-report's line of `text`, into *value: whether it has one.
static bool check_test_tier1_field(const char* text, const char* name, uint64_t* value)
{
    const char* line = text != NULL ? strstr(text, "tier1\tfunctions=") : NULL;
    char key[64];
    snprintf(key, sizeof(key), "\t%s=", name);
    const char* end = line != NULL ? strchr(line, '\n') : NULL;
    const char* field = line != NULL ? strstr(line, key) : NULL;
    const bool found = field != NULL && (end == NULL || field < end);
    *value = found ? strtoull(field + strlen(key), NULL, 10) : 0;
    return found;
}

// The function `name` in `-c`'s object at `path`: its bytes, decoded by the scalar disassembler into `records` (room for
// `capacity`), and its relocations, each one's offset from the function's start and type (as R_X86_64_PC32) in
// `offsets` and `types` (room for `capacity` too). Returns how many records, *relocation_count set, or 0 on a failure.
static uint32_t check_test_tier1_function(const char* path, const char* name, X86Decoded* records, uint32_t capacity, uint32_t* offsets, uint32_t* types, uint32_t* relocation_count)
{
    uint64_t length = 0;
    uint8_t* bytes = check_test_driver_read_aligned(path, &length);
    CheckTestElf elf;
    bool valid = bytes != NULL && check_test_elf_open(&elf, (String8){ .pointer = (char*)bytes, .length = length });
    const Elf64_Sym* function = NULL;
    for (uint64_t i = 1; valid && function == NULL && i < elf.symbol_count; i += 1)
    {
        const Elf64_Sym* symbol = &elf.symbols[i];
        const char* symbol_name = check_test_elf_string(elf.symbol_names, elf.symbol_names_size, symbol->st_name);
        const bool named = symbol_name != NULL && strcmp(symbol_name, name) == 0 && ELF64_ST_TYPE(symbol->st_info) == STT_FUNC && symbol->st_shndx != SHN_UNDEF &&
                           symbol->st_shndx < elf.section_count;
        function = named ? symbol : NULL;
    }
    const Elf64_Shdr* text = function != NULL ? &elf.sections[function->st_shndx] : NULL;
    valid = valid && text != NULL && function->st_size != 0 && function->st_value <= text->sh_size && function->st_size <= text->sh_size - function->st_value &&
            check_test_elf_range(elf.file, text->sh_offset, text->sh_size);
    const X86DecodeResult decoded = valid ? x86_disassemble_scalar((const uint8_t*)bytes + text->sh_offset + function->st_value, function->st_size, records, capacity)
                                          : (X86DecodeResult){ .status = X86_DECODE_BAD_ARGUMENT };
    valid = valid && decoded.status == X86_DECODE_OK && decoded.consumed == function->st_size;
    *relocation_count = 0;
    for (uint32_t s = 0; valid && s < elf.section_count; s += 1)
    {
        const Elf64_Shdr* section = &elf.sections[s];
        const bool applies = section->sh_type == SHT_RELA && section->sh_info == function->st_shndx;
        valid = !applies || (section->sh_entsize == sizeof(Elf64_Rela) && check_test_elf_range(elf.file, section->sh_offset, section->sh_size));
        const Elf64_Rela* relocations = applies && valid ? (const Elf64_Rela*)(bytes + section->sh_offset) : NULL;
        for (uint64_t r = 0; relocations != NULL && valid && r < section->sh_size / sizeof(Elf64_Rela); r += 1)
        {
            const uint64_t offset = relocations[r].r_offset;
            const bool inside = offset >= function->st_value && offset < function->st_value + function->st_size;
            valid = !inside || *relocation_count < capacity;
            if (inside && valid)
            {
                offsets[*relocation_count] = (uint32_t)(offset - function->st_value);
                types[*relocation_count] = (uint32_t)ELF64_R_TYPE(relocations[r].r_info);
                *relocation_count += 1;
            }
        }
    }
    free(bytes);
    return valid ? (uint32_t)decoded.count : 0;
}

// The status flags (CF, PF, AF, ZF, SF and OF) live before decoded record `index`, given those live after it, live[index]:
// those it reads, and those live after it that it neither writes nor leaves undefined (x86_semantics.c).
static uint8_t check_test_tier1_live_in(const X86Decoded* records, const uint8_t* live, uint32_t index)
{
    const X86Effects* effects = x86_effects(records[index].instruction.form & BUSTER_X86_FORM_NUMBER);
    return (uint8_t)(effects->flags_read | (live[index] & ~(effects->flags_written | effects->flags_undefined)));
}

// The status flags live after each of `count` decoded records of one function, into `live`: a backward fixpoint over
// the records' successors, the next record (but after a jump, a return or a trap) and a direct jump's or branch's target.
// A record whose control the effects can't follow (an indirect jump, an uncovered form), a jump out of the function and
// falling off its end keep every flag live.
static void check_test_tier1_flags(const X86Decoded* records, uint32_t count, uint8_t* live)
{
    const uint8_t status = BUSTER_X86_FLAG_CF | BUSTER_X86_FLAG_PF | BUSTER_X86_FLAG_AF | BUSTER_X86_FLAG_ZF | BUSTER_X86_FLAG_SF | BUSTER_X86_FLAG_OF;
    memset(live, 0, count);
    bool changed = true;
    while (changed)
    {
        changed = false;
        for (uint32_t k = count; k > 0; k -= 1)
        {
            const uint32_t i = k - 1;
            const uint32_t form = records[i].instruction.form & BUSTER_X86_FORM_NUMBER;
            const X86Effects* effects = x86_effects(form);
            const uint32_t control = (uint32_t)effects->kinds >> 4;
            const int64_t goal = (int64_t)records[i].offset + records[i].length + records[i].instruction.displacement;
            // The target's record, by halving over the records' offsets.
            uint32_t low = 0;
            uint32_t high = count;
            while (low < high)
            {
                const uint32_t middle = low + (high - low) / 2;
                low = (int64_t)records[middle].offset < goal ? middle + 1 : low;
                high = (int64_t)records[middle].offset < goal ? high : middle;
            }
            const bool targeted = x86_form_descriptions[form].relative_bytes != 0 && low < count && (int64_t)records[low].offset == goal;
            const bool next = control == BUSTER_X86_CONTROL_NONE || control == BUSTER_X86_CONTROL_CALL || control == BUSTER_X86_CONTROL_BRANCH;
            const bool jumps = control == BUSTER_X86_CONTROL_JUMP || control == BUSTER_X86_CONTROL_BRANCH;
            const bool unknown = control == BUSTER_X86_CONTROL_UNKNOWN || effects->operation == X86_OPERATION_NONE;
            const uint8_t falls = next ? (i + 1 < count ? check_test_tier1_live_in(records, live, i + 1) : status) : 0;
            const uint8_t taken = jumps ? (targeted ? check_test_tier1_live_in(records, live, low) : status) : 0;
            const uint8_t after = (uint8_t)((falls | taken | (unknown ? status : 0)) & status);
            changed |= after != live[i];
            live[i] = after;
        }
    }
}

// Whether a decoded record is a zeroing xor of a register (any width the selector and Tier 1 emit).
static bool check_test_tier1_zeroing(const X86Instruction* instruction)
{
    const uint32_t form = instruction->form & BUSTER_X86_FORM_NUMBER;
    const bool exclusive = form == X86_FORM_XOR_RM32_R32 || form == X86_FORM_XOR_R32_RM32 || form == X86_FORM_XOR_RM64_R64 || form == X86_FORM_XOR_R64_RM64;
    return exclusive && instruction->mode == BUSTER_X86_REGISTER_DIRECT && instruction->reg == instruction->rm;
}

// What one build of the fixture by the driver did: its report's fields.
typedef struct CheckTestTier1Build CheckTestTier1Build;
struct CheckTestTier1Build
{
    uint64_t records;
    uint64_t rematerializations;
    uint64_t dropped;
    uint64_t stores;
    uint64_t loads;
    uint64_t slots;
    uint64_t frame;
    uint64_t call_stores;
    bool reported;
};

// Whether `text`, a --tier1-refusals report, refuses none of `count` functions `names` (no refusal line names one), or
// with an empty name in them none at all.
static bool check_test_tier1_taken(const char* text, const char* const* names, uint32_t count)
{
    bool taken = text != NULL;
    for (uint32_t n = 0; taken && n < count; n += 1)
    {
        char refused[128];
        snprintf(refused, sizeof(refused), "tier1-refused\t%s%s", names[n], names[n][0] == 0 ? "" : "\t");
        taken = strstr(text, refused) == NULL;
    }
    return taken;
}

// A fixture compiled by --tier1 with and without rematerialization (`rematerialize`) by each path, into an executable
// (`-o output`) or with `object` an object (-c), with `option` (as -march=x86-64-v4) where it isn't NULL, whose report
// goes into *build: whether it compiled with each of its `name_count` functions `names` by Tier 1
// (check_test_tier1_taken).
static bool check_test_tier1_build_names(const char* executable, const char* source, const char* const* names, uint32_t name_count, const char* output, bool rematerialize, bool scalar, bool object, bool gvn,
                                         const char* option, CheckTestTier1Build* build)
{
    char* arguments[16] = { (char*)executable, "--tier1", "--tier1-report", "--tier1-refusals", "--verify" };
    uint32_t count = 5;
    if (option != NULL)
    {
        arguments[count++] = (char*)option;
    }
    if (!rematerialize)
    {
        arguments[count++] = "--tier1-no-rematerialize";
    }
    if (gvn)
    {
        arguments[count++] = "--tier1-gvn";
    }
    if (scalar)
    {
        arguments[count++] = "--scalar";
    }
    if (object)
    {
        arguments[count++] = "-c";
    }
    arguments[count++] = "-o";
    arguments[count++] = (char*)output;
    arguments[count++] = (char*)source;
    arguments[count] = NULL;
    char* text = NULL;
    const int status = check_test_driver_spawn(arguments, &text);
    *build = (CheckTestTier1Build){ 0 };
    build->reported = check_test_tier1_field(text, "records", &build->records) && check_test_tier1_field(text, "rematerializations", &build->rematerializations) &&
                      check_test_tier1_field(text, "dropped_definitions", &build->dropped) &&
                      check_test_tier1_field(text, "spill_stores", &build->stores) && check_test_tier1_field(text, "spill_loads", &build->loads) &&
                      check_test_tier1_field(text, "spill_slots", &build->slots) && check_test_tier1_field(text, "frame_bytes", &build->frame) &&
                      check_test_tier1_field(text, "call_stores", &build->call_stores);
    const bool taken = check_test_tier1_taken(text, names, name_count);
    if (status != 0 && text != NULL)
    {
        fprintf(stderr, "%s", text);
    }
    free(text);
    return status == 0 && build->reported && taken;
}

// check_test_tier1_build_names of one function `name`, or with an empty name every function.
static bool check_test_tier1_build(const char* executable, const char* source, const char* name, const char* output, bool rematerialize, bool scalar, bool object, bool gvn, const char* option,
                                   CheckTestTier1Build* build)
{
    return check_test_tier1_build_names(executable, source, &name, 1, output, rematerialize, scalar, object, gvn, option, build);
}

// The fixture's object, decoded: every zeroing xor leaves no status flag live (#178: a recomputed zero is an xor only
// where no flag is live, as every point between two nodes' records is), there is one at least, and every instruction
// that addresses from rip has its own relocation at its displacement, a PC32 for a lea and a GOT one for a load (each
// recomputed address one more). Into *loop_addresses, how many instructions in the loop (the range of its back edge, the
// last backward jump) address from rip or from rsp's frame: the recomputed addresses there; and into *zeroing_count, its
// zeroing xors.
static bool check_test_tier1_code(CheckTest* test, const char* object, uint32_t* loop_addresses, uint32_t* zeroing_count)
{
    enum
    {
        CHECK_TEST_TIER1_RECORDS = 4096,
    };
    X86Decoded* records = calloc(CHECK_TEST_TIER1_RECORDS, sizeof(X86Decoded));
    uint32_t* offsets = calloc(CHECK_TEST_TIER1_RECORDS, sizeof(uint32_t));
    uint32_t* types = calloc(CHECK_TEST_TIER1_RECORDS, sizeof(uint32_t));
    uint8_t* live = calloc(CHECK_TEST_TIER1_RECORDS, 1);
    uint32_t relocations = 0;
    const uint32_t count = records != NULL && offsets != NULL && types != NULL && live != NULL ? check_test_tier1_function(object, "tier1_remat", records, CHECK_TEST_TIER1_RECORDS, offsets, types, &relocations) : 0;
    check_test_expect(test, count != 0, "the fixture's function decodes, with its relocations");
    if (count != 0)
    {
        check_test_tier1_flags(records, count, live);
    }
    uint32_t zeroing = 0;
    bool safe = true;
    bool referenced = true;
    uint32_t addressed = 0;
    // The loop: from the last backward jump's target to it.
    uint32_t loop_first = count;
    uint32_t loop_last = 0;
    for (uint32_t i = 0; i < count; i += 1)
    {
        const X86Instruction* instruction = &records[i].instruction;
        const uint32_t form = instruction->form & BUSTER_X86_FORM_NUMBER;
        const bool jump = x86_form_descriptions[form].relative_bytes != 0 && (x86_effects(form)->kinds >> 4) != BUSTER_X86_CONTROL_CALL;
        const int64_t goal = (int64_t)records[i].offset + records[i].length + instruction->displacement;
        for (uint32_t j = 0; jump && goal < (int64_t)records[i].offset && j < count; j += 1)
        {
            loop_first = (int64_t)records[j].offset == goal ? j : loop_first;
            loop_last = (int64_t)records[j].offset == goal ? i : loop_last;
        }
        if (check_test_tier1_zeroing(instruction))
        {
            zeroing += 1;
            safe &= live[i] == 0;
        }
        if (instruction->mode == BUSTER_X86_MEMORY_RIP && (form == X86_FORM_LEA_R64_M || form == X86_FORM_MOV_R64_RM64))
        {
            // The displacement is the record's last four bytes: neither form has an immediate.
            const uint32_t field = records[i].offset + records[i].length - 4;
            const uint32_t expected = form == X86_FORM_LEA_R64_M ? R_X86_64_PC32 : R_X86_64_REX_GOTPCRELX;
            uint32_t found = 0;
            for (uint32_t r = 0; r < relocations; r += 1)
            {
                found += offsets[r] == field && types[r] == expected;
            }
            referenced &= found == 1;
        }
    }
    for (uint32_t i = loop_first; i <= loop_last && i < count; i += 1)
    {
        const X86Instruction* instruction = &records[i].instruction;
        const uint32_t form = instruction->form & BUSTER_X86_FORM_NUMBER;
        const bool rip = instruction->mode == BUSTER_X86_MEMORY_RIP && (form == X86_FORM_LEA_R64_M || form == X86_FORM_MOV_R64_RM64);
        const bool frame = instruction->mode == BUSTER_X86_MEMORY_BASE && instruction->rm == X86_REGISTER_RSP && form == X86_FORM_LEA_R64_M;
        addressed += rip || frame;
    }
    check_test_expect(test, count == 0 || (zeroing != 0 && safe), "every zeroing xor of Tier 1's code leaves no flag live, and the fixture has one");
    check_test_expect(test, count == 0 || referenced, "each instruction that addresses from rip has a relocation of its own");
    check_test_expect(test, count == 0 || loop_first < loop_last, "the fixture's loop is found by its back edge");
    *loop_addresses = addressed;
    *zeroing_count = zeroing;
    free(records);
    free(offsets);
    free(types);
    free(live);
    return count != 0;
}

// See the file's header: each build's program prints clang's output, rematerialization recomputes in the loop what its
// absence reloads, with fewer spills, slots and frame bytes, and its objects' code passes check_test_tier1_code.
static void check_test_driver_tier1_rematerialization(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    char source[512];
    char reference[512];
    snprintf(source, sizeof(source), "%s/tier1-remat.c", directory);
    snprintf(reference, sizeof(reference), "%s/tier1-remat-clang", directory);
    const bool written = parse_test_write(source, check_test_tier1_remat_source, sizeof(check_test_tier1_remat_source) - 1);
    char* clang_arguments[] = { (char*)clang, "-O0", "-o", reference, source, NULL };
    char* clang_text = NULL;
    const bool referenced = written && check_test_driver_spawn(clang_arguments, &clang_text) == 0;
    free(clang_text);
    char expected[256] = { 0 };
    const bool ran = referenced && check_test_driver_capture(reference, expected, sizeof(expected)) && expected[0] != 0;
    check_test_expect(test, ran, "clang's build of the rematerialization fixture runs");
    const bool ready = ran && (x86_semantics_initialized || x86_semantics_initialize()) && x86_disassembler_initialize();
    check_test_expect(test, ready, "the semantics and the disassembler's tables");
    CheckTestTier1Build builds[2][2];
    uint32_t addresses[2][2] = { { 0 } };
    uint32_t zeroing[2][2] = { { 0 } };
    for (uint32_t rematerialize = 0; ready && rematerialize < 2; rematerialize += 1)
    {
        for (uint32_t path = 0; path < 2; path += 1)
        {
            char output[512];
            char object[512];
            char linked[512];
            snprintf(output, sizeof(output), "%s/tier1-remat-%u-%u", directory, rematerialize, path);
            snprintf(object, sizeof(object), "%s/tier1-remat-%u-%u.o", directory, rematerialize, path);
            snprintf(linked, sizeof(linked), "%s/tier1-remat-%u-%u-cc", directory, rematerialize, path);
            // The executable by the direct path, and the object, which cc links.
            CheckTestTier1Build object_build;
            const bool built = check_test_tier1_build(executable, source, "tier1_remat", output, rematerialize != 0, path != 0, false, false, NULL, &builds[rematerialize][path]);
            const bool compiled = check_test_tier1_build(executable, source, "tier1_remat", object, rematerialize != 0, path != 0, true, false, NULL, &object_build);
            check_test_expect(test, built && compiled, "the fixture compiles by --tier1, its function Tier 1's");
            char* cc_arguments[] = { "cc", "-o", linked, object, NULL };
            char* cc_text = NULL;
            const bool cc_linked = compiled && check_test_driver_spawn(cc_arguments, &cc_text) == 0;
            free(cc_text);
            char actual[256] = { 0 };
            char from_object[256] = { 0 };
            check_test_expect(test, built && check_test_driver_capture(output, actual, sizeof(actual)) && strcmp(actual, expected) == 0, "the executable prints clang's build's output");
            check_test_expect(test, cc_linked && check_test_driver_capture(linked, from_object, sizeof(from_object)) && strcmp(from_object, expected) == 0,
                              "the object, linked by cc, prints clang's build's output");
            const bool decoded = compiled && check_test_tier1_code(test, object, &addresses[rematerialize][path], &zeroing[rematerialize][path]);
            check_test_expect(test, !decoded || (rematerialize != 0 ? addresses[rematerialize][path] != 0 : addresses[rematerialize][path] == 0),
                              rematerialize != 0 ? "the loop recomputes addresses from rip and the frame" : "without rematerialization the loop computes no address");
            unlink(output);
            unlink(object);
            unlink(linked);
        }
    }
    for (uint32_t path = 0; ready && path < 2; path += 1)
    {
        const CheckTestTier1Build* with = &builds[1][path];
        const CheckTestTier1Build* without = &builds[0][path];
        check_test_expect(test, with->rematerializations != 0 && with->dropped != 0 && without->rematerializations == 0 && without->dropped == 0,
                          "the report counts recomputed values and the definitions left out only with rematerialization");
        check_test_expect(test, with->stores < without->stores && with->loads < without->loads && with->slots < without->slots && with->frame < without->frame,
                          "rematerialization leaves fewer spill stores, reloads, slots and frame bytes");
        printf("  Tier 1's rematerialization, %s path: stores %" PRIu64 " (%" PRIu64 " without), reloads %" PRIu64 " (%" PRIu64 "), slots %" PRIu64 " (%" PRIu64 "), frame bytes %" PRIu64
               " (%" PRIu64 "); %" PRIu64 " recomputed, %" PRIu64 " definitions left out, %u addresses recomputed in the loop, %u zeroing xors\n",
               path == 0 ? "vector" : "scalar", with->stores, without->stores, with->loads, without->loads, with->slots, without->slots, with->frame, without->frame,
               with->rematerializations, with->dropped, addresses[1][path], zeroing[1][path]);
    }
    unlink(source);
    unlink(reference);
}

// `path` as sh reads it between single quotes: each ' closes, escapes and reopens its quote. False if it doesn't fit.
static bool check_test_tier1_quoted(char* out, size_t size, const char* path)
{
    size_t n = 0;
    for (const char* p = path; *p != 0; p += 1)
    {
        const char* piece = *p == '\'' ? "'\\''" : NULL;
        const size_t length = piece != NULL ? 4 : 1;
        if (n + length >= size)
        {
            out[0] = 0;
            return false;
        }
        if (piece != NULL)
        {
            memcpy(out + n, piece, length);
        }
        else
        {
            out[n] = *p;
        }
        n += length;
    }
    out[n] = 0;
    return true;
}

// --tier1-gvn (#866): the rematerialization fixture compiled with #866's congruence solver before promotion, by each
// path, against the same fixture compiled without it. Its function is still Tier 1's and its executable still prints
// clang's build's output, so the solver's merged values reach Tier 1's code without changing what the program does; and
// its record count must differ from the build without the option, which is what a dropped `tier1_gvn` would show. The
// option is a measurement one (docs/tier1.md's "The C driver's --tier1" has what it costs), so no direction of that
// difference is asserted here.
static void check_test_driver_tier1_gvn(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    char source[512];
    char reference[512];
    snprintf(source, sizeof(source), "%s/tier1-gvn.c", directory);
    snprintf(reference, sizeof(reference), "%s/tier1-gvn-clang", directory);
    const bool written = parse_test_write(source, check_test_tier1_remat_source, sizeof(check_test_tier1_remat_source) - 1);
    char* clang_arguments[] = { (char*)clang, "-O0", "-o", reference, source, NULL };
    char* clang_text = NULL;
    const bool referenced = written && check_test_driver_spawn(clang_arguments, &clang_text) == 0;
    free(clang_text);
    char expected[256] = { 0 };
    const bool ran = referenced && check_test_driver_capture(reference, expected, sizeof(expected)) && expected[0] != 0;
    check_test_expect(test, ran, "clang's build of the gvn fixture runs");
    for (uint32_t path = 0; ran && path < 2; path += 1)
    {
        char plain_output[512];
        char gvn_output[512];
        snprintf(plain_output, sizeof(plain_output), "%s/tier1-gvn-plain-%u", directory, path);
        snprintf(gvn_output, sizeof(gvn_output), "%s/tier1-gvn-%u", directory, path);
        CheckTestTier1Build plain;
        CheckTestTier1Build with;
        const bool plain_built = check_test_tier1_build(executable, source, "tier1_remat", plain_output, true, path != 0, false, false, NULL, &plain);
        const bool gvn_built = check_test_tier1_build(executable, source, "tier1_remat", gvn_output, true, path != 0, false, true, NULL, &with);
        check_test_expect(test, gvn_built, "--tier1-gvn compiles the fixture, its function Tier 1's");
        check_test_expect(test, plain_built && gvn_built && plain.records != with.records, "--tier1-gvn changes the fixture's code, so the option reached Tier 1");
        char actual[256] = { 0 };
        check_test_expect(test, gvn_built && check_test_driver_capture(gvn_output, actual, sizeof(actual)) && strcmp(actual, expected) == 0,
                          "--tier1-gvn's executable prints clang's build's output");
        printf("  Tier 1's GVN, %s path: %" PRIu64 " records (%" PRIu64 " without the option)\n", path == 0 ? "vector" : "scalar", with.records, plain.records);
        unlink(plain_output);
        unlink(gvn_output);
    }
    // Two source files in one command: each unit's lanes take the option too (#866's review), which a single-file
    // fixture cannot show. -c writes each unit's object in the working directory, so the command runs inside `directory`.
    char first[512];
    char second[512];
    snprintf(first, sizeof(first), "%s/tier1-gvn-two-a.c", directory);
    snprintf(second, sizeof(second), "%s/tier1-gvn-two-b.c", directory);
    // The second unit defines nothing the first does: a command of two units refuses a global name twice, and the
    // first unit's function is the one the solver changes.
    static const char other[] = "int tier1_gvn_two_other(int x)\n{\n    return x + 1;\n}\n";
    const bool two = parse_test_write(first, check_test_tier1_remat_source, sizeof(check_test_tier1_remat_source) - 1) &&
                     parse_test_write(second, other, sizeof(other) - 1);
    check_test_expect(test, two, "the gvn two-file fixture writes");
    uint64_t two_records[2] = { 0, 0 };
    for (uint32_t gvn = 0; two && gvn < 2; gvn += 1)
    {
        // The command runs inside `directory`, so a relative --executable must be resolved first.
        char resolved[4096];
        const char* program = realpath(executable, resolved) != NULL ? resolved : executable;
        // Quoted for sh: a --directory with a quote would otherwise break the command.
        char quoted_directory[4096];
        char quoted_program[4096];
        char quoted_first[4096];
        char quoted_second[4096];
        char script[16384] = { 0 };
        const bool quoted = check_test_tier1_quoted(quoted_directory, sizeof(quoted_directory), directory) &&
                            check_test_tier1_quoted(quoted_program, sizeof(quoted_program), program) &&
                            check_test_tier1_quoted(quoted_first, sizeof(quoted_first), first) &&
                            check_test_tier1_quoted(quoted_second, sizeof(quoted_second), second);
        const int written = quoted ? snprintf(script, sizeof(script), "cd '%s' && '%s' --tier1 %s --tier1-report -c '%s' '%s'", quoted_directory,
                                              quoted_program, gvn != 0 ? "--tier1-gvn" : "", quoted_first, quoted_second)
                                   : -1;
        check_test_expect(test, written > 0 && (size_t)written < sizeof(script), "the gvn two-file command fits its buffer");
        char* arguments[] = { "sh", "-c", script, NULL };
        char* text = NULL;
        const int status = check_test_driver_spawn(arguments, &text);
        // The report's tier1 lines: one per unit or one summed over them, so every line's records are added.
        uint64_t records = 0;
        for (const char* line = text; line != NULL && *line != 0;)
        {
            const char* end = strchr(line, '\n');
            const size_t length = end != NULL ? (size_t)(end - line) : strlen(line);
            if (length > 6 && memcmp(line, "tier1\t", 6) == 0)
            {
                uint64_t value = 0;
                // A report line is longer than its first fields: keep it whole, up to the buffer.
                char field[1024];
                const size_t kept = length < sizeof(field) - 1 ? length : sizeof(field) - 1;
                snprintf(field, sizeof(field), "%.*s", (int)kept, line);
                records += check_test_tier1_field(field, "records", &value) ? value : 0;
            }
            line = end != NULL ? end + 1 : NULL;
        }
        two_records[gvn] = records;
        check_test_expect(test, status == 0 && records != 0, "--tier1 -c compiles two files in one command");
        free(text);
    }
    check_test_expect(test, two && two_records[0] != 0 && two_records[0] != two_records[1], "--tier1-gvn reaches the lanes of a command of several files");
    char object_a[512];
    char object_b[512];
    snprintf(object_a, sizeof(object_a), "%s/tier1-gvn-two-a.o", directory);
    snprintf(object_b, sizeof(object_b), "%s/tier1-gvn-two-b.o", directory);
    unlink(object_a);
    unlink(object_b);
    unlink(first);
    unlink(second);
    unlink(source);
    unlink(reference);
}

// The division fixture (#141), by each path, with and without rematerialization: it prints clang's build's output; its
// function's object has a one-operand multiplication for each of the eight constant divisors and one div, for the
// register; values left in rax and rdx past the divisions are saved around them (main alone saves nothing); and with
// rematerialization (#178) values are recomputed and definitions left out among the divisions, without it none.
static void check_test_driver_tier1_divide(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    enum
    {
        CHECK_TEST_TIER1_DIVIDE_RECORDS = 4096,
    };
    char source[512];
    char main_source[512];
    char reference[512];
    snprintf(source, sizeof(source), "%s/tier1-divide.c", directory);
    snprintf(main_source, sizeof(main_source), "%s/tier1-divide-main.c", directory);
    snprintf(reference, sizeof(reference), "%s/tier1-divide-clang", directory);
    const bool written = parse_test_write(source, check_test_tier1_divide_source, sizeof(check_test_tier1_divide_source) - 1) &&
                         parse_test_write(main_source, check_test_tier1_divide_main, sizeof(check_test_tier1_divide_main) - 1);
    char* clang_arguments[] = { (char*)clang, "-O0", "-o", reference, source, NULL };
    char* clang_text = NULL;
    const bool referenced = written && check_test_driver_spawn(clang_arguments, &clang_text) == 0;
    free(clang_text);
    char expected[256] = { 0 };
    const bool ran = referenced && check_test_driver_capture(reference, expected, sizeof(expected)) && expected[0] != 0;
    check_test_expect(test, ran, "clang's build of the division fixture runs");
    const bool ready = ran && x86_disassembler_initialize();
    X86Decoded* records = calloc(CHECK_TEST_TIER1_DIVIDE_RECORDS, sizeof(X86Decoded));
    uint32_t* offsets = calloc(CHECK_TEST_TIER1_DIVIDE_RECORDS, sizeof(uint32_t));
    uint32_t* types = calloc(CHECK_TEST_TIER1_DIVIDE_RECORDS, sizeof(uint32_t));
    CheckTestTier1Build builds[2][2] = { { { 0 } } };
    for (uint32_t rematerialize = 0; ready && rematerialize < 2; rematerialize += 1)
    {
        for (uint32_t path = 0; path < 2; path += 1)
        {
            char output[512];
            char object[512];
            char main_object[512];
            snprintf(output, sizeof(output), "%s/tier1-divide-%u-%u", directory, rematerialize, path);
            snprintf(object, sizeof(object), "%s/tier1-divide-%u-%u.o", directory, rematerialize, path);
            snprintf(main_object, sizeof(main_object), "%s/tier1-divide-main-%u-%u.o", directory, rematerialize, path);
            CheckTestTier1Build object_build;
            CheckTestTier1Build main_build;
            const bool built = check_test_tier1_build(executable, source, "tier1_divide", output, rematerialize != 0, path != 0, false, false, NULL, &builds[rematerialize][path]);
            const bool compiled = check_test_tier1_build(executable, source, "tier1_divide", object, rematerialize != 0, path != 0, true, false, NULL, &object_build);
            const bool main_compiled = check_test_tier1_build(executable, main_source, "main", main_object, rematerialize != 0, path != 0, true, false, NULL, &main_build);
            check_test_expect(test, built && compiled && main_compiled, "the division fixture compiles by --tier1, its functions Tier 1's");
            check_test_expect(test, main_compiled && main_build.call_stores == 0, "the division fixture's main alone saves nothing around its calls");
            char actual[256] = { 0 };
            check_test_expect(test, built && check_test_driver_capture(output, actual, sizeof(actual)) && strcmp(actual, expected) == 0,
                              "the division fixture prints clang's build's output");
            uint32_t relocations = 0;
            const uint32_t count = compiled && records != NULL && offsets != NULL && types != NULL ?
                                       check_test_tier1_function(object, "tier1_divide", records, CHECK_TEST_TIER1_DIVIDE_RECORDS, offsets, types, &relocations) : 0;
            uint32_t multiplications = 0;
            uint32_t divisions = 0;
            for (uint32_t i = 0; i < count; i += 1)
            {
                const uint32_t form = records[i].instruction.form & BUSTER_X86_FORM_NUMBER;
                multiplications += form == X86_FORM_MUL_RM32 || form == X86_FORM_MUL_RM64 || form == X86_FORM_IMUL_RM32 || form == X86_FORM_IMUL_RM64;
                divisions += form == X86_FORM_DIV_RM32 || form == X86_FORM_DIV_RM64 || form == X86_FORM_IDIV_RM32 || form == X86_FORM_IDIV_RM64;
            }
            check_test_expect(test, count != 0 && multiplications == 8 && divisions == 1,
                              "each of the fixture's eight constant divisors is a reciprocal's multiplication, its register divisor a div");
            unlink(output);
            unlink(object);
            unlink(main_object);
        }
    }
    for (uint32_t path = 0; ready && path < 2; path += 1)
    {
        const CheckTestTier1Build* with = &builds[1][path];
        const CheckTestTier1Build* without = &builds[0][path];
        check_test_expect(test, with->call_stores != 0 && without->call_stores != 0, "values left in rax and rdx past the divisions are saved around them");
        check_test_expect(test, with->rematerializations != 0 && with->dropped != 0 && without->rematerializations == 0 && without->dropped == 0,
                          "with rematerialization, values are recomputed and definitions left out among the divisions, and without it none");
        printf("  Tier 1's divisions, %s path: %" PRIu64 " saves around them (%" PRIu64 " without rematerialization), %" PRIu64 " recomputed values, %" PRIu64
               " definitions left out\n",
               path == 0 ? "vector" : "scalar", with->call_stores, without->call_stores, with->rematerializations, with->dropped);
    }
    free(records);
    free(offsets);
    free(types);
    unlink(source);
    unlink(main_source);
    unlink(reference);
}

// The labels fixture's function `name` in the object at `path`: its indirect jumps through a register and through
// memory, into *registers and *memory, and into *trailing the direct jumps after its last indirect jump: its edges'
// stubs, each of which ends in one to its label's block, since the dispatch block is laid out last (#1016's review).
// Whether it was found.
static bool check_test_tier1_jumps(const char* path, const char* name, X86Decoded* records, uint32_t capacity, uint32_t* offsets, uint32_t* types, uint32_t* registers, uint32_t* memory,
                                   uint32_t* trailing)
{
    uint32_t relocations = 0;
    const uint32_t count = check_test_tier1_function(path, name, records, capacity, offsets, types, &relocations);
    *registers = 0;
    *memory = 0;
    *trailing = 0;
    for (uint32_t i = 0; i < count; i += 1)
    {
        const uint32_t form = records[i].instruction.form & BUSTER_X86_FORM_NUMBER;
        const bool jump = form == X86_FORM_JMP_RM64;
        *registers += jump && records[i].instruction.mode == BUSTER_X86_REGISTER_DIRECT;
        *memory += jump && records[i].instruction.mode != BUSTER_X86_REGISTER_DIRECT;
        *trailing = jump ? 0 : *trailing + (form == X86_FORM_JMP_REL8 || form == X86_FORM_JMP_REL32);
    }
    return count != 0;
}

// The labels fixture (#789): by --tier1, every function Tier 1's, by each path with and without rematerialization, and
// its program prints what clang -O0's and Tier 0's builds of it print. Tier 1's indirect jumps are each function's own:
// one through a register where computed gotos share their dispatch block or a switch's table dispatches, none where the
// jump can go to one label alone, and one with a stub after it where an edge into a label has moves.
static void check_test_driver_tier1_labels(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    enum
    {
        CHECK_TEST_TIER1_LABELS_RECORDS = 4096,
    };
    char source[512];
    char reference[512];
    char tier0[512];
    snprintf(source, sizeof(source), "%s/tier1-labels.c", directory);
    snprintf(reference, sizeof(reference), "%s/tier1-labels-clang", directory);
    snprintf(tier0, sizeof(tier0), "%s/tier1-labels-tier0", directory);
    const bool written = parse_test_write(source, check_test_tier1_labels_source, sizeof(check_test_tier1_labels_source) - 1);
    char* clang_arguments[] = { (char*)clang, "-O0", "-o", reference, source, NULL };
    char* tier0_arguments[] = { (char*)executable, "--verify", "-o", tier0, source, NULL };
    char* text = NULL;
    const bool referenced = written && check_test_driver_spawn(clang_arguments, &text) == 0;
    free(text);
    text = NULL;
    const bool tier0_built = written && check_test_driver_spawn(tier0_arguments, &text) == 0;
    free(text);
    char expected[512] = { 0 };
    char tier0_output[512] = { 0 };
    const bool ran = referenced && check_test_driver_capture(reference, expected, sizeof(expected)) && expected[0] != 0;
    check_test_expect(test, ran, "clang's build of the labels fixture runs");
    check_test_expect(test, ran && tier0_built && check_test_driver_capture(tier0, tier0_output, sizeof(tier0_output)) && strcmp(tier0_output, expected) == 0,
                      "Tier 0's build of the labels fixture prints clang's build's output");
    const bool ready = ran && x86_disassembler_initialize();
    X86Decoded* records = calloc(CHECK_TEST_TIER1_LABELS_RECORDS, sizeof(X86Decoded));
    uint32_t* offsets = calloc(CHECK_TEST_TIER1_LABELS_RECORDS, sizeof(uint32_t));
    uint32_t* types = calloc(CHECK_TEST_TIER1_LABELS_RECORDS, sizeof(uint32_t));
    for (uint32_t rematerialize = 0; ready && rematerialize < 2; rematerialize += 1)
    {
        for (uint32_t path = 0; path < 2; path += 1)
        {
            char output[512];
            char object[512];
            snprintf(output, sizeof(output), "%s/tier1-labels-%u-%u", directory, rematerialize, path);
            snprintf(object, sizeof(object), "%s/tier1-labels-%u-%u.o", directory, rematerialize, path);
            CheckTestTier1Build build;
            CheckTestTier1Build object_build;
            const bool built = check_test_tier1_build(executable, source, "", output, rematerialize != 0, path != 0, false, false, NULL, &build);
            const bool compiled = check_test_tier1_build(executable, source, "", object, rematerialize != 0, path != 0, true, false, NULL, &object_build);
            check_test_expect(test, built && compiled, "the labels fixture compiles by --tier1, every function Tier 1's");
            char actual[512] = { 0 };
            check_test_expect(test, built && check_test_driver_capture(output, actual, sizeof(actual)) && strcmp(actual, expected) == 0,
                              "the labels fixture prints clang's build's output");
            static const char* const names[5] = { "tier1_interpret", "tier1_classify", "tier1_once", "tier1_single", "tier1_both" };
            uint32_t registers[5] = { 0 };
            uint32_t memory[5] = { 0 };
            uint32_t trailing[5] = { 0 };
            bool decoded = compiled && records != NULL && offsets != NULL && types != NULL;
            for (uint32_t f = 0; decoded && f < BUSTER_ARRAY_LENGTH(names); f += 1)
            {
                decoded = check_test_tier1_jumps(object, names[f], records, CHECK_TEST_TIER1_LABELS_RECORDS, offsets, types, &registers[f], &memory[f], &trailing[f]);
            }
            check_test_expect(test, decoded && registers[0] == 1 && memory[0] == 0 && registers[2] == 1 && memory[2] == 0,
                              "a function's computed gotos share one indirect jump through a register, in their dispatch block");
            check_test_expect(test, decoded && registers[1] == 1 && memory[1] == 0, "tier1_classify's switch is one indirect jump through a register");
            check_test_expect(test, decoded && registers[3] == 0 && memory[3] == 0, "tier1_single's jump to its one label is a direct jump");
            check_test_expect(test, decoded && registers[4] == 1 && trailing[4] != 0, "tier1_both's indirect jump has a stub after it, for the label its edge moves into");
            unlink(output);
            unlink(object);
        }
    }
    free(records);
    free(offsets);
    free(types);
    unlink(source);
    unlink(reference);
    unlink(tier0);
}

// Whether the function `name` in `-c`'s object at `path` saves the argument registers in its prologue as a variadic
// definition's register save area: rdi to r9 and xmm0 to xmm7 stored from rsp's frame, the vector ones after al's test.
static bool check_test_tier1_variadic_saves(const char* path, const char* name, X86Decoded* records, uint32_t capacity, uint32_t* offsets, uint32_t* types)
{
    uint32_t relocations = 0;
    const uint32_t count = check_test_tier1_function(path, name, records, capacity, offsets, types, &relocations);
    uint32_t general = 0;
    uint32_t vectors = 0;
    bool tested = false;
    for (uint32_t i = 0; i < count; i += 1)
    {
        const X86Instruction* instruction = &records[i].instruction;
        const uint32_t form = instruction->form & BUSTER_X86_FORM_NUMBER;
        const bool frame = instruction->mode == BUSTER_X86_MEMORY_BASE && instruction->rm == X86_REGISTER_RSP;
        for (uint32_t k = 0; k < BUSTER_ABI_INTEGER_ARGUMENTS; k += 1)
        {
            general |= form == X86_FORM_MOV_RM64_R64 && frame && instruction->reg == abi_integer_arguments[k] ? 1u << k : 0;
        }
        tested |= form == X86_FORM_TEST_RM8_IMM8 && instruction->rm == X86_REGISTER_RAX;
        vectors |= tested && form == X86_FORM_MOVAPS_XM_X && frame && instruction->reg < BUSTER_ABI_VECTOR_ARGUMENTS ? 1u << instruction->reg : 0;
    }
    return general == (1u << BUSTER_ABI_INTEGER_ARGUMENTS) - 1 && vectors == (1u << BUSTER_ABI_VECTOR_ARGUMENTS) - 1;
}

// Whether the function `name` in `-c`'s object at `path` builds its frame without a frame pointer, pushing callee-saved
// registers before its reservation, and takes its overflow area's address in one lea at the first stack argument: rsp
// + 8 per push, plus the reservation, plus 8 for the return address, read off the decoded prologue (#1060's review).
static bool check_test_tier1_variadic_overflow(const char* path, const char* name, X86Decoded* records, uint32_t capacity, uint32_t* offsets, uint32_t* types)
{
    uint32_t relocations = 0;
    const uint32_t count = check_test_tier1_function(path, name, records, capacity, offsets, types, &relocations);
    int64_t pushes = 0;
    int64_t reservation = -1;
    bool pointer = false;
    for (uint32_t i = 0; reservation < 0 && i < count; i += 1)
    {
        const X86Instruction* instruction = &records[i].instruction;
        const uint32_t form = instruction->form & BUSTER_X86_FORM_NUMBER;
        const bool direct = (instruction->mode & BUSTER_X86_MODE_MEMORY) == 0;
        pushes += form == X86_FORM_PUSH_R64;
        pointer |= form == X86_FORM_MOV_RM64_R64 && direct && instruction->rm == X86_REGISTER_RBP && instruction->reg == X86_REGISTER_RSP;
        reservation = (form == X86_FORM_SUB_RM64_IMM8 || form == X86_FORM_SUB_RM64_IMM32) && direct && instruction->rm == X86_REGISTER_RSP ? instruction->immediate : -1;
    }
    const int64_t displacement = 8 * pushes + reservation + 8;
    uint32_t found = 0;
    for (uint32_t i = 0; i < count; i += 1)
    {
        const X86Instruction* instruction = &records[i].instruction;
        found += (instruction->form & BUSTER_X86_FORM_NUMBER) == X86_FORM_LEA_R64_M && instruction->mode == BUSTER_X86_MEMORY_BASE && instruction->rm == X86_REGISTER_RSP &&
                 instruction->displacement == displacement;
    }
    return !pointer && pushes != 0 && reservation > 0 && found == 1;
}

// The variadic definitions' fixture (#789): by --tier1, every variadic definition Tier 1's, by each path with and without
// rematerialization, and its program prints what clang -O0's and Tier 0's builds of it print. Each definition that calls
// va_start saves the argument registers in its prologue; the one that doesn't has no save area. tier1_late pushes the
// callee-saved register its call's crossing value takes, and its overflow area's address counts the push.
static void check_test_driver_tier1_variadic(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    enum
    {
        CHECK_TEST_TIER1_VARIADIC_RECORDS = 4096,
    };
    char source[512];
    char reference[512];
    char tier0[512];
    snprintf(source, sizeof(source), "%s/tier1-variadic.c", directory);
    snprintf(reference, sizeof(reference), "%s/tier1-variadic-clang", directory);
    snprintf(tier0, sizeof(tier0), "%s/tier1-variadic-tier0", directory);
    const bool written = parse_test_write(source, check_test_tier1_variadic_source, sizeof(check_test_tier1_variadic_source) - 1);
    char* clang_arguments[] = { (char*)clang, "-O0", "-o", reference, source, NULL };
    char* tier0_arguments[] = { (char*)executable, "--verify", "-o", tier0, source, NULL };
    char* text = NULL;
    const bool referenced = written && check_test_driver_spawn(clang_arguments, &text) == 0;
    free(text);
    text = NULL;
    const bool tier0_built = written && check_test_driver_spawn(tier0_arguments, &text) == 0;
    free(text);
    char expected[512] = { 0 };
    char tier0_output[512] = { 0 };
    const bool ran = referenced && check_test_driver_capture(reference, expected, sizeof(expected)) && expected[0] != 0;
    check_test_expect(test, ran, "clang's build of the variadic fixture runs");
    check_test_expect(test, ran && tier0_built && check_test_driver_capture(tier0, tier0_output, sizeof(tier0_output)) && strcmp(tier0_output, expected) == 0,
                      "Tier 0's build of the variadic fixture prints clang's build's output");
    const bool ready = ran && x86_disassembler_initialize();
    X86Decoded* records = calloc(CHECK_TEST_TIER1_VARIADIC_RECORDS, sizeof(X86Decoded));
    uint32_t* offsets = calloc(CHECK_TEST_TIER1_VARIADIC_RECORDS, sizeof(uint32_t));
    uint32_t* types = calloc(CHECK_TEST_TIER1_VARIADIC_RECORDS, sizeof(uint32_t));
    // Every definition but the last calls va_start.
    static const char* const names[7] = { "tier1_sum", "tier1_mixed", "tier1_copy", "tier1_tail", "tier1_format", "tier1_late", "tier1_ignore" };
    const uint32_t starts = BUSTER_ARRAY_LENGTH(names) - 1;
    for (uint32_t rematerialize = 0; ready && rematerialize < 2; rematerialize += 1)
    {
        for (uint32_t path = 0; path < 2; path += 1)
        {
            char output[512];
            char object[512];
            snprintf(output, sizeof(output), "%s/tier1-variadic-%u-%u", directory, rematerialize, path);
            snprintf(object, sizeof(object), "%s/tier1-variadic-%u-%u.o", directory, rematerialize, path);
            CheckTestTier1Build build;
            CheckTestTier1Build object_build;
            const bool built = check_test_tier1_build_names(executable, source, names, BUSTER_ARRAY_LENGTH(names), output, rematerialize != 0, path != 0, false, false, NULL, &build);
            const bool compiled = check_test_tier1_build_names(executable, source, names, BUSTER_ARRAY_LENGTH(names), object, rematerialize != 0, path != 0, true, false, NULL, &object_build);
            check_test_expect(test, built && compiled, "the variadic fixture compiles by --tier1, every variadic definition Tier 1's");
            char actual[512] = { 0 };
            check_test_expect(test, built && check_test_driver_capture(output, actual, sizeof(actual)) && strcmp(actual, expected) == 0,
                              "the variadic fixture prints clang's build's output");
            bool saved = compiled && records != NULL && offsets != NULL && types != NULL;
            for (uint32_t f = 0; saved && f < starts; f += 1)
            {
                saved = check_test_tier1_variadic_saves(object, names[f], records, CHECK_TEST_TIER1_VARIADIC_RECORDS, offsets, types);
            }
            check_test_expect(test, saved, "each variadic definition that calls va_start saves the argument registers in its prologue");
            const bool decodable = compiled && records != NULL && offsets != NULL && types != NULL;
            check_test_expect(test, decodable && !check_test_tier1_variadic_saves(object, names[starts], records, CHECK_TEST_TIER1_VARIADIC_RECORDS, offsets, types),
                              "a variadic definition that reads no unnamed argument saves none");
            check_test_expect(test, decodable && check_test_tier1_variadic_overflow(object, "tier1_late", records, CHECK_TEST_TIER1_VARIADIC_RECORDS, offsets, types),
                              "tier1_late pushes a callee-saved register, and its overflow area's address is rsp past the push, its reservation and the return address");
            unlink(output);
            unlink(object);
        }
    }
    free(records);
    free(offsets);
    free(types);
    unlink(source);
    unlink(reference);
    unlink(tier0);
}

// The opmask fixture (#179), by each path, with and without rematerialization, for x86-64-v4: its function of every _k*
// builtin compiles by Tier 1 (its rows through ir_simd_typed.c's decoder, its masks in the opmask class), and where the
// host has AVX-512 F, BW, DQ and VL its executable prints clang -O0's build's output. Its object holds opmask
// instructions: the masks stay in k registers between their conversions.
static void check_test_driver_tier1_masks(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    enum
    {
        CHECK_TEST_TIER1_MASKS_RECORDS = 16384,
    };
    char source[512];
    char reference[512];
    snprintf(source, sizeof(source), "%s/tier1-masks.c", directory);
    snprintf(reference, sizeof(reference), "%s/tier1-masks-clang", directory);
    const bool written = parse_test_write(source, check_test_tier1_masks_source, sizeof(check_test_tier1_masks_source) - 1);
    char* clang_arguments[] = { (char*)clang, "-O0", "-march=x86-64-v4", "-o", reference, source, NULL };
    char* clang_text = NULL;
    const bool referenced = written && check_test_driver_spawn(clang_arguments, &clang_text) == 0;
    free(clang_text);
    check_test_expect(test, referenced, "clang builds the opmask fixture");
    const uint64_t needed = BUSTER_X86_FEATURE(AVX512F) | BUSTER_X86_FEATURE(AVX512BW) | BUSTER_X86_FEATURE(AVX512DQ) | BUSTER_X86_FEATURE(AVX512VL);
    const bool host = (x86_target_native().features & needed) == needed;
    char expected[256] = { 0 };
    const bool ran = referenced && host && check_test_driver_capture(reference, expected, sizeof(expected)) && expected[0] != 0;
    check_test_expect(test, !host || ran, "clang's build of the opmask fixture runs");
    const bool ready = referenced && x86_disassembler_initialize();
    X86Decoded* records = calloc(CHECK_TEST_TIER1_MASKS_RECORDS, sizeof(X86Decoded));
    uint32_t* offsets = calloc(CHECK_TEST_TIER1_MASKS_RECORDS, sizeof(uint32_t));
    uint32_t* types = calloc(CHECK_TEST_TIER1_MASKS_RECORDS, sizeof(uint32_t));
    uint32_t opmask = 0;
    for (uint32_t rematerialize = 0; ready && rematerialize < 2; rematerialize += 1)
    {
        for (uint32_t path = 0; path < 2; path += 1)
        {
            char output[512];
            char object[512];
            snprintf(output, sizeof(output), "%s/tier1-masks-%u-%u", directory, rematerialize, path);
            snprintf(object, sizeof(object), "%s/tier1-masks-%u-%u.o", directory, rematerialize, path);
            CheckTestTier1Build build;
            CheckTestTier1Build object_build;
            const bool built = check_test_tier1_build(executable, source, "tier1_masks", output, rematerialize != 0, path != 0, false, false, "-march=x86-64-v4", &build);
            const bool compiled = check_test_tier1_build(executable, source, "tier1_masks", object, rematerialize != 0, path != 0, true, false, "-march=x86-64-v4", &object_build);
            check_test_expect(test, built && compiled, "the opmask fixture compiles by --tier1, its function of _k* builtins Tier 1's");
            char actual[256] = { 0 };
            check_test_expect(test, !host || (built && check_test_driver_capture(output, actual, sizeof(actual)) && strcmp(actual, expected) == 0),
                              "the opmask fixture prints clang's build's output");
            uint32_t relocations = 0;
            const uint32_t count = compiled && records != NULL && offsets != NULL && types != NULL ?
                                       check_test_tier1_function(object, "tier1_masks", records, CHECK_TEST_TIER1_MASKS_RECORDS, offsets, types, &relocations) : 0;
            uint32_t found = 0;
            for (uint32_t i = 0; i < count; i += 1)
            {
                const X86FormDescription* description = x86_form_description(records[i].instruction.form & BUSTER_X86_FORM_NUMBER);
                found += (description->reg & BUSTER_X86_OPERAND_CLASS) == BUSTER_X86_CLASS_OPMASK || (description->rm & BUSTER_X86_OPERAND_CLASS) == BUSTER_X86_CLASS_OPMASK;
            }
            check_test_expect(test, count != 0 && found != 0, "the opmask fixture's function keeps its masks in opmask registers");
            opmask = found > opmask ? found : opmask;
            unlink(output);
            unlink(object);
        }
    }
    printf("  Tier 1's opmask builtins: %u instructions on opmask registers%s\n", opmask, host ? "" : " (compiled only: no AVX-512 here)");
    free(records);
    free(offsets);
    free(types);
    unlink(source);
    unlink(reference);
}

// The narrow arguments' fixture (#141): its caller unit by --tier1 -c, by each path with and without rematerialization,
// every function but main Tier 1's, linked with its callee unit compiled by clang -O2, prints what clang -O0's build of
// both prints: each narrow argument arrives extended to 32 bits by its parameter's type, as clang's callees trust.
static void check_test_driver_tier1_narrow(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    char caller[512];
    char callee[512];
    char callee_object[512];
    char reference[512];
    snprintf(caller, sizeof(caller), "%s/tier1-narrow.c", directory);
    snprintf(callee, sizeof(callee), "%s/tier1-narrow-callee.c", directory);
    snprintf(callee_object, sizeof(callee_object), "%s/tier1-narrow-callee.o", directory);
    snprintf(reference, sizeof(reference), "%s/tier1-narrow-clang", directory);
    const bool written = parse_test_write(caller, check_test_tier1_narrow_caller_source, sizeof(check_test_tier1_narrow_caller_source) - 1) &&
                         parse_test_write(callee, check_test_tier1_narrow_callee_source, sizeof(check_test_tier1_narrow_callee_source) - 1);
    char* reference_arguments[] = { (char*)clang, "-O0", "-o", reference, caller, callee, NULL };
    char* callee_arguments[] = { (char*)clang, "-O2", "-c", "-o", callee_object, callee, NULL };
    char* text = NULL;
    const bool referenced = written && check_test_driver_spawn(reference_arguments, &text) == 0;
    free(text);
    text = NULL;
    const bool callee_built = written && check_test_driver_spawn(callee_arguments, &text) == 0;
    free(text);
    char expected[256] = { 0 };
    const bool ran = referenced && callee_built && check_test_driver_capture(reference, expected, sizeof(expected)) && expected[0] != 0;
    check_test_expect(test, ran, "clang's builds of the narrow arguments' fixture run");
    static const char* const names[5] = { "tier1_call_narrow", "tier1_call_mixed", "tier1_call_cross_signedness", "tier1_narrow_parameters", "tier1_narrow_entry" };
    for (uint32_t rematerialize = 0; ran && rematerialize < 2; rematerialize += 1)
    {
        for (uint32_t path = 0; path < 2; path += 1)
        {
            char object[512];
            char program[512];
            snprintf(object, sizeof(object), "%s/tier1-narrow-%u-%u.o", directory, rematerialize, path);
            snprintf(program, sizeof(program), "%s/tier1-narrow-%u-%u", directory, rematerialize, path);
            CheckTestTier1Build build;
            const bool compiled = check_test_tier1_build_names(executable, caller, names, BUSTER_ARRAY_LENGTH(names), object, rematerialize != 0, path != 0, true, false, NULL, &build);
            check_test_expect(test, compiled, "the narrow arguments' caller unit compiles by --tier1, every caller Tier 1's");
            char* link_arguments[] = { (char*)clang, "-o", program, object, callee_object, NULL };
            char* link_text = NULL;
            const bool linked = compiled && check_test_driver_spawn(link_arguments, &link_text) == 0;
            free(link_text);
            char actual[256] = { 0 };
            check_test_expect(test, linked && check_test_driver_capture(program, actual, sizeof(actual)) && strcmp(actual, expected) == 0,
                              "Tier 1's narrow arguments reach clang -O2's callees extended by their types");
            unlink(object);
            unlink(program);
        }
    }
    unlink(caller);
    unlink(callee);
    unlink(callee_object);
    unlink(reference);
}

// Slot sharing between frame objects (#177, c_check_program_lifetime.c and tier1_emit_cfg_lifetimes): each function's
// objects in blocks of their own, which --tier1 gives lifetimes and Tier 1's frame lets share slots, and the blocks that
// keep theirs: one a goto enters past its opening, a switch's own block, those around labels whose addresses are taken.
// Arrays in sibling blocks, in a loop's body left by break and continue, after it, beside the switch's cases, compound
// literals and an if's two arms; and a pointer to an array kept across a goto back inside its block, which C lets the
// next round read. Clang's build is the oracle of each build, with lifetimes and without (--tier1-no-lifetimes).
static const char check_test_tier1_lifetimes_source[] =
    "#include <stdio.h>\n"
    "#include <string.h>\n"
    "\n"
    "static void fill(int* p, int n, int seed) { for (int i = 0; i < n; i += 1) { p[i] = seed + i; } }\n"
    "static int sum(const int* p, int n) { int s = 0; for (int i = 0; i < n; i += 1) { s += p[i]; } return s; }\n"
    "\n"
    "// Two sibling blocks: their arrays can share one slot.\n"
    "int siblings(int x)\n"
    "{\n"
    "    int total = 0;\n"
    "    {\n"
    "        int a[64];\n"
    "        fill(a, 64, x);\n"
    "        total += sum(a, 64);\n"
    "    }\n"
    "    {\n"
    "        int b[64];\n"
    "        fill(b, 64, x * 2);\n"
    "        total += sum(b, 64);\n"
    "    }\n"
    "    return total;\n"
    "}\n"
    "\n"
    "// A loop body's array, live across the call, and a break and a continue that leave its block.\n"
    "int loop(int n)\n"
    "{\n"
    "    int total = 0;\n"
    "    for (int i = 0; i < n; i += 1)\n"
    "    {\n"
    "        int a[32];\n"
    "        fill(a, 32, i);\n"
    "        if (i == 3) { continue; }\n"
    "        if (i == 7) { break; }\n"
    "        {\n"
    "            int b[32];\n"
    "            fill(b, 32, sum(a, 32));\n"
    "            total += sum(b, 32);\n"
    "        }\n"
    "    }\n"
    "    {\n"
    "        int c[32];\n"
    "        fill(c, 32, total);\n"
    "        total = sum(c, 32);\n"
    "    }\n"
    "    return total;\n"
    "}\n"
    "\n"
    "// A goto that leaves a block, and one that enters another past its declaration (no lifetimes there).\n"
    "int jumps(int x)\n"
    "{\n"
    "    int total = 0;\n"
    "    {\n"
    "        int a[16];\n"
    "        fill(a, 16, x);\n"
    "        if (x > 2) { goto out; }\n"
    "        total += sum(a, 16);\n"
    "    }\n"
    "    {\n"
    "        int b[16];\n"
    "        fill(b, 16, x + 1);\n"
    "        total += sum(b, 16);\n"
    "    }\n"
    "out:\n"
    "    if (x > 5) { goto inside; }\n"
    "    {\n"
    "        int c[16];\n"
    "        fill(c, 16, x + 2);\n"
    "inside:\n"
    "        c[0] = x;\n"
    "        total += c[0];\n"
    "    }\n"
    "    return total;\n"
    "}\n"
    "\n"
    "// A switch's own block's array, and a case's block.\n"
    "int choose(int x)\n"
    "{\n"
    "    int total = 0;\n"
    "    switch (x)\n"
    "    {\n"
    "        int shared[8];\n"
    "    case 0:\n"
    "        fill(shared, 8, 1);\n"
    "        total = sum(shared, 8);\n"
    "        break;\n"
    "    case 1:\n"
    "    {\n"
    "        int mine[8];\n"
    "        fill(mine, 8, 2);\n"
    "        total = sum(mine, 8);\n"
    "        break;\n"
    "    }\n"
    "    default:\n"
    "    {\n"
    "        int other[8];\n"
    "        fill(other, 8, 3);\n"
    "        total = sum(other, 8);\n"
    "    }\n"
    "    }\n"
    "    {\n"
    "        int after[8];\n"
    "        fill(after, 8, total);\n"
    "        total += sum(after, 8);\n"
    "    }\n"
    "    return total;\n"
    "}\n"
    "\n"
    "// A label whose address is taken: its blocks have no lifetimes.\n"
    "int computed(int x)\n"
    "{\n"
    "    static void* targets[] = { &&first, &&second };\n"
    "    int total = 0;\n"
    "    {\n"
    "        int a[8];\n"
    "        fill(a, 8, x);\n"
    "        total += sum(a, 8);\n"
    "        goto *targets[x & 1];\n"
    "    }\n"
    "    {\n"
    "first:;\n"
    "        int b[8];\n"
    "        fill(b, 8, 5);\n"
    "        total += sum(b, 8);\n"
    "    }\n"
    "second:\n"
    "    {\n"
    "        int c[8];\n"
    "        fill(c, 8, 7);\n"
    "        total += sum(c, 8);\n"
    "    }\n"
    "    return total;\n"
    "}\n"
    "\n"
    "// Compound literals and structs in sibling blocks.\n"
    "struct point { int x, y, z, w; };\n"
    "static int norm(const struct point* p) { return p->x + p->y + p->z + p->w; }\n"
    "int literals(int x)\n"
    "{\n"
    "    int total = 0;\n"
    "    {\n"
    "        total += norm(&(struct point){ x, x + 1, x + 2, x + 3 });\n"
    "    }\n"
    "    {\n"
    "        struct point q = { x * 2, 1, 2, 3 };\n"
    "        total += norm(&q);\n"
    "    }\n"
    "    if (x) {\n"
    "        char text[40];\n"
    "        snprintf(text, sizeof(text), \"%d\", x);\n"
    "        total += (int)strlen(text);\n"
    "    } else {\n"
    "        char other[40];\n"
    "        memset(other, 'a', 39);\n"
    "        other[39] = 0;\n"
    "        total += (int)strlen(other);\n"
    "    }\n"
    "    return total;\n"
    "}\n"
    "\n"
    "// A pointer saved in one iteration and read after a backward goto within the same block: valid C.\n"
    "int backward(int n)\n"
    "{\n"
    "    int total = 0;\n"
    "    {\n"
    "        int* saved = 0;\n"
    "        int rounds = 0;\n"
    "again:\n"
    "        if (saved != 0) { total += *saved; }\n"
    "        int value[2] = { rounds * 3, 1 };\n"
    "        saved = value;\n"
    "        rounds += 1;\n"
    "        if (rounds < n) { goto again; }\n"
    "    }\n"
    "    {\n"
    "        int spill[2] = { 100, 200 };\n"
    "        total += spill[0] + spill[1];\n"
    "    }\n"
    "    return total;\n"
    "}\n"
    "\n"
    "int main(void)\n"
    "{\n"
    "    int total = 0;\n"
    "    for (int i = 0; i < 9; i += 1)\n"
    "    {\n"
    "        total += siblings(i) + loop(i) + jumps(i) + choose(i % 3) + computed(i) + literals(i) + backward(i);\n"
    "        printf(\"%d %d %d %d %d %d %d\\n\", siblings(i), loop(i), jumps(i), choose(i % 3), computed(i), literals(i), backward(i));\n"
    "    }\n"
    "    printf(\"%d\\n\", total);\n"
    "    return 0;\n"
    "}\n";

// The lifetime fixture compiled by --tier1 (`lifetimes` or not, by each path) into `output`, whose report's frame bytes
// go into *frame: whether it compiled with siblings, loop and literals Tier 1's.
static bool check_test_tier1_lifetime_build(const char* executable, const char* source, const char* output, bool lifetimes, bool scalar, uint64_t* frame)
{
    char* arguments[16] = { (char*)executable, "--tier1", "--tier1-report", "--tier1-refusals", "--verify" };
    uint32_t count = 5;
    if (!lifetimes)
    {
        arguments[count++] = "--tier1-no-lifetimes";
    }
    if (scalar)
    {
        arguments[count++] = "--scalar";
    }
    arguments[count++] = "-o";
    arguments[count++] = (char*)output;
    arguments[count++] = (char*)source;
    arguments[count] = NULL;
    char* text = NULL;
    const int status = check_test_driver_spawn(arguments, &text);
    *frame = 0;
    const bool reported = check_test_tier1_field(text, "frame_bytes", frame);
    const bool taken = text != NULL && strstr(text, "tier1-refused\tsiblings\t") == NULL && strstr(text, "tier1-refused\tloop\t") == NULL &&
                       strstr(text, "tier1-refused\tliterals\t") == NULL;
    if (status != 0 && text != NULL)
    {
        fprintf(stderr, "%s", text);
    }
    free(text);
    return status == 0 && reported && taken;
}

static void check_test_driver_tier1_lifetimes(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    char source[512];
    char reference[512];
    snprintf(source, sizeof(source), "%s/tier1-lifetimes.c", directory);
    snprintf(reference, sizeof(reference), "%s/tier1-lifetimes-clang", directory);
    const bool written = parse_test_write(source, check_test_tier1_lifetimes_source, sizeof(check_test_tier1_lifetimes_source) - 1);
    char* clang_arguments[] = { (char*)clang, "-O0", "-w", "-o", reference, source, NULL };
    char* clang_text = NULL;
    const bool referenced = written && check_test_driver_spawn(clang_arguments, &clang_text) == 0;
    free(clang_text);
    char expected[4096] = { 0 };
    const bool ran = referenced && check_test_driver_capture(reference, expected, sizeof(expected)) && expected[0] != 0;
    check_test_expect(test, ran, "clang's build of the lifetime fixture runs");
    uint64_t frames[2][2] = { { 0 } };
    for (uint32_t lifetimes = 0; ran && lifetimes < 2; lifetimes += 1)
    {
        for (uint32_t path = 0; path < 2; path += 1)
        {
            char output[512];
            snprintf(output, sizeof(output), "%s/tier1-lifetimes-%u-%u", directory, lifetimes, path);
            const bool built = check_test_tier1_lifetime_build(executable, source, output, lifetimes != 0, path != 0, &frames[lifetimes][path]);
            check_test_expect(test, built, "the lifetime fixture compiles by --tier1, its sharing functions Tier 1's");
            char actual[4096] = { 0 };
            check_test_expect(test, built && check_test_driver_capture(output, actual, sizeof(actual)) && strcmp(actual, expected) == 0,
                              "the lifetime fixture prints clang's build's output");
            unlink(output);
        }
    }
    for (uint32_t path = 0; ran && path < 2; path += 1)
    {
        check_test_expect(test, frames[1][path] != 0 && frames[1][path] < frames[0][path], "objects' lifetimes shrink Tier 1's frames");
        printf("  Tier 1's frames, %s path: %" PRIu64 " bytes with lifetimes, %" PRIu64 " without\n", path == 0 ? "vector" : "scalar", frames[1][path], frames[0][path]);
    }
    check_test_expect(test, !ran || (frames[1][0] == frames[1][1] && frames[0][0] == frames[0][1]), "both paths' frames take the same bytes");
    unlink(source);
    unlink(reference);
}
