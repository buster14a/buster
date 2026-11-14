// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Tier 1's rematerialization (#178) through the C driver's --tier1. An original fixture's loop keeps more addresses and
// constants live than there are registers, each recipe among them: statics' addresses from rip, a static array's
// member's at an addend, extern variables' from their GOT slots, frame objects' addresses and a member's of one, and
// constants of each encoding (a zero, a 32-bit one, a sign-extended and a 64-bit one), each a value the C driver's typed
// stream converts (its pointers are 64-bit integers). Its statics are indexed by the loop's k: an access at a constant
// offset takes its symbol rip-relative (#2573), which leaves no address live. A second fixture's loop divides by
// constants and by a register while such values stay live across the divisions, whose sequences take rax and rdx
// (#141). Clang's build of the same source is each one's oracle. Included by c_checker_driver_test.c after its ELF
// readers.

// The fixture: tier1_remat is the function under test, main prints its results.
static const char check_test_tier1_remat_source[] =
    "#include <stdio.h>\n"
    "#include <unistd.h>\n"
    "extern char** environ;\n"
    "static long s0[2], s1[2], s2[2], s3[2], s4[2], s5[2];\n"
    "static long sa[8];\n"
    "long tier1_remat(long n)\n"
    "{\n"
    "    long a0[2], a1[2], a2[2], a3[2], a4[2], a5[4];\n"
    "    long *p0 = a0, *p1 = a1, *p2 = a2, *p3 = a3, *p4 = a4, *p5 = &a5[1];\n"
    "    long *q0 = s0, *q1 = s1, *q2 = s2, *q3 = s3, *q4 = s4, *q5 = s5, *q6 = &sa[3];\n"
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
    "        q0[k] += i; q1[k] ^= i; q2[k] -= i; q3[k] += i * 5; q4[k] ^= i + 3; q5[k] += i ^ 9; q6[k] += i;\n"
    "        total += (*r0 != 0) + *r1 + *r2;\n"
    "        total = total * 31 + wide;\n"
    "        total += middle;\n"
    "        p5[k ^ 1] = zero;\n"
    "        q6[k ^ 1] = negative;\n"
    "    }\n"
    "    return total + p0[0] + p0[1] + p1[0] + p1[1] + p2[0] + p2[1] + p3[0] + p3[1] + p4[0] + p4[1] + a5[0] + a5[1] + a5[2] + a5[3] +\n"
    "           s0[0] + s0[1] + s1[0] + s1[1] + s2[0] + s2[1] + s3[0] + s3[1] + s4[0] + s4[1] + s5[0] + s5[1] + sa[3] + sa[4];\n"
    "}\n"
    "int main(void)\n"
    "{\n"
    "    printf(\"%ld %ld %ld\\n\", tier1_remat(0), tier1_remat(7), tier1_remat(1000));\n"
    "    return 0;\n"
    "}\n";

// The division fixture: tier1_divide is the function under test. Its loop divides by eight constants, each a reciprocal's
// sequence of another shape (64-bit signed by 10 and its remainder by 641, unsigned remainder by 1000003, the add
// sequence's division by 7 and the even divisor's pre-shift by 14; 32-bit signed by -7, unsigned remainder by 10 and
// division by 7), and by a register, while statics' and frame objects' addresses and constants stay live. The statics are
// indexed by k: an access at a constant offset takes its symbol rip-relative (#2573), which would leave no address to
// keep. main's calls leave nothing live across them, so the saves its build reports are tier1_divide's
// (check_test_tier1_divide_main).
static const char check_test_tier1_divide_source[] =
    "#include <stdio.h>\n"
    "static long s0[2], s1[2], s2[2], s3[2];\n"
    "static unsigned long su[4];\n"
    "unsigned long tier1_divide(long n, long d)\n"
    "{\n"
    "    long a0[2], a1[2], a2[2];\n"
    "    long *p0 = a0, *p1 = a1, *p2 = a2;\n"
    "    long *q0 = s0, *q1 = s1, *q2 = s2, *q3 = s3;\n"
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
    "        q0[k] += (unsigned)x % 10u;\n"
    "        q1[k] ^= i / d;\n"
    "        q2[k] += i % 641;\n"
    "        q3[k] ^= (long)(x / 7u);\n"
    "        u[k] += x / 14u + (unsigned)x / 7u;\n"
    "        total = total * 31u + wide + middle + (unsigned long)p0[k];\n"
    "    }\n"
    "    return total + (unsigned long)(p0[0] + p0[1] + p1[0] + p1[1] + p2[0] + p2[1]) + (unsigned long)(s0[0] + s0[1] + s1[0] + s1[1] + s2[0] + s2[1] + s3[0] + s3[1]) + su[1] + su[2];\n"
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
// tier1_once loads its target from its table; tier1_single can jump to one label alone. tier1_tables has two switch
// tables (#1217), with holes, signed and unsigned nonzero lower bounds, whose dispatches share each table's address.
// tier1_table_loop keeps one table base before its loop and dispatches with movsxd, add and jmp on each iteration.
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
    "static long tier1_tables(int x, unsigned long y)\n"
    "{\n"
    "    long r = 13;\n"
    "    switch (x)\n"
    "    {\n"
    "        case -3: r += 5; break;\n"
    "        case -2: r ^= 7; break;\n"
    "        case 0: r -= 11; break;\n"
    "        case 2: r *= 3; break;\n"
    "        default: r += 17; break;\n"
    "    }\n"
    "    switch (y)\n"
    "    {\n"
    "        case 18446744073709551611ul: r += x; break;\n"
    "        case 18446744073709551612ul: r ^= x; break;\n"
    "        case 18446744073709551614ul: r += x * 3; break;\n"
    "        case 18446744073709551615ul: r -= x * 5; break;\n"
    "        default: r -= x; break;\n"
    "    }\n"
    "    return r;\n"
    "}\n"
    "static long tier1_table_loop(long n)\n"
    "{\n"
    "    long r = 0;\n"
    "    for (long i = 0; i < n; i += 1)\n"
    "    {\n"
    "        switch (i & 7)\n"
    "        {\n"
    "        case 0: r += 3; break;\n"
    "        case 1: r ^= 5; break;\n"
    "        case 2: r -= 7; break;\n"
    "        case 3: r += i; break;\n"
    "        default: r ^= i; break;\n"
    "        }\n"
    "    }\n"
    "    return r;\n"
    "}\n"
    "int main(void)\n"
    "{\n"
    "    static const unsigned char program[] = { 0, 1, 2, 0, 1, 1, 2, 0, 3 };\n"
    "    printf(\"%ld %ld %ld\\n\", tier1_interpret(program, 5), tier1_classify(1000, 3), tier1_classify(77, 9));\n"
    "    printf(\"%d %d\\n\", tier1_pick(1), tier1_pick(0));\n"
    "    printf(\"%ld %ld %ld %ld\\n\", tier1_both(5), tier1_both(6), tier1_both(8), tier1_both(-3));\n"
    "    printf(\"%d %d %d %d\\n\", tier1_once(0), tier1_once(1), tier1_once(2), tier1_single(41));\n"
    "    unsigned long sum = 0;\n"
    "    for (int i = -8; i <= 8; i += 1)\n"
    "    {\n"
    "        sum = sum * 33 + (unsigned long)tier1_tables(i, (unsigned long)i);\n"
    "    }\n"
    "    printf(\"%lu\\n\", sum);\n"
    "    printf(\"%ld %ld %ld %ld\\n\", tier1_table_loop(0), tier1_table_loop(1), tier1_table_loop(17), tier1_table_loop(257));\n"
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
    "__attribute__((noinline)) static long tier1_bump(long x)\n"
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

// The stack arguments' fixture (#141): a unit by --tier1 whose functions pass 7 to 24 arguments of mixed types (longs,
// ints, pointers, chars and shorts, extended by their types, doubles and a float past xmm7; 21 to a variadic function)
// to functions clang -O2 compiles in the other unit, which read the ones past the registers from the stack, and take 7
// to 64 parameters (#1847: _mm512_set_epi8's 64 chars) from callers clang -O2 compiles there, some through function
// pointers. Each of the other unit's functions folds its arguments into one value.
static const char check_test_tier1_stack_caller_source[] =
    "#include <stdio.h>\n"
    "long tier1_other7(long a, int b, long c, char* d, short e, long f, long g);\n"
    "long tier1_other8(int a, long b, long c, long d, long e, unsigned char f, long g, int h);\n"
    "long tier1_other12(long a, long b, int c, long d, long e, long f, signed char g, long h, long* i, int j, long k, unsigned short l);\n"
    "long tier1_other16(long a, long b, long c, long d, long e, long f, long g, long h, long i, long j, long k, long l, long m, long n, long o, long p);\n"
    "long tier1_drive(long (*seven)(long, long, long, long, long, long, long), long (*twelve)(long, long, long, long, long, long, int, long, long, long, short, long), long x);\n"
    "double tier1_other24(long a, double b, int c, double d, long e, double f, short g, double h, long i, double j, long k, double l, long m, double n, long o, double p, long q, double r, long s, double t, long u, float v, long w, double x);\n"
    "long tier1_other_va(int n, ...);\n"
    "long tier1_drive64(long (*take)(signed char, signed char, signed char, signed char, signed char, signed char, signed char, signed char, signed char, signed char, signed char, signed char, signed char, signed char, signed char, signed char,"
    " signed char, signed char, signed char, signed char, signed char, signed char, signed char, signed char, signed char, signed char, signed char, signed char, signed char, signed char, signed char, signed char,"
    " signed char, signed char, signed char, signed char, signed char, signed char, signed char, signed char, signed char, signed char, signed char, signed char, signed char, signed char, signed char, signed char,"
    " signed char, signed char, signed char, signed char, signed char, signed char, signed char, signed char, signed char, signed char, signed char, signed char, signed char, signed char, signed char, signed char), long x);\n"
    "static long tier1_many(long x, long y)\n"
    "{\n"
    "    double d = tier1_other24(x, 0.5 * (double)y, (int)(x + 1), (double)x - 2.25, y * 3, 1.0 / ((double)y + 0.5), (short)(x * 5), -(double)x, y ^ 9, (double)(x * y), x - 11, 3.75,\n"
    "                             y + 13, (double)x * 0.125, x * 17, (double)y - 19.5, y * 23, 29.0, x ^ y, (double)(x + y), ~x, (float)y * 0.5f, x + y + 31, (double)(x - y) * 0.25);\n"
    "    long v = tier1_other_va(20, x, y, x + 1, y + 1, x + 2, y + 2, x + 3, y + 3, x + 4, y + 4, x + 5, y + 5, x + 6, y + 6, x + 7, y + 7, x + 8, y + 8, x + 9, y + 9);\n"
    "    return (long)(d * 8.0) * 31 + v;\n"
    "}\n"
    "long tier1_take64(signed char a0, signed char a1, signed char a2, signed char a3, signed char a4, signed char a5, signed char a6, signed char a7, signed char a8, signed char a9, signed char a10, signed char a11,"
    " signed char a12, signed char a13, signed char a14, signed char a15, signed char a16, signed char a17, signed char a18, signed char a19, signed char a20, signed char a21, signed char a22, signed char a23,"
    " signed char a24, signed char a25, signed char a26, signed char a27, signed char a28, signed char a29, signed char a30, signed char a31, signed char a32, signed char a33, signed char a34, signed char a35,"
    " signed char a36, signed char a37, signed char a38, signed char a39, signed char a40, signed char a41, signed char a42, signed char a43, signed char a44, signed char a45, signed char a46, signed char a47,"
    " signed char a48, signed char a49, signed char a50, signed char a51, signed char a52, signed char a53, signed char a54, signed char a55, signed char a56, signed char a57, signed char a58, signed char a59,"
    " signed char a60, signed char a61, signed char a62, signed char a63)\n"
    "{\n"
    "    const signed char v[64] = { a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25, a26, a27, a28, a29, a30, a31,\n"
    "                                a32, a33, a34, a35, a36, a37, a38, a39, a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53, a54, a55, a56, a57, a58, a59, a60, a61, a62, a63 };\n"
    "    long h = 0;\n"
    "    for (int i = 0; i < 64; i += 1) h = h * 3 + v[i] * (i + 1);\n"
    "    return h;\n"
    "}\n"
    "static long tier1_pass(long x, long y)\n"
    "{\n"
    "    char text[4] = { 'a', 'b', 'c', 0 };\n"
    "    long cell = x ^ 77;\n"
    "    long r = tier1_other7(x, (int)y, x + y, text, (short)(x * 3), x - y, y * 5);\n"
    "    r = r * 31 + tier1_other8((int)(x + 1), y, x * y, x ^ y, ~x, (unsigned char)(y + 200), x + 7, (int)-y);\n"
    "    r = r * 31 + tier1_other12(x, y, (int)(x - 9), y + 1, x + 2, y + 3, (signed char)(x * 11), y - 4, &cell, (int)(y * 13), x * 9, (unsigned short)(y * 4099));\n"
    "    r = r * 31 + tier1_other16(x, y, x + 1, y + 1, x + 2, y + 2, x + 3, y + 3, x + 4, y + 4, x + 5, y + 5, x + 6, y + 6, x + 7, y + 7);\n"
    "    return r;\n"
    "}\n"
    "long tier1_take7(long a, long b, long c, long d, long e, long f, long g)\n"
    "{\n"
    "    return ((((((a * 3 + b) * 3 + c) * 3 + d) * 3 + e) * 3 + f) * 3 + g);\n"
    "}\n"
    "long tier1_take12(long a, long b, long c, long d, long e, long f, int g, long h, long i, long j, short k, long l)\n"
    "{\n"
    "    long r = a ^ (b * 2) ^ (c * 4) ^ (d * 8) ^ (e * 16) ^ (f * 32);\n"
    "    return r * 1000003 + g * 7 + h * 11 + (i ^ j) * 13 + k * 17 + l;\n"
    "}\n"
    "int main(void)\n"
    "{\n"
    "    unsigned long total = 0;\n"
    "    for (long x = -40; x < 40; x += 3)\n"
    "    {\n"
    "        total = total * 31 + (unsigned long)tier1_pass(x, x * 7 - 5) + (unsigned long)tier1_drive(tier1_take7, tier1_take12, x);\n"
    "        total = total * 31 + (unsigned long)tier1_many(x, x * 5 + 3) + (unsigned long)tier1_drive64(tier1_take64, x);\n"
    "    }\n"
    "    printf(\"%lu\\n\", total);\n"
    "    return 0;\n"
    "}\n";

static const char check_test_tier1_stack_callee_source[] =
    "long tier1_other7(long a, int b, long c, char* d, short e, long f, long g) { return a * 3 + b * 5 + c * 7 + d[1] * 11 + e * 13 + f * 17 + g * 19; }\n"
    "long tier1_other8(int a, long b, long c, long d, long e, unsigned char f, long g, int h) { return (a ^ b) + c * 3 - d + (e ^ 99) + f * 7 + g * 11 + h * 13; }\n"
    "long tier1_other12(long a, long b, int c, long d, long e, long f, signed char g, long h, long* i, int j, long k, unsigned short l)\n"
    "{\n"
    "    return a + b * 2 + c * 3 + d * 4 + e * 5 + f * 6 + g * 7 + h * 8 + *i * 9 + j * 10 + k * 11 + l * 12;\n"
    "}\n"
    "long tier1_other16(long a, long b, long c, long d, long e, long f, long g, long h, long i, long j, long k, long l, long m, long n, long o, long p)\n"
    "{\n"
    "    return ((((a * 31 + b) * 31 + c - d) * 31 + e * f - g) * 31 + h ^ i) + j * 3 + k * 5 + l * 7 + m * 11 + n * 13 + o * 17 + p * 19;\n"
    "}\n"
    "long tier1_drive(long (*seven)(long, long, long, long, long, long, long), long (*twelve)(long, long, long, long, long, long, int, long, long, long, short, long), long x)\n"
    "{\n"
    "    return seven(x, x + 1, x - 2, x * 3, x ^ 4, ~x, x * x) + twelve(x, 2, 3, x, 5, 6, (int)(x * 7), 8, x - 9, 10, (short)(x * 11), x + 12);\n"
    "}\n"
    "double tier1_other24(long a, double b, int c, double d, long e, double f, short g, double h, long i, double j, long k, double l, long m, double n, long o, double p, long q, double r, long s, double t, long u, float v, long w, double x)\n"
    "{\n"
    "    return (double)(a + c * 3 + e * 5 + g * 7 + i * 11 + k * 13 + m * 17 + o * 19 + q * 23 + s * 29 + u * 31 + w * 37) + b - d * 2 + f * 3 - h + j * 0.5 + l - n + p * 0.25 + r + t * 0.125 + v + x;\n"
    "}\n"
    "#include <stdarg.h>\n"
    "long tier1_other_va(int n, ...) { va_list ap; va_start(ap, n); long h = 0; for (int i = 0; i < n; i += 1) h = h * 7 + va_arg(ap, long); va_end(ap); return h; }\n"
    "typedef signed char sc;\n"
    "long tier1_drive64(long (*take)(sc, sc, sc, sc, sc, sc, sc, sc, sc, sc, sc, sc, sc, sc, sc, sc, sc, sc, sc, sc, sc, sc, sc, sc, sc, sc, sc, sc, sc, sc, sc, sc,"
    " sc, sc, sc, sc, sc, sc, sc, sc, sc, sc, sc, sc, sc, sc, sc, sc, sc, sc, sc, sc, sc, sc, sc, sc, sc, sc, sc, sc, sc, sc, sc, sc), long x)\n"
    "{\n"
    "    sc b = (sc)x;\n"
    "    return take(b, b + 1, b - 2, b * 3, b ^ 4, ~b, 6, 7, b, 9, 10, b + 11, 12, 13, 14, b - 15, 16, 17, 18, 19, b, 21, 22, 23, 24, 25, b * 2, 27, 28, 29, 30, b,\n"
    "                -1, -2, b, -4, -5, -6, -7, -8, b + 9, -10, -11, -12, -13, -14, -15, b, 100, 101, 102, b, 104, 105, 106, 107, b, 109, 110, 111, 112, 113, 114, b);\n"
    "}\n";

// The floating values' fixture (#141): F32 and F64 parameters, results and arguments interleaved with integers, past
// xmm7 onto the stack, to clang -O2's code and from it through a pointer, a variadic call of doubles (printf's and the
// other unit's: al their count), every comparison against NaNs, infinities and signed zeros, the conversions between
// the precisions and to and from integers of every width, signed and unsigned (an unsigned 64-bit one's past 2^63 both
// ways among them), a negation, loops of doubles and floats through arrays and a struct's members, and a conditional
// expression of doubles.
static const char check_test_tier1_float_source[] =
    "#include <stdio.h>\n"
    "double tier1_other_mix(double a, int b, float c, double d, long e, double f, double g, double h, double i, double j, float k, double l);\n"
    "float tier1_other_scale(float x, double y);\n"
    "double tier1_other_sum(int count, ...);\n"
    "double tier1_drive_float(double (*f)(double, float, long, double), double x);\n"
    "struct tier1_point\n"
    "{\n"
    "    double x;\n"
    "    float y;\n"
    "    int k;\n"
    "};\n"
    "double tier1_float_take(double a, float b, long c, double d)\n"
    "{\n"
    "    double product = a * b;\n"
    "    return product - c / d;\n"
    "}\n"
    "static double tier1_pass_float(double x, long n)\n"
    "{\n"
    "    float f = (float)x;\n"
    "    double r = tier1_other_mix(x, (int)n, f, x * 2.0, n * 3, x + 1.0, x - 1.0, -x, x * x, 0.5, f * 2.0f, (double)n);\n"
    "    r = r + tier1_other_scale(f, r);\n"
    "    r = r + tier1_other_sum(3, x, r, (double)n);\n"
    "    r = r + tier1_drive_float(tier1_float_take, x);\n"
    "    return r;\n"
    "}\n"
    "static int tier1_classify(double x, double y)\n"
    "{\n"
    "    int r = 0;\n"
    "    r = r * 2 + (x == y);\n"
    "    r = r * 2 + (x != y);\n"
    "    r = r * 2 + (x < y);\n"
    "    r = r * 2 + (x <= y);\n"
    "    r = r * 2 + (x > y);\n"
    "    r = r * 2 + (x >= y);\n"
    "    if (x < y)\n"
    "    {\n"
    "        r += 100;\n"
    "    }\n"
    "    if (!(x >= y))\n"
    "    {\n"
    "        r += 1000;\n"
    "    }\n"
    "    if (x != x)\n"
    "    {\n"
    "        r += 10000;\n"
    "    }\n"
    "    if (x == y)\n"
    "    {\n"
    "        r += 100000;\n"
    "    }\n"
    "    return r;\n"
    "}\n"
    "static long tier1_convert(double x, float f, unsigned u, short s, unsigned char c)\n"
    "{\n"
    "    long a = (long)x;\n"
    "    int b = (int)(x * 0.5);\n"
    "    float g = (float)u + (float)s;\n"
    "    double h = (double)c - (double)u;\n"
    "    float k = -f;\n"
    "    unsigned w = (unsigned)(x * x);\n"
    "    return a * 7 + b * 5 + (long)(g * 3.0f) + (long)h + (long)(k * 100.0f) + (long)w + (long)(signed char)(int)(f * 0.25f);\n"
    "}\n"
    "static double tier1_accumulate(double* values, int n, float* weights)\n"
    "{\n"
    "    double s = 0.0;\n"
    "    float w = 0.0f;\n"
    "    for (int i = 0; i < n; i += 1)\n"
    "    {\n"
    "        s += values[i] * weights[i];\n"
    "        w += weights[i];\n"
    "        values[i] = s;\n"
    "    }\n"
    "    return s / w;\n"
    "}\n"
    "static double tier1_points(struct tier1_point* p, int n)\n"
    "{\n"
    "    double t = 0;\n"
    "    for (int i = 0; i < n; i += 1)\n"
    "    {\n"
    "        t += p[i].x * p[i].y + p[i].k;\n"
    "        p[i].y = (float)t;\n"
    "    }\n"
    "    return t;\n"
    "}\n"
    "static void tier1_print(double x, float f, long n)\n"
    "{\n"
    "    printf(\"%.17g %.9g %ld\\n\", x, (double)f, n);\n"
    "}\n"
    "unsigned long tier1_wide_hash(unsigned long seed)\n"
    "{\n"
    "    static const unsigned long edges[] = { 0, 1, 3, 0x7fffffffUL, 0xffffffffUL, (1UL << 53) + 1, 0x7fffffffffffffffUL, 0x8000000000000000UL, 0x8000000000000401UL, 0x8000000000000c00UL,\n"
    "                                           0xfffffffffffffbffUL, 0xfffffffffffffc00UL, 0xffffffffffffffffUL, 0x9000017fffffffffUL };\n"
    "    unsigned long h = 0;\n"
    "    for (int i = 0; i < 14; i += 1)\n"
    "    {\n"
    "        const double d = (double)edges[i];\n"
    "        const float f = (float)edges[i];\n"
    "        h = h * 1000003 + (unsigned long)(d * 0.75) + (unsigned long)(f * 0.5f);\n"
    "    }\n"
    "    unsigned long r = seed;\n"
    "    for (int i = 0; i < 4096; i += 1)\n"
    "    {\n"
    "        r ^= r << 13;\n"
    "        r ^= r >> 7;\n"
    "        r ^= r << 17;\n"
    "        const double d = (double)r;\n"
    "        h = h * 31 + (unsigned long)(d * 0.875) + (unsigned long)((float)(r >> (i & 63)) * 0.5f);\n"
    "    }\n"
    "    return h;\n"
    "}\n"
    "int main(void)\n"
    "{\n"
    "    volatile double zero = 0.0;\n"
    "    const double nan = zero / zero;\n"
    "    const double inf = 1.0 / zero;\n"
    "    const double samples[] = { 0.0, -0.0, 1.5, -2.25, 3.0e10, -7.5e-3, 1.0e300, 123.456, nan, inf, -inf };\n"
    "    unsigned long total = 0;\n"
    "    for (int i = 0; i < 11; i += 1)\n"
    "    {\n"
    "        for (int j = 0; j < 11; j += 1)\n"
    "        {\n"
    "            total = total * 31 + (unsigned long)tier1_classify(samples[i], samples[j]);\n"
    "        }\n"
    "    }\n"
    "    printf(\"%lu\\n\", total);\n"
    "    for (int i = 0; i < 8; i += 1)\n"
    "    {\n"
    "        const double x = samples[i] < 1.0e20 && samples[i] > -1.0e20 ? samples[i] : 4.0;\n"
    "        printf(\"%.17g %ld\\n\", tier1_pass_float(x * 0.001, i * 3 - 7), tier1_convert(x * 0.01, (float)(x * 0.5), (unsigned)(i * 1000), (short)(i * -77), (unsigned char)(i * 33)));\n"
    "        tier1_print(x, (float)x, i);\n"
    "    }\n"
    "    double values[6] = { 1.0, 2.5, -3.0, 4.25, 0.5, 6.0 };\n"
    "    float weights[6] = { 0.5f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f };\n"
    "    printf(\"%.17g\", tier1_accumulate(values, 6, weights));\n"
    "    for (int i = 0; i < 6; i += 1)\n"
    "    {\n"
    "        printf(\" %.17g\", values[i]);\n"
    "    }\n"
    "    struct tier1_point points[4] = { { 1.5, 2.0f, 3 }, { -0.5, 4.0f, -1 }, { 10.0, 0.25f, 7 }, { 2.0, -1.0f, 0 } };\n"
    "    printf(\" %.17g %.9g %.9g\\n\", tier1_points(points, 4), (double)points[0].y, (double)points[3].y);\n"
    "    printf(\"%lx\\n\", tier1_wide_hash(0x243f6a8885a308d3UL));\n"
    "    return 0;\n"
    "}\n";

static const char check_test_tier1_float_other_source[] =
    "#include <stdarg.h>\n"
    "double tier1_other_mix(double a, int b, float c, double d, long e, double f, double g, double h, double i, double j, float k, double l)\n"
    "{\n"
    "    return a + b * 2.0 + c * 3.0 + d * 4.0 + e * 5.0 + f * 6.0 + g * 7.0 + h * 8.0 + i * 9.0 + j * 10.0 + k * 11.0 + l * 12.0;\n"
    "}\n"
    "float tier1_other_scale(float x, double y)\n"
    "{\n"
    "    return x * (float)y + 1.0f;\n"
    "}\n"
    "double tier1_other_sum(int count, ...)\n"
    "{\n"
    "    va_list arguments;\n"
    "    va_start(arguments, count);\n"
    "    double s = 0;\n"
    "    for (int i = 0; i < count; i += 1)\n"
    "    {\n"
    "        s += va_arg(arguments, double) * (i + 1);\n"
    "    }\n"
    "    va_end(arguments);\n"
    "    return s;\n"
    "}\n"
    "double tier1_drive_float(double (*f)(double, float, long, double), double x)\n"
    "{\n"
    "    return f(x, (float)x * 0.5f, (long)x + 3, x + 2.5);\n"
    "}\n";

// The by-value aggregates' fixture (#789): 19 shapes without padding, of 1 to 24 bytes (general and xmm parts of every
// size, a part of each class in one aggregate, _Float16's odd xmm part, and the memory class's on the stack and through
// the hidden pointer), each made, hashed and passed back by the other unit (clang -O2's) and twisted and returned by this
// one (Tier 1's), so that aggregates cross between the compilers both ways as arguments and as results; a call of nine
// arguments whose aggregates run past the registers, each way; and a variadic call of three aggregates. Its hashes are
// unsigned, so that the oracle's output is a defined program's: a signed one overflows (#141, as the vectors' fixture).
static const char check_test_tier1_records_header[] =
    "#include <stdio.h>\n"
    "typedef struct { char c[1]; } S1;\n"
    "typedef struct { char c[2]; } S2;\n"
    "typedef struct { char c[3]; } S3;\n"
    "typedef struct { char c[5]; } S5;\n"
    "typedef struct { short s[3]; } S6;\n"
    "typedef struct { char c[7]; } S7;\n"
    "typedef struct { int a, b; } S8;\n"
    "typedef struct { char c[9]; } S9;\n"
    "typedef struct { int i[3]; } S12;\n"
    "typedef struct { char c[15]; } S15;\n"
    "typedef struct { long a, b; } S16;\n"
    "typedef struct { char c[17]; } S17;\n"
    "typedef struct { float f; } F1;\n"
    "typedef struct { float a, b, c; } F3;\n"
    "typedef struct { double a, b; } D2;\n"
    "typedef struct { double d; int i[2]; } DI;\n"
    "typedef struct { int i[2]; double d; } ID;\n"
    "typedef struct { _Float16 h[3]; } H3;\n"
    "typedef struct { long a, b, c; } Big;\n"
    "#define SHAPES(X) X(S1) X(S2) X(S3) X(S5) X(S6) X(S7) X(S8) X(S9) X(S12) X(S15) X(S16) X(S17) X(F1) X(F3) X(D2) X(DI) X(ID) X(H3) X(Big)\n"
    "#define DECLARE(S) S b_make_##S(int seed); unsigned long b_hash_##S(S s); unsigned long b_back_##S(int seed); S a_twist_##S(S s, int k);\n"
    "SHAPES(DECLARE)\n"
    "unsigned long b_many(long a, long b, long c, long d, S16 t, Big x, DI y, S6 z, S1 w);\n"
    "unsigned long a_many(long a, long b, long c, long d, S16 t, Big x, DI y, S6 z, S1 w);\n"
    "unsigned long b_call_many(int seed);\n"
    "unsigned long b_vsum(int n, ...);\n";

static const char check_test_tier1_records_other_source[] =
    "#include \"tier1-records.h\"\n"
    "#include <stdarg.h>\n"
    "#define DEFINE(S) \\\n"
    "S b_make_##S(int seed) { S s; unsigned char* p = (unsigned char*)&s; for (unsigned i = 0; i < sizeof s; i += 1) p[i] = (unsigned char)(seed * 7 + i * 13 + 1); return s; } \\\n"
    "unsigned long b_hash_##S(S s) { const unsigned char* p = (const unsigned char*)&s; unsigned long h = 0; for (unsigned i = 0; i < sizeof s; i += 1) h = h * 31 + p[i]; return h; } \\\n"
    "unsigned long b_back_##S(int seed) { S s = b_make_##S(seed); S t = a_twist_##S(s, seed); return b_hash_##S(t) * 3 + b_hash_##S(s); }\n"
    "SHAPES(DEFINE)\n"
    "unsigned long b_many(long a, long b, long c, long d, S16 t, Big x, DI y, S6 z, S1 w) { return a + b * 2 + c * 3 + d * 4 + b_hash_S16(t) + b_hash_Big(x) * 5 + b_hash_DI(y) * 7 + b_hash_S6(z) * 11 + b_hash_S1(w) * 13; }\n"
    "unsigned long b_call_many(int seed) { return a_many(seed, seed + 1, seed + 2, seed + 3, b_make_S16(seed), b_make_Big(seed + 1), b_make_DI(seed + 2), b_make_S6(seed + 3), b_make_S1(seed + 4)); }\n"
    "unsigned long b_vsum(int n, ...) { va_list ap; va_start(ap, n); unsigned long h = 0; for (int i = 0; i < n; i += 1) h = h * 131 + b_hash_S12(va_arg(ap, S12)); va_end(ap); return h; }\n";

// The returns-twice fixture (#1847): functions that call setjmp (glibc's macro, _setjmp), sigsetjmp (__sigsetjmp), a
// function a declaration gives returns_twice (the other unit's, which jumps to _setjmp) and _setjmp through a volatile
// pointer of setjmp's type (check_lower_indirect_twice), each holding values across the call in a caller-saved register
// and a spill slot while its first return's path calls with others in the same places, then longjmps; and one that
// calls none, through a volatile pointer of another type. Tier 1's code for the callers would find the others' values
// on the second return: they are Tier 0's, and the other function Tier 1's.
static const char check_test_tier1_twice_source[] =
    "#include <setjmp.h>\n#include <stdio.h>\n"
    "static jmp_buf buf;\n"
    "static sigjmp_buf sbuf;\n"
    "extern int tier1_declared_setjmp(jmp_buf) __attribute__((returns_twice));\n"
    "__attribute__((noinline)) static void jump(void) { longjmp(buf, 1); }\n"
    "__attribute__((noinline)) static void sjump(void) { siglongjmp(sbuf, 1); }\n"
    "__attribute__((noinline)) static int f(int x) { return x * 3 + 1; }\n"
    "__attribute__((noinline)) static long sink(long a, long b, long c, long d) { return a ^ b ^ c ^ d; }\n"
    "__attribute__((noinline)) static long churn(int v, int n) { long x = f(v + 7), y = f(v + 9), z = f(n + 11), q = f(n + 13); long r = sink(x, y, z, q); r += sink(r, x, y, z); return r + f((int)r); }\n"
    "int tier1_twice_setjmp(int n) { int v = f(n); long w = f(n + 1); if (setjmp(buf) == 0) { long r = churn(v, n); jump(); return (int)r; } return v + (int)w; }\n"
    "int tier1_twice_sigsetjmp(int n) { int v = f(n); long w = f(n + 2); if (sigsetjmp(sbuf, 0) == 0) { long r = churn(v, n); sjump(); return (int)r; } return v * 2 + (int)w; }\n"
    "int tier1_twice_declared(int n) { int v = f(n); long w = f(n + 3); if (tier1_declared_setjmp(buf) == 0) { long r = churn(v, n); jump(); return (int)r; } return v * 3 + (int)w; }\n"
    "static int (*volatile tier1_save)(jmp_buf) = _setjmp;\n"
    "static long (*volatile tier1_churn)(int, int) = churn;\n"
    "int tier1_twice_pointer(int n) { int v = f(n); long w = f(n + 4); if (tier1_save(buf) == 0) { long r = churn(v, n); jump(); return (int)r; } return v * 5 + (int)w; }\n"
    "int tier1_twice_none(int n) { int v = f(n); long r = tier1_churn(v, n); return v + (int)r; }\n"
    "int main(void) { printf(\"%d %d %d %d %d\\n\", tier1_twice_setjmp(5), tier1_twice_sigsetjmp(6), tier1_twice_declared(7), tier1_twice_pointer(9), tier1_twice_none(8)); return 0; }\n";
static const char check_test_tier1_twice_other_source[] =
    "__asm__(\".text\\n.globl tier1_declared_setjmp\\n.type tier1_declared_setjmp, @function\\ntier1_declared_setjmp:\\n    jmp _setjmp@PLT\\n\");\n";

// The dynamic stack's fixture (#141): variable-length arrays in a loop's body (a save and a restore per iteration), beside
// calls with stack arguments (each with its outgoing area at its own rsp), in a frame realigned by an object aligned to 64
// (its base r15), with values live across calls in callee-saved registers, in a variadic definition, and alloca's; and a
// loop's variable-length array beside by-value aggregates (#1117's review): a 24-byte one passed on the stack, 16-byte
// ones returned in a general and an xmm register, and a call of ten integers and two doubles, so that the outgoing
// area from rsp, the aggregates' staging below it and the result's object meet in one dynamic frame. Each callee's
// backtrace() walks through the dynamic frames' unwind tables to the C runtime's: the frames' total it prints is
// clang's build's only where none of them stops the walk.
static const char check_test_tier1_dynamic_source[] =
    "#include <alloca.h>\n#include <execinfo.h>\n#include <stdarg.h>\n#include <stdint.h>\n#include <stdio.h>\n#include <string.h>\n"
    "static long unwound;\n"
    "__attribute__((noinline)) static void probe(void) { void* frames[64]; unwound += backtrace(frames, 64); }\n"
    "__attribute__((noinline)) static long sum8(long a, long b, long c, long d, long e, long f, long g, long h) { return a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * f + 7 * g + 8 * h; }\n"
    "__attribute__((noinline)) static long touch(long* p, long n) { probe(); long s = 0; for (long i = 0; i < n; i += 1) s += p[i] * (i + 1); return s; }\n"
    "long tier1_vla_loop(long n) { long total = 0; for (long k = 1; k <= n; k += 1) { long a[k]; for (long i = 0; i < k; i += 1) a[i] = i * k + 1; total += touch(a, k); } return total; }\n"
    "long tier1_vla_calls(long n) { long a[n + 1]; for (long i = 0; i <= n; i += 1) a[i] = i + 3; long s = sum8(a[0], a[1], a[2 % (n + 1)], n, 5, 6, a[n], 8); char b[n * 3 + 1];\n"
    "    memset(b, 'x', n * 3); b[n * 3] = 0; return s + (long)strlen(b) + sum8(1, 2, 3, 4, 5, 6, a[n / 2], s); }\n"
    "long tier1_vla_aligned(long n) { _Alignas(64) char wide[64]; for (int i = 0; i < 64; i += 1) wide[i] = (char)i; long a[n];\n"
    "    for (long i = 0; i < n; i += 1) a[i] = wide[i % 64] + (long)((uintptr_t)wide & 63); long s = touch(a, n); double d[n];\n"
    "    for (long i = 0; i < n; i += 1) d[i] = (double)i / 2; for (long i = 0; i < n; i += 1) s += (long)(d[i] * 4);\n"
    "    return s + sum8(s, n, 3, 4, 5, 6, 7, (long)((uintptr_t)a & 15)); }\n"
    "long tier1_alloca(long n) { long* p = alloca(n * sizeof(long)); long* q = __builtin_alloca(32); for (long i = 0; i < n; i += 1) p[i] = i * i;\n"
    "    q[0] = 1; q[1] = 2; q[2] = 3; q[3] = (long)(((uintptr_t)p | (uintptr_t)q) & 15); return touch(p, n) + touch(q, 4); }\n"
    "long tier1_vla_live(long n, long m) { long kept = n * 7 + m; long other = m * 13; long r = 0; for (long k = 0; k < 3; k += 1) { long a[n + k];\n"
    "    for (long i = 0; i < n + k; i += 1) a[i] = kept + i; r += touch(a, n + k) + other + sum8(kept, other, r, k, n, m, 7, 8); if (r > 1000000) break; }\n"
    "    return r + kept + other; }\n"
    "long tier1_vla_variadic(int n, ...) { va_list list; va_start(list, n); long a[n]; for (int i = 0; i < n; i += 1) a[i] = va_arg(list, long); va_end(list);\n"
    "    return touch(a, n) + sum8(n, a[0], a[n - 1], 4, 5, 6, 7, 8); }\n"
    "typedef struct { long a, b, c; } Agg24;\n"
    "typedef struct { double x; long y; } Agg16;\n"
    "__attribute__((noinline)) static long take24(Agg24 s, long k) { probe(); return s.a * 3 + s.b * 5 + s.c * 7 + k; }\n"
    "__attribute__((noinline)) static Agg16 make16(long i, double d) { Agg16 r = { d * 2 + (double)i, i * 9 }; return r; }\n"
    "__attribute__((noinline)) static long many10(long a, long b, long c, long d, long e, long f, long g, long h, long i, long j, double x, double y)\n"
    "    { return a + b * 2 + c * 3 + d * 4 + e * 5 + f * 6 + g * 7 + h * 8 + i * 9 + j * 10 + (long)(x * 4) + (long)(y * 8); }\n"
    "long tier1_vla_records(long n) { long total = 0; for (long k = 1; k <= n; k += 1) { long a[k + 2]; for (long i = 0; i < k + 2; i += 1) a[i] = i * k + 3;\n"
    "    Agg24 s = { a[0], a[k], a[k + 1] }; total += take24(s, k); Agg16 r = make16(k, (double)a[1] / 4); total += r.y + (long)(r.x * 2);\n"
    "    total += many10(a[0], a[1], k, n, 5, 6, 7, 8, 9, a[k + 1], r.x, (double)k / 2) + touch(a, k + 2); } return total; }\n"
    "int main(void) {\n"
    "    printf(\"%ld %ld %ld %ld %ld %ld\\n\", tier1_vla_loop(9), tier1_vla_calls(10), tier1_vla_aligned(70), tier1_alloca(12), tier1_vla_live(5, 3),\n"
    "           tier1_vla_variadic(9, 1L, 2L, 3L, 4L, 5L, 6L, 7L, 8L, 9L));\n"
    "    printf(\"%ld %ld %ld %ld %ld %ld\\n\", tier1_vla_loop(1), tier1_vla_calls(1), tier1_vla_aligned(1), tier1_alloca(1), tier1_vla_live(1, 0), tier1_vla_variadic(1, 11L));\n"
    "    printf(\"%ld %ld\\n\", tier1_vla_records(9), tier1_vla_records(1));\n"
    "    printf(\"%ld\\n\", unwound);\n"
    "    return 0;\n"
    "}\n";

static const char check_test_tier1_records_source[] =
    "#include \"tier1-records.h\"\n"
    "#define TWIST(S) S a_twist_##S(S s, int k) { unsigned char* p = (unsigned char*)&s; for (unsigned i = 0; i < sizeof s; i += 1) p[i] = (unsigned char)(p[i] + k + i); return s; }\n"
    "SHAPES(TWIST)\n"
    "unsigned long a_many(long a, long b, long c, long d, S16 t, Big x, DI y, S6 z, S1 w) { return b_many(a + 1, b, c, d, t, x, y, z, w) + b_hash_S1(w); }\n"
    "#define PRINT(S) printf(\"%s %lu %lu %lu\\n\", #S, b_hash_##S(b_make_##S(5)), b_hash_##S(a_twist_##S(b_make_##S(9), 3)), b_back_##S(11));\n"
    "int main(void)\n"
    "{\n"
    "    SHAPES(PRINT)\n"
    "    printf(\"%lu %lu\\n\", b_call_many(4), a_many(1, 2, 3, 4, b_make_S16(1), b_make_Big(2), b_make_DI(3), b_make_S6(4), b_make_S1(5)));\n"
    "    printf(\"%lu\\n\", b_vsum(3, b_make_S12(1), b_make_S12(2), b_make_S12(3)));\n"
    "    return 0;\n"
    "}\n";

// The wide values' fixture (#141): long double values, parameters and results, in a loop's phis and through a variadic
// definition's va_arg; aggregates returned in the x87's registers (a struct of a long double in st0, a _Complex long
// double in st0 and st1) and passed on the stack, and one of the memory class through the hidden pointer; and 128-bit
// integers within the bodies (a high product and a loop's shifts, products and xors). Each crosses between this unit
// (Tier 1's) and the other (clang -O2's) both ways, as arguments and results, and printf takes the long doubles.
static const char check_test_tier1_wide_header[] =
    "#include <stdio.h>\n"
    "#include <complex.h>\n"
    "typedef struct { long double x; } L1;\n"
    "typedef struct { long double x; long n; } LN;\n"
    "L1 b_make_L1(int seed);\n"
    "_Complex long double b_make_C(int seed);\n"
    "long b_check_L1(L1 s);\n"
    "long b_check_C(_Complex long double c);\n"
    "long double b_mix(long double a, int k, long double b);\n"
    "long b_back(int seed);\n"
    "L1 a_twist_L1(L1 s, int k);\n"
    "_Complex long double a_twist_C(_Complex long double c, int k);\n"
    "LN a_pair(long double x, long n);\n"
    "long double a_poly(long double x, int n);\n"
    "long double a_sum(int n, ...);\n"
    "long a_mulhi(long a, long b);\n"
    "long a_wide_loop(int n);\n";

static const char check_test_tier1_wide_other_source[] =
    "#include \"tier1-wide.h\"\n"
    "L1 b_make_L1(int seed) { L1 r = { seed * 1.5L + 0.125L }; return r; }\n"
    "_Complex long double b_make_C(int seed) { return (seed + 0.5L) + (seed * 2 - 1.25L) * I; }\n"
    "long b_check_L1(L1 s) { return (long)(s.x * 1024); }\n"
    "long b_check_C(_Complex long double c) { return (long)(__real__ c * 1024) * 31 + (long)(__imag__ c * 1024); }\n"
    "long double b_mix(long double a, int k, long double b) { return a / (k + 1) + b; }\n"
    "long b_back(int seed) { L1 s = a_twist_L1(b_make_L1(seed), seed); _Complex long double c = a_twist_C(b_make_C(seed), seed); LN p = a_pair(s.x, seed);\n"
    "    return b_check_L1(s) * 7 + b_check_C(c) + (long)(p.x * 64) + p.n + (long)(a_poly(1.5L, seed + 2) * 16) + (long)(a_sum(3, 1.25L, 2.5L, (long double)seed) * 8); }\n";

static const char check_test_tier1_wide_source[] =
    "#include \"tier1-wide.h\"\n"
    "#include <stdarg.h>\n"
    "L1 a_twist_L1(L1 s, int k) { L1 r = { s.x * k + b_mix(s.x, k, 0.25L) }; return r; }\n"
    "_Complex long double a_twist_C(_Complex long double c, int k) { return c * (long double)k + (__real__ c - __imag__ c) * I; }\n"
    "LN a_pair(long double x, long n) { LN r = { x / 3, n * 5 }; return r; }\n"
    "long double a_poly(long double x, int n) { long double s = 0; for (int i = 0; i < n; i += 1) { s = s * x + (long double)i; if (s > 1e30L) s = s / 1e20L; } return s; }\n"
    "long double a_sum(int n, ...) { va_list ap; va_start(ap, n); long double s = 0; for (int i = 0; i < n; i += 1) s += va_arg(ap, long double) * (i + 1); va_end(ap); return s; }\n"
    "long a_mulhi(long a, long b) { return (long)(((__int128)a * b) >> 64); }\n"
    "long a_wide_loop(int n) { unsigned __int128 acc = 1; for (int i = 1; i <= n; i += 1) { acc = acc * (unsigned)(i + 3) + ((unsigned __int128)i << 70); acc ^= acc >> 33; } return (long)(acc ^ (acc >> 64)); }\n"
    "int main(void) {\n"
    "    for (int seed = 0; seed < 6; seed += 1) printf(\"%ld %ld\\n\", b_back(seed), a_mulhi(seed * 0x123456789abcdefL, -0x7edcba9876543210L - seed));\n"
    "    L1 s = a_twist_L1(b_make_L1(3), 4); _Complex long double c = a_twist_C(b_make_C(2), 5);\n"
    "    printf(\"%.10Lg %.10Lg %.10Lg %ld\\n\", s.x, __real__ c, __imag__ c, a_wide_loop(40));\n"
    "    printf(\"%.10Lg\\n\", a_sum(4, 1.0L, 2.0L, 3.0L, 0.5L));\n"
    "    return 0;\n"
    "}\n";

// The convention's rarer shapes' fixture (#141): by-value aggregates whose low eightbyte is padding (HIGH: eightbyte 1
// alone in a register or a stack slot) or whose high one is (in a register alone, or LOW: on the stack in one slot), and
// SPLIT ones, a part in a register and the other in a stack slot, where an _Atomic scalar takes a register clang doesn't
// count: an integer pair split between r9 and the stack, and a double and a long split between rdi and the stack, each
// way round; and ones the convention ignores, passed and returned nowhere: an empty struct, and one of unnamed
// bit-fields. Each crosses between this unit (Tier 1's) and the other (clang -O2's) both ways, as arguments and results:
// the other unit's b_empty, by --tier1, once passed its empty struct's address in rdi, x's register (a call without
// records, ir_typed_aggregate_call). And (#1879) a struct of a flexible array member alone, of no size and MEMORY, which
// clang passes byval in an 8-byte slot: after five longs in rdi to r8 and a double in xmm0, then a long in r9 and one
// on the stack after its slot, where a call without records once put its address in r9 and the rest one place on;
// first too, before a long in rdi.
static const char check_test_tier1_shapes_header[] =
    "#include <stdio.h>\n"
    "typedef struct { long x; long : 64; } Low;\n"
    "typedef struct { long : 64; long y; } High;\n"
    "typedef struct { int : 32; int : 32; float f; int i; } HighMixed;\n"
    "typedef struct { double d; long l; } DL;\n"
    "typedef struct { long l; double d; } LD;\n"
    "typedef struct { long a, b; } LL;\n"
    "typedef struct {} Empty;\n"
    "typedef struct { int : 32; } Bits;\n"
    "typedef struct { long *p[]; } Flex;\n"
    "long b_low(Low s);\n"
    "long b_high(High s);\n"
    "long b_high_mixed(HighMixed s);\n"
    "long b_low_stack(long a, long b, long c, long d, long e, _Atomic long u, Low s, long t);\n"
    "long b_high_stack(long a, long b, long c, long d, long e, _Atomic long u, High s, long t);\n"
    "long b_split_int(long a, long b, long c, long d, _Atomic long u, LL s, long t);\n"
    "long b_split_sse(double a, double b, double c, double d, double e, double f, double g, _Atomic double u, DL s, long t);\n"
    "long b_split_ld(double a, double b, double c, double d, double e, double f, double g, _Atomic double u, LD s, long t);\n"
    "Low b_make_low(long x);\n"
    "High b_make_high(long y);\n"
    "HighMixed b_make_high_mixed(int k);\n"
    "long b_back(int k);\n"
    "long a_low(Low s);\n"
    "long a_high(High s);\n"
    "long a_high_mixed(HighMixed s);\n"
    "long a_low_stack(long a, long b, long c, long d, long e, _Atomic long u, Low s, long t);\n"
    "long a_high_stack(long a, long b, long c, long d, long e, _Atomic long u, High s, long t);\n"
    "long a_split_int(long a, long b, long c, long d, _Atomic long u, LL s, long t);\n"
    "long a_split_sse(double a, double b, double c, double d, double e, double f, double g, _Atomic double u, DL s, long t);\n"
    "long a_split_ld(double a, double b, double c, double d, double e, double f, double g, _Atomic double u, LD s, long t);\n"
    "Low a_make_low(long x);\n"
    "High a_make_high(long y);\n"
    "HighMixed a_make_high_mixed(int k);\n"
    "long b_empty(Empty e, long x);\n"
    "long b_bits(long a, Bits b, long c);\n"
    "Empty b_make_empty(long x);\n"
    "long a_empty(Empty e, long x);\n"
    "long a_bits(long a, Bits b, long c);\n"
    "Empty a_make_empty(long x);\n"
    "long a_empty_chain(long x);\n"
    "long b_flex(long a, long b, long c, long d, long e, double f, Flex s, long t, long u);\n"
    "long b_flex_first(Flex s, long x);\n"
    "long a_flex(long a, long b, long c, long d, long e, double f, Flex s, long t, long u);\n"
    "long a_flex_first(Flex s, long x);\n"
    "long a_flex_chain(long k);\n";

static const char check_test_tier1_shapes_other_source[] =
    "#include \"tier1-shapes.h\"\n"
    "long b_low(Low s) { return a_low(s) * 2 + s.x; }\n"
    "long b_high(High s) { return a_high(s) * 2 + s.y; }\n"
    "long b_high_mixed(HighMixed s) { return a_high_mixed(s) * 2 + s.i; }\n"
    "long b_low_stack(long a, long b, long c, long d, long e, _Atomic long u, Low s, long t) { return a_low_stack(a, b, c, d, e, u, s, t) + s.x; }\n"
    "long b_high_stack(long a, long b, long c, long d, long e, _Atomic long u, High s, long t) { return a_high_stack(a, b, c, d, e, u, s, t) + s.y; }\n"
    "long b_split_int(long a, long b, long c, long d, _Atomic long u, LL s, long t) { return a_split_int(a, b, c, d, u, s, t) + s.a - s.b; }\n"
    "long b_split_sse(double a, double b, double c, double d, double e, double f, double g, _Atomic double u, DL s, long t) { return a_split_sse(a, b, c, d, e, f, g, u, s, t) + s.l; }\n"
    "long b_split_ld(double a, double b, double c, double d, double e, double f, double g, _Atomic double u, LD s, long t) { return a_split_ld(a, b, c, d, e, f, g, u, s, t) + s.l; }\n"
    "Low b_make_low(long x) { Low r = { x * 11 }; return r; }\n"
    "High b_make_high(long y) { High r; r.y = y * 13; return r; }\n"
    "HighMixed b_make_high_mixed(int k) { HighMixed r; r.f = k * 1.25f; r.i = k * 3; return r; }\n"
    "long b_back(int k) { return a_make_low(k).x * 3 + a_make_high(k + 1).y * 5 + a_make_high_mixed(k + 2).i + (long)(a_make_high_mixed(k).f * 8); }\n"
    "long b_empty(Empty e, long x) { return a_empty(e, x) * 2 + x; }\n"
    "long b_bits(long a, Bits b, long c) { return a_bits(a, b, c) + 1; }\n"
    "Empty b_make_empty(long x) { Empty e = { }; (void)x; return e; }\n"
    "long b_flex(long a, long b, long c, long d, long e, double f, Flex s, long t, long u) { return a_flex(a, b, c, d, e, f, s, t, u) * 3 + a + t * 5 + u * 7; }\n"
    "long b_flex_first(Flex s, long x) { return a_flex_first(s, x) * 2 + x; }\n";

static const char check_test_tier1_shapes_source[] =
    "#include \"tier1-shapes.h\"\n"
    "long a_low(Low s) { return s.x * 3 + 1; }\n"
    "long a_high(High s) { return s.y * 5 + 2; }\n"
    "long a_high_mixed(HighMixed s) { return (long)(s.f * 4) + s.i * 7; }\n"
    "long a_low_stack(long a, long b, long c, long d, long e, _Atomic long u, Low s, long t) { return a + b * 2 + c * 3 + d * 4 + e * 5 + u * 6 + s.x * 7 + t * 8; }\n"
    "long a_high_stack(long a, long b, long c, long d, long e, _Atomic long u, High s, long t) { return a + b * 2 + c * 3 + d * 4 + e * 5 + u * 6 + s.y * 9 + t * 10; }\n"
    "long a_split_int(long a, long b, long c, long d, _Atomic long u, LL s, long t) { return a + b * 2 + c * 3 + d * 4 + u * 5 + s.a * 6 + s.b * 7 + t * 8; }\n"
    "long a_split_sse(double a, double b, double c, double d, double e, double f, double g, _Atomic double u, DL s, long t) { return (long)(a + b * 2 + c * 3 + d * 4 + e * 5 + f * 6 + g * 7 + u * 8 + s.d * 9) + s.l * 11 + t * 13; }\n"
    "long a_split_ld(double a, double b, double c, double d, double e, double f, double g, _Atomic double u, LD s, long t) { return (long)(a + b + c + d + e + f + g + u * 2 + s.d * 3) + s.l * 17 + t * 19; }\n"
    "Low a_make_low(long x) { Low r = { x * 3 }; return r; }\n"
    "High a_make_high(long y) { High r; r.y = y * 5; return r; }\n"
    "HighMixed a_make_high_mixed(int k) { HighMixed r; r.f = k * 0.5f; r.i = k * 9; return r; }\n"
    "long a_empty(Empty e, long x) { (void)e; return x * 3 + 2; }\n"
    "long a_bits(long a, Bits b, long c) { Bits copy = b; (void)copy; return a * 5 + c; }\n"
    "Empty a_make_empty(long x) { Empty e; (void)x; return e; }\n"
    "long a_empty_chain(long x) { Empty e = b_make_empty(x); Bits b = { }; return b_empty(e, x) + b_bits(x, b, x + 1) + a_empty(a_make_empty(x), x); }\n"
    "long a_flex(long a, long b, long c, long d, long e, double f, Flex s, long t, long u) { (void)s; return a + b * 2 + c * 3 + d * 4 + e * 5 + (long)(f * 6) + t * 7 + u * 8; }\n"
    "long a_flex_first(Flex s, long x) { (void)s; return x * 9 + 4; }\n"
    "long a_flex_chain(long k) { Flex s; return b_flex(k, k + 1, k + 2, k + 3, k + 4, k * 0.5, s, k + 5, k + 6) + b_flex_first(s, k); }\n"
    "int main(void)\n"
    "{\n"
    "    for (int k = 1; k < 4; k += 1)\n"
    "    {\n"
    "        Low l = b_make_low(k);\n"
    "        High h = b_make_high(k + 1);\n"
    "        HighMixed m = b_make_high_mixed(k + 2);\n"
    "        LL p = { k, k * 2 };\n"
    "        DL q = { k * 1.5, k * 3 };\n"
    "        LD r = { k * 4, k * 2.25 };\n"
    "        printf(\"%ld %ld %ld %ld\\n\", b_low(l), b_high(h), b_high_mixed(m), b_back(k));\n"
    "        printf(\"%ld %ld %ld\\n\", b_low_stack(1, 2, 3, 4, 5, k, l, 6), b_high_stack(1, 2, 3, 4, 5, k, h, 7), b_split_int(1, 2, 3, 4, k, p, 8));\n"
    "        printf(\"%ld %ld\\n\", b_split_sse(1, 2, 3, 4, 5, 6, 7, k, q, 9), b_split_ld(1, 2, 3, 4, 5, 6, 7, k, r, 10));\n"
    "        printf(\"%ld %ld %ld\\n\", a_make_low(k).x, a_make_high(k).y, (long)a_make_high_mixed(k).i);\n"
    "        Empty e = { };\n"
    "        printf(\"%ld %ld\\n\", b_empty(e, k), a_empty_chain(k));\n"
    "        printf(\"%ld\\n\", a_flex_chain(k));\n"
    "    }\n"
    "    return 0;\n"
    "}\n";

// The vectors' fixture (#141), for x86-64-v4: vectors of each width, v4i32, v8i32 and v16i32 (unsigned among them),
// passed and returned by value, as aggregates in a vector register whole (an xmm, ymm or zmm one), between this unit
// (Tier 1's) and the other (clang -O2's) both ways, with the operations, comparisons and constants between. Its hash,
// a_fold's, is unsigned, so that the oracle's output is a defined program's: a signed one overflows (#1204's review).
static const char check_test_tier1_vectors_header[] =
    "#include <stdio.h>\n"
    "typedef int V4 __attribute__((vector_size(16)));\n"
    "typedef unsigned U8 __attribute__((vector_size(32)));\n"
    "typedef int V16 __attribute__((vector_size(64)));\n"
    "V4 b_v4(V4 a, int k);\n"
    "U8 b_u8(U8 a, U8 b);\n"
    "V16 b_v16(V16 a, long k, V4 c);\n"
    "unsigned long b_back(int k);\n"
    "V4 a_v4(V4 a, int k);\n"
    "U8 a_u8(U8 a, U8 b);\n"
    "V16 a_v16(V16 a, long k, V4 c);\n"
    "unsigned long a_fold(V16 v, U8 u, V4 w);\n"
    "unsigned long a_member(int k);\n";

static const char check_test_tier1_vectors_other_source[] =
    "#include \"tier1-vectors.h\"\n"
    "V4 b_v4(V4 a, int k) { V4 s = { k, k, k, k }; return a - s; }\n"
    "U8 b_u8(U8 a, U8 b) { return (a ^ b) + a_u8(b, a); }\n"
    "V16 b_v16(V16 a, long k, V4 c) { return a_v16(a, k + 1, c) + a; }\n"
    "unsigned long b_back(int k) { V4 w = { k, 2, 3, 4 }; U8 u = { 9u, 8u, 7u, 6u, 5u, 4u, 3u, (unsigned)k }; V16 v = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, k }; return a_fold(a_v16(v, k, a_v4(w, k)), a_u8(u, u), w); }\n";

static const char check_test_tier1_vectors_source[] =
    "#include \"tier1-vectors.h\"\n"
    "V4 a_v4(V4 a, int k) { V4 s = { 1, 2, 3, 4 }; V4 m = a < s; return (a + s) ^ (m & b_v4(s, k)); }\n"
    "U8 a_u8(U8 a, U8 b) { U8 m = a >= b; return a * b - (a | b) + m; }\n"
    "V16 a_v16(V16 a, long k, V4 c) { V16 s = { 7, 1, 2, 3, 5, 8, 13, 21, 34, 55, 89, 144, 233, 377, 610, 987 }; V16 m = a != s; return a * s + (m & ~a) - (V16){ c[0], c[1], c[2], c[3] }; }\n"
    "unsigned long a_fold(V16 v, U8 u, V4 w) { unsigned long h = 0; for (int i = 0; i < 16; i += 1) h = h * 31 + v[i]; for (int i = 0; i < 8; i += 1) h = h * 37 + u[i]; for (int i = 0; i < 4; i += 1) h = h * 41 + w[i]; return h; }\n"
    "typedef float F4 __attribute__((vector_size(16)));\n"
    "unsigned long a_member(int k) { struct { V4 v; F4 f; } x; union { V4 v; F4 f; } u; x.v = (V4){ 1, -2, 3, k }; x.f = (F4){ 1.0f, 4.0f, 9.0f, (float)(k * k) }; u.v = x.v; V4 r[] = { __builtin_elementwise_abs(x.v), x.v }; F4 q[] = { __builtin_elementwise_sqrt(x.f), x.f }; V4 s[] = { __builtin_elementwise_max(u.v, (V4){ 0 }), u.v }; u.f = x.f; F4 t[] = { __builtin_elementwise_floor(u.f), u.f }; unsigned long h = sizeof r + sizeof q + sizeof s + sizeof t; for (int i = 0; i < 4; i += 1) h = h * 43 + (unsigned)r[1][i] + 7u * (unsigned)r[0][i] + 11u * (unsigned)s[1][i] + 13u * (unsigned)s[0][i] + (unsigned)(q[0][i] + q[1][i] + t[0][i] + t[1][i]); return h; }\n"
    "int main(void)\n"
    "{\n"
    "    for (int k = 0; k < 4; k += 1)\n"
    "    {\n"
    "        V4 w = { k, -k, k * 7, 100 - k };\n"
    "        U8 u = { 1u, 2u, 3u, (unsigned)k, 0xffffffffu, 9u, 4u, 0x80000000u };\n"
    "        V16 v = { k, 1, -2, 3, 4, -5, 6, 7, 8, 9, -10, 11, 12, 13, 14, 15 };\n"
    "        printf(\"%lu %lu\\n\", a_fold(a_v16(v, k, w), a_u8(u, b_u8(u, u)), a_v4(w, k)), b_back(k));\n"
    "        printf(\"%lu\\n\", a_fold(b_v16(v, k, w), b_u8(u, u), b_v4(w, k)));\n"
    "        printf(\"%lu\\n\", a_member(k));\n"
    "    }\n"
    "    return 0;\n"
    "}\n";

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

#include "c_check_tier1_table_targets_test.c"
#include "c_check_tier1_table_entry_test.c"

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
    // Its machine rows and the moves around them into their roles' registers and out of them (#1283).
    uint64_t machine_rows;
    uint64_t row_moves;
    // The functions whose code --verify's certificate proved, and those it left uncertified (#939).
    uint64_t code_certified;
    uint64_t code_uncertified;
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
                      check_test_tier1_field(text, "call_stores", &build->call_stores) && check_test_tier1_field(text, "machine_rows", &build->machine_rows) &&
                      check_test_tier1_field(text, "row_moves", &build->row_moves) && check_test_tier1_field(text, "code_certified", &build->code_certified) &&
                      check_test_tier1_field(text, "code_uncertified", &build->code_uncertified);
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
// that addresses from rip has its own relocation at its displacement: a PC32 for a lea and for a direct symbol's access
// (#2573), and a GOT one for a load of an address (each recomputed address one more). Into *loop_addresses, how many
// instructions in the loop (the range of its back edge, the last backward jump) compute an address from rip (a lea, or
// a load from a GOT slot) or from rsp's frame: the recomputed addresses there; and into *zeroing_count, its zeroing xors.
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
    // Each record that computes an address from rip: a lea, or a load from a GOT slot.
    uint8_t* from_rip = calloc(CHECK_TEST_TIER1_RECORDS, 1);
    uint32_t relocations = 0;
    const uint32_t count = records != NULL && offsets != NULL && types != NULL && live != NULL && from_rip != NULL ?
                               check_test_tier1_function(object, "tier1_remat", records, CHECK_TEST_TIER1_RECORDS, offsets, types, &relocations) : 0;
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
        if (instruction->mode == BUSTER_X86_MEMORY_RIP)
        {
            // The displacement field ends where any immediate after it starts. A lea's reference is a PC32, and a mov
            // r64's a PC32 for a direct symbol's value or a GOT slot's for an address; any other access's a PC32.
            const uint32_t field = records[i].offset + records[i].length - x86_fixup_distance(form, X86_FIXUP_DISPLACEMENT);
            uint32_t direct = 0;
            uint32_t slot = 0;
            for (uint32_t r = 0; r < relocations; r += 1)
            {
                direct += offsets[r] == field && types[r] == R_X86_64_PC32;
                slot += offsets[r] == field && types[r] == R_X86_64_REX_GOTPCRELX;
            }
            referenced &= direct + slot == 1 && (slot == 0 || form == X86_FORM_MOV_R64_RM64);
            from_rip[i] = form == X86_FORM_LEA_R64_M || slot != 0;
        }
    }
    for (uint32_t i = loop_first; i <= loop_last && i < count; i += 1)
    {
        const X86Instruction* instruction = &records[i].instruction;
        const uint32_t form = instruction->form & BUSTER_X86_FORM_NUMBER;
        const bool frame = instruction->mode == BUSTER_X86_MEMORY_BASE && instruction->rm == X86_REGISTER_RSP && form == X86_FORM_LEA_R64_M;
        addressed += from_rip[i] != 0 || frame;
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
    free(from_rip);
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

// #1395: the same target must produce the same object regardless of the compiler host's kernel path. The address
// sum and load-add loop exposed the former maximal-munch/costed split; multiplication by 3/5/9 and by a power of two
// retain #1217's already established costs. A parameter-free leaf's volatile loads keep every operand visible and
// leave register assignment unrestricted, exercising the common allocation policy without phi order. Unsigned
// arithmetic keeps every oracle input defined. Scaled indices exercise #1217's index folding through C lowering,
// allocation and both rematerialization modes, with a store between the loads to preserve the access-order check.
static const char check_test_tier1_reproducible_source[] =
    "#include <stdio.h>\n"
    "__attribute__((noinline)) unsigned long m3(unsigned long x) { return x * 3; }\n"
    "__attribute__((noinline)) unsigned long m5(unsigned long x) { return x * 5; }\n"
    "__attribute__((noinline)) unsigned long m9(unsigned long x) { return x * 9; }\n"
    "__attribute__((noinline)) unsigned long m4(unsigned long x) { return x * 4; }\n"
    "__attribute__((noinline)) unsigned m3i(unsigned x) { return x * 3; }\n"
    "__attribute__((noinline)) unsigned long address_sum(unsigned long x, unsigned long y) { return x + y * 4 + 8; }\n"
    "__attribute__((noinline)) unsigned long chain(unsigned long x) { return (x * 3) * 3; }\n"
    "__attribute__((noinline)) unsigned long load_add(unsigned long x, const unsigned long* p) { return x + *p; }\n"
    "__attribute__((noinline)) unsigned long two_loads(const unsigned long* p) { return p[0] + p[1]; }\n"
    "__attribute__((noinline)) unsigned long scaled_indices(unsigned* p, unsigned long i, unsigned x)\n"
    "{ unsigned a = p[(i << 1) + 2]; p[(i + 1) << 1] = x; return (unsigned long)a + p[i * 2 + 1] + p[(i << 1) + 2]; }\n"
    "volatile unsigned long allocation_inputs[] = {0x123456789abcdeful, ~0ul, 0x8000000000000000ul, 5};\n"
    "__attribute__((noinline)) unsigned long unrestricted(void)\n"
    "{ unsigned long a = allocation_inputs[0], b = allocation_inputs[1], c = allocation_inputs[2], d = allocation_inputs[3]; return (a + b) + (c + d); }\n"
    "__attribute__((noinline)) unsigned long loop(const unsigned long* p, unsigned n)\n"
    "{ unsigned long s = 1; for (unsigned i = 0; i < n; i += 1) s = (s * 3) * 3 + p[i]; return s; }\n"
    "int main(void)\n"
    "{\n"
    "    unsigned long p[] = {0, 1, ~0ul, 0x8000000000000000ul, 0x123456789abcdeful};\n"
    "    unsigned q[] = {1, 3, 5, 7, 9, 11};\n"
    "    unsigned long s = loop(p, 0) ^ loop(p, 5) ^ two_loads(p + 2) ^ unrestricted();\n"
    "    for (unsigned i = 0; i < 5; i += 1)\n"
    "    {\n"
    "        unsigned long x = p[i];\n"
    "        s += m3(x) ^ m5(x) ^ m9(x) ^ m4(x) ^ m3i((unsigned)x);\n"
    "        s ^= address_sum(x, p[4 - i]) + chain(x) + load_add(x, p + i);\n"
    "        s += scaled_indices(q, i & 1, (unsigned)x);\n"
    "    }\n"
    "    printf(\"%lx\\n\", s);\n"
    "    return 0;\n"
    "}\n";

// A failed whole-object comparison must locate a code difference without a local rerun: report its first byte and
// each fixture function's first different decoded record, with a short window on either side and its relocations.
static void check_test_tier1_reproducible_difference(const char* first, const char* second, uint32_t remat)
{
    const char* paths[2] = { first, second };
    uint64_t lengths[2] = { 0 };
    uint8_t* bytes[2] = { check_test_driver_read_aligned(first, &lengths[0]), check_test_driver_read_aligned(second, &lengths[1]) };
    if (bytes[0] != NULL && bytes[1] != NULL)
    {
        uint64_t at = 0;
        while (at < lengths[0] && at < lengths[1] && bytes[0][at] == bytes[1][at])
        {
            at += 1;
        }
        char values[2][8];
        for (uint32_t path = 0; path < 2; path += 1)
        {
            if (at < lengths[path])
            {
                snprintf(values[path], sizeof(values[path]), "%02x", bytes[path][at]);
            }
            else
            {
                snprintf(values[path], sizeof(values[path]), "EOF");
            }
        }
        fprintf(stderr, "test-checker: Tier 1 object difference remat=%u offset=%" PRIu64 " vector=%s scalar=%s sizes=%" PRIu64 "/%" PRIu64 "\n",
                remat, at, values[0], values[1], lengths[0], lengths[1]);
    }
    else
    {
        fprintf(stderr, "test-checker: Tier 1 object comparison remat=%u cannot read vector=%s or scalar=%s\n", remat, first, second);
    }
    free(bytes[0]);
    free(bytes[1]);
    enum
    {
        CHECK_TEST_TIER1_REPRODUCIBLE_RECORDS = 4096,
    };
    X86Decoded* records[2] = { calloc(CHECK_TEST_TIER1_REPRODUCIBLE_RECORDS, sizeof(X86Decoded)), calloc(CHECK_TEST_TIER1_REPRODUCIBLE_RECORDS, sizeof(X86Decoded)) };
    uint32_t* offsets = calloc(2 * CHECK_TEST_TIER1_REPRODUCIBLE_RECORDS, sizeof(uint32_t));
    uint32_t* types = calloc(2 * CHECK_TEST_TIER1_REPRODUCIBLE_RECORDS, sizeof(uint32_t));
    const bool ready = records[0] != NULL && records[1] != NULL && offsets != NULL && types != NULL && x86_disassembler_initialize();
    static const char* const names[] = { "m3", "m5", "m9", "m4", "m3i", "address_sum", "chain", "load_add", "two_loads", "scaled_indices", "unrestricted", "loop", "main" };
    for (uint32_t f = 0; ready && f < sizeof(names) / sizeof(names[0]); f += 1)
    {
        uint32_t counts[2];
        uint32_t relocations[2];
        for (uint32_t path = 0; path < 2; path += 1)
        {
            counts[path] = check_test_tier1_function(paths[path], names[f], records[path], CHECK_TEST_TIER1_REPRODUCIBLE_RECORDS,
                                                   offsets + path * CHECK_TEST_TIER1_REPRODUCIBLE_RECORDS, types + path * CHECK_TEST_TIER1_REPRODUCIBLE_RECORDS, &relocations[path]);
        }
        uint32_t at = 0;
        while (at < counts[0] && at < counts[1] && records[0][at].offset == records[1][at].offset && records[0][at].length == records[1][at].length &&
               records[0][at].address32 == records[1][at].address32 && memcmp(&records[0][at].instruction, &records[1][at].instruction, sizeof(X86Instruction)) == 0)
        {
            at += 1;
        }
        const bool same = counts[0] != 0 && counts[0] == counts[1] && at == counts[0];
        fprintf(stderr, "test-checker: Tier 1 object function=%s remat=%u records=%u/%u relocations=%u/%u code=%s\n",
                names[f], remat, counts[0], counts[1], relocations[0], relocations[1], same ? "same" : "different or undecodable");
        for (uint32_t r = at != 0 ? at - 1 : 0; !same && r < at + 4; r += 1)
        {
            for (uint32_t path = 0; path < 2; path += 1)
            {
                if (r < counts[path])
                {
                    const X86Decoded* decoded = &records[path][r];
                    const X86Instruction* instruction = &decoded->instruction;
                    fprintf(stderr, "  function=%s remat=%u path=%s record=%u offset=%u length=%u form=%u reg=%u rm=%u index=%u mode=%u vvvv=%u decorations=%u address32=%u disp=%" PRId32 " imm=%" PRId32 "\n",
                            names[f], remat, path == 0 ? "vector" : "scalar", r, decoded->offset, decoded->length, instruction->form, instruction->reg, instruction->rm, instruction->index,
                            instruction->mode, instruction->vvvv, instruction->decorations, decoded->address32, instruction->displacement, instruction->immediate);
                }
            }
        }
        uint32_t reported = 0;
        for (uint32_t r = 0; reported < 4 && (r < relocations[0] || r < relocations[1]); r += 1)
        {
            const uint32_t s = r + CHECK_TEST_TIER1_REPRODUCIBLE_RECORDS;
            if (r >= relocations[0] || r >= relocations[1] || offsets[r] != offsets[s] || types[r] != types[s])
            {
                fprintf(stderr, "  function=%s remat=%u relocation=%u vector-offset/type=%u/%u scalar-offset/type=%u/%u (UINT32_MAX absent)\n", names[f], remat, r,
                        r < relocations[0] ? offsets[r] : UINT32_MAX, r < relocations[0] ? types[r] : UINT32_MAX,
                        r < relocations[1] ? offsets[s] : UINT32_MAX, r < relocations[1] ? types[s] : UINT32_MAX);
                reported += 1;
            }
        }
    }
    if (!ready)
    {
        fprintf(stderr, "test-checker: Tier 1 object comparison remat=%u cannot allocate or initialize decoded diagnostics\n", remat);
    }
    free(records[0]);
    free(records[1]);
    free(offsets);
    free(types);
}

static void check_test_driver_tier1_reproducible(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    char source[512];
    char reference[512];
    snprintf(source, sizeof(source), "%s/tier1-reproducible.c", directory);
    snprintf(reference, sizeof(reference), "%s/tier1-reproducible-clang", directory);
    const bool written = parse_test_write(source, check_test_tier1_reproducible_source, sizeof(check_test_tier1_reproducible_source) - 1);
    char* oracle[] = { (char*)clang, "-O0", "-march=x86-64", "-o", reference, source, NULL };
    char* diagnostics = NULL;
    const bool referenced = written && check_test_driver_spawn(oracle, &diagnostics) == 0;
    free(diagnostics);
    char expected[256] = {0};
    const bool ready = referenced && check_test_driver_capture(reference, expected, sizeof(expected)) && expected[0] != 0;
    check_test_expect(test, ready, "the independent C oracle runs the host-reproducibility fixture");
    for (uint32_t remat = 0; ready && remat < 2; remat += 1)
    {
        char objects[2][512];
        bool compiled[2] = {false};
        for (uint32_t path = 0; path < 2; path += 1)
        {
            char linked[512];
            snprintf(objects[path], sizeof(objects[path]), "%s/tier1-reproducible-%u-%u.o", directory, remat, path);
            snprintf(linked, sizeof(linked), "%s/tier1-reproducible-%u-%u", directory, remat, path);
            CheckTestTier1Build build;
            compiled[path] = check_test_tier1_build(executable, source, "", objects[path], remat != 0, path != 0, true, false, "-march=x86-64", &build);
            check_test_expect(test, compiled[path], "both host paths compile every reproducibility fixture function through Tier 1");
            char* link[] = { (char*)clang, "-o", linked, objects[path], NULL };
            char* text = NULL;
            const bool built = compiled[path] && check_test_driver_spawn(link, &text) == 0;
            free(text);
            char actual[256] = {0};
            check_test_expect(test, built && check_test_driver_capture(linked, actual, sizeof(actual)) && strcmp(actual, expected) == 0,
                              "each reproducibility object executes like the independent C oracle");
            unlink(linked);
        }
        const bool same = compiled[0] && compiled[1] && check_test_driver_same_files(objects[0], objects[1]);
        if (compiled[0] && compiled[1] && !same)
        {
            check_test_tier1_reproducible_difference(objects[0], objects[1], remat);
        }
        check_test_expect(test, same,
                          "Tier 1 emits byte-identical scalar/vector objects for the same baseline target, with either rematerialization mode");
        unlink(objects[0]);
        unlink(objects[1]);
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

// --tier1-schedule (#172): the rematerialization fixture, its loops' loads, stores, multiplications and flags, compiled by
// each path with each scheduling variant. --verify (always the builds') checks every scheduled block against the scalar
// reference (tier1_schedule_verify after allocation, tier1_schedule_ir_verify before selection); each executable must print
// clang's build's output, and the report must say records or instructions moved, which a dropped option would not. The
// postallocation variant only permutes records, so its record count is the plain build's.
static void check_test_driver_tier1_schedule(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    char source[512];
    char reference[512];
    snprintf(source, sizeof(source), "%s/tier1-schedule.c", directory);
    snprintf(reference, sizeof(reference), "%s/tier1-schedule-clang", directory);
    const bool written = parse_test_write(source, check_test_tier1_remat_source, sizeof(check_test_tier1_remat_source) - 1);
    char* clang_arguments[] = { (char*)clang, "-O0", "-o", reference, source, NULL };
    char* clang_text = NULL;
    const bool referenced = written && check_test_driver_spawn(clang_arguments, &clang_text) == 0;
    free(clang_text);
    char expected[256] = { 0 };
    const bool ran = referenced && check_test_driver_capture(reference, expected, sizeof(expected)) && expected[0] != 0;
    check_test_expect(test, ran, "clang's build of the scheduling fixture runs");
    static const char* const variants[3] = { NULL, "--tier1-schedule=post", "--tier1-schedule=pre" };
    for (uint32_t path = 0; ran && path < 2; path += 1)
    {
        uint64_t records[3] = { 0 };
        uint64_t moved[3] = { 0 };
        for (uint32_t v = 0; v < 3; v += 1)
        {
            char output[512];
            snprintf(output, sizeof(output), "%s/tier1-schedule-%u-%u", directory, path, v);
            char* arguments[12] = { (char*)executable, "--tier1", "--tier1-report", "--verify" };
            uint32_t count = 4;
            if (variants[v] != NULL)
            {
                arguments[count++] = (char*)variants[v];
            }
            if (path != 0)
            {
                arguments[count++] = "--scalar";
            }
            arguments[count++] = "-o";
            arguments[count++] = output;
            arguments[count++] = source;
            arguments[count] = NULL;
            char* text = NULL;
            const int status = check_test_driver_spawn(arguments, &text);
            const bool reported = check_test_tier1_field(text, "records", &records[v]) && check_test_tier1_field(text, "schedule_moved", &moved[v]);
            uint64_t internal = 1;
            const bool clean = check_test_tier1_field(text, "internal_failures", &internal) && internal == 0;
            if (status != 0 && text != NULL)
            {
                fprintf(stderr, "%s", text);
            }
            free(text);
            check_test_expect(test, status == 0 && reported && clean, "each scheduling variant compiles the fixture, every block passing its reference");
            char actual[256] = { 0 };
            check_test_expect(test, status == 0 && check_test_driver_capture(output, actual, sizeof(actual)) && strcmp(actual, expected) == 0,
                              "each scheduling variant's executable prints clang's build's output");
            unlink(output);
        }
        check_test_expect(test, moved[0] == 0 && moved[1] != 0 && moved[2] != 0, "both variants reach Tier 1 and move its code, none without the option");
        check_test_expect(test, records[1] == records[0], "the postallocation variant permutes records alone");
        printf("  Tier 1's scheduling, %s path: %" PRIu64 " records, %" PRIu64 " moved after allocation, %" PRIu64 " IR instructions moved before (%" PRIu64 " records)\n",
               path == 0 ? "vector" : "scalar", records[0], moved[1], moved[2], records[2]);
    }
    unlink(source);
    unlink(reference);
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

// The --tier1-passes= fixture (#139): tier1_passes gives each pass of Tier 1's pipeline something to do: a constant
// compare (SCCP), an unused product (dead-code removal), a branch whose arms only call, leaving an empty block that jumps
// to a join without phis (CFG cleanup), a loop-invariant sum (LICM), a struct local (SROA), a repeated sum (#866's
// solver) and two small static callees (the inlining).
static const char check_test_tier1_passes_source[] =
    "#include <stdio.h>\n"
    "struct tier1_passes_pair { long low; long high; };\n"
    "static long tier1_passes_twice(long x) { return x * 2 + 1; }\n"
    "static struct tier1_passes_pair tier1_passes_make(long a) { struct tier1_passes_pair p = { a, a * 2 }; return p; }\n"
    "long tier1_passes(const long* values, long n, long k)\n"
    "{\n"
    "    struct tier1_passes_pair pair = tier1_passes_make(k + 4);\n"
    "    const long scale = 3;\n"
    "    long total = 0;\n"
    "    long unused = n * 77;\n"
    "    for (long i = 0; i < n; i += 1)\n"
    "    {\n"
    "        long invariant = k * scale + (n + 3) * k;\n"
    "        if (scale > 4)\n"
    "            total -= values[i];\n"
    "        else\n"
    "            total += values[i] * (n + 3) + invariant + tier1_passes_twice(i);\n"
    "    }\n"
    "    if (k > 0)\n"
    "    {\n"
    "        if (n > 50)\n"
    "        {\n"
    "            printf(\"long\\n\");\n"
    "        }\n"
    "    }\n"
    "    else\n"
    "    {\n"
    "        printf(\"negative\\n\");\n"
    "    }\n"
    "    long repeated = (k + n) * (k + n);\n"
    "    return total + pair.low + pair.high + repeated;\n"
    "}\n"
    "int main(int argc, char** argv)\n"
    "{\n"
    "    long values[100];\n"
    "    for (long i = 0; i < 100; i += 1)\n"
    "        values[i] = i * argc;\n"
    "    printf(\"%ld %ld\\n\", tier1_passes(values, 100, argc), tier1_passes(values, 7, -argc));\n"
    "    return 0;\n"
    "}\n";

// The report's counters of #139's passes (CheckTier1Counts' folded to gvn_removed, and the inlining's).
typedef struct CheckTestTier1Passes CheckTestTier1Passes;
struct CheckTestTier1Passes
{
    uint64_t inlined;
    uint64_t folded;
    uint64_t removed_instructions;
    uint64_t removed_blocks;
    uint64_t merged_blocks;
    uint64_t licm_moved;
    uint64_t sroa_split;
    uint64_t gvn_removed;
};

// The fixture compiled into `output` by --tier1 with `passes` (as "--tier1-passes=-sccp", or NULL for the default) and
// --verify, by the scalar path where `scalar` asks: whether it compiled with every function Tier 1's and reported each
// counter, into *counters.
static bool check_test_tier1_passes_build(const char* executable, const char* source, const char* output, const char* passes, bool scalar, CheckTestTier1Passes* counters)
{
    char* arguments[16] = { (char*)executable, "--tier1", "--tier1-report", "--tier1-refusals", "--verify" };
    uint32_t count = 5;
    if (passes != NULL)
    {
        arguments[count++] = (char*)passes;
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
    *counters = (CheckTestTier1Passes){ 0 };
    const char* every = "";
    const bool reported = check_test_tier1_field(text, "inlined", &counters->inlined) && check_test_tier1_field(text, "folded", &counters->folded) &&
                          check_test_tier1_field(text, "removed_instructions", &counters->removed_instructions) &&
                          check_test_tier1_field(text, "removed_blocks", &counters->removed_blocks) && check_test_tier1_field(text, "merged_blocks", &counters->merged_blocks) &&
                          check_test_tier1_field(text, "licm_moved", &counters->licm_moved) &&
                          check_test_tier1_field(text, "sroa_split", &counters->sroa_split) && check_test_tier1_field(text, "gvn_removed", &counters->gvn_removed);
    const bool taken = check_test_tier1_taken(text, &every, 1);
    if (status != 0 && text != NULL)
    {
        fprintf(stderr, "%s", text);
    }
    free(text);
    return status == 0 && reported && taken;
}

// --tier1-passes= (#139): the fixture by each path with each pass of Tier 1's pipeline turned from its default, and with
// all on and all off. Each build keeps every function Tier 1's and prints clang's build's output, and the report's counter
// of the pass it turned says that the option reached that pass: off, the pass did nothing (or less than by default, for
// dead-code removal and CFG cleanup, whose counts include what the rewrite drops whatever the passes); on, it did
// something it doesn't do by default. Then two files in one command: each unit's lanes take the option too.
static void check_test_driver_tier1_passes(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    char source[512];
    char reference[512];
    snprintf(source, sizeof(source), "%s/tier1-passes.c", directory);
    snprintf(reference, sizeof(reference), "%s/tier1-passes-clang", directory);
    const bool written = parse_test_write(source, check_test_tier1_passes_source, sizeof(check_test_tier1_passes_source) - 1);
    char* clang_arguments[] = { (char*)clang, "-O0", "-o", reference, source, NULL };
    char* clang_text = NULL;
    const bool referenced = written && check_test_driver_spawn(clang_arguments, &clang_text) == 0;
    free(clang_text);
    char expected[256] = { 0 };
    const bool ran = referenced && check_test_driver_capture(reference, expected, sizeof(expected)) && expected[0] != 0;
    check_test_expect(test, ran, "clang's build of the passes fixture runs");
    static const char* const variants[] = {
        "--tier1-passes=-sccp", "--tier1-passes=-dce", "--tier1-passes=-cfg", "--tier1-passes=+licm", "--tier1-passes=+sroa",
        "--tier1-passes=+gvn", "--tier1-passes=-inline", "--tier1-passes=+all", "--tier1-passes=-all", "--tier1-passes=-merge",
    };
    for (uint32_t path = 0; ran && path < 2; path += 1)
    {
        char output[512];
        snprintf(output, sizeof(output), "%s/tier1-passes-%u", directory, path);
        CheckTestTier1Passes plain;
        const bool plain_built = check_test_tier1_passes_build(executable, source, output, NULL, path != 0, &plain);
        char actual[256] = { 0 };
        check_test_expect(test, plain_built && check_test_driver_capture(output, actual, sizeof(actual)) && strcmp(actual, expected) == 0,
                          "the passes fixture's default --tier1 build prints clang's build's output, every function Tier 1's");
        // By default every pass of the pipeline but LICM, SROA and the solver does something.
        check_test_expect(test, plain_built && plain.inlined != 0 && plain.folded != 0 && plain.removed_instructions != 0 && plain.removed_blocks != 0 &&
                                    plain.merged_blocks != 0 && plain.licm_moved == 0 && plain.sroa_split == 0 && plain.gvn_removed == 0,
                          "the default pipeline inlines, folds, removes and merges, and runs neither LICM, SROA nor the solver");
        printf("  Tier 1's passes, %s path: inlined %" PRIu64 ", folded %" PRIu64 ", removed %" PRIu64 " instructions and %" PRIu64 " blocks (%" PRIu64 " merged) by default\n",
               path == 0 ? "vector" : "scalar", plain.inlined, plain.folded, plain.removed_instructions, plain.removed_blocks, plain.merged_blocks);
        for (uint32_t v = 0; plain_built && v < BUSTER_ARRAY_LENGTH(variants); v += 1)
        {
            CheckTestTier1Passes with;
            const bool built = check_test_tier1_passes_build(executable, source, output, variants[v], path != 0, &with);
            char variant_output[256] = { 0 };
            const bool same = built && check_test_driver_capture(output, variant_output, sizeof(variant_output)) && strcmp(variant_output, expected) == 0;
            const bool reached = v == 0   ? with.folded == 0 :
                                 v == 1   ? with.removed_instructions < plain.removed_instructions :
                                 v == 2   ? with.removed_blocks < plain.removed_blocks :
                                 v == 3   ? with.licm_moved != 0 :
                                 v == 4   ? with.sroa_split != 0 :
                                 v == 5   ? with.gvn_removed != 0 :
                                 v == 6   ? with.inlined == 0 :
                                 v == 7   ? with.inlined != 0 && with.folded != 0 && with.merged_blocks != 0 && with.licm_moved != 0 && with.sroa_split != 0 &&
                                            with.gvn_removed != 0 :
                                 v == 8   ? with.inlined == 0 && with.folded == 0 && with.removed_instructions < plain.removed_instructions && with.merged_blocks == 0 &&
                                            with.licm_moved == 0 && with.sroa_split == 0 && with.gvn_removed == 0 :
                                            with.merged_blocks == 0 && with.removed_blocks < plain.removed_blocks;
            check_test_expect(test, same, variants[v]);
            check_test_expect(test, built && reached, variants[v]);
            if (!(built && reached))
            {
                printf("  %s, %s path: inlined %" PRIu64 " folded %" PRIu64 " removed %" PRIu64 "/%" PRIu64 " merged %" PRIu64 " licm %" PRIu64 " sroa %" PRIu64 " gvn %" PRIu64 "\n",
                       variants[v], path == 0 ? "vector" : "scalar", with.inlined, with.folded, with.removed_instructions, with.removed_blocks, with.merged_blocks,
                       with.licm_moved, with.sroa_split, with.gvn_removed);
            }
        }
        unlink(output);
    }
    // Two source files in one command (c_checker_units.c's lanes): -c writes each unit's object in the working directory, so
    // the command runs inside `directory`.
    char first[512];
    char second[512];
    snprintf(first, sizeof(first), "%s/tier1-passes-two-a.c", directory);
    snprintf(second, sizeof(second), "%s/tier1-passes-two-b.c", directory);
    static const char other[] = "long tier1_passes_two_other(long x)\n{\n    return x + 1;\n}\n";
    const bool two = parse_test_write(first, check_test_tier1_passes_source, sizeof(check_test_tier1_passes_source) - 1) &&
                     parse_test_write(second, other, sizeof(other) - 1);
    check_test_expect(test, two, "the passes two-file fixture writes");
    char resolved[4096];
    const char* program = realpath(executable, resolved) != NULL ? resolved : executable;
    char quoted_directory[4096];
    char quoted_program[4096];
    char quoted_first[4096];
    char quoted_second[4096];
    char script[16384] = { 0 };
    const bool quoted = two && check_test_tier1_quoted(quoted_directory, sizeof(quoted_directory), directory) &&
                        check_test_tier1_quoted(quoted_program, sizeof(quoted_program), program) &&
                        check_test_tier1_quoted(quoted_first, sizeof(quoted_first), first) && check_test_tier1_quoted(quoted_second, sizeof(quoted_second), second);
    const int length = quoted ? snprintf(script, sizeof(script), "cd '%s' && '%s' --tier1 --tier1-passes=+licm,-sccp --tier1-report -c '%s' '%s'", quoted_directory,
                                         quoted_program, quoted_first, quoted_second)
                              : -1;
    check_test_expect(test, length > 0 && (size_t)length < sizeof(script), "the passes two-file command fits its buffer");
    char* arguments[] = { "sh", "-c", script, NULL };
    char* text = NULL;
    const int status = length > 0 ? check_test_driver_spawn(arguments, &text) : -1;
    // The report's tier1 lines: one per unit or one summed over them.
    uint64_t moved = 0;
    uint64_t folded = 0;
    for (const char* line = text; line != NULL && *line != 0;)
    {
        const char* end = strchr(line, '\n');
        const size_t span = end != NULL ? (size_t)(end - line) : strlen(line);
        if (span > 6 && memcmp(line, "tier1\t", 6) == 0)
        {
            char field[4096];
            const size_t kept = span < sizeof(field) - 1 ? span : sizeof(field) - 1;
            snprintf(field, sizeof(field), "%.*s", (int)kept, line);
            uint64_t value = 0;
            moved += check_test_tier1_field(field, "licm_moved", &value) ? value : 0;
            folded += check_test_tier1_field(field, "folded", &value) ? value : 0;
        }
        line = end != NULL ? end + 1 : NULL;
    }
    free(text);
    check_test_expect(test, status == 0 && moved != 0 && folded == 0, "--tier1-passes= reaches the lanes of a command of several files");
    char object_a[512];
    char object_b[512];
    snprintf(object_a, sizeof(object_a), "%s/tier1-passes-two-a.o", directory);
    snprintf(object_b, sizeof(object_b), "%s/tier1-passes-two-b.o", directory);
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
// memory, into *registers and *memory, into *same whether every one through a register reads the same register (a
// dispatch block's indirect jump and the jumps' copies of it, #2554), and into *trailing the direct jumps after its last
// indirect jump: its edges' stubs, each of which ends in one to its label's block, since the dispatch block is laid out
// last (#1016's review). Whether it was found.
static bool check_test_tier1_jumps(const char* path, const char* name, X86Decoded* records, uint32_t capacity, uint32_t* offsets, uint32_t* types, uint32_t* registers, uint32_t* memory,
                                   uint32_t* trailing, bool* same)
{
    uint32_t relocations = 0;
    const uint32_t count = check_test_tier1_function(path, name, records, capacity, offsets, types, &relocations);
    *registers = 0;
    *memory = 0;
    *trailing = 0;
    *same = true;
    uint32_t first = UINT32_MAX;
    for (uint32_t i = 0; i < count; i += 1)
    {
        const uint32_t form = records[i].instruction.form & BUSTER_X86_FORM_NUMBER;
        const bool jump = form == X86_FORM_JMP_RM64;
        const bool direct = jump && records[i].instruction.mode == BUSTER_X86_REGISTER_DIRECT;
        *same = *same && (!direct || first == UINT32_MAX || records[i].instruction.rm == first);
        first = direct && first == UINT32_MAX ? records[i].instruction.rm : first;
        *registers += direct;
        *memory += jump && records[i].instruction.mode != BUSTER_X86_REGISTER_DIRECT;
        *trailing = jump ? 0 : *trailing + (form == X86_FORM_JMP_REL8 || form == X86_FORM_JMP_REL32);
    }
    return count != 0;
}

// The labels fixture (#789): by --tier1, every function Tier 1's, by each path with and without rematerialization, and
// its program prints what clang -O0's and Tier 0's builds of it print. Tier 1's indirect jumps are each function's own:
// through one register where computed gotos share their dispatch block (its indirect jump, and each jump to it's copy of
// it, #2554), one where a switch's table dispatches, none where the jump can go to one label alone, and a stub after the
// dispatch block's where an edge into a label has moves.
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
    // TSan's and MSan's PIE executables aren't baked. The compiler children initialize their own tables,
    // but the ELF object oracle below also reads x86_effects in this test process (#1829).
    const bool semantics = (x86_semantics_initialized || x86_semantics_initialize()) &&
                           x86_effects_control(x86_effects(X86_FORM_JMP_RM64)) == BUSTER_X86_CONTROL_JUMP &&
                           x86_effects_control(x86_effects(X86_FORM_JNE_REL8)) == BUSTER_X86_CONTROL_BRANCH &&
                           x86_effects_control(x86_effects(X86_FORM_RET)) == BUSTER_X86_CONTROL_RETURN;
    check_test_expect(test, semantics, "the Tier 1 table-label oracle has initialized x86 control effects");
    const bool ready = ran && semantics && x86_disassembler_initialize();
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
            bool same[5] = { false };
            bool decoded = compiled && records != NULL && offsets != NULL && types != NULL;
            for (uint32_t f = 0; decoded && f < BUSTER_ARRAY_LENGTH(names); f += 1)
            {
                decoded = check_test_tier1_jumps(object, names[f], records, CHECK_TEST_TIER1_LABELS_RECORDS, offsets, types, &registers[f], &memory[f], &trailing[f], &same[f]);
            }
            check_test_expect(test, decoded && registers[0] >= 1 && memory[0] == 0 && same[0] && registers[2] >= 1 && memory[2] == 0 && same[2],
                              "a function's computed gotos jump through their dispatch block's register: its indirect jump, or a jump's copy of it");
            check_test_expect(test, decoded && registers[1] == 1 && memory[1] == 0, "tier1_classify's switch is one indirect jump through a register");
            check_test_expect(test, decoded && registers[3] == 0 && memory[3] == 0, "tier1_single's jump to its one label is a direct jump");
            check_test_expect(test, decoded && registers[4] >= 1 && same[4] && trailing[4] != 0, "tier1_both's indirect jump has a stub after it, for the label its edge moves into");
            uint32_t table_relocations = 0;
            const uint32_t table_records = decoded ? check_test_tier1_function(object, "tier1_tables", records, CHECK_TEST_TIER1_LABELS_RECORDS, offsets, types, &table_relocations) : 0;
            uint32_t table_addresses = 0;
            uint32_t table_jumps = 0;
            for (uint32_t i = 0; i < table_records; i += 1)
            {
                const X86Instruction* instruction = &records[i].instruction;
                const uint32_t form = instruction->form & BUSTER_X86_FORM_NUMBER;
                table_addresses += form == X86_FORM_LEA_R64_M && instruction->mode == BUSTER_X86_MEMORY_RIP;
                table_jumps += form == X86_FORM_JMP_RM64 && instruction->mode == BUSTER_X86_REGISTER_DIRECT;
            }
            check_test_expect(test, table_records != 0 && table_jumps == 2 && table_addresses == 2 && table_relocations == 2,
                              "each of two switches shares one relocated table address between its signed load and target addition");
            uint32_t loop_relocations = 0;
            const uint32_t loop_records = decoded ? check_test_tier1_function(object, "tier1_table_loop", records, CHECK_TEST_TIER1_LABELS_RECORDS, offsets, types, &loop_relocations) : 0;
            uint32_t loop_addresses = 0;
            uint32_t loop_jumps = 0;
            uint32_t loop_address = loop_records;
            uint32_t loop_base = UINT32_MAX;
            uint32_t dispatch = loop_records;
            bool triples = true;
            for (uint32_t i = 0; i < loop_records; i += 1)
            {
                const X86Instruction* instruction = &records[i].instruction;
                const uint32_t form = instruction->form & BUSTER_X86_FORM_NUMBER;
                if (form == X86_FORM_LEA_R64_M && instruction->mode == BUSTER_X86_MEMORY_RIP)
                {
                    loop_addresses += 1;
                    loop_address = i;
                    loop_base = instruction->reg;
                }
                if (form == X86_FORM_JMP_RM64 && instruction->mode == BUSTER_X86_REGISTER_DIRECT)
                {
                    loop_jumps += 1;
                    dispatch = i;
                    triples &= i >= 2;
                    if (i >= 2)
                    {
                        const X86Instruction* load = &records[i - 2].instruction;
                        const X86Instruction* add = &records[i - 1].instruction;
                        const uint32_t add_form = add->form & BUSTER_X86_FORM_NUMBER;
                        const bool reversed = add_form == X86_FORM_ADD_RM64_R64;
                        const uint32_t destination = reversed ? add->rm : add->reg;
                        const uint32_t source_register = reversed ? add->reg : add->rm;
                        triples &= (load->form & BUSTER_X86_FORM_NUMBER) == X86_FORM_MOVSXD_R64_RM32 && load->mode == BUSTER_X86_MEMORY_BASE_INDEX(2) &&
                                   load->displacement == 0 && load->rm == loop_base && load->reg != loop_base &&
                                   (reversed || add_form == X86_FORM_ADD_R64_RM64) && add->mode == BUSTER_X86_REGISTER_DIRECT &&
                                   destination == load->reg && source_register == loop_base && instruction->rm == destination;
                    }
                }
            }
            check_test_expect(test, loop_records != 0 && loop_jumps == 1 && triples,
                              "each loop dispatch is adjacent movsxd [base + index*4], add base and jmp through the same target register");
            uint32_t loop_targets[4] = { 0 };
            const bool table_resolved = loop_addresses == 1 && loop_address < loop_records && records[loop_address].length >= 4 &&
                                        check_test_tier1_table_targets(object, "tier1_table_loop", records[loop_address].offset,
                                                                       records[loop_address].offset + records[loop_address].length - 4, loop_targets, false);
            const bool entry = table_resolved && check_test_tier1_table_entry(records, loop_records, loop_address, dispatch, loop_targets);
            if (loop_addresses != 1 || loop_relocations != 1 || !entry)
            {
                fprintf(stderr, "test-checker: table entry: rematerialize=%u path=%s records=%u addresses=%u relocations=%u address=%u dispatch=%u resolved=%u targets=%u,%u,%u,%u\n",
                        rematerialize, path == 0 ? "vector" : "scalar", loop_records, loop_addresses, loop_relocations, loop_address, dispatch,
                        (uint32_t)table_resolved, loop_targets[0], loop_targets[1], loop_targets[2], loop_targets[3]);
                for (uint32_t i = 0; i < loop_records; i += 1)
                {
                    const X86Instruction* instruction = &records[i].instruction;
                    const uint32_t form = instruction->form & BUSTER_X86_FORM_NUMBER;
                    const uint32_t control = (uint32_t)x86_effects(form)->kinds >> 4;
                    if (i == loop_address || control != BUSTER_X86_CONTROL_NONE)
                    {
                        fprintf(stderr, "test-checker: table entry record=%u offset=%u form=%u control=%u relative=%u target=%" PRId64 "\n",
                                i, records[i].offset, form, control, (uint32_t)x86_form_descriptions[form].relative_bytes,
                                (int64_t)records[i].offset + records[i].length + instruction->displacement);
                    }
                }
            }
            check_test_expect(test, loop_addresses == 1 && loop_relocations == 1 && entry,
                              "the loop's single relocated table base dominates each dispatch and cannot be reentered");
            if (entry && rematerialize == 0 && path == 0)
            {
                check_test_tier1_table_entry_controls(test, object, records, loop_records, loop_address, dispatch, loop_targets);
            }
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
// callee-saved register its call's crossing value takes, and its overflow area's address counts the push: its callee is
// noinline, so that --tier1's inlining (#2541) keeps the call.
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

// The scopes' fixture (#141): an interpreter loop shaped as Lua's luaV_execute, its handlers reached through a computed
// goto, each declaring `double n1, n2` (or `long long i1`) in its block and assigning them on some paths only, as Lua's
// tonumberns does, then calling C. Without the blocks' lifetimes, promotion carries every handler's locals around the
// dispatch loop, and each call saves the floating ones; with them, a block's start ends its locals' liveness. Both
// builds, by each path, print clang -O0's output; with lifetimes, Tier 1 saves fewer values around calls.
static const char check_test_tier1_scopes_source[] =
    "#include <stdio.h>\n"
    "\n"
    "typedef struct Value { int tag; union { double number; long long integer; } u; } Value;\n"
    "\n"
    "static __attribute__((noinline)) double scale(double x) { return x * 0.5; }\n"
    "static __attribute__((noinline)) long long step(long long x) { return x * 3 + 1; }\n"
    "\n"
    "#define TONUMBER(v, n) ((v)->tag == 1 ? ((n) = (v)->u.number, 1) : ((v)->tag == 2 ? ((n) = (double)(v)->u.integer, 1) : 0))\n"
    "\n"
    "double run(const unsigned char* code, Value* registers, int count)\n"
    "{\n"
    "    static void* dispatch[] = { &&op_add, &&op_multiply, &&op_subtract, &&op_step, &&op_halt };\n"
    "    int pc = 0;\n"
    "    double total = 0.0;\n"
    "    goto *dispatch[code[pc]];\n"
    "op_add:\n"
    "    {\n"
    "        double n1;\n"
    "        double n2;\n"
    "        Value* a = &registers[code[pc + 1] % count];\n"
    "        Value* b = &registers[code[pc + 2] % count];\n"
    "        if (TONUMBER(a, n1) && TONUMBER(b, n2)) { total += scale(n1 + n2); a->tag = 1; a->u.number = n1 + n2; }\n"
    "        pc += 3;\n"
    "        goto *dispatch[code[pc]];\n"
    "    }\n"
    "op_multiply:\n"
    "    {\n"
    "        double n1;\n"
    "        double n2;\n"
    "        Value* a = &registers[code[pc + 1] % count];\n"
    "        Value* b = &registers[code[pc + 2] % count];\n"
    "        if (TONUMBER(a, n1) && TONUMBER(b, n2)) { total += scale(n1 * n2); }\n"
    "        pc += 3;\n"
    "        goto *dispatch[code[pc]];\n"
    "    }\n"
    "op_subtract:\n"
    "    {\n"
    "        double n1;\n"
    "        double n2;\n"
    "        Value* a = &registers[code[pc + 1] % count];\n"
    "        Value* b = &registers[code[pc + 2] % count];\n"
    "        if (TONUMBER(a, n1) && TONUMBER(b, n2)) { total -= scale(n1 - n2); b->tag = 1; b->u.number = n2 - n1; }\n"
    "        pc += 3;\n"
    "        goto *dispatch[code[pc]];\n"
    "    }\n"
    "op_step:\n"
    "    {\n"
    "        long long i1;\n"
    "        Value* a = &registers[code[pc + 1] % count];\n"
    "        if (a->tag == 2) { i1 = a->u.integer; }\n"
    "        if (a->tag == 2) { a->u.integer = step(i1); total += (double)a->u.integer; }\n"
    "        pc += 2;\n"
    "        goto *dispatch[code[pc]];\n"
    "    }\n"
    "op_halt:\n"
    "    return total;\n"
    "}\n"
    "\n"
    "int main(void)\n"
    "{\n"
    "    unsigned char code[400];\n"
    "    int length = 0;\n"
    "    for (int k = 0; k < 60; k += 1)\n"
    "    {\n"
    "        const int op = (k * 7 + 3) % 4;\n"
    "        code[length++] = (unsigned char)op;\n"
    "        code[length++] = (unsigned char)(k * 5 + 1);\n"
    "        if (op != 3) { code[length++] = (unsigned char)(k * 11 + 2); }\n"
    "    }\n"
    "    code[length++] = 4;\n"
    "    for (int round = 0; round < 4; round += 1)\n"
    "    {\n"
    "        Value registers[7];\n"
    "        for (int r = 0; r < 7; r += 1)\n"
    "        {\n"
    "            registers[r].tag = (r + round) % 3;\n"
    "            if (registers[r].tag == 1) { registers[r].u.number = 0.25 * (r + 1) + round; } else { registers[r].u.integer = r * 2 + round; }\n"
    "        }\n"
    "        printf(\"%.17g\\n\", run(code, registers, 7));\n"
    "    }\n"
    "    return 0;\n"
    "}\n";

// The scopes' fixture by --tier1 (`lifetimes` or not, by each path) into `output`, its report's values saved around calls
// into *saves: whether it compiled with run Tier 1's.
static bool check_test_tier1_scopes_build(const char* executable, const char* source, const char* output, bool lifetimes, bool scalar, uint64_t* saves)
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
    *saves = 0;
    const bool reported = check_test_tier1_field(text, "call_stores", saves);
    const bool taken = text != NULL && strstr(text, "tier1-refused\trun\t") == NULL;
    if (status != 0 && text != NULL)
    {
        fprintf(stderr, "%s", text);
    }
    free(text);
    return status == 0 && reported && taken;
}

static void check_test_driver_tier1_scopes(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    char source[512];
    char reference[512];
    snprintf(source, sizeof(source), "%s/tier1-scopes.c", directory);
    snprintf(reference, sizeof(reference), "%s/tier1-scopes-clang", directory);
    const bool written = parse_test_write(source, check_test_tier1_scopes_source, sizeof(check_test_tier1_scopes_source) - 1);
    char* clang_arguments[] = { (char*)clang, "-O0", "-w", "-o", reference, source, NULL };
    char* clang_text = NULL;
    const bool referenced = written && check_test_driver_spawn(clang_arguments, &clang_text) == 0;
    free(clang_text);
    char expected[4096] = { 0 };
    const bool ran = referenced && check_test_driver_capture(reference, expected, sizeof(expected)) && expected[0] != 0;
    check_test_expect(test, ran, "clang's build of the scopes' fixture runs");
    uint64_t saves[2][2] = { { 0 } };
    for (uint32_t lifetimes = 0; ran && lifetimes < 2; lifetimes += 1)
    {
        for (uint32_t path = 0; path < 2; path += 1)
        {
            char output[512];
            snprintf(output, sizeof(output), "%s/tier1-scopes-%u-%u", directory, lifetimes, path);
            const bool built = check_test_tier1_scopes_build(executable, source, output, lifetimes != 0, path != 0, &saves[lifetimes][path]);
            check_test_expect(test, built, "the scopes' fixture compiles by --tier1, its interpreter Tier 1's");
            char actual[4096] = { 0 };
            check_test_expect(test, built && check_test_driver_capture(output, actual, sizeof(actual)) && strcmp(actual, expected) == 0, "the scopes' fixture prints clang's build's output");
            unlink(output);
        }
    }
    for (uint32_t path = 0; ran && path < 2; path += 1)
    {
        check_test_expect(test, saves[1][path] < saves[0][path], "a block's start ends its locals' liveness: fewer values saved around calls");
        printf("  Tier 1's saves around calls in the scopes' fixture, %s path: %" PRIu64 " with lifetimes, %" PRIu64 " without\n", path == 0 ? "vector" : "scalar", saves[1][path], saves[0][path]);
    }
    unlink(source);
    unlink(reference);
}

// The floating values' fixture (#141): its unit by --tier1 -c, by each path with and without rematerialization, every
// function Tier 1's (main's conditional expression of doubles among them), linked with its other unit compiled by clang
// -O2, prints what clang -O0's build of both prints, bit for bit in its %.17g digits.
static void check_test_driver_tier1_float(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    char unit[512];
    char other[512];
    char other_object[512];
    char reference[512];
    snprintf(unit, sizeof(unit), "%s/tier1-float.c", directory);
    snprintf(other, sizeof(other), "%s/tier1-float-other.c", directory);
    snprintf(other_object, sizeof(other_object), "%s/tier1-float-other.o", directory);
    snprintf(reference, sizeof(reference), "%s/tier1-float-clang", directory);
    const bool written = parse_test_write(unit, check_test_tier1_float_source, sizeof(check_test_tier1_float_source) - 1) &&
                         parse_test_write(other, check_test_tier1_float_other_source, sizeof(check_test_tier1_float_other_source) - 1);
    char* reference_arguments[] = { (char*)clang, "-O0", "-o", reference, unit, other, NULL };
    char* other_arguments[] = { (char*)clang, "-O2", "-c", "-o", other_object, other, NULL };
    char* text = NULL;
    const bool referenced = written && check_test_driver_spawn(reference_arguments, &text) == 0;
    free(text);
    text = NULL;
    const bool other_built = written && check_test_driver_spawn(other_arguments, &text) == 0;
    free(text);
    char expected[2048] = { 0 };
    const bool ran = referenced && other_built && check_test_driver_capture(reference, expected, sizeof(expected)) && expected[0] != 0;
    check_test_expect(test, ran, "clang's builds of the floating values' fixture run");
    static const char* const names[9] = { "tier1_float_take", "tier1_pass_float", "tier1_classify", "tier1_convert", "tier1_accumulate", "tier1_points", "tier1_print", "tier1_wide_hash", "main" };
    for (uint32_t rematerialize = 0; ran && rematerialize < 2; rematerialize += 1)
    {
        for (uint32_t path = 0; path < 2; path += 1)
        {
            char object[512];
            char program[512];
            snprintf(object, sizeof(object), "%s/tier1-float-%u-%u.o", directory, rematerialize, path);
            snprintf(program, sizeof(program), "%s/tier1-float-%u-%u", directory, rematerialize, path);
            CheckTestTier1Build build;
            const bool compiled = check_test_tier1_build_names(executable, unit, names, BUSTER_ARRAY_LENGTH(names), object, rematerialize != 0, path != 0, true, false, NULL, &build);
            check_test_expect(test, compiled, "the floating values' unit compiles by --tier1, every function Tier 1's");
            char* link_arguments[] = { (char*)clang, "-o", program, object, other_object, NULL };
            char* link_text = NULL;
            const bool linked = compiled && check_test_driver_spawn(link_arguments, &link_text) == 0;
            free(link_text);
            char actual[2048] = { 0 };
            check_test_expect(test, linked && check_test_driver_capture(program, actual, sizeof(actual)) && strcmp(actual, expected) == 0,
                              "Tier 1's floating values meet clang -O2's code and print clang -O0's build's output");
            unlink(object);
            unlink(program);
        }
    }
    unlink(unit);
    unlink(other);
    unlink(other_object);
    unlink(reference);
}

// The returns-twice fixture (#1847, check_test_tier1_twice_source), its unit by --tier1 --verify -c by each path, with
// and without rematerialization, linked with its other unit compiled by clang -O2: the four callers of a returns-twice
// function are refused and Tier 0's, the other function Tier 1's, and the program prints what clang -O0's build prints.
static void check_test_driver_tier1_twice(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    char unit[512];
    char other[512];
    char other_object[512];
    char reference[512];
    snprintf(unit, sizeof(unit), "%s/tier1-twice.c", directory);
    snprintf(other, sizeof(other), "%s/tier1-twice-other.c", directory);
    snprintf(other_object, sizeof(other_object), "%s/tier1-twice-other.o", directory);
    snprintf(reference, sizeof(reference), "%s/tier1-twice-clang", directory);
    const bool written = parse_test_write(unit, check_test_tier1_twice_source, sizeof(check_test_tier1_twice_source) - 1) &&
                         parse_test_write(other, check_test_tier1_twice_other_source, sizeof(check_test_tier1_twice_other_source) - 1);
    char* other_arguments[] = { (char*)clang, "-O2", "-c", "-o", other_object, other, NULL };
    char* reference_arguments[] = { (char*)clang, "-O0", "-o", reference, unit, other_object, NULL };
    char* text = NULL;
    const bool other_built = written && check_test_driver_spawn(other_arguments, &text) == 0;
    free(text);
    text = NULL;
    const bool referenced = other_built && check_test_driver_spawn(reference_arguments, &text) == 0;
    free(text);
    text = NULL;
    char expected[256] = { 0 };
    const bool ran = referenced && check_test_driver_capture(reference, expected, sizeof(expected)) && expected[0] != 0;
    check_test_expect(test, ran, "clang's build of the returns-twice fixture runs");
    static const char* const callers[4] = { "tier1_twice_setjmp", "tier1_twice_sigsetjmp", "tier1_twice_declared", "tier1_twice_pointer" };
    for (uint32_t rematerialize = 0; ran && rematerialize < 2; rematerialize += 1)
    {
        for (uint32_t path = 0; path < 2; path += 1)
        {
            char object[512];
            char program[512];
            snprintf(object, sizeof(object), "%s/tier1-twice-%u-%u.o", directory, rematerialize, path);
            snprintf(program, sizeof(program), "%s/tier1-twice-%u-%u", directory, rematerialize, path);
            char* arguments[12] = { (char*)executable, "--tier1", "--tier1-refusals", "--verify", "-c" };
            uint32_t count = 5;
            if (rematerialize == 0)
            {
                arguments[count++] = "--tier1-no-rematerialize";
            }
            if (path != 0)
            {
                arguments[count++] = "--scalar";
            }
            arguments[count++] = "-o";
            arguments[count++] = object;
            arguments[count++] = unit;
            arguments[count] = NULL;
            const int status = check_test_driver_spawn(arguments, &text);
            bool refused = text != NULL;
            for (uint32_t c = 0; refused && c < BUSTER_ARRAY_LENGTH(callers); c += 1)
            {
                char line[128];
                snprintf(line, sizeof(line), "tier1-refused\t%s\t", callers[c]);
                refused = strstr(text, line) != NULL;
            }
            static const char* const taken[1] = { "tier1_twice_none" };
            const bool compiled = status == 0 && refused && check_test_tier1_taken(text, taken, 1);
            if (!compiled && text != NULL)
            {
                fprintf(stderr, "%s", text);
            }
            free(text);
            text = NULL;
            check_test_expect(test, compiled, "--tier1 leaves the returns-twice callers to Tier 0 and takes the other function");
            char* link_arguments[] = { (char*)clang, "-o", program, object, other_object, NULL };
            const bool linked = compiled && check_test_driver_spawn(link_arguments, &text) == 0;
            free(text);
            text = NULL;
            char actual[256] = { 0 };
            check_test_expect(test, linked && check_test_driver_capture(program, actual, sizeof(actual)) && strcmp(actual, expected) == 0,
                              "the returns-twice callers' second returns print clang -O0's build's output");
            unlink(object);
            unlink(program);
        }
    }
    unlink(unit);
    unlink(other);
    unlink(other_object);
    unlink(reference);
}

// The dynamic stack's fixture (#141), compiled by --tier1 into an executable by each path, with and without
// rematerialization, with no function refused: it must print what clang -O0's build prints.
static void check_test_driver_tier1_dynamic(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    char unit[512];
    char reference[512];
    snprintf(unit, sizeof(unit), "%s/tier1-dynamic.c", directory);
    snprintf(reference, sizeof(reference), "%s/tier1-dynamic-clang", directory);
    const bool written = parse_test_write(unit, check_test_tier1_dynamic_source, sizeof(check_test_tier1_dynamic_source) - 1);
    char* reference_arguments[] = { (char*)clang, "-O0", "-o", reference, unit, NULL };
    char* text = NULL;
    const bool referenced = written && check_test_driver_spawn(reference_arguments, &text) == 0;
    free(text);
    char expected[4096] = { 0 };
    const bool ran = referenced && check_test_driver_capture(reference, expected, sizeof(expected)) && expected[0] != 0;
    check_test_expect(test, ran, "clang's build of the dynamic stack's fixture runs");
    for (uint32_t rematerialize = 0; ran && rematerialize < 2; rematerialize += 1)
    {
        for (uint32_t path = 0; path < 2; path += 1)
        {
            char program[512];
            snprintf(program, sizeof(program), "%s/tier1-dynamic-%u-%u", directory, rematerialize, path);
            CheckTestTier1Build build;
            const bool compiled = check_test_tier1_build(executable, unit, "", program, rematerialize != 0, path != 0, false, false, NULL, &build);
            check_test_expect(test, compiled, "the dynamic stack's fixture compiles by --tier1, every function Tier 1's");
            char actual[4096] = { 0 };
            check_test_expect(test, compiled && check_test_driver_capture(program, actual, sizeof(actual)) && strcmp(actual, expected) == 0,
                              "Tier 1's variable-length arrays, allocas and their calls print clang -O0's build's output");
            unlink(program);
        }
    }
    unlink(unit);
    unlink(reference);
}

// The asm bodies' fixture (#1839): asm statements placed in their callers as asm bodies, which --tier1 compiles itself
// (TIER1_RULE_ASM), each function Tier 1's: cpuid in a loop of eight live values (rbx a callee-saved register the
// prologue saves) and in always_inline wrappers beside rdtsc's, musl's syscall wrappers' shapes (r10, r8 and r9 explicit
// register variables) on system calls whose results don't vary, the x87's fsqrt on `+t` and fprem's loop with the
// status word in %ax, flag outputs, a template's own loop (`1: decl; jnz 1b`), a bswap and an lfence (Tier 0's rows,
// asm bodies under --tier1), callee-saved clobbers inside a loop of live values across a call, xmm clobbers beside live
// floating values, rsp's alignment at the template, rsp's explicit register variable read and moved back, 16-byte
// vectors through their addresses, in an explicit register variable's XMM register and as a matching input (#1846), and
// register operands passed in their registers (#2144): mulq's in a loop whose values keep out of the registers it
// touches, and an xmm operand's. Clang -O0's build is its oracle.
static const char check_test_tier1_asm_source[] =
    "int printf(const char* format, ...);\n"
    "static inline __attribute__((always_inline)) unsigned long tier1_rdtsc(void)\n"
    "{\n"
    "    unsigned lo, hi;\n"
    "    __asm__ volatile(\"rdtsc\" : \"=a\"(lo), \"=d\"(hi));\n"
    "    return (unsigned long)hi << 32 | lo;\n"
    "}\n"
    "static inline __attribute__((always_inline)) unsigned tier1_vendor(void)\n"
    "{\n"
    "    unsigned a, b, c, d;\n"
    "    __asm__ volatile(\"cpuid\" : \"=a\"(a), \"=b\"(b), \"=c\"(c), \"=d\"(d) : \"a\"(0u), \"c\"(0u));\n"
    "    return ((b == 0x756e6547u) | (b == 0x68747541u)) + (a != 0);\n"
    "}\n"
    "unsigned __attribute__((noinline)) tier1_cpuid(unsigned n, unsigned a1, unsigned a2, unsigned a3, unsigned a4, unsigned a5, unsigned a6, unsigned a7)\n"
    "{\n"
    "    unsigned total = 0;\n"
    "    for (unsigned i = 0; i < n; i += 1)\n"
    "    {\n"
    "        unsigned a, b, c, d;\n"
    "        __asm__ volatile(\"cpuid\" : \"=a\"(a), \"=b\"(b), \"=c\"(c), \"=d\"(d) : \"a\"(0u), \"c\"(0u));\n"
    "        total += (b == 0x756e6547u || b == 0x68747541u) + (a != 0) + a1 + a2 + a3 + a4 + a5 + a6 + a7 + i;\n"
    "        a1 += 1;\n"
    "        a2 += 2;\n"
    "        a3 += a1;\n"
    "        a4 ^= a2;\n"
    "        a5 += a4;\n"
    "        a6 -= a5;\n"
    "        a7 *= 3;\n"
    "    }\n"
    "    return total + tier1_vendor() + (tier1_rdtsc() != 0);\n"
    "}\n"
    "static long tier1_syscall1(long n, long a)\n"
    "{\n"
    "    unsigned long ret;\n"
    "    __asm__ __volatile__(\"syscall\" : \"=a\"(ret) : \"a\"(n), \"D\"(a) : \"rcx\", \"r11\", \"memory\");\n"
    "    return (long)ret;\n"
    "}\n"
    "static long tier1_syscall6(long n, long a, long b, long c, long d, long e, long f)\n"
    "{\n"
    "    unsigned long ret;\n"
    "    register long r10 __asm__(\"r10\") = d;\n"
    "    register long r8 __asm__(\"r8\") = e;\n"
    "    register long r9 __asm__(\"r9\") = f;\n"
    "    __asm__ __volatile__(\"syscall\" : \"=a\"(ret) : \"a\"(n), \"D\"(a), \"S\"(b), \"d\"(c), \"r\"(r10), \"r\"(r8), \"r\"(r9) : \"rcx\", \"r11\", \"memory\");\n"
    "    return (long)ret;\n"
    "}\n"
    "static long double tier1_sqrtl(long double x)\n"
    "{\n"
    "    __asm__(\"fsqrt\" : \"+t\"(x));\n"
    "    return x;\n"
    "}\n"
    "static long double tier1_fmodl(long double x, long double y)\n"
    "{\n"
    "    unsigned short status;\n"
    "    do\n"
    "    {\n"
    "        __asm__(\"fprem; fnstsw %%ax\" : \"+t\"(x), \"=a\"(status) : \"u\"(y));\n"
    "    } while (status & 0x400);\n"
    "    return x;\n"
    "}\n"
    "static int tier1_flags(long a, long b)\n"
    "{\n"
    "    int below, zero, sign;\n"
    "    __asm__(\"cmpq %3, %4\" : \"=@ccb\"(below), \"=@ccz\"(zero), \"=@ccs\"(sign) : \"r\"(b), \"r\"(a));\n"
    "    return below * 4 + zero * 2 + sign;\n"
    "}\n"
    "static int tier1_count(int n)\n"
    "{\n"
    "    int r = n;\n"
    "    __asm__(\"1: decl %0; jnz 1b\" : \"+r\"(r));\n"
    "    return r + n;\n"
    "}\n"
    "static unsigned tier1_swap(unsigned x, unsigned y)\n"
    "{\n"
    "    __asm__(\"bswap %0\" : \"+r\"(x));\n"
    "    __asm__ volatile(\"lfence\" ::: \"memory\");\n"
    "    return x + y;\n"
    "}\n"
    "static int __attribute__((noinline)) tier1_callee(int x)\n"
    "{\n"
    "    return x * 3 + 1;\n"
    "}\n"
    "static long tier1_callee_saved(long n)\n"
    "{\n"
    "    long a = n, b = n * 2, c = n * 3, d = n * 4, e = n * 5, f = n * 6, g = n * 7, h = n * 8;\n"
    "    long total = 0;\n"
    "    for (long i = 0; i < n; i += 1)\n"
    "    {\n"
    "        total += tier1_callee((int)(a + i));\n"
    "        __asm__ volatile(\"movq $1, %%rbx; movq $2, %%r12; movq $3, %%r13; movq $4, %%r14\" ::: \"rbx\", \"r12\", \"r13\", \"r14\");\n"
    "        total += a + b + c + d + e + f + g + h;\n"
    "        a += 1;\n"
    "        h ^= a;\n"
    "    }\n"
    "    return total;\n"
    "}\n"
    "static double tier1_vectors(double x, int k)\n"
    "{\n"
    "    double y = x * 2.0;\n"
    "    float z = (float)x + 0.5f;\n"
    "    long r = k;\n"
    "    __asm__ volatile(\"xorps %%xmm0, %%xmm0; xorps %%xmm1, %%xmm1; xorps %%xmm2, %%xmm2; addq $3, %0\" : \"+r\"(r) : : \"xmm0\", \"xmm1\", \"xmm2\");\n"
    "    return y + x + (double)r + (double)z;\n"
    "}\n"
    "static int tier1_aligned(void)\n"
    "{\n"
    "    unsigned long sp;\n"
    "    __asm__ volatile(\"movq %%rsp, %0\" : \"=r\"(sp));\n"
    "    return (int)(sp & 15);\n"
    "}\n"
    "static int tier1_stack(void)\n"
    "{\n"
    "    register unsigned long sp __asm__(\"rsp\");\n"
    "    __asm__(\"\" : \"=r\"(sp));\n"
    "    unsigned long before = sp;\n"
    "    __asm__ volatile(\"\" : \"+r\"(sp));\n"
    "    unsigned long r;\n"
    "    __asm__(\"mov %1, %0\" : \"=r\"(r) : \"r\"(sp));\n"
    "    return sp == before && r == before && (sp & 7) == 0;\n"
    "}\n"
    "typedef float tier1_v4sf __attribute__((vector_size(16)));\n"
    "static float tier1_packed(float x)\n"
    "{\n"
    "    tier1_v4sf a = {x, 1.0f, 2.0f, 3.0f};\n"
    "    register tier1_v4sf r __asm__(\"xmm9\") = a;\n"
    "    __asm__(\"addps %0, %0\" : \"+x\"(r));\n"
    "    tier1_v4sf b;\n"
    "    __asm__(\"mulps %1, %0\" : \"=x\"(b) : \"x\"(r), \"0\"(a + a));\n"
    "    return b[0] + b[3] * 10.0f;\n"
    "}\n"
    "__attribute__((noinline)) unsigned long tier1_mulq(long n)\n"
    "{\n"
    "    unsigned long s = 0;\n"
    "    for (long i = 0; i < n; i++)\n"
    "    {\n"
    "        unsigned long lo, hi;\n"
    "        __asm__(\"mulq %3\" : \"=a\"(lo), \"=d\"(hi) : \"0\"((unsigned long)i), \"r\"(0x9e3779b97f4a7c15ul));\n"
    "        s += lo ^ hi;\n"
    "    }\n"
    "    return s;\n"
    "}\n"
    "static double tier1_addsd(double d, int n)\n"
    "{\n"
    "    for (int i = 0; i < n; i++)\n"
    "    {\n"
    "        __asm__(\"addsd %1, %0\" : \"+x\"(d) : \"x\"((double)(i & 7)));\n"
    "    }\n"
    "    return d;\n"
    "}\n"
    "int main(void)\n"
    "{\n"
    "    printf(\"cpuid %u\\n\", tier1_cpuid(7, 1, 2, 3, 4, 5, 6, 7));\n"
    "    printf(\"syscall %ld %ld\\n\", tier1_syscall1(3, -1), tier1_syscall6(100000, 1, 2, 3, 4, 5, 6));\n"
    "    printf(\"x87 %.6Lf %.6Lf\\n\", tier1_sqrtl(2.0L), tier1_fmodl(1000.75L, 7.25L));\n"
    "    printf(\"flags %d %d %d %d\\n\", tier1_flags(1, 2), tier1_flags(2, 2), tier1_flags(-5, 3), tier1_flags(7, -1));\n"
    "    printf(\"count %d swap %u\\n\", tier1_count(10), tier1_swap(0x01020304u, 5));\n"
    "    printf(\"saved %ld\\n\", tier1_callee_saved(9));\n"
    "    printf(\"vectors %.3f aligned %d\\n\", tier1_vectors(1.25, 4), tier1_aligned());\n"
    "    printf(\"stack %d packed %.2f\\n\", tier1_stack(), (double)tier1_packed(1.25f));\n"
    "    printf(\"mulq %lu addsd %.2f\\n\", tier1_mulq(1000), tier1_addsd(0.5, 100));\n"
    "    return 0;\n"
    "}\n";

// The asm bodies' fixture (#1839) compiled by --tier1 --verify into an executable by each path, with and without
// rematerialization, with no function refused: it must print what clang -O0's build prints. Its -c object's
// tier1_cpuid holds its cpuid among its own records, with no call, and pushes rbx in its prologue.
static void check_test_driver_tier1_asm(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    char unit[512];
    char reference[512];
    char object[512];
    snprintf(unit, sizeof(unit), "%s/tier1-asm.c", directory);
    snprintf(reference, sizeof(reference), "%s/tier1-asm-clang", directory);
    snprintf(object, sizeof(object), "%s/tier1-asm.o", directory);
    const bool written = parse_test_write(unit, check_test_tier1_asm_source, sizeof(check_test_tier1_asm_source) - 1);
    char* reference_arguments[] = { (char*)clang, "-O0", "-w", "-o", reference, unit, NULL };
    char* text = NULL;
    const bool referenced = written && check_test_driver_spawn(reference_arguments, &text) == 0;
    free(text);
    char expected[4096] = { 0 };
    const bool ran = referenced && check_test_driver_capture(reference, expected, sizeof(expected)) && expected[0] != 0;
    check_test_expect(test, ran, "clang's build of the asm bodies' fixture runs");
    for (uint32_t rematerialize = 0; ran && rematerialize < 2; rematerialize += 1)
    {
        for (uint32_t path = 0; path < 2; path += 1)
        {
            char program[512];
            snprintf(program, sizeof(program), "%s/tier1-asm-%u-%u", directory, rematerialize, path);
            CheckTestTier1Build build;
            const bool compiled = check_test_tier1_build(executable, unit, "", program, rematerialize != 0, path != 0, false, false, NULL, &build);
            check_test_expect(test, compiled, "the asm bodies' fixture compiles by --tier1, every function Tier 1's");
            // --verify certifies the code of every function with an asm body (#1839, tier1_cert_emitted_asm): only the
            // x87's long doubles' (tier1_sqrtl's, tier1_fmodl's and main's) and tier1_stack's, whose body moves a value
            // into rsp (#1846, IrAsmBody.stack), are left uncertified.
            char message[256];
            snprintf(message, sizeof(message), "the asm bodies' fixture's code is certified but the long doubles' functions' (%" PRIu64 " certified, %" PRIu64 " not)", build.code_certified,
                     build.code_uncertified);
            check_test_expect(test, compiled && build.code_certified == 14 && build.code_uncertified == 4, message);
            char actual[4096] = { 0 };
            check_test_expect(test, compiled && check_test_driver_capture(program, actual, sizeof(actual)) && strcmp(actual, expected) == 0,
                              "Tier 1's asm bodies print clang -O0's build's output");
            unlink(program);
        }
    }
    for (uint32_t path = 0; written && path < 2; path += 1)
    {
        CheckTestTier1Build build;
        const bool compiled = check_test_tier1_build(executable, unit, "", object, true, path != 0, true, false, NULL, &build);
        X86Decoded records[512];
        uint32_t offsets[512];
        uint32_t types[512];
        uint32_t relocations = 0;
        const uint32_t decoded = compiled ? check_test_tier1_function(object, "tier1_cpuid", records, 512, offsets, types, &relocations) : 0;
        uint32_t cpuids = 0;
        uint32_t calls = 0;
        bool pushed = false;
        for (uint32_t r = 0; r < decoded; r += 1)
        {
            const X86Instruction* record = &records[r].instruction;
            cpuids += record->form == X86_FORM_CPUID;
            calls += record->form == X86_FORM_CALL_REL32 || record->form == X86_FORM_CALL_RM64;
            pushed |= r < 4 && (record->form == X86_FORM_PUSH_R64 || record->form == X86_FORM_PUSH_RM64) && (record->reg == X86_REGISTER_RBX || record->rm == X86_REGISTER_RBX);
        }
        char message[256];
        snprintf(message, sizeof(message), "asm bodies (%s path, --tier1): tier1_cpuid's own records hold %u cpuids and %u calls, rbx %s by its prologue", path != 0 ? "scalar" : "vector", cpuids,
                 calls, pushed ? "pushed" : "not pushed");
        check_test_expect(test, compiled && decoded != 0 && cpuids == 2 && calls == 0 && pushed, message);
        // tier1_mulq's operands are its body call's arguments, moved into rax and their register (#2144), and its loop's
        // values keep out of rax, rcx and rdx: of its frame stores, only the outputs' addresses into the operand block
        // are left, none of an input's and none of a save's.
        const uint32_t mulq_decoded = compiled ? check_test_tier1_function(object, "tier1_mulq", records, 512, offsets, types, &relocations) : 0;
        uint32_t muls = 0;
        uint32_t frame_stores = 0;
        for (uint32_t r = 0; r < mulq_decoded; r += 1)
        {
            const X86Instruction* record = &records[r].instruction;
            const bool memory = (record->mode & BUSTER_X86_MODE_MEMORY) != 0 && (record->mode & BUSTER_X86_MODE_BASE) != 0;
            muls += record->form == X86_FORM_MUL_RM64;
            frame_stores += memory && record->form == X86_FORM_MOV_RM64_R64 && (record->rm == X86_REGISTER_RSP || record->rm == X86_REGISTER_RBP);
        }
        snprintf(message, sizeof(message), "asm bodies (%s path, --tier1): tier1_mulq holds %u mul and %u frame stores", path != 0 ? "scalar" : "vector", muls, frame_stores);
        check_test_expect(test, compiled && mulq_decoded != 0 && muls == 1 && frame_stores <= 2, message);
        unlink(object);
    }
    unlink(unit);
    unlink(reference);
}

// The asm bodies' opmask fixture (#1839): two masks live across a template that writes k1 to k7, its clobbers, in a
// function Tier 1 compiles. The template is an asm body, around which Tier 1's CFG consumer saves the masks live in the
// opmask registers it writes (IrAsmBody.masks, tier1_emit_cfg_asm); the masks then pick the lanes the sum adds.
static const char check_test_tier1_asm_masks_source[] =
    "#include <immintrin.h>\n"
    "#include <stdio.h>\n"
    "__attribute__((noinline)) int tier1_kept(const int* p, int x)\n"
    "{\n"
    "    __m512i v = _mm512_loadu_si512(p);\n"
    "    __mmask16 m = _mm512_cmpgt_epi32_mask(v, _mm512_set1_epi32(x));\n"
    "    __mmask16 n = _mm512_cmplt_epi32_mask(v, _mm512_set1_epi32(x + 6));\n"
    "    __asm__ volatile(\"xorl %%eax, %%eax\\n\\tkmovw %%eax, %%k1\\n\\tkmovw %%eax, %%k2\\n\\tkmovw %%eax, %%k3\\n\\tkmovw %%eax, %%k4\\n\\t\"\n"
    "                     \"kmovw %%eax, %%k5\\n\\tkmovw %%eax, %%k6\\n\\tkmovw %%eax, %%k7\"\n"
    "                     ::: \"eax\", \"k1\", \"k2\", \"k3\", \"k4\", \"k5\", \"k6\", \"k7\");\n"
    "    return _mm512_mask_reduce_add_epi32(m & n, v) + __builtin_popcount(m) * 100 + __builtin_popcount(n) * 10000;\n"
    "}\n"
    "int main(void)\n"
    "{\n"
    "    int a[16];\n"
    "    for (int i = 0; i < 16; i += 1)\n"
    "    {\n"
    "        a[i] = i * 3 - 7;\n"
    "    }\n"
    "    printf(\"kept %d %d\\n\", tier1_kept(a, 2), tier1_kept(a, -5));\n"
    "    return 0;\n"
    "}\n";

// The opmask fixture above by --tier1 --verify for x86-64-v4, by each path, with and without rematerialization: it
// compiles, tier1_kept Tier 1's, and where the host has AVX-512 F, BW, DQ and VL prints clang -O0's build's output.
static void check_test_driver_tier1_asm_masks(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    char unit[512];
    char reference[512];
    snprintf(unit, sizeof(unit), "%s/tier1-asm-masks.c", directory);
    snprintf(reference, sizeof(reference), "%s/tier1-asm-masks-clang", directory);
    const bool written = parse_test_write(unit, check_test_tier1_asm_masks_source, sizeof(check_test_tier1_asm_masks_source) - 1);
    char* reference_arguments[] = { (char*)clang, "-O0", "-w", "-march=x86-64-v4", "-o", reference, unit, NULL };
    char* text = NULL;
    const bool referenced = written && check_test_driver_spawn(reference_arguments, &text) == 0;
    free(text);
    check_test_expect(test, referenced, "clang builds the asm bodies' opmask fixture");
    const uint64_t needed = BUSTER_X86_FEATURE(AVX512F) | BUSTER_X86_FEATURE(AVX512BW) | BUSTER_X86_FEATURE(AVX512DQ) | BUSTER_X86_FEATURE(AVX512VL);
    const bool host = (x86_target_native().features & needed) == needed;
    char expected[256] = { 0 };
    const bool ran = referenced && host && check_test_driver_capture(reference, expected, sizeof(expected)) && expected[0] != 0;
    check_test_expect(test, !host || ran, "clang's build of the asm bodies' opmask fixture runs");
    for (uint32_t rematerialize = 0; referenced && rematerialize < 2; rematerialize += 1)
    {
        for (uint32_t path = 0; path < 2; path += 1)
        {
            char program[512];
            snprintf(program, sizeof(program), "%s/tier1-asm-masks-%u-%u", directory, rematerialize, path);
            CheckTestTier1Build build;
            const bool compiled = check_test_tier1_build(executable, unit, "tier1_kept", program, rematerialize != 0, path != 0, false, false, "-march=x86-64-v4", &build);
            check_test_expect(test, compiled, "the asm bodies' opmask fixture compiles by --tier1, tier1_kept Tier 1's");
            // --verify certifies every function's code (immintrin.h's among them), tier1_kept's asm body and the masks saved around it (#1839).
            check_test_expect(test, compiled && build.code_certified == 7 && build.code_uncertified == 0, "the asm bodies' opmask fixture's code is certified, its intrinsics' functions' with it");
            char actual[256] = { 0 };
            check_test_expect(test, !host || (compiled && check_test_driver_capture(program, actual, sizeof(actual)) && strcmp(actual, expected) == 0),
                              "Tier 1 keeps the masks live across a template that writes k1 to k7, as clang -O0's build does");
            unlink(program);
        }
    }
    // The template is an asm body: tier1_kept's own records write k7 and hold no call.
    char object[512];
    snprintf(object, sizeof(object), "%s/tier1-asm-masks.o", directory);
    for (uint32_t path = 0; referenced && path < 2; path += 1)
    {
        CheckTestTier1Build build;
        const bool compiled = check_test_tier1_build(executable, unit, "tier1_kept", object, true, path != 0, true, false, "-march=x86-64-v4", &build);
        X86Decoded records[1024];
        uint32_t offsets[1024];
        uint32_t types[1024];
        uint32_t relocations = 0;
        const uint32_t decoded = compiled ? check_test_tier1_function(object, "tier1_kept", records, 1024, offsets, types, &relocations) : 0;
        uint32_t written_k7 = 0;
        uint32_t calls = 0;
        for (uint32_t r = 0; r < decoded; r += 1)
        {
            const X86Instruction* record = &records[r].instruction;
            written_k7 += record->form == X86_FORM_KMOVW_K_R32 && record->reg == 7;
            calls += record->form == X86_FORM_CALL_REL32 || record->form == X86_FORM_CALL_RM64;
        }
        char message[256];
        snprintf(message, sizeof(message), "asm bodies' opmask fixture (%s path, --tier1): tier1_kept's own records write k7 %u times and hold %u calls", path != 0 ? "scalar" : "vector",
                 written_k7, calls);
        check_test_expect(test, compiled && decoded != 0 && written_k7 == 1 && calls == 0, message);
        unlink(object);
    }
    unlink(unit);
    unlink(reference);
}

// A fixture of two units sharing a header, `name`.h (#789, #141): its unit by --tier1 -c, by each path with and without
// rematerialization, every function Tier 1's, linked with its other unit compiled by clang -O2, prints what clang -O0's
// build of both prints; and both units by --tier1, every function of both Tier 1's, print it too. `what` names it in the
// checks' messages. `option` (as -march=x86-64-v4), where it isn't NULL, every build takes, and only where the host can
// (`runs`) are the programs run: elsewhere the builds are checked alone. With --verify, the code's certificate (#939,
// #1284) leaves exactly `uncertified` of their functions' code uncertified, for shapes it doesn't decode, and refuses none.
static void check_test_driver_tier1_units(CheckTest* test, const char* clang, const char* executable, const char* directory, const char* name, const char* what, const char* header_source,
                                          const char* unit_source, const char* other_source, const char* option, bool runs, uint32_t uncertified)
{
    char header[512];
    char unit[512];
    char other[512];
    char other_object[512];
    char reference[512];
    char both[512];
    char message[256];
    snprintf(header, sizeof(header), "%s/%s.h", directory, name);
    snprintf(unit, sizeof(unit), "%s/%s.c", directory, name);
    snprintf(other, sizeof(other), "%s/%s-other.c", directory, name);
    snprintf(other_object, sizeof(other_object), "%s/%s-other.o", directory, name);
    snprintf(reference, sizeof(reference), "%s/%s-clang", directory, name);
    snprintf(both, sizeof(both), "%s/%s-both", directory, name);
    const bool written = parse_test_write(header, header_source, strlen(header_source)) && parse_test_write(unit, unit_source, strlen(unit_source)) &&
                         parse_test_write(other, other_source, strlen(other_source));
    // The option, where there is one, after each command's first argument.
    const uint32_t optioned = option != NULL ? 1 : 0;
    char* reference_arguments[8] = { (char*)clang, "-O0", (char*)option };
    char* other_arguments[9] = { (char*)clang, "-O2", (char*)option };
    char* const reference_tail[] = { "-o", reference, unit, other, NULL };
    char* const other_tail[] = { "-c", "-o", other_object, other, NULL };
    memcpy(reference_arguments + 2 + optioned, reference_tail, sizeof(reference_tail));
    memcpy(other_arguments + 2 + optioned, other_tail, sizeof(other_tail));
    char* text = NULL;
    const bool referenced = written && check_test_driver_spawn(reference_arguments, &text) == 0;
    free(text);
    text = NULL;
    const bool other_built = written && check_test_driver_spawn(other_arguments, &text) == 0;
    free(text);
    char expected[4096] = { 0 };
    const bool ran = referenced && other_built && (!runs || (check_test_driver_capture(reference, expected, sizeof(expected)) && expected[0] != 0));
    snprintf(message, sizeof(message), "clang's builds of the %s fixture run", what);
    check_test_expect(test, ran, message);
    for (uint32_t rematerialize = 0; ran && rematerialize < 2; rematerialize += 1)
    {
        for (uint32_t path = 0; path < 2; path += 1)
        {
            char object[512];
            char program[512];
            snprintf(object, sizeof(object), "%s/%s-%u-%u.o", directory, name, rematerialize, path);
            snprintf(program, sizeof(program), "%s/%s-%u-%u", directory, name, rematerialize, path);
            CheckTestTier1Build build;
            const bool compiled = check_test_tier1_build(executable, unit, "", object, rematerialize != 0, path != 0, true, false, option, &build);
            snprintf(message, sizeof(message), "the %s unit compiles by --tier1, every function Tier 1's (rematerialize %u, path %u)", what, rematerialize, path);
            check_test_expect(test, compiled, message);
            char* link_arguments[] = { (char*)clang, "-o", program, object, other_object, NULL };
            char* link_text = NULL;
            const bool linked = compiled && check_test_driver_spawn(link_arguments, &link_text) == 0;
            free(link_text);
            char actual[4096] = { 0 };
            snprintf(message, sizeof(message), "the %s unit's Tier 1 code passes to and from clang -O2's as clang -O0's does (rematerialize %u, path %u)", what, rematerialize, path);
            check_test_expect(test, linked && (!runs || (check_test_driver_capture(program, actual, sizeof(actual)) && strcmp(actual, expected) == 0)), message);
            unlink(object);
            unlink(program);
        }
    }
    char* both_arguments[11] = { (char*)executable, "--tier1", "--tier1-report", "--tier1-refusals", "--verify", (char*)option };
    char* const both_tail[] = { "-o", both, unit, other, NULL };
    memcpy(both_arguments + 5 + optioned, both_tail, sizeof(both_tail));
    text = NULL;
    const bool both_built = ran && check_test_driver_spawn(both_arguments, &text) == 0 && text != NULL && strstr(text, "tier1-refused") == NULL;
    uint64_t certified = 0;
    uint64_t left = 0;
    const bool counted = both_built && check_test_tier1_field(text, "code_certified", &certified) && check_test_tier1_field(text, "code_uncertified", &left) && certified != 0 &&
                         left == uncertified;
    free(text);
    char actual[4096] = { 0 };
    snprintf(message, sizeof(message), "both units of the %s fixture by --tier1, every function Tier 1's, print clang's build's output", what);
    check_test_expect(test, both_built && (!runs || (check_test_driver_capture(both, actual, sizeof(actual)) && strcmp(actual, expected) == 0)), message);
    snprintf(message, sizeof(message), "both units of the %s fixture by --tier1 --verify have their code certified but %u functions'", what, uncertified);
    check_test_expect(test, !both_built || counted, message);
    unlink(header);
    unlink(unit);
    unlink(other);
    unlink(other_object);
    unlink(reference);
    unlink(both);
}

// The by-value aggregates' fixture (#789), the other unit's va_arg of an aggregate among its functions: that variadic
// definition's code alone is left uncertified (its register save area isn't decoded).
static void check_test_driver_tier1_records(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_tier1_units(test, clang, executable, directory, "tier1-records", "by-value aggregates'", check_test_tier1_records_header, check_test_tier1_records_source,
                                  check_test_tier1_records_other_source, NULL, true, 1);
}

// The wide values' fixture (#141): the code of its 11 functions of long doubles (the x87's) and of its variadic one is left
// uncertified.
static void check_test_driver_tier1_wide(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_tier1_units(test, clang, executable, directory, "tier1-wide", "wide values'", check_test_tier1_wide_header, check_test_tier1_wide_source, check_test_tier1_wide_other_source,
                                  NULL, true, 12);
}

// Stack-passed vectors' alignment (#1204's review). A call whose 32- or 64-byte vector argument goes on the stack (its
// eight vector registers already taken by doubles) needs rsp aligned to 32 or 64 bytes at the call, past the ABI's 16:
// Tier 1's frame realigns itself to it, in a fixed frame and in one with an alloca, whose allocations and calls' areas
// keep it, whatever rsp its own caller gives it. Each sink (clang's, naked) returns its first stack argument's address
// modulo the vector's size, and each Tier 1 function is called from rsp 64-aligned and 16, 32 and 48 bytes below that,
// with allocas of several sizes: the expected output is the ABI's, all zeros. clang's own build can't be the oracle:
// clang 23 rounds an alloca to 16 bytes in a realigned frame, misplacing the same call's argument (by 16 at -O0; at -O2
// its vmovaps there faults).
static const char check_test_tier1_stack_vectors_source[] =
    "typedef float V8 __attribute__((vector_size(32)));\n"
    "typedef float V16 __attribute__((vector_size(64)));\n"
    "unsigned long b_sink8(double, double, double, double, double, double, double, double, V8);\n"
    "unsigned long b_sink16(double, double, double, double, double, double, double, double, V16);\n"
    "unsigned long b_sink8_after(double, double, double, double, double, double, double, double, V8, char*);\n"
    "unsigned long b_sink16_after(double, double, double, double, double, double, double, double, V16, char*);\n"
    "unsigned long a_fixed8(V8* p, int n, double d) { return b_sink8(d, d + 1, d + 2, d + 3, d + 4, d + 5, d + 6, d + n, *p); }\n"
    "unsigned long a_fixed16(V16* p, int n, double d) { return b_sink16(d, d + 1, d + 2, d + 3, d + 4, d + 5, d + 6, d + n, *p); }\n"
    "unsigned long a_dynamic8(V8* p, int n, double d) { char* q = __builtin_alloca(n); q[0] = (char)n; return b_sink8_after(d, d, d, d, d, d, d, d, *p, q); }\n"
    "unsigned long a_dynamic16(V16* p, int n, double d) { char* q = __builtin_alloca(n); q[0] = (char)n; return b_sink16_after(d, d, d, d, d, d, d, d, *p, q); }\n";

static const char check_test_tier1_stack_vectors_other_source[] =
    "#include <stdio.h>\n"
    "typedef float V8 __attribute__((vector_size(32)));\n"
    "typedef float V16 __attribute__((vector_size(64)));\n"
    "typedef unsigned long (*F)(void*, int, double);\n"
    "unsigned long a_fixed8(V8* p, int n, double d);\n"
    "unsigned long a_fixed16(V16* p, int n, double d);\n"
    "unsigned long a_dynamic8(V8* p, int n, double d);\n"
    "unsigned long a_dynamic16(V16* p, int n, double d);\n"
    "__attribute__((naked, noinline)) unsigned long b_sink8(double a, double b, double c, double d, double e, double f, double g, double h, V8 x)\n"
    "{ __asm__(\"lea 8(%rsp), %rax\\n\\tand $31, %eax\\n\\tret\"); }\n"
    "__attribute__((naked, noinline)) unsigned long b_sink16(double a, double b, double c, double d, double e, double f, double g, double h, V16 x)\n"
    "{ __asm__(\"lea 8(%rsp), %rax\\n\\tand $63, %eax\\n\\tret\"); }\n"
    "__attribute__((naked, noinline)) unsigned long b_sink8_after(double a, double b, double c, double d, double e, double f, double g, double h, V8 x, char* q)\n"
    "{ __asm__(\"lea 8(%rsp), %rax\\n\\tand $31, %eax\\n\\tret\"); }\n"
    "__attribute__((naked, noinline)) unsigned long b_sink16_after(double a, double b, double c, double d, double e, double f, double g, double h, V16 x, char* q)\n"
    "{ __asm__(\"lea 8(%rsp), %rax\\n\\tand $63, %eax\\n\\tret\"); }\n"
    "// f(p, n, d) from rsp 64-aligned less `below` bytes (a multiple of 16, as the ABI keeps it): f in rdi, p in rsi, n in\n"
    "// edx, below in rcx (the fourth integer argument), d in xmm0.\n"
    "__attribute__((naked, noinline)) unsigned long b_call(F f, void* p, int n, double d, long below)\n"
    "{ __asm__(\"push %rbp\\n\\tmov %rsp, %rbp\\n\\tand $-64, %rsp\\n\\tsub %rcx, %rsp\\n\\tmov %rdi, %rax\\n\\tmov %rsi, %rdi\\n\\tmov %edx, %esi\\n\\t\"\n"
    "          \"call *%rax\\n\\tmov %rbp, %rsp\\n\\tpop %rbp\\n\\tret\"); }\n"
    "int main(void)\n"
    "{\n"
    "    V8 x = { 1, 2, 3, 4, 5, 6, 7, 8 };\n"
    "    V16 y = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };\n"
    "    unsigned long r[4] = { 0, 0, 0, 0 };\n"
    "    for (long below = 0; below < 64; below += 16)\n"
    "    {\n"
    "        for (int n = 1; n < 100; n += 13)\n"
    "        {\n"
    "            r[0] |= b_call((F)a_fixed8, &x, n, 0.5, below);\n"
    "            r[1] |= b_call((F)a_fixed16, &y, n, 0.5, below);\n"
    "            r[2] |= b_call((F)a_dynamic8, &x, n, 0.5, below);\n"
    "            r[3] |= b_call((F)a_dynamic16, &y, n, 0.5, below);\n"
    "        }\n"
    "    }\n"
    "    printf(\"%lu %lu %lu %lu\\n\", r[0], r[1], r[2], r[3]);\n"
    "    return 0;\n"
    "}\n";

// The stack-passed vectors' check (above): its unit by --tier1 -march=x86-64-v4, every function Tier 1's, by each path
// with and without rematerialization, linked with its other unit (clang -O2's), prints zeros where the host can run it.
static void check_test_driver_tier1_stack_vectors(CheckTest* test, const char* clang, const char* executable, const char* directory, bool host)
{
    char unit[512];
    char other[512];
    char other_object[512];
    snprintf(unit, sizeof(unit), "%s/tier1-stack-vectors.c", directory);
    snprintf(other, sizeof(other), "%s/tier1-stack-vectors-other.c", directory);
    snprintf(other_object, sizeof(other_object), "%s/tier1-stack-vectors-other.o", directory);
    const bool written = parse_test_write(unit, check_test_tier1_stack_vectors_source, strlen(check_test_tier1_stack_vectors_source)) &&
                         parse_test_write(other, check_test_tier1_stack_vectors_other_source, strlen(check_test_tier1_stack_vectors_other_source));
    char* other_arguments[] = { (char*)clang, "-O2", "-march=x86-64-v4", "-c", "-o", other_object, other, NULL };
    char* text = NULL;
    const bool other_built = written && check_test_driver_spawn(other_arguments, &text) == 0;
    free(text);
    check_test_expect(test, other_built, "clang builds the stack-passed vectors' other unit");
    for (uint32_t rematerialize = 0; other_built && rematerialize < 2; rematerialize += 1)
    {
        for (uint32_t path = 0; path < 2; path += 1)
        {
            char object[512];
            char program[512];
            char message[256];
            snprintf(object, sizeof(object), "%s/tier1-stack-vectors-%u-%u.o", directory, rematerialize, path);
            snprintf(program, sizeof(program), "%s/tier1-stack-vectors-%u-%u", directory, rematerialize, path);
            CheckTestTier1Build build;
            const bool compiled = check_test_tier1_build(executable, unit, "", object, rematerialize != 0, path != 0, true, false, "-march=x86-64-v4", &build);
            snprintf(message, sizeof(message), "the stack-passed vectors' unit compiles by --tier1, every function Tier 1's (rematerialize %u, path %u)", rematerialize, path);
            check_test_expect(test, compiled, message);
            char* link_arguments[] = { (char*)clang, "-o", program, object, other_object, NULL };
            text = NULL;
            const bool linked = compiled && check_test_driver_spawn(link_arguments, &text) == 0;
            free(text);
            char actual[256] = { 0 };
            snprintf(message, sizeof(message), "Tier 1's calls place 32- and 64-byte vectors on the stack at their alignment, in fixed and dynamic frames (rematerialize %u, path %u)",
                     rematerialize, path);
            const bool aligned = linked && (!host || (check_test_driver_capture(program, actual, sizeof(actual)) && strcmp(actual, "0 0 0 0\n") == 0));
            check_test_expect(test, aligned, message);
            if (linked && host && !aligned)
            {
                fprintf(stderr, "check: stack-passed vectors' residues: %s", actual);
            }
            unlink(object);
            unlink(program);
        }
    }
    unlink(unit);
    unlink(other);
    unlink(other_object);
}

// The 64-byte vector arguments' fixture (#141), for x86-64-v4: calls of two, eight and nine 64-byte vectors (the ninth on
// the stack) and of vectors of each width among integers and a double, more of them in zmm registers than the red zone's
// 14 staging slots hold, which wait in zmm16 and on between their copies and the moves (tier1_select_part_in_zmm), from
// this unit (Tier 1's) to the other's (clang -O2's), which hashes them.
static const char check_test_tier1_zmm_header[] =
    "#include <stdio.h>\n"
    "typedef int V16 __attribute__((vector_size(64)));\n"
    "typedef int V8 __attribute__((vector_size(32)));\n"
    "typedef int V4 __attribute__((vector_size(16)));\n"
    "unsigned long b_two(V16 a, V16 b);\n"
    "unsigned long b_eight(V16 a, V16 b, V16 c, V16 d, V16 e, V16 f, V16 g, V16 h);\n"
    "unsigned long b_mixed(long x, V16 a, V8 b, double d, V4 c, V16 e, V8 f, long y);\n"
    "unsigned long b_nine(V16 a, V16 b, V16 c, V16 d, V16 e, V16 f, V16 g, V16 h, V16 i);\n"
    "unsigned long a_two(V16* p, int k);\n"
    "unsigned long a_eight(V16* p, int k);\n"
    "unsigned long a_mixed(V16* p, V8* q, V4* r, int k);\n"
    "unsigned long a_nine(V16* p, int k);\n";

static const char check_test_tier1_zmm_source[] =
    "#include \"tier1-zmm.h\"\n"
    "unsigned long a_two(V16* p, int k) { V16 s = p[0] + p[2]; return b_two(s, p[1] * p[k]); }\n"
    "unsigned long a_eight(V16* p, int k) { V16 t = p[7] ^ p[k]; return b_eight(p[0], p[1] + p[k], p[2], p[3] - p[k], p[4], p[5], p[6], t); }\n"
    "unsigned long a_mixed(V16* p, V8* q, V4* r, int k) { V4 c = r[0] + r[0]; return b_mixed(k, p[0], q[0], k * 0.5, c, p[1] + p[k], q[1], -k); }\n"
    "unsigned long a_nine(V16* p, int k) { return b_nine(p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8] + p[k]); }\n"
    "int main(void)\n"
    "{\n"
    "    V16 p[9];\n"
    "    V8 q[2];\n"
    "    V4 r[1];\n"
    "    for (int i = 0; i < 9; i += 1) for (int j = 0; j < 16; j += 1) p[i][j] = i * 100 + j * 7 - 50;\n"
    "    for (int i = 0; i < 2; i += 1) for (int j = 0; j < 8; j += 1) q[i][j] = i * 30 + j * 3 + 1;\n"
    "    for (int j = 0; j < 4; j += 1) r[0][j] = j * 11 - 5;\n"
    "    for (int k = 0; k < 3; k += 1) printf(\"%lu %lu %lu %lu\\n\", a_two(p, k), a_eight(p, k), a_mixed(p, q, r, k), a_nine(p, k));\n"
    "    return 0;\n"
    "}\n";

static const char check_test_tier1_zmm_other_source[] =
    "#include \"tier1-zmm.h\"\n"
    "static unsigned long fold(unsigned long h, const int* v, int n) { for (int i = 0; i < n; i += 1) h = h * 31 + (unsigned)v[i]; return h; }\n"
    "unsigned long b_two(V16 a, V16 b) { unsigned long h = fold(1, (int*)&a, 16); return fold(h, (int*)&b, 16); }\n"
    "unsigned long b_eight(V16 a, V16 b, V16 c, V16 d, V16 e, V16 f, V16 g, V16 h) { V16 v[8] = { a, b, c, d, e, f, g, h }; return fold(3, (int*)v, 128); }\n"
    "unsigned long b_mixed(long x, V16 a, V8 b, double d, V4 c, V16 e, V8 f, long y) { unsigned long h = fold((unsigned long)x * 7 + (unsigned long)y, (int*)&a, 16); h = fold(h, (int*)&b, 8); h = fold(h, (int*)&c, 4); h = fold(h, (int*)&e, 16); h = fold(h, (int*)&f, 8); return h + (unsigned long)(d * 4); }\n"
    "unsigned long b_nine(V16 a, V16 b, V16 c, V16 d, V16 e, V16 f, V16 g, V16 h, V16 i) { V16 v[9] = { a, b, c, d, e, f, g, h, i }; return fold(5, (int*)v, 144); }\n";

#include "c_check_tier1_staging_driver_test.c"

// The vectors' fixture (#141), for x86-64-v4, run where the host has every feature of it: both compilers' code is
// x86-64-v4's (EVEX forms at each width, VL's among them, and whatever else clang selects for it; #1204's review). Then
// the stack-passed vectors' check and the 64-byte vector arguments' fixture, on the same hosts.
static void check_test_driver_tier1_vectors(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    X86Target v4 = { 0 };
    const bool parsed = x86_target_parse("x86-64-v4", 9, &v4);
    const bool host = parsed && (x86_target_native().features & v4.features) == v4.features;
    check_test_driver_tier1_units(test, clang, executable, directory, "tier1-vectors", "vectors'", check_test_tier1_vectors_header, check_test_tier1_vectors_source,
                                  check_test_tier1_vectors_other_source, "-march=x86-64-v4", host, 0);
    check_test_driver_tier1_stack_vectors(test, clang, executable, directory, host);
    check_test_driver_tier1_units(test, clang, executable, directory, "tier1-zmm", "64-byte vector arguments'", check_test_tier1_zmm_header, check_test_tier1_zmm_source,
                                  check_test_tier1_zmm_other_source, "-march=x86-64-v4", host, 0);
    check_test_driver_tier1_staging(test, clang, executable, directory, "-march=x86-64-v4", host);
}

// The x86 builtins' bit manipulations' fixture (#141): BMI2's pdep, pext and bzhi and BMI1's bextr of unsigned ints and
// longs, the IR's intrinsics, alone, with values live across them, a result into an operand's register and results fed
// into each other, on the other unit's random values, a bzhi's index and a bextr's start and length within the width
// and past it.
static const char check_test_tier1_bmi_header[] =
    "#include <stdio.h>\n"
    "unsigned a_pdep32(unsigned a, unsigned m);\n"
    "unsigned long long a_pdep64(unsigned long long a, unsigned long long m);\n"
    "unsigned a_pext32(unsigned a, unsigned m);\n"
    "unsigned long long a_pext64(unsigned long long a, unsigned long long m);\n"
    "unsigned a_bzhi32(unsigned a, unsigned n);\n"
    "unsigned long long a_bzhi64(unsigned long long a, unsigned long long n);\n"
    "unsigned a_bextr32(unsigned a, unsigned c);\n"
    "unsigned long long a_bextr64(unsigned long long a, unsigned long long c);\n"
    "unsigned long long a_chain(unsigned long long a, unsigned long long m, unsigned n);\n";

static const char check_test_tier1_bmi_source[] =
    "#include \"tier1-bmi.h\"\n"
    "unsigned a_pdep32(unsigned a, unsigned m) { return __builtin_ia32_pdep_si(a, m); }\n"
    "unsigned long long a_pdep64(unsigned long long a, unsigned long long m) { return __builtin_ia32_pdep_di(a, m); }\n"
    "unsigned a_pext32(unsigned a, unsigned m) { return __builtin_ia32_pext_si(a, m); }\n"
    "unsigned long long a_pext64(unsigned long long a, unsigned long long m) { return __builtin_ia32_pext_di(a, m); }\n"
    "unsigned a_bzhi32(unsigned a, unsigned n) { return __builtin_ia32_bzhi_si(a, n); }\n"
    "unsigned long long a_bzhi64(unsigned long long a, unsigned long long n) { return __builtin_ia32_bzhi_di(a, n); }\n"
    "unsigned a_bextr32(unsigned a, unsigned c) { return __builtin_ia32_bextr_u32(a, c); }\n"
    "unsigned long long a_bextr64(unsigned long long a, unsigned long long c) { return __builtin_ia32_bextr_u64(a, c); }\n"
    "unsigned long long a_chain(unsigned long long a, unsigned long long m, unsigned n)\n"
    "{\n"
    "    unsigned long long d = __builtin_ia32_pdep_di(a, m);\n"
    "    unsigned long long e = __builtin_ia32_pext_di(d ^ a, m | a);\n"
    "    unsigned long long z = __builtin_ia32_bzhi_di(e + d, n);\n"
    "    unsigned x = __builtin_ia32_pext_si((unsigned)z, (unsigned)m) + __builtin_ia32_pdep_si((unsigned)a, (unsigned)(m >> 7));\n"
    "    m = __builtin_ia32_bextr_u64(m, n | (n << 8));\n"
    "    return d * 3 + e * 5 + z * 7 + x + m + __builtin_ia32_bextr_u64(a ^ z, (n & 0x3f) | ((n & 0x1f) << 8));\n"
    "}\n";

static const char check_test_tier1_bmi_other_source[] =
    "#include \"tier1-bmi.h\"\n"
    "static unsigned long long state = 0x9e3779b97f4a7c15ull;\n"
    "static unsigned long long next(void) { state ^= state << 13; state ^= state >> 7; state ^= state << 17; return state; }\n"
    "int main(void)\n"
    "{\n"
    "    unsigned long long hash = 0;\n"
    "    for (int i = 0; i < 2048; i += 1)\n"
    "    {\n"
    "        unsigned long long a = next(), m = next(), c = next();\n"
    "        unsigned n = (unsigned)(c & 0xff);\n"
    "        if ((i & 3) != 0) n &= 0x3f;\n"
    "        unsigned long long control = (i & 1) != 0 ? (c & 0xffff) : (c & 0x3f3f);\n"
    "        hash = hash * 31 + a_pdep32((unsigned)a, (unsigned)m);\n"
    "        hash = hash * 31 + a_pdep64(a, m);\n"
    "        hash = hash * 31 + a_pext32((unsigned)a, (unsigned)m);\n"
    "        hash = hash * 31 + a_pext64(a, m);\n"
    "        hash = hash * 31 + a_bzhi32((unsigned)a, n);\n"
    "        hash = hash * 31 + a_bzhi64(a, n | (c & ~0xffull));\n"
    "        hash = hash * 31 + a_bextr32((unsigned)a, (unsigned)control);\n"
    "        hash = hash * 31 + a_bextr64(a, control);\n"
    "        hash = hash * 31 + a_chain(a, m, n);\n"
    "        if (i < 2) printf(\"%llx %x %llx %x %llx\\n\", a, a_pdep32((unsigned)a, (unsigned)m), a_pext64(a, m), a_bzhi32((unsigned)a, n), a_chain(a, m, n));\n"
    "    }\n"
    "    printf(\"%llx\\n\", hash);\n"
    "    return 0;\n"
    "}\n";

// Its builds, by x86-64-v3's BMI1 and BMI2, run where the host has them.
static void check_test_driver_tier1_bmi(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    X86Target v3 = { 0 };
    const bool parsed = x86_target_parse("x86-64-v3", 9, &v3);
    const bool host = parsed && (x86_target_native().features & v3.features) == v3.features;
    check_test_driver_tier1_units(test, clang, executable, directory, "tier1-bmi", "x86 builtins' bit manipulations'", check_test_tier1_bmi_header, check_test_tier1_bmi_source,
                                  check_test_tier1_bmi_other_source, "-march=x86-64-v3", host, 0);
}

// The classic bit builtins' fixture (#1374, check_lower_bits_row): every one of them alone, a chain of them whose values
// stay live across each other, and a loop that iterates a word's set bits by ctz, each count's IR intrinsic (bsf, bsr,
// tzcnt, lzcnt, popcnt, bswap) by the target's form, on the other unit's (clang -O2's) operands, never 0 for ctz and clz.
// The generic/C23 recipes (#1652) also take zero, negative fallback results, narrow and wide _BitInts and one-zero limbs,
// and _BitInts past 128 bits (#1447) limb by limb, a rotation of a width no power of two against a reference in clang's
// build, whose code generator can't compile one.
static const char check_test_tier1_bits_header[] =
    "unsigned b_bswap16(unsigned short x);\n"
    "unsigned b_bswap32(unsigned x);\n"
    "unsigned long b_bswap64(unsigned long x);\n"
    "int b_clz(unsigned x);\n"
    "int b_clzl(unsigned long x);\n"
    "int b_clzll(unsigned long long x);\n"
    "int b_ctz(unsigned x);\n"
    "int b_ctzl(unsigned long x);\n"
    "int b_ctzll(unsigned long long x);\n"
    "int b_popcount(unsigned x);\n"
    "int b_popcountl(unsigned long x);\n"
    "int b_parity(unsigned x);\n"
    "int b_parityll(unsigned long long x);\n"
    "int b_ffs(int x);\n"
    "int b_ffsl(long x);\n"
    "int b_ffsll(long long x);\n"
    "unsigned long b_chain(unsigned long a, unsigned b);\n"
    "unsigned long b_bits(unsigned long a);\n"
    "unsigned long b_generic(unsigned long a, int f);\n"
    "unsigned long b_stdc(unsigned long a);\n"
    "unsigned long b_generic_wide(unsigned long low, unsigned long high, int f);\n"
    "unsigned long b_generic_limbs(unsigned long low, unsigned long high, int f, int k);\n";

static const char check_test_tier1_bits_source[] =
    "#include \"tier1-bits.h\"\n"
    "unsigned b_bswap16(unsigned short x) { return __builtin_bswap16(x); }\n"
    "unsigned b_bswap32(unsigned x) { return __builtin_bswap32(x); }\n"
    "unsigned long b_bswap64(unsigned long x) { return __builtin_bswap64(x); }\n"
    "int b_clz(unsigned x) { return __builtin_clz(x); }\n"
    "int b_clzl(unsigned long x) { return __builtin_clzl(x); }\n"
    "int b_clzll(unsigned long long x) { return __builtin_clzll(x); }\n"
    "int b_ctz(unsigned x) { return __builtin_ctz(x); }\n"
    "int b_ctzl(unsigned long x) { return __builtin_ctzl(x); }\n"
    "int b_ctzll(unsigned long long x) { return __builtin_ctzll(x); }\n"
    "int b_popcount(unsigned x) { return __builtin_popcount(x); }\n"
    "int b_popcountl(unsigned long x) { return __builtin_popcountl(x); }\n"
    "int b_parity(unsigned x) { return __builtin_parity(x); }\n"
    "int b_parityll(unsigned long long x) { return __builtin_parityll(x); }\n"
    "int b_ffs(int x) { return __builtin_ffs(x); }\n"
    "int b_ffsl(long x) { return __builtin_ffsl(x); }\n"
    "int b_ffsll(long long x) { return __builtin_ffsll(x); }\n"
    "unsigned long b_chain(unsigned long a, unsigned b)\n"
    "{\n"
    "    unsigned long s = __builtin_bswap64(a);\n"
    "    int t = __builtin_ctzl(a | 1) + __builtin_clz(b | 1);\n"
    "    unsigned u = __builtin_bswap32(b) ^ (unsigned)__builtin_popcountl(s);\n"
    "    int v = __builtin_ffsl((long)(a & s)) + __builtin_parity(u) + __builtin_clzl(s | 2);\n"
    "    return s * 3 + (unsigned long)t * 5 + u * 7 + (unsigned long)v + __builtin_bswap16((unsigned short)(t + v));\n"
    "}\n"
    "unsigned long b_bits(unsigned long a)\n"
    "{\n"
    "    unsigned long sum = 0;\n"
    "    for (unsigned long m = a; m != 0; m &= m - 1)\n"
    "    {\n"
    "        sum = sum * 67 + (unsigned long)__builtin_ctzl(m) + (unsigned long)__builtin_popcountl(m) * 131;\n"
    "    }\n"
    "    return sum;\n"
    "}\n"
    "unsigned long b_generic(unsigned long a, int f)\n"
    "{\n"
    "    unsigned char c = a; unsigned short s = a; unsigned u = a; _Bool b = a & 1;\n"
    "    unsigned _BitInt(37) w = a;\n"
    "    unsigned long h = __builtin_popcountg(c) + __builtin_popcountg(s) + __builtin_popcountg(u) + __builtin_popcountg(a);\n"
    "    h = h * 31 + (unsigned)__builtin_clzg(c, f) + (unsigned)__builtin_ctzg(s, f);\n"
    "    h = h * 31 + (unsigned)__builtin_clzg(u, f) + (unsigned)__builtin_ctzg(a, f);\n"
    "    h = h * 31 + __builtin_popcountg(w) + (unsigned)__builtin_clzg(w, f) + (unsigned)__builtin_ctzg(w, f);\n"
    "    h = h * 31 + __builtin_popcountg(b) + (unsigned)__builtin_clzg(b, f) + (unsigned)__builtin_ctzg(b, f);\n"
    "    h = h * 31 + __builtin_clrsb((int)u) + __builtin_clrsbl((long)a) + __builtin_clrsbll((long long)a);\n"
    "    if (s) h = h * 31 + __builtin_clzs(s) + __builtin_ctzs(s);\n"
    "    h = h * 31 + __builtin_bswapg(s) + __builtin_bswapg(u) + __builtin_bswapg(a);\n"
    "    return h + (long)__builtin_bswapg((_BitInt(48))a);\n"
    "}\n"
    "#define STDC(x) h=h*31+__builtin_stdc_leading_zeros(x)+__builtin_stdc_leading_ones(x);"
    "h=h*31+__builtin_stdc_trailing_zeros(x)+__builtin_stdc_trailing_ones(x);"
    "h=h*31+__builtin_stdc_first_leading_zero(x)+__builtin_stdc_first_leading_one(x);"
    "h=h*31+__builtin_stdc_first_trailing_zero(x)+__builtin_stdc_first_trailing_one(x);"
    "h=h*31+__builtin_stdc_count_ones(x)+__builtin_stdc_count_zeros(x)+__builtin_stdc_has_single_bit(x);"
    "h=h*31+__builtin_stdc_bit_width(x)+(unsigned long)__builtin_stdc_bit_floor(x);"
    "h=h*31+(unsigned long)__builtin_stdc_bit_ceil(x)\n"
    "unsigned long b_stdc(unsigned long a)\n"
    "{\n"
    "    unsigned char c=a; unsigned short s=a; unsigned u=a; unsigned _BitInt(5) x=a; unsigned _BitInt(31) y=a; unsigned _BitInt(33) z=a;\n"
    "    unsigned long h=0; STDC(c); STDC(s); STDC(u); STDC(a); STDC(x); STDC(y); STDC(z);\n"
    "    return h;\n"
    "}\n"
    "unsigned long b_generic_wide(unsigned long low, unsigned long high, int f)\n"
    "{\n"
    "    unsigned __int128 u=((unsigned __int128)high<<64)|low;\n"
    "    unsigned _BitInt(65) a=u; unsigned _BitInt(96) b=u; unsigned _BitInt(127) c=u;\n"
    "    unsigned long h=__builtin_popcountg(u)+__builtin_popcountg(a)+__builtin_popcountg(b)+__builtin_popcountg(c);\n"
    "    h=h*31+(unsigned)__builtin_clzg(u,f)+(unsigned)__builtin_ctzg(u,f);\n"
    "    h=h*31+(unsigned)__builtin_clzg(a,f)+(unsigned)__builtin_ctzg(a,f);\n"
    "    h=h*31+(unsigned)__builtin_clzg(b,f)+(unsigned)__builtin_ctzg(b,f);\n"
    "    h=h*31+(unsigned)__builtin_clzg(c,f)+(unsigned)__builtin_ctzg(c,f);\n"
    "    STDC(a); STDC(b); STDC(c); STDC(u);\n"
    "    unsigned __int128 swap=__builtin_bswapg(u); h=h*31+(unsigned long)swap+(unsigned long)(swap>>64);\n"
    "    swap=(unsigned __int128)__builtin_bswapg((unsigned _BitInt(80))u); h=h*31+(unsigned long)swap+(unsigned long)(swap>>64);\n"
    "    swap=__builtin_stdc_bit_floor(u); h=h*31+(unsigned long)swap+(unsigned long)(swap>>64);\n"
    "    swap=__builtin_stdc_bit_ceil(u); h=h*31+(unsigned long)swap+(unsigned long)(swap>>64);\n"
    "    return h;\n"
    "}\n"
    "#define LIMBS(p,n) for(unsigned j=0;j<(n);j++){unsigned long w;__builtin_memcpy(&w,(const char*)(p)+8*j,8);h=h*31+w;}\n"
    "#ifdef __BUSTER__\n"
    "#define ROTL(x,c,w) __builtin_stdc_rotate_left(x,c)\n"
    "#else\n"
    "#define ROTL(x,c,w) ({__typeof__(x) x_=(x);unsigned k_=(unsigned)((((c)%(w))+(w))%(w));k_?(x_<<k_)|(x_>>((w)-k_)):x_;})\n"
    "#endif\n"
    "unsigned long b_generic_limbs(unsigned long low, unsigned long high, int f, int k)\n"
    "{\n"
    "    unsigned _BitInt(256) w=((unsigned _BitInt(256))high<<((unsigned)k%190))^((unsigned _BitInt(256))low<<64)^high;\n"
    "    unsigned _BitInt(200) a=(unsigned _BitInt(200))w; unsigned _BitInt(129) b=(unsigned _BitInt(129))(w>>(unsigned)k%128); _BitInt(208) c=(_BitInt(208))~w;\n"
    "    unsigned long h=__builtin_popcountg(w)+__builtin_popcountg(a)+__builtin_popcountg(b);\n"
    "    h=h*31+(unsigned)__builtin_clzg(a,f)+(unsigned)__builtin_ctzg(a,f)+(unsigned)__builtin_clzg(w,f)+(unsigned)__builtin_ctzg(b,f);\n"
    "    STDC(a); STDC(b); STDC(w);\n"
    "    unsigned _BitInt(200) r=__builtin_bitreverseg(a); LIMBS(&r,4);\n"
    "    _BitInt(208) q=__builtin_bswapg(c); LIMBS(&q,4); q=__builtin_bitreverseg(c); LIMBS(&q,4);\n"
    "    unsigned _BitInt(256) t=__builtin_stdc_rotate_left(w,k); LIMBS(&t,4); t=__builtin_stdc_rotate_right(w,c); LIMBS(&t,4);\n"
    "    r=ROTL(a,k,200); LIMBS(&r,4); unsigned _BitInt(129) s=ROTL(b,(long)f,129); LIMBS(&s,3);\n"
    "    h=h*31+__builtin_stdc_rotate_left((unsigned)low,c)+__builtin_stdc_rotate_right((unsigned short)high,a);\n"
    "    return h;\n"
    "}\n";

static const char check_test_tier1_bits_other_source[] =
    "#include <stdio.h>\n"
    "#include \"tier1-bits.h\"\n"
    "static unsigned long long state = 0x9e3779b97f4a7c15ull;\n"
    "static unsigned long long next(void) { state ^= state << 13; state ^= state >> 7; state ^= state << 17; return state; }\n"
    "int main(void)\n"
    "{\n"
    "    unsigned long long hash = 0;\n"
    "    for (int i = 0; i < 4096; i += 1)\n"
    "    {\n"
    "        unsigned long long a = next() >> (i % 61);\n"
    "        unsigned long long n = a | (1ull << (i % 64));\n"
    "        unsigned m = (unsigned)n | (1u << (i % 32));\n"
    "        hash = hash * 31 + b_bswap16((unsigned short)a) + b_bswap32((unsigned)a) + b_bswap64(a);\n"
    "        hash = hash * 31 + (unsigned)(b_clz(m) + b_clzl(n) + b_clzll(n) + b_ctz(m) + b_ctzl(n) + b_ctzll(n));\n"
    "        hash = hash * 31 + (unsigned)(b_popcount((unsigned)a) + b_popcountl(a) + b_parity((unsigned)a) + b_parityll(a));\n"
    "        hash = hash * 31 + (unsigned)(b_ffs((int)a) + b_ffsl((long)a) + b_ffsll((long long)a) + b_ffs(0) + b_ffsl((long)(a << 40)));\n"
    "        hash = hash * 31 + b_chain(a, (unsigned)(a >> 17)) + b_bits(a);\n"
    "        int f = (i & 1) ? (-2147483647 - 1) : 2147483647;\n"
    "        hash = hash * 31 + b_generic(a, f) + b_generic(0, f) + b_stdc(a) + b_stdc(0);\n"
    "        hash = hash * 31 + b_generic_wide(a, 0, f) + b_generic_wide(0, a, f) + b_generic_wide(a, ~a, f);\n"
    "        if (i == 0) hash = hash * 31 + b_generic_wide(0, 0, f);\n"
    "        hash = hash * 31 + b_generic_limbs(a, ~a >> (i % 7), f, i - 2048) + b_generic_limbs(0, 0, f, i) + b_generic_limbs(~0ull, ~0ull, f, -i);\n"
    "        if (i < 2) printf(\"%llx %x %d %d %lx %lx\\n\", a, b_bswap32((unsigned)a), b_ctzl(n), b_clzl(n), b_chain(a, (unsigned)a), b_bits(a));\n"
    "    }\n"
    "    printf(\"%llx\\n\", hash);\n"
    "    return 0;\n"
    "}\n";

// Its builds, by x86-64's baseline (bsf, bsr and the popcount recipe), x86-64-v2's popcnt and x86-64-v3's tzcnt and lzcnt,
// each run where the host has the target's features.
static void check_test_driver_tier1_bits(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char* const marches[] = { "-march=x86-64", "-march=x86-64-v2", "-march=x86-64-v3" };
    for (uint32_t t = 0; t < BUSTER_ARRAY_LENGTH(marches); t += 1)
    {
        const char* level = marches[t] + strlen("-march=");
        X86Target target = { 0 };
        const bool parsed = x86_target_parse(level, (uint32_t)strlen(level), &target);
        const bool host = parsed && (x86_target_native().features & target.features) == target.features;
        check_test_expect(test, parsed, "the bit builtins' fixture's target");
        check_test_driver_tier1_units(test, clang, executable, directory, "tier1-bits", "classic and generic bit builtins'", check_test_tier1_bits_header, check_test_tier1_bits_source,
                                      check_test_tier1_bits_other_source, marches[t], host, 0);
    }
}

// The overflow builtins' fixture (#1385): signed longs' sums, differences and products (check_lower_overflow_row's rows,
// the IR's saddo, ssubo and smulo, Tier 1's operation and seto), ints' 64-bit operations, unsigned longs' carries and
// 128-bit products and mixed signedness (c_builtin_lower.c's recipes), each alone; a chain whose values and flags stay
// live across each other; and the issue's loop of a running sum and products, on the other unit's (clang -O2's) operands,
// many of them at the types' boundaries; and _BitInts of 65 to 127 bits' sums, differences and products (#1447), passed by
// pointer, whose destinations' 16 bytes, the padding extended from the width as clang stores it, are hashed; and a
// _BitInt(200)'s difference and an unsigned one's product into an unsigned _BitInt(130), past 128 bits, so hashed.
static const char check_test_tier1_overflow_header[] =
    "int o_add(long a, long b, long* r);\n"
    "int o_sub(long a, long b, long* r);\n"
    "int o_mul(long a, long b, long* r);\n"
    "int o_smull(long a, long b, long* r);\n"
    "int o_addi(int a, int b, int* r);\n"
    "int o_muli(int a, int b, int* r);\n"
    "int o_addu(unsigned long a, unsigned long b, unsigned long* r);\n"
    "int o_subu(unsigned long a, unsigned long b, unsigned long* r);\n"
    "int o_mulu(unsigned long a, unsigned long b, unsigned long* r);\n"
    "int o_mixed(long a, unsigned long b, int* r);\n"
    "long o_chain(long a, long b);\n"
    "long o_sum(const long* p, unsigned n);\n"
    "int o_add100(const _BitInt(100)* a, const _BitInt(100)* b, _BitInt(100)* r);\n"
    "int o_mul100(const _BitInt(100)* a, long b, _BitInt(100)* r);\n"
    "int o_mul127u(const unsigned _BitInt(127)* a, const unsigned _BitInt(127)* b, unsigned _BitInt(127)* r);\n"
    "int o_sub65(const unsigned _BitInt(127)* a, const _BitInt(100)* b, _BitInt(65)* r);\n"
    "int o_add_long(const _BitInt(100)* a, long b, long* r);\n"
    "int o_sub200(const _BitInt(200)* a, const _BitInt(200)* b, _BitInt(200)* r);\n"
    "int o_mul200u(const unsigned _BitInt(200)* a, const unsigned _BitInt(200)* b, unsigned _BitInt(130)* r);\n";

static const char check_test_tier1_overflow_source[] =
    "#include \"tier1-overflow.h\"\n"
    "int o_add(long a, long b, long* r) { return __builtin_add_overflow(a, b, r); }\n"
    "int o_sub(long a, long b, long* r) { return __builtin_sub_overflow(a, b, r); }\n"
    "int o_mul(long a, long b, long* r) { return __builtin_mul_overflow(a, b, r); }\n"
    "int o_smull(long a, long b, long* r) { return __builtin_smull_overflow(a, b, r); }\n"
    "int o_addi(int a, int b, int* r) { return __builtin_add_overflow(a, b, r); }\n"
    "int o_muli(int a, int b, int* r) { return __builtin_mul_overflow(a, b, r); }\n"
    "int o_addu(unsigned long a, unsigned long b, unsigned long* r) { return __builtin_add_overflow(a, b, r); }\n"
    "int o_subu(unsigned long a, unsigned long b, unsigned long* r) { return __builtin_sub_overflow(a, b, r); }\n"
    "int o_mulu(unsigned long a, unsigned long b, unsigned long* r) { return __builtin_mul_overflow(a, b, r); }\n"
    "int o_mixed(long a, unsigned long b, int* r)\n"
    "{\n"
    "    int s = 0;\n"
    "    int f = __builtin_add_overflow(a, b, &s);\n"
    "    return __builtin_mul_overflow(a, b, r) * 2 + f + (s & 1) * 4;\n"
    "}\n"
    "long o_chain(long a, long b)\n"
    "{\n"
    "    long x = 0, y = 0, z = 0;\n"
    "    int f = __builtin_mul_overflow(a, b, &x);\n"
    "    int g = __builtin_sub_overflow(x, a, &y);\n"
    "    g += __builtin_add_overflow(y, b, &z) * 2;\n"
    "    int h = __builtin_smull_overflow(z, 3, &x);\n"
    "    return (long)((unsigned long)x * 7 + (unsigned long)y * 5 + (unsigned long)z * 3 + (unsigned long)(f * 1000 + g * 100 + h * 10) + (unsigned long)a);\n"
    "}\n"
    "long o_sum(const long* p, unsigned n)\n"
    "{\n"
    "    long s = 0, r = 0;\n"
    "    int ov = 0;\n"
    "    for (unsigned i = 0; i < n; i += 1)\n"
    "    {\n"
    "        ov += __builtin_add_overflow(s, p[i], &s);\n"
    "        ov += __builtin_mul_overflow(p[i], p[n - 1 - i], &r);\n"
    "        s ^= r;\n"
    "    }\n"
    "    return (long)((unsigned long)s + (unsigned long)ov);\n"
    "}\n"
    "int o_add100(const _BitInt(100)* a, const _BitInt(100)* b, _BitInt(100)* r) { return __builtin_add_overflow(*a, *b, r); }\n"
    "int o_mul100(const _BitInt(100)* a, long b, _BitInt(100)* r) { return __builtin_mul_overflow(*a, b, r); }\n"
    "int o_mul127u(const unsigned _BitInt(127)* a, const unsigned _BitInt(127)* b, unsigned _BitInt(127)* r) { return __builtin_mul_overflow(*a, *b, r); }\n"
    "int o_sub65(const unsigned _BitInt(127)* a, const _BitInt(100)* b, _BitInt(65)* r) { return __builtin_sub_overflow(*a, *b, r); }\n"
    "int o_add_long(const _BitInt(100)* a, long b, long* r) { return __builtin_add_overflow(*a, b, r); }\n"
    "int o_sub200(const _BitInt(200)* a, const _BitInt(200)* b, _BitInt(200)* r) { return __builtin_sub_overflow(*a, *b, r); }\n"
    "int o_mul200u(const unsigned _BitInt(200)* a, const unsigned _BitInt(200)* b, unsigned _BitInt(130)* r) { return __builtin_mul_overflow(*a, *b, r); }\n";

static const char check_test_tier1_overflow_other_source[] =
    "#include <stdio.h>\n"
    "#include \"tier1-overflow.h\"\n"
    "static unsigned long long state = 0x9e3779b97f4a7c15ull;\n"
    "static unsigned long long next(void) { state ^= state << 13; state ^= state >> 7; state ^= state << 17; return state; }\n"
    "static void raw(unsigned long long* hash, const void* p)\n"
    "{\n"
    "    unsigned long long w[2];\n"
    "    __builtin_memcpy(w, p, 16);\n"
    "    *hash = (*hash * 31 + w[0]) * 31 + w[1];\n"
    "}\n"
    "static long value(int i)\n"
    "{\n"
    "    static const long edges[] = { 0, 1, -1, 2, 0x7fffffffffffffffL, -0x7fffffffffffffffL - 1, 3037000499L, 3037000500L, -3037000500L, 0x80000000L, -0x80000000L };\n"
    "    unsigned long long r = next();\n"
    "    return (i & 3) == 0 ? (long)((unsigned long)edges[r % 11] + (r >> 62) - 1) : (long)r >> (r % 64);\n"
    "}\n"
    "int main(void)\n"
    "{\n"
    "    unsigned long long hash = 0;\n"
    "    long p[64];\n"
    "    for (int i = 0; i < 4096; i += 1)\n"
    "    {\n"
    "        long a = value(i), b = value(i + 1), r = 0;\n"
    "        unsigned long u = 0;\n"
    "        int k = 0, f = 0;\n"
    "        f = o_add(a, b, &r);\n"
    "        hash = hash * 31 + (unsigned)f + (unsigned long long)r;\n"
    "        f = o_sub(a, b, &r);\n"
    "        hash = hash * 31 + (unsigned)f + (unsigned long long)r;\n"
    "        f = o_mul(a, b, &r);\n"
    "        hash = hash * 31 + (unsigned)f + (unsigned long long)r;\n"
    "        f = o_smull(a, b, &r);\n"
    "        hash = hash * 31 + (unsigned)f + (unsigned long long)r;\n"
    "        f = o_addi((int)a, (int)b, &k);\n"
    "        hash = hash * 31 + (unsigned)f + (unsigned)k;\n"
    "        f = o_muli((int)a, (int)b, &k);\n"
    "        hash = hash * 31 + (unsigned)f + (unsigned)k;\n"
    "        f = o_addu((unsigned long)a, (unsigned long)b, &u);\n"
    "        hash = hash * 31 + (unsigned)f + u;\n"
    "        f = o_subu((unsigned long)a, (unsigned long)b, &u);\n"
    "        hash = hash * 31 + (unsigned)f + u;\n"
    "        f = o_mulu((unsigned long)a, (unsigned long)b, &u);\n"
    "        hash = hash * 31 + (unsigned)f + u;\n"
    "        f = o_mixed(a, (unsigned long)b, &k);\n"
    "        hash = hash * 31 + (unsigned)f + (unsigned)k;\n"
    "        hash = hash * 31 + (unsigned long long)o_chain(a, b);\n"
    "        {\n"
    "            unsigned _BitInt(127) ua = ((unsigned _BitInt(127))(unsigned long)a << (i % 64)) ^ (unsigned long)b;\n"
    "            unsigned _BitInt(127) ub = ((unsigned _BitInt(127))(unsigned long)b << 63) | (unsigned long)a, ur = 0;\n"
    "            _BitInt(100) xa = (_BitInt(100))(ua >> (i % 40)), xb = (_BitInt(100))(ub >> (i % 30)), xr = 0;\n"
    "            _BitInt(65) sr = 0;\n"
    "            f = o_add100(&xa, &xb, &xr);\n"
    "            hash = hash * 31 + (unsigned)f;\n"
    "            raw(&hash, &xr);\n"
    "            f = o_mul100(&xa, b >> (i % 64), &xr);\n"
    "            hash = hash * 31 + (unsigned)f;\n"
    "            raw(&hash, &xr);\n"
    "            f = o_mul127u(&ua, &ub, &ur);\n"
    "            hash = hash * 31 + (unsigned)f;\n"
    "            raw(&hash, &ur);\n"
    "            f = o_sub65(&ua, &xb, &sr);\n"
    "            hash = hash * 31 + (unsigned)f;\n"
    "            raw(&hash, &sr);\n"
    "            f = o_add_long(&xa, a, &r);\n"
    "            hash = hash * 31 + (unsigned)f + (unsigned long long)r;\n"
    "            {\n"
    "                _BitInt(200) wa = (_BitInt(200))((unsigned _BitInt(200))ua << (i % 80)), wb = (_BitInt(200))(-(unsigned _BitInt(200))ub << (i % 70)), wr = 0;\n"
    "                unsigned _BitInt(200) va = (unsigned _BitInt(200))ua >> (i % 64), vb = (unsigned _BitInt(200))xb;\n"
    "                unsigned _BitInt(130) vr = 0;\n"
    "                unsigned long long w[4];\n"
    "                f = o_sub200(&wa, &wb, &wr);\n"
    "                __builtin_memcpy(w, &wr, 32);\n"
    "                hash = (((hash * 31 + (unsigned)f) * 31 + w[0]) * 31 + w[1] + w[2]) * 31 + w[3];\n"
    "                f = o_mul200u(&va, &vb, &vr);\n"
    "                __builtin_memcpy(w, &vr, 24);\n"
    "                hash = ((hash * 31 + (unsigned)f) * 31 + w[0]) * 31 + w[1] + w[2];\n"
    "            }\n"
    "        }\n"
    "        p[i % 64] = a;\n"
    "        if (i % 64 == 63) hash = hash * 31 + (unsigned long long)o_sum(p, 64);\n"
    "        if (i < 2)\n"
    "        {\n"
    "            int added = o_add(a, b, &r);\n"
    "            int multiplied = o_mul(a, b, &r);\n"
    "            printf(\"%lx %lx %d %d %lx %lx\\n\", a, b, added, multiplied, r, o_chain(a, b));\n"
    "        }\n"
    "    }\n"
    "    printf(\"%llx\\n\", hash);\n"
    "    return 0;\n"
    "}\n";

// Its builds at x86-64's baseline, whose forms are every target's, run on every host.
static void check_test_driver_tier1_overflow(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_tier1_units(test, clang, executable, directory, "tier1-overflow", "overflow builtins'", check_test_tier1_overflow_header, check_test_tier1_overflow_source,
                                  check_test_tier1_overflow_other_source, "-march=x86-64", true, 0);
}

// The floating extremes' fixture (#967): fmax, fmin, fmaximum_num and fminimum_num of float and double, as __builtin_X's
// and plain calls of <math.h>'s declarations, which clang -O0's code computes inline (llvm.maxnum and its kin: a row, or
// two rows of x86-64's maximumnum and minimumnum), alone, chained, and in a loop whose values stay live across them; the
// other unit (clang -O2's) passes zeros of both signs, infinities, quiet and signaling NaNs of both signs and subnormals,
// and hashes every result's bits.
static const char check_test_tier1_extremes_header[] =
    "double e_max(double a, double b);\n"
    "double e_min(double a, double b);\n"
    "double e_maximum(double a, double b);\n"
    "double e_minimum(double a, double b);\n"
    "float e_maxf(float a, float b);\n"
    "float e_minf(float a, float b);\n"
    "float e_maximumf(float a, float b);\n"
    "float e_minimumf(float a, float b);\n"
    "double e_chain(double a, double b, float c, float d);\n"
    "double e_fold(const double* p, unsigned n);\n";

static const char check_test_tier1_extremes_source[] =
    "#define _GNU_SOURCE\n"
    "#include <math.h>\n"
    "#include \"tier1-extremes.h\"\n"
    "double e_max(double a, double b) { return fmax(a, b); }\n"
    "double e_min(double a, double b) { return __builtin_fmin(a, b); }\n"
    "double e_maximum(double a, double b) { return fmaximum_num(a, b); }\n"
    "double e_minimum(double a, double b) { return __builtin_fminimum_num(a, b); }\n"
    "float e_maxf(float a, float b) { return __builtin_fmaxf(a, b); }\n"
    "float e_minf(float a, float b) { return fminf(a, b); }\n"
    "float e_maximumf(float a, float b) { return __builtin_fmaximum_numf(a, b); }\n"
    "float e_minimumf(float a, float b) { return fminimum_numf(a, b); }\n"
    "double e_chain(double a, double b, float c, float d)\n"
    "{\n"
    "    double x = fmax(a, b), y = fminimum_num(b, a);\n"
    "    float z = fmaximum_numf(c, d), w = __builtin_fminf(d, c);\n"
    "    return fmaximum_num(fmin(fmaximum_num(x, (double)z), __builtin_fmax(y, (double)w)), fminimum_num((double)z, x));\n"
    "}\n"
    "double e_fold(const double* p, unsigned n)\n"
    "{\n"
    "    double high = p[0], low = p[0], high_number = p[0], low_number = p[0];\n"
    "    for (unsigned i = 1; i < n; i += 1)\n"
    "    {\n"
    "        high = fmax(high, p[i]);\n"
    "        low = __builtin_fmin(p[i], low);\n"
    "        high_number = __builtin_fmaximum_num(high_number, p[i]);\n"
    "        low_number = fminimum_num(p[i], low_number);\n"
    "    }\n"
    "    return high * 3 + low * 5 + high_number * 7 + low_number * 11;\n"
    "}\n";

static const char check_test_tier1_extremes_other_source[] =
    "#include <stdio.h>\n"
    "#include <string.h>\n"
    "#include \"tier1-extremes.h\"\n"
    "static const unsigned long long doubles[] = { 0, 0x8000000000000000ull, 0x7ff0000000000000ull, 0xfff0000000000000ull, 0x7ff8000000000001ull,\n"
    "    0xfff8000000000002ull, 0x7ff0000000000003ull, 0xfff4000000000000ull, 1, 0x8000000000000001ull, 0x3ff8000000000000ull, 0xc000000000000000ull };\n"
    "static const unsigned floats[] = { 0, 0x80000000u, 0x7f800000u, 0xff800000u, 0x7fc00001u, 0xffc00002u, 0x7f800003u, 0xffa00000u, 1, 0x80000001u,\n"
    "    0x3fc00000u, 0xc0000000u };\n"
    "static double d(unsigned i) { double v; memcpy(&v, &doubles[i % 12], 8); return v; }\n"
    "static float f(unsigned i) { float v; memcpy(&v, &floats[i % 12], 4); return v; }\n"
    "static unsigned long long hd(unsigned long long h, double v) { unsigned long long b; memcpy(&b, &v, 8); return (h ^ b) * 1099511628211ull; }\n"
    "static unsigned long long hf(unsigned long long h, float v) { unsigned b; memcpy(&b, &v, 4); return (h ^ b) * 1099511628211ull; }\n"
    "int main(void)\n"
    "{\n"
    "    unsigned long long h = 14695981039346656037ull;\n"
    "    double p[12];\n"
    "    for (unsigned i = 0; i < 12; i += 1)\n"
    "    {\n"
    "        p[i] = d(i * 5 + 3);\n"
    "        for (unsigned j = 0; j < 12; j += 1)\n"
    "        {\n"
    "            h = hd(h, e_max(d(i), d(j)));\n"
    "            h = hd(h, e_min(d(i), d(j)));\n"
    "            h = hd(h, e_maximum(d(i), d(j)));\n"
    "            h = hd(h, e_minimum(d(i), d(j)));\n"
    "            h = hf(h, e_maxf(f(i), f(j)));\n"
    "            h = hf(h, e_minf(f(i), f(j)));\n"
    "            h = hf(h, e_maximumf(f(i), f(j)));\n"
    "            h = hf(h, e_minimumf(f(i), f(j)));\n"
    "            h = hd(h, e_chain(d(i), d(j), f(j), f(i)));\n"
    "        }\n"
    "        if (i < 2) printf(\"%a %a %a %a\\n\", e_max(d(i), 1.5), e_minimum(-0.0, d(i)), (double)e_maxf(f(i), -2.0f), e_chain(d(i), 2.5, 1.0f, f(i)));\n"
    "    }\n"
    "    for (unsigned n = 1; n <= 12; n += 1) h = hd(h, e_fold(p, n));\n"
    "    printf(\"%llx\\n\", h);\n"
    "    return 0;\n"
    "}\n";

// The frame address's fixture (#1847): __builtin_frame_address(0), a stack_save of BUSTER_IR_STACK_SAVE_FRAME, read in a
// leaf, in a function with calls and stack arguments and through __builtin_return_address(0), each Tier 1's with a frame
// pointer kept and its code certified. What the units print doesn't depend on where the stack is: the frame address's
// alignment, the return address in the word above it, the incoming stack arguments past the pushed rbp and the return
// address, and a local below it.
static const char check_test_tier1_frame_address_header[] =
    "#include <stdint.h>\n"
    "int fa_leaf(void);\n"
    "int fa_return(void);\n"
    "long fa_stack(long a, long b, long c, long d, long e, long f, long g, long h);\n"
    "int fa_nested(int n);\n";

static const char check_test_tier1_frame_address_source[] =
    "#include \"tier1-frame-address.h\"\n"
    "int fa_leaf(void) { uintptr_t f = (uintptr_t)__builtin_frame_address(0); return (int)(f & 15); }\n"
    "int fa_return(void) { void* const* f = __builtin_frame_address(0); return f[1] == __builtin_return_address(0) && f[1] != 0; }\n"
    "long fa_stack(long a, long b, long c, long d, long e, long f, long g, long h)\n"
    "{\n"
    "    const long* frame = __builtin_frame_address(0);\n"
    "    return frame[2] * 1000 + frame[3] * 100 + a + b + c + d + e + f;\n"
    "}\n"
    "int fa_nested(int n)\n"
    "{\n"
    "    volatile int local = n;\n"
    "    const char* frame = __builtin_frame_address(0);\n"
    "    const int below = (const char*)&local < frame;\n"
    "    return below + (n > 0 ? fa_nested(n - 1) : 0);\n"
    "}\n";

static const char check_test_tier1_frame_address_other_source[] =
    "#include \"tier1-frame-address.h\"\n"
    "#include <stdio.h>\n"
    "int main(void)\n"
    "{\n"
    "    printf(\"%d %d %ld %d\\n\", fa_leaf(), fa_return(), fa_stack(1, 2, 3, 4, 5, 6, 7, 8), fa_nested(5));\n"
    "    return 0;\n"
    "}\n";

// Its builds at the default target, every function Tier 1's and its code certified, run against clang -O0's.
static void check_test_driver_tier1_frame_address(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_tier1_units(test, clang, executable, directory, "tier1-frame-address", "frame address's", check_test_tier1_frame_address_header,
                                  check_test_tier1_frame_address_source, check_test_tier1_frame_address_other_source, 0, true, 0);
}

// Its builds at x86-64's baseline (SSE2's sequences, two rows for maximumnum and minimumnum), x86-64-v3's (AVX's
// VBLENDVPD) and x86-64-v4's (AVX-512's masked moves), each run where the host has the target's features.
static void check_test_driver_tier1_extremes(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char* const marches[] = { "-march=x86-64", "-march=x86-64-v3", "-march=x86-64-v4" };
    for (uint32_t t = 0; t < BUSTER_ARRAY_LENGTH(marches); t += 1)
    {
        const char* level = marches[t] + strlen("-march=");
        X86Target target = { 0 };
        const bool parsed = x86_target_parse(level, (uint32_t)strlen(level), &target);
        const bool host = parsed && (x86_target_native().features & target.features) == target.features;
        check_test_expect(test, parsed, "the floating extremes' fixture's target");
        check_test_driver_tier1_units(test, clang, executable, directory, "tier1-extremes", "floating extremes'", check_test_tier1_extremes_header,
                                      check_test_tier1_extremes_source, check_test_tier1_extremes_other_source, marches[t], host, 0);
    }
}

// The wide vectors' fixture (#1624): GNU C's 32- and 64-byte vectors at targets whose registers are narrower (x86-64's
// and x86-64-v2's 16 bytes, x86-64-v3's 32), which ir_vector_split.c splits into the target's widest: the scalar host's
// emulations' shape (host_scalar_vectors.h: a union of a vector and its lanes, the lanes computed, the vector returned),
// vectors passed and returned by value (the results split across xmm0 to xmm3 or ymm0 and ymm1), loaded and stored
// through pointers in a loop, chosen at a join (a phi), and constants; and a vector operator, which Tier 0 computes in
// pieces. The other unit (clang -O2's) passes random vectors and hashes every result.
static const char check_test_tier1_wide_vectors_header[] =
    "#include <stdint.h>\n"
    "typedef int v8si __attribute__((vector_size(32)));\n"
    "typedef int v16si __attribute__((vector_size(64)));\n"
    "typedef unsigned char v64qu __attribute__((vector_size(64)));\n"
    "v16si w_add(v16si a, v16si b);\n"
    "v8si w_pick(const v8si* p, const v8si* q, int which);\n"
    "void w_reverse(v16si* out, const v16si* in, unsigned n);\n"
    "v16si w_constant(int k);\n"
    "uint64_t w_sum(v16si a);\n"
    "v64qu w_bytes(v64qu a, v64qu b);\n"
    "v8si w_ops(v8si a, v8si b);\n";

static const char check_test_tier1_wide_vectors_source[] =
    "#include \"tier1-wide-vectors.h\"\n"
    "typedef union W512 { v16si value; uint32_t u32[16]; } W512;\n"
    "typedef union B512 { v64qu value; uint8_t u8[64]; } B512;\n"
    "__attribute__((noinline)) v16si w_add(v16si a, v16si b)\n"
    "{\n"
    "    W512 a_ = { .value = a };\n"
    "    W512 b_ = { .value = b };\n"
    "    W512 r = { 0 };\n"
    "    for (unsigned i = 0; i < 16; i += 1)\n"
    "    {\n"
    "        r.u32[i] = a_.u32[i] + b_.u32[i] * 3u;\n"
    "    }\n"
    "    return r.value;\n"
    "}\n"
    "v8si w_pick(const v8si* p, const v8si* q, int which)\n"
    "{\n"
    "    v8si r = *p;\n"
    "    if (which & 1)\n"
    "    {\n"
    "        r = *q;\n"
    "    }\n"
    "    return r;\n"
    "}\n"
    "void w_reverse(v16si* out, const v16si* in, unsigned n)\n"
    "{\n"
    "    for (unsigned i = 0; i < n; i += 1)\n"
    "    {\n"
    "        out[i] = in[n - 1 - i];\n"
    "    }\n"
    "}\n"
    "v16si w_constant(int k)\n"
    "{\n"
    "    v16si c = { 1, -2, 3, -4, 5, -6, 7, -8, 9, -10, 11, -12, 13, -14, 15, 0x7fffffff };\n"
    "    v16si d = { 100, 200, 300, 400, 500, 600, 700, 800, 900, 1000, 1100, 1200, 1300, 1400, 1500, -1 };\n"
    "    v16si r = d;\n"
    "    if (k > 0)\n"
    "    {\n"
    "        r = c;\n"
    "    }\n"
    "    return r;\n"
    "}\n"
    "uint64_t w_sum(v16si a)\n"
    "{\n"
    "    W512 a_ = { .value = a };\n"
    "    uint64_t s = 0;\n"
    "    for (unsigned i = 0; i < 16; i += 1)\n"
    "    {\n"
    "        s = s * 31 + a_.u32[i];\n"
    "    }\n"
    "    return s;\n"
    "}\n"
    "v64qu w_bytes(v64qu a, v64qu b)\n"
    "{\n"
    "    B512 a_ = { .value = a };\n"
    "    B512 b_ = { .value = b };\n"
    "    B512 r = { 0 };\n"
    "    for (unsigned i = 0; i < 64; i += 1)\n"
    "    {\n"
    "        r.u8[i] = (uint8_t)(a_.u8[i] ^ b_.u8[63 - i]);\n"
    "    }\n"
    "    return r.value;\n"
    "}\n"
    "v8si w_ops(v8si a, v8si b)\n"
    "{\n"
    "    return a + b;\n"
    "}\n";

static const char check_test_tier1_wide_vectors_other_source[] =
    "#include \"tier1-wide-vectors.h\"\n"
    "#include <stdio.h>\n"
    "#include <string.h>\n"
    "static uint64_t hash = 1469598103934665603ull;\n"
    "static void mix(const void* p, unsigned n) { const unsigned char* b = p; for (unsigned i = 0; i < n; i += 1) { hash ^= b[i]; hash *= 1099511628211ull; } }\n"
    "int main(void)\n"
    "{\n"
    "    uint32_t s = 12345;\n"
    "    for (int round = 0; round < 200; round += 1)\n"
    "    {\n"
    "        uint32_t words[64];\n"
    "        for (int i = 0; i < 64; i += 1) { s = s * 1664525u + 1013904223u; words[i] = s; }\n"
    "        v16si a, b; v8si p, q; v64qu x, y; v16si arr[5], out[5];\n"
    "        memcpy(&a, words, 64); memcpy(&b, words + 16, 64); memcpy(&p, words + 32, 32); memcpy(&q, words + 40, 32);\n"
    "        memcpy(&x, words + 48, 64); memcpy(&y, words, 64);\n"
    "        for (int i = 0; i < 5; i += 1) memcpy(&arr[i], words + 8 * i, 64);\n"
    "        v16si r = w_add(a, b); mix(&r, 64);\n"
    "        v8si t = w_pick(&p, &q, round); mix(&t, 32);\n"
    "        w_reverse(out, arr, 1 + round % 5); mix(out, 64 * (1 + round % 5));\n"
    "        v16si c = w_constant(round % 3 - 1); mix(&c, 64);\n"
    "        uint64_t sum = w_sum(a); mix(&sum, 8);\n"
    "        v64qu z = w_bytes(x, y); mix(&z, 64);\n"
    "        v8si o = w_ops(p, q); mix(&o, 32);\n"
    "    }\n"
    "    printf(\"%016llx\\n\", (unsigned long long)hash);\n"
    "    return 0;\n"
    "}\n";

// Its builds by x86-64, x86-64-v2 and x86-64-v3, every function Tier 1's and its code certified, run where the host
// has each target's features.
static void check_test_driver_tier1_wide_vectors(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char* const targets[3] = { "x86-64", "x86-64-v2", "x86-64-v3" };
    for (uint32_t t = 0; t < 3; t += 1)
    {
        X86Target target = { 0 };
        const bool parsed = x86_target_parse(targets[t], (uint32_t)strlen(targets[t]), &target);
        const bool host = parsed && (x86_target_native().features & target.features) == target.features;
        char option[32];
        char what[64];
        snprintf(option, sizeof(option), "-march=%s", targets[t]);
        snprintf(what, sizeof(what), "wide vectors' (%s)", targets[t]);
        check_test_driver_tier1_units(test, clang, executable, directory, "tier1-wide-vectors", what, check_test_tier1_wide_vectors_header, check_test_tier1_wide_vectors_source,
                                      check_test_tier1_wide_vectors_other_source, option, host, 0);
    }
}

// The wide results' fixture (#1847): a 128-byte vector returned by value, which the convention splits across the
// target's widest vector registers (zmm0 and zmm1 at x86-64-v4, ymm0 to ymm3 at x86-64-v3), the function's own return
// loading every part straight from the result's address (#1847, tier1_select_part_direct) and its caller storing them
// into its object.
// The other unit (clang -O2's) passes random vectors and hashes every result.
static const char check_test_tier1_wide_results_header[] =
    "#include <stdint.h>\n"
    "typedef int v32si __attribute__((vector_size(128)));\n"
    "v32si x_mix(const v32si* p, int k);\n"
    "void x_call(v32si* out, const v32si* p, int k);\n";

static const char check_test_tier1_wide_results_source[] =
    "#include \"tier1-wide-results.h\"\n"
    "typedef union X1024 { v32si value; uint32_t u32[32]; } X1024;\n"
    "__attribute__((noinline)) v32si x_mix(const v32si* p, int k)\n"
    "{\n"
    "    X1024 a = { .value = *p };\n"
    "    X1024 r = { 0 };\n"
    "    for (unsigned i = 0; i < 32; i += 1)\n"
    "    {\n"
    "        r.u32[i] = a.u32[31 - i] * 3u + (uint32_t)k + i;\n"
    "    }\n"
    "    return r.value;\n"
    "}\n"
    "void x_call(v32si* out, const v32si* p, int k)\n"
    "{\n"
    "    v32si r = x_mix(p, k);\n"
    "    *out = r;\n"
    "}\n";

static const char check_test_tier1_wide_results_other_source[] =
    "#include \"tier1-wide-results.h\"\n"
    "#include <stdio.h>\n"
    "#include <string.h>\n"
    "static uint64_t hash = 1469598103934665603ull;\n"
    "static void mix(const void* p, unsigned n) { const unsigned char* b = p; for (unsigned i = 0; i < n; i += 1) { hash ^= b[i]; hash *= 1099511628211ull; } }\n"
    "int main(void)\n"
    "{\n"
    "    uint32_t s = 2024;\n"
    "    for (int round = 0; round < 100; round += 1)\n"
    "    {\n"
    "        uint32_t words[32];\n"
    "        for (int i = 0; i < 32; i += 1) { s = s * 1664525u + 1013904223u; words[i] = s; }\n"
    "        v32si a, out;\n"
    "        memcpy(&a, words, 128);\n"
    "        v32si r = x_mix(&a, round); mix(&r, 128);\n"
    "        x_call(&out, &a, round + 7); mix(&out, 128);\n"
    "    }\n"
    "    printf(\"%016llx\\n\", (unsigned long long)hash);\n"
    "    return 0;\n"
    "}\n";

// Its builds by x86-64-v3 and x86-64-v4, every function Tier 1's and its code certified, run where the host has each
// target's features.
static void check_test_driver_tier1_wide_results(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char* const targets[2] = { "x86-64-v3", "x86-64-v4" };
    for (uint32_t t = 0; t < 2; t += 1)
    {
        X86Target target = { 0 };
        const bool parsed = x86_target_parse(targets[t], (uint32_t)strlen(targets[t]), &target);
        const bool host = parsed && (x86_target_native().features & target.features) == target.features;
        char option[32];
        char what[64];
        snprintf(option, sizeof(option), "-march=%s", targets[t]);
        snprintf(what, sizeof(what), "wide results' (%s)", targets[t]);
        check_test_driver_tier1_units(test, clang, executable, directory, "tier1-wide-results", what, check_test_tier1_wide_results_header, check_test_tier1_wide_results_source,
                                      check_test_tier1_wide_results_other_source, option, host, 0);
    }
}

// The 256-byte results' fixture (#1847): a 256-byte vector returned by value at x86-64-v4, in zmm0 to zmm3 (make64), the
// function's own return loading every part straight from the result's address (tier1_select_part_direct), as the red
// zone's 16 slots can't stage four 64-byte parts, and its caller (use64) storing them into its object. Below AVX-512 LLVM
// returns it through a hidden pointer clang doesn't count (DEMOTED), which Tier 1 refuses, so the fixture is x86-64-v4's
// alone. The other unit (clang -O2's) passes random vectors and hashes every result.
static const char check_test_tier1_results256_header[] =
    "#include <stdint.h>\n"
    "typedef int v64si __attribute__((vector_size(256)));\n"
    "v64si make64(const v64si* p, int k);\n"
    "void use64(v64si* out, const v64si* p, int k);\n";

static const char check_test_tier1_results256_source[] =
    "#include \"tier1-results256.h\"\n"
    "typedef union X2048 { v64si value; uint32_t u32[64]; } X2048;\n"
    "__attribute__((noinline)) v64si make64(const v64si* p, int k)\n"
    "{\n"
    "    X2048 a = { .value = *p };\n"
    "    X2048 r = { 0 };\n"
    "    for (unsigned i = 0; i < 64; i += 1)\n"
    "    {\n"
    "        r.u32[i] = a.u32[63 - i] * 5u + (uint32_t)k + i;\n"
    "    }\n"
    "    return r.value;\n"
    "}\n"
    "void use64(v64si* out, const v64si* p, int k)\n"
    "{\n"
    "    v64si r = make64(p, k);\n"
    "    *out = r;\n"
    "}\n";

static const char check_test_tier1_results256_other_source[] =
    "#include \"tier1-results256.h\"\n"
    "#include <stdio.h>\n"
    "#include <string.h>\n"
    "static uint64_t hash = 1469598103934665603ull;\n"
    "static void mix(const void* p, unsigned n) { const unsigned char* b = p; for (unsigned i = 0; i < n; i += 1) { hash ^= b[i]; hash *= 1099511628211ull; } }\n"
    "int main(void)\n"
    "{\n"
    "    uint32_t s = 1847;\n"
    "    for (int round = 0; round < 100; round += 1)\n"
    "    {\n"
    "        uint32_t words[64];\n"
    "        for (int i = 0; i < 64; i += 1) { s = s * 1664525u + 1013904223u; words[i] = s; }\n"
    "        v64si a, out;\n"
    "        memcpy(&a, words, 256);\n"
    "        v64si r = make64(&a, round); mix(&r, 256);\n"
    "        use64(&out, &a, round + 3); mix(&out, 256);\n"
    "    }\n"
    "    printf(\"%016llx\\n\", (unsigned long long)hash);\n"
    "    return 0;\n"
    "}\n";

// Its build at x86-64-v4, every function Tier 1's and its code certified, run where the host has AVX-512.
static void check_test_driver_tier1_results256(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    X86Target v4 = { 0 };
    const bool parsed = x86_target_parse("x86-64-v4", 9, &v4);
    const bool host = parsed && (x86_target_native().features & v4.features) == v4.features;
    check_test_driver_tier1_units(test, clang, executable, directory, "tier1-results256", "256-byte results'", check_test_tier1_results256_header,
                                  check_test_tier1_results256_source, check_test_tier1_results256_other_source, "-march=x86-64-v4", host, 0);
}

#include "buster/compiler/c_check_tier1_rows_fixture.h"

static const char check_test_tier1_rows_other_source[] =
    "#include <stdio.h>\n"
    "#include <string.h>\n"
    "#include \"tier1-rows.h\"\n"
    "static uint64_t hash = 0xcbf29ce484222325ull;\n"
    "static void mix(const void* p, unsigned n) { const unsigned char* b = p; for (unsigned i = 0; i < n; i += 1) hash = (hash ^ b[i]) * 0x100000001b3ull; }\n"
    "int main(void)\n"
    "{\n"
    "    unsigned char in[512];\n"
    "    unsigned char out[256];\n"
    "    for (int round = 0; round < 4; round += 1)\n"
    "    {\n"
    "        for (unsigned i = 0; i < sizeof(in); i += 1) in[i] = (unsigned char)(i * 37u + (i >> 3) * 11u + round * 101u + (i % 7 == 0 ? 'a' : 0));\n"
    "        memset(out, 0, sizeof(out));\n"
    "        __m512i a = _mm512_loadu_si512(in);\n"
    "        __m512i b = _mm512_loadu_si512(in + 64);\n"
    "        __m512i c = _mm512_loadu_si512(in + 128);\n"
    "        __m512i r = r_bytes(a, b);\n"
    "        mix(&r, 64);\n"
    "        uint64_t m = r_compares(a, b) ^ r_compares(b, c);\n"
    "        mix(&m, 8);\n"
    "        r = r_masked(a, b, m, (__mmask16)m);\n"
    "        mix(&r, 64);\n"
    "        r = r_permutes(a, b, c);\n"
    "        mix(&r, 64);\n"
    "        r = r_shifts(a, b, 5 + round);\n"
    "        mix(&r, 64);\n"
    "        r_memory(out, in, m, (__mmask16)(m >> 9));\n"
    "        mix(out, sizeof(out));\n"
    "        r = r_lanes(a, _mm256_loadu_si256((const void*)(in + 200)), _mm_loadu_si128((const void*)(in + 300)), 0x123456789abcll * (round + 1), -77 + round);\n"
    "        mix(&r, 64);\n"
    "        long long s = r_reduce(a, c);\n"
    "        mix(&s, 8);\n"
    "        __m256i n = r_narrow(_mm256_loadu_si256((const void*)(in + 17)), _mm256_loadu_si256((const void*)(in + 99)), _mm_loadu_si128((const void*)(in + 222)));\n"
    "        mix(&n, 32);\n"
    "        int v = r_vex(_mm256_loadu_si256((const void*)(in + 5)), _mm256_loadu_si256((const void*)(in + 41)), _mm256_loadu_si256((const void*)(in + 77)));\n"
    "        mix(&v, 4);\n"
    "        uint32_t k = r_loop(in, sizeof(in), out);\n"
    "        mix(&k, 4);\n"
    "        mix(out, 64);\n"
    "        printf(\"%016llx %u\\n\", (unsigned long long)hash, k);\n"
    "    }\n"
    "    return 0;\n"
    "}\n";

// The shared operands' fixture (#1871): machine rows that take one value as two operands, which Tier 1 places in one
// register with their result's, and the code's certificate takes, each operand read in the record that first writes the
// result; and import.c's import_archive_size, whose _mm_packus_epi32(fours, fours) is one. At x86-64-v4, every function
// Tier 1's and its code certified, run against clang -O0's where the host has its features.
static const char check_test_tier1_shared_header[] =
    "#include <immintrin.h>\n"
    "#include <stdint.h>\n"
    "__m128i s_pack(__m128i a);\n"
    "__m128i s_madd(__m128i a);\n"
    "__m256i s_wide(__m256i a);\n"
    "int s_size(const char* field, uint64_t* value);\n";

static const char check_test_tier1_shared_source[] =
    "#include \"tier1-shared.h\"\n"
    "__m128i s_pack(__m128i a) { return _mm_xor_si128(_mm_packus_epi32(a, a), _mm_packs_epi16(a, a)); }\n"
    "__m128i s_madd(__m128i a) { return _mm_add_epi32(_mm_madd_epi16(a, a), _mm_maddubs_epi16(a, a)); }\n"
    "__m256i s_wide(__m256i a) { return _mm256_packus_epi32(a, a); }\n"
    "int s_size(const char* field, uint64_t* value)\n"
    "{\n"
    "    const __mmask16 within = 0x3ff;\n"
    "    const __m128i bytes = _mm_maskz_loadu_epi8(within, field);\n"
    "    const __m128i values = _mm_sub_epi8(bytes, _mm_set1_epi8('0'));\n"
    "    const uint32_t digits = _mm_mask_cmplt_epu8_mask(within, values, _mm_set1_epi8(10));\n"
    "    const uint32_t spaces = _mm_mask_cmpeq_epi8_mask(within, bytes, _mm_set1_epi8(' '));\n"
    "    const uint32_t count = (uint32_t)__builtin_popcount(digits);\n"
    "    const int result = (digits | spaces) == within && digits != 0 && (digits & (digits + 1)) == 0;\n"
    "    const __m128i positions = _mm_sub_epi8(_mm_setr_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15), _mm_set1_epi8((char)(16 - count)));\n"
    "    const __m128i aligned = _mm_shuffle_epi8(_mm_maskz_mov_epi8((__mmask16)digits, values), positions);\n"
    "    const __m128i pairs = _mm_maddubs_epi16(aligned, _mm_setr_epi8(10, 1, 10, 1, 10, 1, 10, 1, 10, 1, 10, 1, 10, 1, 10, 1));\n"
    "    const __m128i fours = _mm_madd_epi16(pairs, _mm_setr_epi16(100, 1, 100, 1, 100, 1, 100, 1));\n"
    "    const __m128i eights = _mm_madd_epi16(_mm_packus_epi32(fours, fours), _mm_setr_epi16(10000, 1, 10000, 1, 10000, 1, 10000, 1));\n"
    "    *value = (uint64_t)(uint32_t)_mm_cvtsi128_si32(eights) * 100000000ull + (uint32_t)_mm_extract_epi32(eights, 1);\n"
    "    return result;\n"
    "}\n";

static const char check_test_tier1_shared_other_source[] =
    "#include \"tier1-shared.h\"\n"
    "#include <stdio.h>\n"
    "#include <string.h>\n"
    "static uint64_t hash = 1469598103934665603ull;\n"
    "static void mix(const void* p, unsigned n) { const unsigned char* b = p; for (unsigned i = 0; i < n; i += 1) { hash ^= b[i]; hash *= 1099511628211ull; } }\n"
    "int main(void)\n"
    "{\n"
    "    static const char* const fields[] = { \"0         \", \"1234567890\", \"42        \", \"12 4      \", \"x         \", \"9999999   \" };\n"
    "    for (unsigned f = 0; f < sizeof(fields) / sizeof(fields[0]); f += 1)\n"
    "    {\n"
    "        uint64_t value = 0;\n"
    "        const int ok = s_size(fields[f], &value);\n"
    "        if (ok) printf(\"%d %llu\\n\", ok, (unsigned long long)value); else printf(\"%d\\n\", ok);\n"
    "    }\n"
    "    uint32_t s = 1871;\n"
    "    for (int round = 0; round < 64; round += 1)\n"
    "    {\n"
    "        uint32_t words[8];\n"
    "        for (int i = 0; i < 8; i += 1) { s = s * 1664525u + 1013904223u; words[i] = s; }\n"
    "        __m128i a;\n"
    "        __m256i w;\n"
    "        memcpy(&a, words, 16);\n"
    "        memcpy(&w, words, 32);\n"
    "        __m128i p = s_pack(a); mix(&p, 16);\n"
    "        __m128i m = s_madd(a); mix(&m, 16);\n"
    "        __m256i q = s_wide(w); mix(&q, 32);\n"
    "    }\n"
    "    printf(\"%016llx\\n\", (unsigned long long)hash);\n"
    "    return 0;\n"
    "}\n";

static void check_test_driver_tier1_shared(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    X86Target v4 = { 0 };
    const bool parsed = x86_target_parse("x86-64-v4", 9, &v4);
    const bool host = parsed && (x86_target_native().features & v4.features) == v4.features;
    check_test_driver_tier1_units(test, clang, executable, directory, "tier1-shared", "shared operands'", check_test_tier1_shared_header, check_test_tier1_shared_source,
                                  check_test_tier1_shared_other_source, "-march=x86-64-v4", host, 0);
}

// Its builds, by x86-64-v4, run where the host has every feature of it.
static void check_test_driver_tier1_rows(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    X86Target v4 = { 0 };
    const bool parsed = x86_target_parse("x86-64-v4", 9, &v4);
    const bool host = parsed && (x86_target_native().features & v4.features) == v4.features;
    check_test_driver_tier1_units(test, clang, executable, directory, "tier1-rows", "machine rows'", check_test_tier1_rows_header, check_test_tier1_rows_source,
                                  check_test_tier1_rows_other_source, "-march=x86-64-v4", host, 0);
    // #1283: Tier 1 places a row's vector roles one at a time, so that most rows take no move into or out of place: by
    // each path, with rematerialization and without, every function Tier 1's, at most one move for every two rows (Tier 0's
    // placement moved every vector operand in and every vector result out: 150 for the unit's 58 rows). The unit's 11
    // compares into masks were rows that took no move; #1623 decodes them into the IR's comparisons, so the bound counts
    // them with the rows still. #1840 decodes its masked operations' and blends' selects into the IR's vselect, its
    // inserts, extracts and casts between widths into the IR's own intrinsics, its variable permutes and byte shuffles
    // into the IR's permutes and its widening conversion into the IR's extend, which leaves 45 rows and 26 moves by Tier
    // 1 (75 and 39 before). #1840's later families decode rows that took no move (its shifts by an int: 39 rows left; its
    // masks' conversions and lane operations: 33; its permutes of two tables: 27; its bitwise functions, compresses and
    // AES rounds: 24; its masked loads and stores: 16; its one-lane moves: 13; its reductions: 9; its lanes' population
    // counts, leading zero counts and conflicts: 7), so the bound counts the unit's 45 rows as of the conversions, as it
    // counts the compares. The floor of 5 rows keeps the unit a machine rows' test as #1840 decodes more of them.
    const uint64_t compares = 11;
    const uint64_t rows = 45;
    char header[512];
    char unit[512];
    char object[512];
    snprintf(header, sizeof(header), "%s/tier1-rows.h", directory);
    snprintf(unit, sizeof(unit), "%s/tier1-rows-moves.c", directory);
    snprintf(object, sizeof(object), "%s/tier1-rows-moves.o", directory);
    const bool written = parse_test_write(header, check_test_tier1_rows_header, strlen(check_test_tier1_rows_header)) &&
                         parse_test_write(unit, check_test_tier1_rows_source, strlen(check_test_tier1_rows_source));
    check_test_expect(test, written, "the machine rows' unit is written for its moves' count");
    for (uint32_t run = 0; written && run < 4; run += 1)
    {
        CheckTestTier1Build build;
        const bool compiled = check_test_tier1_build(executable, unit, "", object, (run & 2) == 0, (run & 1) != 0, true, false, "-march=x86-64-v4", &build);
        char message[256];
        snprintf(message, sizeof(message),
                 "the machine rows' unit takes at most a move into or out of place for every two rows and compares (path %u, rematerialize %u: %llu rows, %llu moves)", run & 1,
                 (run & 2) == 0 ? 1u : 0u, (unsigned long long)build.machine_rows, (unsigned long long)build.row_moves);
        check_test_expect(test, compiled && build.machine_rows >= 5 && build.machine_rows <= rows && 2 * build.row_moves <= rows + compares, message);
        unlink(object);
    }
    unlink(unit);
    unlink(header);
}

// The lanes' fixture (#141's element types), for x86-64-v4: C's operators on vectors of 8-, 16- and 64-bit integers
// (signed and unsigned), floats and doubles at 16, 32 and 64 bytes, which the IR computes as its own operations of their
// lanes rather than machine rows (ir_simd_typed_operation): additions, subtractions, products (but bytes', which x86
// hasn't), shifts by each lane's count, the floating arithmetic, and every comparison, whose results come back as -1 or
// 0 lanes and are hashed apart from the arithmetic. Each floating operation is a statement of its own, so that clang
// -O0 contracts none into an FMA (-ffp-contract=on). The other unit (clang -O2's) fills the operands and hashes the
// results.
static const char check_test_tier1_lanes_header[] =
    "#include <stdio.h>\n"
    "#include <stdint.h>\n"
    "#include <string.h>\n"
    "#define L_INTEGERS(X) X(i8x16, int8_t, 16, 0) X(u8x32, uint8_t, 32, 0) X(i8x64, int8_t, 64, 0) X(i16x8, int16_t, 16, 15) X(u16x16, uint16_t, 32, 15) \\\n"
    "    X(i16x32, int16_t, 64, 15) X(u32x4, uint32_t, 16, 31) X(i32x16, int32_t, 64, 31) X(i64x2, int64_t, 16, 63) X(u64x4, uint64_t, 32, 63) X(i64x8, int64_t, 64, 63)\n"
    "#define L_FLOATS(X) X(f32x4, float, int32_t, 16) X(f32x8, float, int32_t, 32) X(f32x16, float, int32_t, 64) X(f64x2, double, int64_t, 16) \\\n"
    "    X(f64x4, double, int64_t, 32) X(f64x8, double, int64_t, 64)\n"
    "#define L_INTEGER_TYPE(n, t, w, k) typedef t n __attribute__((vector_size(w)));\n"
    "#define L_FLOAT_TYPE(n, t, i, w) typedef t n __attribute__((vector_size(w))); typedef i n##_mask __attribute__((vector_size(w)));\n"
    "L_INTEGERS(L_INTEGER_TYPE)\n"
    "L_FLOATS(L_FLOAT_TYPE)\n"
    "#define L_INTEGER_DECLARE(n, t, w, k) n l_##n(n x, n y);\n"
    "#define L_FLOAT_DECLARE(n, t, i, w) n l_##n(n x, n y, n##_mask* compares);\n"
    "L_INTEGERS(L_INTEGER_DECLARE)\n"
    "L_FLOATS(L_FLOAT_DECLARE)\n";

static const char check_test_tier1_lanes_source[] =
    "#include \"tier1-lanes.h\"\n"
    "#define L_INTEGER(n, t, w, k) n l_##n(n a, n b)\\\n"
    "{\\\n"
    "    n s = a + b;\\\n"
    "    n d = a - b;\\\n"
    "    n r = s ^ d;\\\n"
    "    r = r + ((n)(a < b) & s);\\\n"
    "    r = r - ((n)(a <= b) | d);\\\n"
    "    r = r ^ (n)(a > b);\\\n"
    "    r = r + (n)(a >= b);\\\n"
    "    r = r - ((n)(a == b) & d);\\\n"
    "    r = r ^ ((n)(a != s) | b);\\\n"
    "    if (k != 0)\\\n"
    "    {\\\n"
    "        n c = b & k;\\\n"
    "        r = r + a * b;\\\n"
    "        r = r ^ (a << c);\\\n"
    "        r = r - (s >> c);\\\n"
    "    }\\\n"
    "    return r;\\\n"
    "}\n"
    "#define L_FLOAT(n, t, i, w) n l_##n(n a, n b, n##_mask* compares)\\\n"
    "{\\\n"
    "    n s = a + b;\\\n"
    "    n p = s * a;\\\n"
    "    n d = p - b;\\\n"
    "    n q = d / (b + 3);\\\n"
    "    n##_mask m = (a < b) + (a <= b);\\\n"
    "    m = m - (a > b) + (a >= b);\\\n"
    "    m = m + (a == b) - (q != s);\\\n"
    "    *compares = m;\\\n"
    "    return q;\\\n"
    "}\n"
    "L_INTEGERS(L_INTEGER)\n"
    "L_FLOATS(L_FLOAT)\n";

static const char check_test_tier1_lanes_other_source[] =
    "#include \"tier1-lanes.h\"\n"
    "static uint64_t hash = 1469598103934665603ull;\n"
    "static void mix(const void* p, unsigned n) { const unsigned char* q = p; for (unsigned i = 0; i < n; i++) { hash ^= q[i]; hash *= 1099511628211ull; } }\n"
    "static uint32_t seed = 12345;\n"
    "static uint32_t next(void) { seed = seed * 1103515245u + 12345u; return seed >> 8; }\n"
    "static void fill(unsigned char* p, unsigned n) { for (unsigned i = 0; i < n; i++) p[i] = (unsigned char)next(); }\n"
    "#define L_INTEGER_RUN(n, t, w, k) { n a; n c; fill((unsigned char*)&a, w); fill((unsigned char*)&c, w); if (round % 4 == 0) c = a; n r = l_##n(a, c); mix(&r, w); }\n"
    "#define L_FLOAT_RUN(n, t, i, w) { n a; n c; for (unsigned e = 0; e < w / sizeof(t); e++) { a[e] = (t)((int)(next() % 4001) - 2000) / 16; c[e] = (t)((int)(next() % 4001) - 2000) / 16; } \\\n"
    "    if (round % 4 == 1) c = a; n##_mask m; n r = l_##n(a, c, &m); mix(&r, w); mix(&m, w); }\n"
    "int main(void)\n"
    "{\n"
    "    for (unsigned round = 0; round < 64; round++)\n"
    "    {\n"
    "        L_INTEGERS(L_INTEGER_RUN)\n"
    "        L_FLOATS(L_FLOAT_RUN)\n"
    "        printf(\"%016llx\\n\", (unsigned long long)hash);\n"
    "    }\n"
    "    return 0;\n"
    "}\n";

// Its builds, by x86-64-v4, run where the host has every feature of it; then the unit by --tier1 -c, by each path with
// and without rematerialization, has no machine row: the former 14 scalar splats (8 shift counts' masks, 6 floating
// `b + 3`) are the IR's unary splat intrinsic (#1450), alongside its own lane operators.
static void check_test_driver_tier1_lanes(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    X86Target v4 = { 0 };
    const bool parsed = x86_target_parse("x86-64-v4", 9, &v4);
    const bool host = parsed && (x86_target_native().features & v4.features) == v4.features;
    check_test_driver_tier1_units(test, clang, executable, directory, "tier1-lanes", "lanes'", check_test_tier1_lanes_header, check_test_tier1_lanes_source,
                                  check_test_tier1_lanes_other_source, "-march=x86-64-v4", host, 0);
    char header[512];
    char unit[512];
    char object[512];
    snprintf(header, sizeof(header), "%s/tier1-lanes.h", directory);
    snprintf(unit, sizeof(unit), "%s/tier1-lanes-rows.c", directory);
    snprintf(object, sizeof(object), "%s/tier1-lanes-rows.o", directory);
    const bool written = parse_test_write(header, check_test_tier1_lanes_header, strlen(check_test_tier1_lanes_header)) &&
                         parse_test_write(unit, check_test_tier1_lanes_source, strlen(check_test_tier1_lanes_source));
    check_test_expect(test, written, "the lanes' unit is written for its rows' count");
    for (uint32_t run = 0; written && run < 4; run += 1)
    {
        CheckTestTier1Build build;
        const bool compiled = check_test_tier1_build(executable, unit, "", object, (run & 2) == 0, (run & 1) != 0, true, false, "-march=x86-64-v4", &build);
        char message[256];
        snprintf(message, sizeof(message), "the lanes' unit and its scalar splats have no machine row (path %u, rematerialize %u: %llu rows)", run & 1,
                 (run & 2) == 0 ? 1u : 0u, (unsigned long long)build.machine_rows);
        check_test_expect(test, compiled && build.machine_rows == 0, message);
        unlink(object);
    }
    unlink(unit);
    unlink(header);
}

// An unused comparison of floating lanes keeps its effect on the floating environment (#1413's review): a NaN operand
// raises MXCSR's invalid flag, as fcmp's does, so dead code elimination keeps it where its mask goes unused. clang -O0
// drops such a comparison itself (C allows it without FENV_ACCESS, which the checker refuses: #1135), so the oracle is
// Tier 0's build of the same unit, which keeps every expression. A float's and a double's comparison, each the flag it
// leaves: 3 where both operands' NaN raise it, 0 where none has one.
static const char check_test_tier1_invalid_source[] =
    "#include <stdio.h>\n"
    "typedef float f4 __attribute__((vector_size(16)));\n"
    "typedef double d4 __attribute__((vector_size(32)));\n"
    "static unsigned csr(void) { return __builtin_ia32_stmxcsr(); }\n"
    "__attribute__((noinline)) int invalid(f4 a, f4 b, d4 c, d4 d)\n"
    "{\n"
    "    __builtin_ia32_ldmxcsr(csr() & ~0x3fu);\n"
    "    (void)(a < b);\n"
    "    const unsigned first = csr() & 1u;\n"
    "    __builtin_ia32_ldmxcsr(csr() & ~0x3fu);\n"
    "    (void)(c >= d);\n"
    "    return (int)(first * 2 + (csr() & 1u));\n"
    "}\n"
    "int main(void)\n"
    "{\n"
    "    const f4 n = { 1.0f, __builtin_nanf(\"\"), 2.0f, 3.0f };\n"
    "    const f4 o = { 0.5f, 1.0f, 4.0f, 3.0f };\n"
    "    const d4 m = { 1.0, 2.0, __builtin_nan(\"\"), 3.0 };\n"
    "    const d4 p = { 1.0, 2.0, 3.0, 4.0 };\n"
    "    printf(\"%d %d %d\\n\", invalid(n, o, m, p), invalid(o, o, p, p), invalid(o, n, p, m));\n"
    "    return 0;\n"
    "}\n";

// Its builds, by x86-64-v4: Tier 0's, and by --tier1 by each path with and without rematerialization, `invalid` Tier 1's,
// each printing "3 0 3", run where the host has x86-64-v4.
static void check_test_driver_tier1_invalid(CheckTest* test, const char* executable, const char* directory)
{
    X86Target v4 = { 0 };
    const bool parsed = x86_target_parse("x86-64-v4", 9, &v4);
    const bool host = parsed && (x86_target_native().features & v4.features) == v4.features;
    char source[512];
    char program[512];
    snprintf(source, sizeof(source), "%s/tier1-invalid.c", directory);
    snprintf(program, sizeof(program), "%s/tier1-invalid", directory);
    const bool written = parse_test_write(source, check_test_tier1_invalid_source, strlen(check_test_tier1_invalid_source));
    check_test_expect(test, written, "the unused comparisons' unit is written");
    for (uint32_t run = 0; written && run < 5; run += 1)
    {
        bool built = false;
        CheckTestTier1Build build;
        if (run == 4)
        {
            char* arguments[] = { (char*)executable, "-march=x86-64-v4", "-o", program, source, NULL };
            char* text = NULL;
            built = check_test_driver_spawn(arguments, &text) == 0;
            free(text);
        }
        else
        {
            built = check_test_tier1_build(executable, source, "invalid", program, (run & 2) == 0, (run & 1) != 0, false, false, "-march=x86-64-v4", &build);
        }
        char* output = NULL;
        char* run_arguments[] = { program, NULL };
        const bool ran = built && host && check_test_driver_spawn_stream(run_arguments, &output, true) == 0;
        char message[256];
        snprintf(message, sizeof(message), "an unused floating comparison raises invalid on a NaN, as Tier 0's does (%s)", run == 4 ? "Tier 0" : (run & 1) != 0 ? "Tier 1, scalar path" : "Tier 1, vector path");
        check_test_expect(test, built && (!host || (ran && output != NULL && strcmp(output, "3 0 3\n") == 0)), message);
        free(output);
        unlink(program);
    }
    unlink(source);
}

// The SSE targets' fixture (#141), for the x86-64 baseline and x86-64-v2, targets without AVX: C's operators on 16-byte
// vectors of every element type, which the IR computes as its own operations of their lanes by legacy SSE's forms
// (additions, subtractions, the bitwise operations, words' products and at x86-64-v2 doublewords', the floating
// arithmetic, constants), and its comparisons, the rows' (pcmpeq and pcmpgt sequences, cmpps and cmppd), as machine
// rows (but quadwords', which the baseline has no form for: the front end refuses them, as clang computes them a lane at
// a time), every function Tier 1's. Each floating operation is a statement of its own, so that clang -O0 contracts none.
static const char check_test_tier1_sse_header[] =
    "#include <stdio.h>\n"
    "#include <stdint.h>\n"
    "#define S_INTEGERS(X) X(i8x16, int8_t, 1) X(u8x16, uint8_t, 1) X(i16x8, int16_t, 1) X(u16x8, uint16_t, 1) X(i32x4, int32_t, 1) X(u32x4, uint32_t, 1) X(i64x2, int64_t, 0) \\\n"
    "    X(u64x2, uint64_t, 0)\n"
    "#define S_FLOATS(X) X(f32x4, float, int32_t) X(f64x2, double, int64_t)\n"
    "#define S_INTEGER_TYPE(n, t, cmp) typedef t n __attribute__((vector_size(16)));\n"
    "#define S_FLOAT_TYPE(n, t, i) typedef t n __attribute__((vector_size(16))); typedef i n##_mask __attribute__((vector_size(16)));\n"
    "S_INTEGERS(S_INTEGER_TYPE)\n"
    "S_FLOATS(S_FLOAT_TYPE)\n"
    "#define S_INTEGER_DECLARE(n, t, cmp) n s_##n(n x, n y);\n"
    "#define S_FLOAT_DECLARE(n, t, i) n s_##n(n x, n y, n##_mask* compares);\n"
    "S_INTEGERS(S_INTEGER_DECLARE)\n"
    "S_FLOATS(S_FLOAT_DECLARE)\n"
    "i16x8 s_words(i16x8 x, i16x8 y);\n"
    "#if defined(__SSE4_1__)\n"
    "u32x4 s_doublewords(u32x4 x, u32x4 y);\n"
    "#endif\n";

static const char check_test_tier1_sse_source[] =
    "#include \"tier1-sse.h\"\n"
    "#define S_INTEGER(n, t, cmp) n s_##n(n a, n b)\\\n"
    "{\\\n"
    "    n s = a + b;\\\n"
    "    n d = a - b;\\\n"
    "    n r = s ^ d;\\\n"
    "    r = r + (s & d);\\\n"
    "    r = r - (a | b);\\\n"
    "    if (cmp)\\\n"
    "    {\\\n"
    "        r = r ^ (n)(a < b);\\\n"
    "        r = r + (n)(a == s);\\\n"
    "    }\\\n"
    "    n k = { 3 };\\\n"
    "    return r - k;\\\n"
    "}\n"
    "#define S_FLOAT(n, t, i) n s_##n(n a, n b, n##_mask* compares)\\\n"
    "{\\\n"
    "    n s = a + b;\\\n"
    "    n p = s * a;\\\n"
    "    n d = p - b;\\\n"
    "    n q = d / b;\\\n"
    "    n r = b - q;\\\n"
    "    *compares = (a < b) + (a >= q) - (r != s);\\\n"
    "    return r;\\\n"
    "}\n"
    "S_INTEGERS(S_INTEGER)\n"
    "S_FLOATS(S_FLOAT)\n"
    "i16x8 s_words(i16x8 a, i16x8 b) { i16x8 p = a * b; return p - (a * p); }\n"
    "#if defined(__SSE4_1__)\n"
    "u32x4 s_doublewords(u32x4 a, u32x4 b) { u32x4 p = a * b; return b * p + a; }\n"
    "#endif\n";

static const char check_test_tier1_sse_other_source[] =
    "#include \"tier1-sse.h\"\n"
    "static uint64_t hash = 1469598103934665603ull;\n"
    "static void mix(const void* p, unsigned n) { const unsigned char* q = p; for (unsigned i = 0; i < n; i++) { hash ^= q[i]; hash *= 1099511628211ull; } }\n"
    "static uint32_t seed = 4321;\n"
    "static uint32_t next(void) { seed = seed * 1103515245u + 12345u; return seed >> 8; }\n"
    "static void fill(unsigned char* p) { for (unsigned i = 0; i < 16; i++) p[i] = (unsigned char)next(); }\n"
    "#define S_INTEGER_RUN(n, t, cmp) { n a; n c; fill((unsigned char*)&a); fill((unsigned char*)&c); if (round % 4 == 0) c = a; n r = s_##n(a, c); mix(&r, 16); }\n"
    "#define S_FLOAT_RUN(n, t, i) { n a; n c; for (unsigned e = 0; e < 16 / sizeof(t); e++) { a[e] = (t)((int)(next() % 4001) - 2000) / 16; c[e] = (t)((int)(next() % 4001) - 2001) / 8; } \\\n"
    "    if (round % 4 == 1) c = a; n##_mask m; n r = s_##n(a, c, &m); mix(&r, 16); mix(&m, 16); }\n"
    "int main(void)\n"
    "{\n"
    "    for (unsigned round = 0; round < 64; round++)\n"
    "    {\n"
    "        S_INTEGERS(S_INTEGER_RUN)\n"
    "        S_FLOATS(S_FLOAT_RUN)\n"
    "        { i16x8 a; i16x8 c; fill((unsigned char*)&a); fill((unsigned char*)&c); i16x8 r = s_words(a, c); mix(&r, 16); }\n"
    "#if defined(__SSE4_1__)\n"
    "        { u32x4 a; u32x4 c; fill((unsigned char*)&a); fill((unsigned char*)&c); u32x4 r = s_doublewords(a, c); mix(&r, 16); }\n"
    "#endif\n"
    "        printf(\"%016llx\\n\", (unsigned long long)hash);\n"
    "    }\n"
    "    return 0;\n"
    "}\n";

// Its builds, at the x86-64 baseline and at x86-64-v2, each run on any x86-64 host, but x86-64-v2's only where the host
// has its features.
static void check_test_driver_tier1_sse(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    X86Target v2 = { 0 };
    const bool parsed = x86_target_parse("x86-64-v2", 9, &v2);
    const bool host = parsed && (x86_target_native().features & v2.features) == v2.features;
    check_test_driver_tier1_units(test, clang, executable, directory, "tier1-sse", "SSE targets' (x86-64)", check_test_tier1_sse_header, check_test_tier1_sse_source,
                                  check_test_tier1_sse_other_source, "-march=x86-64", true, 0);
    check_test_driver_tier1_units(test, clang, executable, directory, "tier1-sse", "SSE targets' (x86-64-v2)", check_test_tier1_sse_header, check_test_tier1_sse_source,
                                  check_test_tier1_sse_other_source, "-march=x86-64-v2", host, 0);
}

// The VEX fixture (#1281): x86-64-v3's AVX2 and F16C builtins and the vector operators, at a target without AVX-512,
// whose vectors Tier 1 moves, spills and computes by VEX forms in xmm0 to xmm15 alone and whose machine rows' roles it
// places below 16: the 32-bit integers' operators (the IR's own operations by their VEX forms) and their comparisons,
// shifts and complements (machine rows), on 16- and 32-byte vectors; the operators on words, bytes, quadwords, floats and
// doubles (machine rows: the floats' splat of a scalar names a floating operand in a VEX form); AVX2's shuffles, blends,
// permutes, shifts by counts and by vectors, unpacks and maxima, tests and movemasks, extracts and inserts of 128-bit
// lanes, widening conversions, masked loads and stores and gathers (whose mask the row writes); F16C's conversions of
// scalars (_cvtsh_ss's float result and _cvtss_sh's float operand in VEX forms) and of vectors; FMA's; vector constants
// of zeros, every bit, splats and others; a loop whose vectors cross its back edge and live across a call; and more
// vectors live at once than xmm0 to xmm14, spilled. Its other unit (clang -O2's) hashes the results. Each expression
// that clang would contract into an FMA (-ffp-contract=on, even at -O0) is split into statements. Its SSE carriers
// (#1280: _mm_cvtph_ps's and _mm256_cvtepu8_epi32's 8-byte operands) move by VEX's vmovq and vinsertps.
static const char check_test_tier1_vex_header[] =
    "#include <immintrin.h>\n"
    "#include <stdint.h>\n"
    "typedef int v8si __attribute__((vector_size(32)));\n"
    "typedef int v4si __attribute__((vector_size(16)));\n"
    "typedef short v16hi __attribute__((vector_size(32)));\n"
    "typedef signed char v16qi __attribute__((vector_size(16)));\n"
    "typedef long long v4di __attribute__((vector_size(32)));\n"
    "typedef float v8sf __attribute__((vector_size(32)));\n"
    "typedef double v2df __attribute__((vector_size(16)));\n"
    "v8si x_ints(v8si a, v8si b, int n);\n"
    "v4si x_small(v4si a, v4si b);\n"
    "v16hi x_words(v16hi a, v16hi b);\n"
    "v16qi x_bytes(v16qi a, v16qi b);\n"
    "v4di x_quads(v4di a, v4di b);\n"
    "v8sf x_floats(v8sf a, v8sf b, float s);\n"
    "v2df x_doubles(v2df a, v2df b);\n"
    "__m256i x_builtins(__m256i a, __m256i b, __m256i m, int n);\n"
    "int x_tests(__m256i a, __m256i b, __m256i m);\n"
    "__m256i x_lanes(__m256i a, __m128i l, int i, long long g);\n"
    "void x_memory(int* out, const int* in, __m256i m);\n"
    "float x_half(unsigned short h, float f, float g);\n"
    "__m256 x_fma(__m256 a, __m256 b, __m256 c);\n"
    "uint32_t x_loop(const unsigned char* p, unsigned n, unsigned char* out);\n"
    "__m256i x_pressure(const int* p);\n"
    "int b_tick(int k);\n";

static const char check_test_tier1_vex_source[] =
    "#include \"tier1-vex.h\"\n"
    "v8si x_ints(v8si a, v8si b, int n)\n"
    "{\n"
    "    v8si s = (a + b) * a - b;\n"
    "    v8si t = (s & b) | (a ^ s);\n"
    "    v8si c = (a < b) + (s == t) - (b >= s);\n"
    "    v8si u = (t << 3) + (s >> n) + ~c - -a;\n"
    "    return u + (v8si){ 1, -2, 3, -4, 5, -6, 7, -8 } + n;\n"
    "}\n"
    "v4si x_small(v4si a, v4si b)\n"
    "{\n"
    "    v4si z = { 0, 0, 0, 0 };\n"
    "    v4si o = { -1, -1, -1, -1 };\n"
    "    return (a * b + z) ^ (o & (a - b)) ^ (a > b) ^ (v4si){ 9, 9, 9, 9 };\n"
    "}\n"
    "v16hi x_words(v16hi a, v16hi b) { return (a + b) * (a - b) ^ (a > b); }\n"
    "v16qi x_bytes(v16qi a, v16qi b) { return (a + b) - (a == b) + (b + b + b); }\n"
    "v4di x_quads(v4di a, v4di b) { return (a - b) + (a < b) + (b & 7); }\n"
    "v8sf x_floats(v8sf a, v8sf b, float s)\n"
    "{\n"
    "    v8sf p = a * b;\n"
    "    return p + s - b / (a + 3.0f);\n"
    "}\n"
    "v2df x_doubles(v2df a, v2df b)\n"
    "{\n"
    "    v2df p = a * b;\n"
    "    return p - a + (v2df){ 0.5, -0.25 };\n"
    "}\n"
    "__m256i x_builtins(__m256i a, __m256i b, __m256i m, int n)\n"
    "{\n"
    "    __m256i x = _mm256_add_epi8(a, _mm256_shuffle_epi8(a, b));\n"
    "    __m256i y = _mm256_blendv_epi8(x, b, m);\n"
    "    __m256i z = _mm256_permutevar8x32_epi32(y, a);\n"
    "    __m256i w = _mm256_srli_epi16(z, n);\n"
    "    __m256i v = _mm256_sllv_epi32(b, _mm256_and_si256(a, _mm256_set1_epi32(31)));\n"
    "    __m256i u = _mm256_max_epu8(_mm256_cmpgt_epi32(a, b), _mm256_sub_epi64(w, v));\n"
    "    __m256i t = _mm256_mullo_epi32(_mm256_add_epi32(u, _mm256_set1_epi32(n)), _mm256_or_si256(a, _mm256_setzero_si256()));\n"
    "    return _mm256_xor_si256(t, _mm256_unpacklo_epi8(u, _mm256_set1_epi32(-1)));\n"
    "}\n"
    "int x_tests(__m256i a, __m256i b, __m256i m)\n"
    "{\n"
    "    __m256i x = _mm256_blendv_epi8(a, b, m);\n"
    "    int mask = _mm256_movemask_epi8(x);\n"
    "    return mask ^ _mm256_testz_si256(a, b) ^ (_mm256_testc_si256(x, m) << 1) ^ (_mm256_movemask_epi8(_mm256_add_epi8(x, m)) << 3) ^ _mm_cvtsi128_si32(_mm256_castsi256_si128(x));\n"
    "}\n"
    "__m256i x_lanes(__m256i a, __m128i l, int i, long long g)\n"
    "{\n"
    "    __m128i e = _mm256_extracti128_si256(a, 1);\n"
    "    __m256i x = _mm256_inserti128_si256(a, _mm_add_epi32(l, e), 0);\n"
    "    __m256i y = _mm256_add_epi64(x, _mm256_set1_epi64x(g));\n"
    "    __m256i z = _mm256_cvtepu8_epi16(_mm_xor_si128(e, _mm_set1_epi32(i)));\n"
    "    z = _mm256_add_epi32(z, _mm256_cvtepu8_epi32(l));\n"
    "    return _mm256_sub_epi16(_mm256_add_epi32(y, _mm256_set1_epi32(i)), z);\n"
    "}\n"
    "void x_memory(int* out, const int* in, __m256i m)\n"
    "{\n"
    "    __m256i a = _mm256_maskload_epi32(in, m);\n"
    "    __m256i b = _mm256_loadu_si256((const __m256i*)(in + 3));\n"
    "    __m256i g = _mm256_i32gather_epi32(in, _mm256_and_si256(b, _mm256_set1_epi32(15)), 4);\n"
    "    _mm256_maskstore_epi32(out, m, _mm256_add_epi32(a, b));\n"
    "    _mm256_storeu_si256((__m256i*)(out + 8), g);\n"
    "    _mm_storeu_si128((__m128i*)(out + 16), _mm_mask_i32gather_epi32(_mm_set1_epi32(-7), in, _mm_set_epi32(1, 5, 2, 7), _mm256_castsi256_si128(m), 4));\n"
    "}\n"
    "float x_half(unsigned short h, float f, float g)\n"
    "{\n"
    "    float a = _cvtsh_ss(h);\n"
    "    unsigned short n = _cvtss_sh(f * g, 0);\n"
    "    __m256 v = _mm256_cvtph_ps(_mm_set1_epi16((short)n));\n"
    "    __m128 q = _mm_cvtph_ps(_mm_set1_epi16((short)(n ^ 0x0400)));\n"
    "    __m128i p = _mm_cvtps_ph(_mm_set_ps(f, g, a, 1.5f), 0);\n"
    "    return a + _cvtsh_ss(n) + _mm256_cvtss_f32(v) + _mm_cvtss_f32(q) + (float)_mm_extract_epi16(p, 1) + _cvtsh_ss((unsigned short)_mm_extract_epi16(p, 2));\n"
    "}\n"
    "__m256 x_fma(__m256 a, __m256 b, __m256 c) { return _mm256_fmadd_ps(a, b, _mm256_fmsub_ps(b, c, a)); }\n"
    "uint32_t x_loop(const unsigned char* p, unsigned n, unsigned char* out)\n"
    "{\n"
    "    __m256i acc = _mm256_setzero_si256();\n"
    "    __m256i ones = _mm256_set1_epi32(0x01010101);\n"
    "    v8si total = { 0, 0, 0, 0, 0, 0, 0, 0 };\n"
    "    uint32_t count = 0;\n"
    "    for (unsigned i = 0; i + 32 <= n; i += 32)\n"
    "    {\n"
    "        __m256i v = _mm256_loadu_si256((const __m256i*)(p + i));\n"
    "        __m256i k = _mm256_cmpeq_epi8(v, _mm256_set1_epi32(0x61616161));\n"
    "        count += (uint32_t)__builtin_popcount((unsigned)_mm256_movemask_epi8(k)) + (uint32_t)b_tick((int)i);\n"
    "        acc = _mm256_add_epi8(acc, _mm256_and_si256(k, ones));\n"
    "        acc = _mm256_max_epu8(acc, v);\n"
    "        total += (v8si)v;\n"
    "    }\n"
    "    _mm256_storeu_si256((__m256i*)out, acc);\n"
    "    return count + (uint32_t)(total[0] ^ total[7]);\n"
    "}\n"
    "__m256i x_pressure(const int* p)\n"
    "{\n"
    "    v8si v[18];\n"
    "    for (int i = 0; i < 18; i += 1) v[i] = (v8si){ p[i], p[i + 1], p[i + 2], p[i + 3], p[i + 4], p[i + 5], p[i + 6], p[i + 7] };\n"
    "    v8si a = v[0] * v[1] + v[2] * v[3] + v[4] * v[5] + v[6] * v[7] + v[8] * v[9];\n"
    "    v8si b = v[10] * v[11] + v[12] * v[13] + v[14] * v[15] + v[16] * v[17];\n"
    "    v8si c = (v[0] + v[1] + v[2] + v[3] + v[4] + v[5] + v[6] + v[7] + v[8]) ^ (v[9] + v[10] + v[11] + v[12] + v[13] + v[14] + v[15] + v[16] + v[17]);\n"
    "    return (__m256i)(a - b + c);\n"
    "}\n";

static const char check_test_tier1_vex_other_source[] =
    "#include <stdio.h>\n"
    "#include <string.h>\n"
    "#include \"tier1-vex.h\"\n"
    "static uint64_t hash = 0xcbf29ce484222325ull;\n"
    "static void mix(const void* p, unsigned n) { const unsigned char* b = p; for (unsigned i = 0; i < n; i += 1) hash = (hash ^ b[i]) * 0x100000001b3ull; }\n"
    "static int ticks = 0;\n"
    "int b_tick(int k) { ticks += 1; return k & 3; }\n"
    "int main(void)\n"
    "{\n"
    "    unsigned char in[512];\n"
    "    unsigned char out[256];\n"
    "    for (int round = 0; round < 4; round += 1)\n"
    "    {\n"
    "        for (unsigned i = 0; i < sizeof(in); i += 1) in[i] = (unsigned char)(i * 37u + (i >> 3) * 11u + round * 101u + (i % 7 == 0 ? 'a' : 0));\n"
    "        memset(out, 0, sizeof(out));\n"
    "        __m256i a = _mm256_loadu_si256((const void*)in);\n"
    "        __m256i b = _mm256_loadu_si256((const void*)(in + 32));\n"
    "        __m256i c = _mm256_loadu_si256((const void*)(in + 64));\n"
    "        v8si i8 = x_ints((v8si)a, (v8si)b, 1 + round);\n"
    "        mix(&i8, 32);\n"
    "        v4si i4 = x_small((v4si)_mm256_castsi256_si128(a), (v4si)_mm256_castsi256_si128(c));\n"
    "        mix(&i4, 16);\n"
    "        v16hi w = x_words((v16hi)b, (v16hi)c);\n"
    "        mix(&w, 32);\n"
    "        v16qi q = x_bytes((v16qi)_mm256_castsi256_si128(b), (v16qi)_mm256_castsi256_si128(a));\n"
    "        mix(&q, 16);\n"
    "        v4di d = x_quads((v4di)a, (v4di)c);\n"
    "        mix(&d, 32);\n"
    "        v8sf f = x_floats(_mm256_cvtepi32_ps(_mm256_and_si256(a, _mm256_set1_epi32(0xffff))), _mm256_cvtepi32_ps(_mm256_and_si256(b, _mm256_set1_epi32(0xfff))), 0.75f * round);\n"
    "        mix(&f, 32);\n"
    "        v2df e = x_doubles((v2df){ in[3] * 0.5, in[9] * -1.25 }, (v2df){ in[11], round + 0.5 });\n"
    "        mix(&e, 16);\n"
    "        __m256i r = x_builtins(a, b, c, 3 + round);\n"
    "        mix(&r, 32);\n"
    "        int t = x_tests(a, b, c);\n"
    "        mix(&t, 4);\n"
    "        r = x_lanes(a, _mm256_castsi256_si128(c), -77 + round, 0x123456789abcll * (round + 1));\n"
    "        mix(&r, 32);\n"
    "        int words[64];\n"
    "        for (int k = 0; k < 64; k += 1) words[k] = (int)(in[k] * 131u + (unsigned)round);\n"
    "        int results[24];\n"
    "        memset(results, 0, sizeof(results));\n"
    "        x_memory(results, words, _mm256_setr_epi32(-1, 0, -1, -1, 0, 0, -1, round & 1 ? -1 : 0));\n"
    "        mix(results, sizeof(results));\n"
    "        float h = x_half((unsigned short)(0x3c00 + round * 77), 1.5f + round, -0.375f * round);\n"
    "        mix(&h, 4);\n"
    "        __m256 m = x_fma(_mm256_set1_ps(1.25f + round), _mm256_cvtepi32_ps(_mm256_and_si256(c, _mm256_set1_epi32(255))), _mm256_set1_ps(-3.5f));\n"
    "        mix(&m, 32);\n"
    "        uint32_t k = x_loop(in, sizeof(in), out);\n"
    "        mix(&k, 4);\n"
    "        mix(out, 32);\n"
    "        r = x_pressure(words + round);\n"
    "        mix(&r, 32);\n"
    "        printf(\"%016llx %u %d\\n\", (unsigned long long)hash, k, ticks);\n"
    "    }\n"
    "    return 0;\n"
    "}\n";

// The SSE carriers' fixture (#1280): vectors of 4 and 8 bytes, an f32 or f64 in the IR, in and out of the machine rows
// that read and make them. The intrinsics' widening conversions of each width (cvtepu8, cvtepi8, cvtepu16 and cvtepi16
// into 16-, 32- and 64-byte vectors, whose shuffles make the narrow operand); loadl and storel, loadu and storeu of 4
// and 8 bytes, move_epi64 and the float halves' loadl_pi and storel_pi; the movq forms (cvtsi64_si128, cvtsi128_si64,
// movepi64_pi64 and movpi64_epi64, MMX's cvtsi64_m64 and cvtm64_si64); MMX's __m64 operations, passed by value to and
// from clang -O2's unit; GNU C's operators on vectors of 8 and 4 bytes; constants (a signaling NaN's and -0's bits,
// which no floating move may change); a loop whose carriers cross its back edge; and, under FENV_ACCESS (where clang
// computes a narrow float vector one element at a time), a product of two-float shuffles of vectors whose other lanes
// would overflow, which raises no exception only where the carriers' registers are zero past their bytes, as Tier 0's
// are.
static const char check_test_tier1_carriers_header[] =
    "#include <immintrin.h>\n"
    "#include <stdint.h>\n"
    "__m256i s_widen(__m128i a, __m128i b);\n"
    "__m512i s_widen512(__m128i a, __m128i b);\n"
    "__m128i s_widen128(__m128i a, __m128i b);\n"
    "void s_memory(unsigned char* out, const unsigned char* in);\n"
    "long long s_movq(__m128i a, long long x);\n"
    "long long s_mmx(long long x, long long y, int n);\n"
    "__m64 s_m64(__m64 a, __m64 b);\n"
    "uint64_t s_operators(uint64_t x, uint32_t y);\n"
    "uint64_t s_constants(void);\n"
    "uint64_t s_loop(const unsigned char* p, unsigned n);\n"
    "int s_flags(__m128 a, __m128 b, float* out);\n";

static const char check_test_tier1_carriers_source[] =
    "#include \"tier1-carriers.h\"\n"
    "typedef unsigned char v8u8 __attribute__((vector_size(8)));\n"
    "typedef short v4i16 __attribute__((vector_size(8)));\n"
    "typedef unsigned char v4u8 __attribute__((vector_size(4)));\n"
    "typedef float v2f32 __attribute__((vector_size(8)));\n"
    "typedef float v4f32 __attribute__((vector_size(16)));\n"
    "__m256i s_widen(__m128i a, __m128i b)\n"
    "{\n"
    "    __m256i x = _mm256_cvtepu8_epi32(a);\n"
    "    __m256i y = _mm256_cvtepi8_epi32(_mm_srli_si128(a, 8));\n"
    "    __m256i z = _mm256_cvtepu16_epi64(b);\n"
    "    __m256i w = _mm256_cvtepu8_epi64(_mm_srli_si128(b, 3));\n"
    "    return _mm256_xor_si256(_mm256_add_epi32(x, y), _mm256_sub_epi64(z, w));\n"
    "}\n"
    "__m512i s_widen512(__m128i a, __m128i b)\n"
    "{\n"
    "    __m512i x = _mm512_cvtepu8_epi64(a);\n"
    "    __m512i y = _mm512_cvtepi8_epi64(b);\n"
    "    return _mm512_add_epi64(x, y);\n"
    "}\n"
    "__m128i s_widen128(__m128i a, __m128i b)\n"
    "{\n"
    "    __m128i x = _mm_cvtepu8_epi32(a);\n"
    "    __m128i y = _mm_cvtepu16_epi32(b);\n"
    "    __m128i z = _mm_cvtepi8_epi16(_mm_srli_si128(a, 5));\n"
    "    __m128i w = _mm_cvtepu16_epi64(b);\n"
    "    __m128i v = _mm_cvtepi16_epi64(_mm_srli_si128(b, 7));\n"
    "    __m128i u = _mm_cvtepu8_epi64(a);\n"
    "    return _mm_add_epi32(_mm_xor_si128(x, y), _mm_add_epi64(_mm_sub_epi16(z, w), _mm_xor_si128(v, u)));\n"
    "}\n"
    "void s_memory(unsigned char* out, const unsigned char* in)\n"
    "{\n"
    "    __m128i l = _mm_loadl_epi64((const __m128i*)(in + 3));\n"
    "    _mm_storel_epi64((__m128i*)out, _mm_add_epi8(l, _mm_set1_epi8(1)));\n"
    "    __m128i m = _mm_move_epi64(_mm_loadu_si128((const __m128i*)(in + 20)));\n"
    "    _mm_storeu_si128((__m128i*)(out + 8), m);\n"
    "    __m128i s = _mm_loadu_si64(in + 41);\n"
    "    _mm_storeu_si64(out + 24, _mm_cvtepu8_epi16(s));\n"
    "    __m128i t = _mm_loadu_si32(in + 50);\n"
    "    _mm_storeu_si32(out + 32, _mm_cvtepu8_epi32(t));\n"
    "    _mm_storeu_si128((__m128i*)(out + 36), _mm_cvtepu8_epi32(t));\n"
    "    __m128 f = _mm_loadl_pi(_mm_set1_ps(2.5f), (const __m64*)(in + 60));\n"
    "    _mm_storel_pi((__m64*)(out + 52), _mm_shuffle_ps(f, f, 0x1b));\n"
    "    _mm_storeu_ps((float*)(out + 60), f);\n"
    "}\n"
    "long long s_movq(__m128i a, long long x)\n"
    "{\n"
    "    __m128i b = _mm_add_epi64(_mm_move_epi64(a), _mm_cvtsi64_si128(x));\n"
    "    __m64 m = _mm_movepi64_pi64(b);\n"
    "    __m128i c = _mm_movpi64_epi64(m);\n"
    "    return _mm_cvtsi128_si64(c) ^ _mm_cvtm64_si64(_mm_cvtsi64_m64(x * 3)) ^ (long long)_mm_cvtsi128_si32(_mm_cvtsi32_si128((int)x));\n"
    "}\n"
    "long long s_mmx(long long x, long long y, int n)\n"
    "{\n"
    "    __m64 a = _mm_cvtsi64_m64(x);\n"
    "    __m64 b = _mm_cvtsi64_m64(y);\n"
    "    __m64 c = _mm_add_pi16(a, b);\n"
    "    __m64 d = _mm_unpacklo_pi16(c, a);\n"
    "    __m64 e = _mm_sub_pi32(d, _mm_set_pi16(1, -2, 3, -4));\n"
    "    __m64 f = _mm_mullo_pi16(e, b);\n"
    "    __m64 g = _mm_slli_pi16(f, n);\n"
    "    __m64 h = _mm_cmpeq_pi8(g, _mm_and_si64(a, b));\n"
    "    __m64 i = _mm_sad_pu8(a, b);\n"
    "    __m64 j = _mm_shuffle_pi16(g, 0x4e);\n"
    "    __m64 k = _mm_packs_pi16(c, j);\n"
    "    return _mm_cvtm64_si64(_mm_xor_si64(_mm_or_si64(h, i), _mm_add_pi8(k, _mm_set1_pi8((char)n))));\n"
    "}\n"
    "__m64 s_m64(__m64 a, __m64 b)\n"
    "{\n"
    "    return _mm_adds_pu8(_mm_avg_pu8(a, b), _mm_max_pi16(a, b));\n"
    "}\n"
    "uint64_t s_operators(uint64_t x, uint32_t y)\n"
    "{\n"
    "    v8u8 a;\n"
    "    v4u8 b;\n"
    "    __builtin_memcpy(&a, &x, 8);\n"
    "    __builtin_memcpy(&b, &y, 4);\n"
    "    v8u8 c = (a + (v8u8){ 1, 2, 3, 4, 5, 6, 7, 8 }) ^ (a & 0x7e);\n"
    "    v4u8 d = (b + (v4u8){ 3, 5, 7, 9 }) - (b & 0x0f);\n"
    "    v4i16 e = (v4i16)c;\n"
    "    v4i16 less = e < (v4i16){ 0, 100, -100, 7 };\n"
    "    e = (e & less) | (-e & ~less);\n"
    "    uint64_t r;\n"
    "    uint32_t s;\n"
    "    __builtin_memcpy(&r, &e, 8);\n"
    "    __builtin_memcpy(&s, &d, 4);\n"
    "    return r ^ ((uint64_t)s << 7);\n"
    "}\n"
    "uint64_t s_constants(void)\n"
    "{\n"
    "    volatile v2f32 nan = (v2f32){ __builtin_nansf(\"0x12345\"), -0.0f };\n"
    "    volatile v4u8 bytes = (v4u8){ 0xff, 0x80, 0x01, 0x7f };\n"
    "    v2f32 n = nan;\n"
    "    v4u8 b = bytes;\n"
    "    uint64_t bits;\n"
    "    uint32_t more;\n"
    "    __builtin_memcpy(&bits, &n, 8);\n"
    "    __builtin_memcpy(&more, &b, 4);\n"
    "    return bits ^ more;\n"
    "}\n"
    "uint64_t s_loop(const unsigned char* p, unsigned n)\n"
    "{\n"
    "    v4i16 acc = { 0 };\n"
    "    v4u8 low = { 0 };\n"
    "    for (unsigned i = 0; i + 8 <= n; i += 8)\n"
    "    {\n"
    "        v4i16 v;\n"
    "        __builtin_memcpy(&v, p + i, 8);\n"
    "        acc = acc + (v >> 2);\n"
    "        v4u8 w;\n"
    "        __builtin_memcpy(&w, p + i + 1, 4);\n"
    "        low = low ^ w;\n"
    "    }\n"
    "    uint64_t r;\n"
    "    uint32_t s;\n"
    "    __builtin_memcpy(&r, &acc, 8);\n"
    "    __builtin_memcpy(&s, &low, 4);\n"
    "    return r + s;\n"
    "}\n"
    "int s_flags(__m128 a, __m128 b, float* out)\n"
    "{\n"
    "#pragma STDC FENV_ACCESS ON\n"
    "    _mm_setcsr(_mm_getcsr() & ~0x3fu);\n"
    "    v4f32 x = (v4f32)a;\n"
    "    v4f32 y = (v4f32)b;\n"
    "    v2f32 p = __builtin_shufflevector(x, x, 0, 1) * __builtin_shufflevector(y, y, 0, 1) + __builtin_shufflevector(x, x, 1, 0);\n"
    "    int flags = (int)(_mm_getcsr() & 0x3fu);\n"
    "    __builtin_memcpy(out, &p, 8);\n"
    "    return flags;\n"
    "}\n";

static const char check_test_tier1_carriers_other_source[] =
    "#include <stdio.h>\n"
    "#include <string.h>\n"
    "#include \"tier1-carriers.h\"\n"
    "static uint64_t hash = 0xcbf29ce484222325ull;\n"
    "static void mix(const void* p, unsigned n) { const unsigned char* b = p; for (unsigned i = 0; i < n; i += 1) hash = (hash ^ b[i]) * 0x100000001b3ull; }\n"
    "int main(void)\n"
    "{\n"
    "    unsigned char in[256];\n"
    "    unsigned char out[128];\n"
    "    for (int round = 0; round < 4; round += 1)\n"
    "    {\n"
    "        for (unsigned i = 0; i < sizeof(in); i += 1) in[i] = (unsigned char)(i * 53u + (i >> 2) * 7u + round * 89u);\n"
    "        memset(out, 0, sizeof(out));\n"
    "        __m128i a = _mm_loadu_si128((const __m128i*)in);\n"
    "        __m128i b = _mm_loadu_si128((const __m128i*)(in + 16));\n"
    "        __m256i w = s_widen(a, b);\n"
    "        mix(&w, 32);\n"
    "        __m512i z = s_widen512(a, b);\n"
    "        mix(&z, 64);\n"
    "        __m128i x = s_widen128(a, b);\n"
    "        mix(&x, 16);\n"
    "        s_memory(out, in + round);\n"
    "        mix(out, sizeof(out));\n"
    "        long long q = s_movq(b, 0x0123456789abcdefll * (round + 1));\n"
    "        mix(&q, 8);\n"
    "        long long m = s_mmx(0x7f00ff0180017ffell + round, -0x123456789abcll * (round + 3), round + 1);\n"
    "        mix(&m, 8);\n"
    "        __m64 n = s_m64(_mm_cvtsi64_m64(0x1122334455667788ll + round), _mm_cvtsi64_m64(-0x0102030405060708ll));\n"
    "        long long nn = _mm_cvtm64_si64(n);\n"
    "        mix(&nn, 8);\n"
    "        uint64_t o = s_operators(0x8899aabbccddeeffull * (round + 1), 0xdeadbeefu + round);\n"
    "        mix(&o, 8);\n"
    "        uint64_t c = s_constants();\n"
    "        mix(&c, 8);\n"
    "        uint64_t l = s_loop(in, sizeof(in) - round * 9);\n"
    "        mix(&l, 8);\n"
    "        float f[2];\n"
    "        int flags = s_flags(_mm_setr_ps(1.5f + round, -2.0f, 1e30f, 1e30f), _mm_setr_ps(3.0f, 0.25f, 1e30f, 0.0f / 0.0f), f);\n"
    "        mix(&flags, 4);\n"
    "        mix(f, 8);\n"
    "        printf(\"%016llx %llx %d\\n\", (unsigned long long)hash, (unsigned long long)c, flags);\n"
    "    }\n"
    "    return 0;\n"
    "}\n";

// Its builds, by x86-64-v3, run where the host has every feature of it.
static void check_test_driver_tier1_vex(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    X86Target v3 = { 0 };
    const bool parsed = x86_target_parse("x86-64-v3", 9, &v3);
    const bool host = parsed && (x86_target_native().features & v3.features) == v3.features;
    check_test_driver_tier1_units(test, clang, executable, directory, "tier1-vex", "VEX vectors'", check_test_tier1_vex_header, check_test_tier1_vex_source, check_test_tier1_vex_other_source,
                                  "-march=x86-64-v3", host, 0);
    check_test_driver_tier1_staging(test, clang, executable, directory, "-march=x86-64-v3", host);
}

// The AVX fixture (#1281's review): Sandy Bridge's AVX without AVX2, whose ymm integer forms Tier 1 doesn't take: 32-byte
// vectors' bitwise operators and constants of every kind (AVX's vandps, vorps, vxorps, vpcmpeqd, vpshufd and vinsertf128), 16-byte
// vectors' arithmetic, and AVX's float builtins, every function Tier 1's.
static const char check_test_tier1_avx_header[] =
    "#include <immintrin.h>\n"
    "typedef int a8 __attribute__((vector_size(32)));\n"
    "typedef int a4 __attribute__((vector_size(16)));\n"
    "a8 x_bits(a8 a, a8 b, int n);\n"
    "a4 x_small(a4 a, a4 b);\n"
    "__m256 x_floats(__m256 a, __m256 b);\n";

static const char check_test_tier1_avx_source[] =
    "#include \"tier1-avx.h\"\n"
    "a8 x_bits(a8 a, a8 b, int n)\n"
    "{\n"
    "    a8 ones = { -1, -1, -1, -1, -1, -1, -1, -1 };\n"
    "    a8 seven = { 7, 7, 7, 7, 7, 7, 7, 7 };\n"
    "    a8 zero = { 0, 0, 0, 0, 0, 0, 0, 0 };\n"
    "    a8 other = { 1, 2, 3, 4, 5, 6, 7, 8 };\n"
    "    a8 x = ((a & b) | (a ^ seven)) ^ (ones & b);\n"
    "    for (int i = 0; i < n; i += 1) x = (x ^ other) | zero;\n"
    "    return x;\n"
    "}\n"
    "a4 x_small(a4 a, a4 b) { return a * b + a - (a4){ 1, 2, 3, 4 }; }\n"
    "__m256 x_floats(__m256 a, __m256 b) { return _mm256_add_ps(_mm256_and_ps(a, b), _mm256_set1_ps(1.5f)); }\n";

static const char check_test_tier1_avx_other_source[] =
    "#include <stdio.h>\n"
    "#include \"tier1-avx.h\"\n"
    "int main(void)\n"
    "{\n"
    "    unsigned long long hash = 0xcbf29ce484222325ull;\n"
    "    for (int round = 0; round < 4; round += 1)\n"
    "    {\n"
    "        a8 a = { round, 3, -5, 7 * round, 11, -13, 17, 19 };\n"
    "        a8 b = { 23, round * 29, 31, -37, 41, 43, -47, round };\n"
    "        a8 r = x_bits(a, b, round);\n"
    "        a4 s = x_small((a4){ round, 5, -7, 9 }, (a4){ 2, -3, round, 4 });\n"
    "        __m256 f = x_floats(_mm256_set1_ps(2.5f * round), _mm256_set1_ps(-3.25f));\n"
    "        const unsigned char* bytes[3] = { (const unsigned char*)&r, (const unsigned char*)&s, (const unsigned char*)&f };\n"
    "        const unsigned sizes[3] = { 32, 16, 32 };\n"
    "        for (int k = 0; k < 3; k += 1) for (unsigned i = 0; i < sizes[k]; i += 1) hash = (hash ^ bytes[k][i]) * 0x100000001b3ull;\n"
    "        printf(\"%016llx\\n\", hash);\n"
    "    }\n"
    "    return 0;\n"
    "}\n";

// Its builds, by Sandy Bridge, run where the host has AVX.
static void check_test_driver_tier1_avx(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    // x86-64-v2 with AVX, as Sandy Bridge is.
    X86Target v2 = { 0 };
    const bool parsed = x86_target_parse("x86-64-v2", 9, &v2);
    const uint64_t features = v2.features | BUSTER_X86_FEATURE(AVX);
    const bool host = parsed && (x86_target_native().features & features) == features;
    check_test_driver_tier1_units(test, clang, executable, directory, "tier1-avx", "AVX vectors'", check_test_tier1_avx_header, check_test_tier1_avx_source, check_test_tier1_avx_other_source,
                                  "-march=sandybridge", host, 0);
}

// Its builds, by x86-64-v4, run where the host has every feature of it.
static void check_test_driver_tier1_carriers(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    X86Target v4 = { 0 };
    const bool parsed = x86_target_parse("x86-64-v4", 9, &v4);
    const bool host = parsed && (x86_target_native().features & v4.features) == v4.features;
    check_test_driver_tier1_units(test, clang, executable, directory, "tier1-carriers", "SSE carriers'", check_test_tier1_carriers_header, check_test_tier1_carriers_source,
                                  check_test_tier1_carriers_other_source, "-march=x86-64-v4", host, 0);
}

// A machine row with a floating role in a VEX form (#141, #1281): F16C's half-precision conversions at x86-64-v3, whose
// rows name the float in an XMM field that only a register below 16 holds. Tier 1 places such a role below 16
// (tier1_emit_cfg_row): by each path, the build by --tier1 compiles every function, _cvtsh_ss's among them, by Tier 1,
// and where the host has x86-64-v3, it runs as clang -O0's build does.
static const char check_test_tier1_half_source[] =
    "#include <immintrin.h>\n"
    "#include <stdio.h>\n"
    "__attribute__((noinline)) float widen(unsigned short h) { return _cvtsh_ss(h); }\n"
    "__attribute__((noinline)) unsigned short narrow(float f) { return _cvtss_sh(f, 0); }\n"
    "int main(void)\n"
    "{\n"
    "    float s = 0;\n"
    "    for (int i = 0; i < 1000; i += 1) s += widen(narrow((float)i * 0.37f));\n"
    "    printf(\"%.3f %u\\n\", s, narrow(3.25f));\n"
    "    return 0;\n"
    "}\n";

static void check_test_driver_tier1_half(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    X86Target v3 = { 0 };
    const bool parsed = x86_target_parse("x86-64-v3", 9, &v3);
    const bool host = parsed && (x86_target_native().features & v3.features) == v3.features;
    char unit[512];
    char reference[512];
    snprintf(unit, sizeof(unit), "%s/tier1-half.c", directory);
    snprintf(reference, sizeof(reference), "%s/tier1-half-clang", directory);
    const bool written = parse_test_write(unit, check_test_tier1_half_source, strlen(check_test_tier1_half_source));
    char* reference_arguments[] = { (char*)clang, "-O0", "-march=x86-64-v3", "-o", reference, unit, NULL };
    char* text = NULL;
    const bool referenced = written && check_test_driver_spawn(reference_arguments, &text) == 0;
    free(text);
    char expected[256] = { 0 };
    const bool ran = referenced && (!host || (check_test_driver_capture(reference, expected, sizeof(expected)) && expected[0] != 0));
    check_test_expect(test, ran, "clang's build of the half-precision fixture runs");
    for (uint32_t path = 0; ran && path < 2; path += 1)
    {
        char program[512];
        char message[256];
        snprintf(program, sizeof(program), "%s/tier1-half-%u", directory, path);
        char* arguments[10] = { (char*)executable, "--tier1", "--tier1-report", "--tier1-refusals", "-march=x86-64-v3" };
        uint32_t count = 5;
        if (path != 0)
        {
            arguments[count++] = "--scalar";
        }
        arguments[count++] = "-o";
        arguments[count++] = program;
        arguments[count++] = unit;
        arguments[count] = NULL;
        text = NULL;
        const bool built = check_test_driver_spawn(arguments, &text) == 0;
        const bool taken = built && text != NULL && strstr(text, "tier1-refused") == NULL && strstr(text, "fallbacks=0\t") != NULL;
        free(text);
        snprintf(message, sizeof(message), "a row with a floating role in a VEX form is placed below 16, and every function is Tier 1's (path %u)", path);
        check_test_expect(test, taken, message);
        char actual[256] = { 0 };
        snprintf(message, sizeof(message), "the half-precision fixture by --tier1 runs as clang -O0's build does (path %u)", path);
        check_test_expect(test, built && (!host || (check_test_driver_capture(program, actual, sizeof(actual)) && strcmp(actual, expected) == 0)), message);
        unlink(program);
    }
    unlink(reference);
    unlink(unit);
}

// #1282's inlining: the unit's always_inline functions of internal linkage expanded into their callers by --tier1, the
// x86 intrinsics' wrappers above all, whose vectors pass and return by value: their byte-wise copies into fresh slots,
// split by SROA and promoted. A static always_inline helper of a struct by value (copies by integers), and a static
// noinline one, which stays a call, beside them. #2541's helpers with control flow, whose blocks the general splice
// places at their calls: two returns, a loop, a switch, a vector by value chosen by a branch, a local array in a loop,
// and one that calls two of them. #2541 step 1's small class, static functions without an inline attribute, from their
// summaries: one with two returns, one with a loop that calls it (its summary expands it first), and two that call each
// other, which stay calls. By each path, with --verify's certificates, the build inlines (the report's `inlined`), and
// runs as clang -O0's build and as --tier1-no-inline's do, where the host has x86-64-v4; and by each path, its object
// by one lane is its object by eight.
static const char check_test_tier1_inline_source[] =
    "#include <immintrin.h>\n"
    "#include <stdint.h>\n"
    "#include <stdio.h>\n"
    "typedef struct { long long a; int b; short c; } Triple;\n"
    "static inline __attribute__((always_inline)) Triple twist(Triple t, long long k) { Triple r = { t.a * 3 + k, t.b ^ (int)k, (short)(t.c + 1) }; return r; }\n"
    "static __attribute__((noinline)) int kept(int x) { return x * 7; }\n"
    "static inline __attribute__((always_inline)) int clamp(int x, int lo, int hi) { if (x < lo) return lo; if (x > hi) return hi; return x; }\n"
    "static inline __attribute__((always_inline)) unsigned spin(unsigned x, int n) { for (int i = 0; i < n; i += 1) x = x * 33u + (unsigned)i; return x; }\n"
    "static inline __attribute__((always_inline)) int classify(int x) { switch (x & 3) { case 0: return 10; case 1: return x * 2; case 2: return -x; default: return x ^ 5; } }\n"
    "static inline __attribute__((always_inline)) __m128i pick(__m128i a, __m128i b, int k) { if (k & 1) return _mm_add_epi32(a, b); return _mm_xor_si128(a, b); }\n"
    "static inline __attribute__((always_inline)) long walk(const int* p, int n) { int w[4] = { 1, 2, 3, 4 }; long s = 0; for (int i = 0; i < n; i += 1) s += p[i] * w[i & 3]; return s; }\n"
    "static inline __attribute__((always_inline)) int twice(int x) { return clamp(x, 0, 9) + classify(x); }\n"
    "static int bounce(int x) { if (x & 1) return x * 3 + 1; return x / 2; }\n"
    "static long tally(const int* p, int n) { long s = 0; for (int i = 0; i < n; i += 1) s += bounce(p[i]); return s; }\n"
    "static int ping(int n);\n"
    "static int pong(int n) { return n <= 0 ? 0 : 1 + ping(n - 1); }\n"
    "static int ping(int n) { return n <= 0 ? 0 : 2 + pong(n - 1); }\n"
    "__attribute__((noinline)) __m512i mix(__m512i a, __m512i b, __m512i c)\n"
    "{\n"
    "    __m512i x = _mm512_add_epi32(a, b);\n"
    "    x = _mm512_xor_si512(x, _mm512_min_epu8(x, c));\n"
    "    return _mm512_mask_sub_epi32(x, (__mmask16)0x5a5a, x, _mm512_set1_epi32(17));\n"
    "}\n"
    "__attribute__((noinline)) uint64_t count(const uint8_t* p, int n)\n"
    "{\n"
    "    __m512i acc = _mm512_setzero_si512();\n"
    "    for (int i = 0; i + 64 <= n; i += 64)\n"
    "    {\n"
    "        __m512i v = _mm512_loadu_si512(p + i);\n"
    "        __mmask64 m = _mm512_cmpeq_epi8_mask(v, _mm512_set1_epi8('a'));\n"
    "        acc = _mm512_add_epi64(acc, _mm512_set1_epi64((long long)__builtin_popcountll(m)));\n"
    "        acc = _mm512_mask_add_epi32(acc, (__mmask16)m, acc, _mm512_set1_epi32(3));\n"
    "    }\n"
    "    return (uint64_t)_mm512_reduce_add_epi64(acc);\n"
    "}\n"
    "__attribute__((noinline)) long long structs(int n)\n"
    "{\n"
    "    Triple t = { 5, 9, 2 };\n"
    "    for (int i = 0; i < n; i += 1) t = twist(t, i);\n"
    "    return t.a + t.b + t.c + kept(n);\n"
    "}\n"
    "__attribute__((noinline)) long long control(int n)\n"
    "{\n"
    "    long long r = 0;\n"
    "    int data[8] = { 3, 1, 4, 1, 5, 9, 2, 6 };\n"
    "    __m128i v = _mm_set1_epi32(n);\n"
    "    for (int i = 0; i < n; i += 1)\n"
    "    {\n"
    "        r += clamp(i * 7 - 20, -5, 30) + classify(i) + twice(i - 3);\n"
    "        r ^= spin((unsigned)i, i & 7);\n"
    "        v = pick(v, _mm_set1_epi32(i), i);\n"
    "    }\n"
    "    int lanes[4];\n"
    "    _mm_storeu_si128((__m128i*)lanes, v);\n"
    "    return r + walk(data, 8) + lanes[0] + lanes[3];\n"
    "}\n"
    "__attribute__((noinline)) long small(int n)\n"
    "{\n"
    "    int d[6] = { 7, 12, 5, 30, 1, 8 };\n"
    "    long r = tally(d, 6);\n"
    "    for (int i = 0; i < n; i += 1) r += bounce(i * 5 + 3) + ping(i & 7);\n"
    "    return r;\n"
    "}\n"
    "int main(void)\n"
    "{\n"
    "    uint8_t buffer[640];\n"
    "    for (int i = 0; i < 640; i += 1) buffer[i] = (uint8_t)(\"abcab\"[i % 5] + (i % 7 == 0));\n"
    "    __m512i a = _mm512_set1_epi32(7);\n"
    "    __m512i b = _mm512_set_epi32(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16);\n"
    "    __m512i c = _mm512_set1_epi8(9);\n"
    "    long long out[8];\n"
    "    _mm512_storeu_si512(out, mix(a, b, c));\n"
    "    printf(\"%llx %llx %llu %lld %lld %lld %ld\\n\", out[0], out[7], (unsigned long long)count(buffer, 640), structs(9), control(13), control(4), small(11));\n"
    "    return 0;\n"
    "}\n";

static void check_test_driver_tier1_inline(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    X86Target v4 = { 0 };
    const bool parsed = x86_target_parse("x86-64-v4", 9, &v4);
    const bool host = parsed && (x86_target_native().features & v4.features) == v4.features;
    char unit[512];
    char reference[512];
    snprintf(unit, sizeof(unit), "%s/tier1-inline.c", directory);
    snprintf(reference, sizeof(reference), "%s/tier1-inline-clang", directory);
    const bool written = parse_test_write(unit, check_test_tier1_inline_source, strlen(check_test_tier1_inline_source));
    char* reference_arguments[] = { (char*)clang, "-O0", "-march=x86-64-v4", "-o", reference, unit, NULL };
    char* text = NULL;
    const bool referenced = written && check_test_driver_spawn(reference_arguments, &text) == 0;
    free(text);
    char expected[256] = { 0 };
    const bool ran = referenced && (!host || (check_test_driver_capture(reference, expected, sizeof(expected)) && expected[0] != 0));
    check_test_expect(test, ran, "clang's build of the inlining fixture runs");
    for (uint32_t build = 0; ran && build < 4; build += 1)
    {
        // Builds 0 and 1 inline, by the vector path and the scalar twin; 2 and 3 keep every call (--tier1-no-inline).
        const bool scalar = build % 2 != 0;
        const bool inlining = build < 2;
        char program[512];
        char message[256];
        snprintf(program, sizeof(program), "%s/tier1-inline-%u", directory, build);
        char* arguments[12] = { (char*)executable, "--tier1", "--tier1-report", "--verify", "-march=x86-64-v4" };
        uint32_t count = 5;
        if (scalar)
        {
            arguments[count++] = "--scalar";
        }
        if (!inlining)
        {
            arguments[count++] = "--tier1-no-inline";
        }
        arguments[count++] = "-o";
        arguments[count++] = program;
        arguments[count++] = unit;
        arguments[count] = NULL;
        text = NULL;
        const bool built = check_test_driver_spawn(arguments, &text) == 0;
        uint64_t inlined = UINT64_MAX;
        uint64_t internal = UINT64_MAX;
        const bool reported = built && check_test_tier1_field(text, "inlined", &inlined) && check_test_tier1_field(text, "internal_failures", &internal);
        free(text);
        // 37: the straight-line wrappers' and the struct helper's calls are 26, which the straight-line splice alone took
        // (main before #2541), the always_inline helpers with control flow's are 8, and the small class's 3: bounce in
        // tally's summary, and tally and bounce in small.
        snprintf(message, sizeof(message), "--tier1 %s the wrappers', the struct helper's, the control flow helpers' and the small ones' calls (build %u)", inlining ? "inlines" : "keeps", build);
        check_test_expect(test, reported && internal == 0 && (inlining ? inlined >= 37 : inlined == 0), message);
        char actual[256] = { 0 };
        snprintf(message, sizeof(message), "the inlining fixture by --tier1 runs as clang -O0's build does (build %u)", build);
        check_test_expect(test, built && (!host || (check_test_driver_capture(program, actual, sizeof(actual)) && strcmp(actual, expected) == 0)), message);
        unlink(program);
    }
    for (uint32_t path = 0; ran && path < 2; path += 1)
    {
        // The unit's summaries are built by whichever lane needs each first (#2541): the object is the same by any count.
        char objects[2][512];
        bool compiled = true;
        for (uint32_t k = 0; k < 2; k += 1)
        {
            snprintf(objects[k], sizeof(objects[k]), "%s/tier1-inline-lanes-%u.o", directory, k);
            char* arguments[12] = { (char*)executable, "--tier1", "-march=x86-64-v4", "--lanes", k == 0 ? "1" : "8", "-c", "-o", objects[k], unit, path != 0 ? "--scalar" : NULL, NULL };
            text = NULL;
            compiled = check_test_driver_spawn(arguments, &text) == 0 && compiled;
            free(text);
        }
        char message[256];
        snprintf(message, sizeof(message), "the inlining fixture's --tier1 object by one lane is its object by eight (%s path)", path != 0 ? "scalar" : "vector");
        check_test_expect(test, compiled && check_test_driver_same_files(objects[0], objects[1]), message);
        unlink(objects[0]);
        unlink(objects[1]);
    }
    unlink(reference);
    unlink(unit);
}

// The convention's rarer shapes' fixture (#141).
static void check_test_driver_tier1_shapes(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_tier1_units(test, clang, executable, directory, "tier1-shapes", "rarer aggregate shapes'", check_test_tier1_shapes_header, check_test_tier1_shapes_source,
                                  check_test_tier1_shapes_other_source, NULL, true, 0);
}

// The thread-locals' fixture (#141): this unit's own thread-locals (a long, a static array and a struct), whose addresses
// Tier 1 takes from the thread pointer at their TPOFF32 offsets (local-exec), and the other unit's (clang -O2's), from
// its GOT slot's GOTTPOFF offset (initial-exec), read, written and returned as addresses by Tier 1's functions on four
// threads the other unit starts and on the main thread, each thread's results its own copies'.
static const char check_test_tier1_tls_header[] =
    "#include <stdio.h>\n"
    "extern _Thread_local long b_counter;\n"
    "long a_bump(long k);\n"
    "long a_peek(void);\n"
    "long* a_own_address(void);\n"
    "long* a_other_address(void);\n"
    "long a_sum(int n);\n"
    "unsigned long b_run(unsigned long* results, int n);\n";

static const char check_test_tier1_tls_source[] =
    "#include \"tier1-tls.h\"\n"
    "_Thread_local long a_own = 7;\n"
    "static _Thread_local long a_array[16];\n"
    "_Thread_local struct { int x; long y; } a_pair = { 3, 4 };\n"
    "long a_bump(long k) { a_own += k; b_counter += 2 * k; for (int i = 0; i < 16; i += 1) a_array[i] += k * i; a_pair.y += k; return a_own * 3 + b_counter; }\n"
    "long a_peek(void) { return a_own; }\n"
    "long* a_own_address(void) { return &a_own; }\n"
    "long* a_other_address(void) { return &b_counter; }\n"
    "long a_sum(int n) { long s = a_pair.x; for (int i = 0; i < n && i < 16; i += 1) s += a_array[i]; return s + a_pair.y; }\n"
    "int main(void)\n"
    "{\n"
    "    unsigned long results[4];\n"
    "    const unsigned long main_result = b_run(results, 4);\n"
    "    printf(\"%lu %lu %lu %lu %lu\\n\", results[0], results[1], results[2], results[3], main_result);\n"
    "    printf(\"%ld %ld %ld\\n\", a_own, b_counter, a_sum(16));\n"
    "    return 0;\n"
    "}\n";

static const char check_test_tier1_tls_other_source[] =
    "#include <pthread.h>\n"
    "#include \"tier1-tls.h\"\n"
    "_Thread_local long b_counter = 100;\n"
    "static void* b_worker(void* p)\n"
    "{\n"
    "    const long k = (long)p;\n"
    "    unsigned long r = 0;\n"
    "    for (int i = 0; i < 5; i += 1) r = r * 7 + (unsigned long)a_bump(k + i);\n"
    "    r = r * 11 + (unsigned long)a_sum(16);\n"
    "    r = r * 13 + (a_other_address() == &b_counter) + 2 * (*a_own_address() == a_peek());\n"
    "    return (void*)r;\n"
    "}\n"
    "unsigned long b_run(unsigned long* results, int n)\n"
    "{\n"
    "    pthread_t threads[4];\n"
    "    for (int t = 0; t < n; t += 1) pthread_create(&threads[t], 0, b_worker, (void*)(long)(t + 1));\n"
    "    for (int t = 0; t < n; t += 1) { void* r; pthread_join(threads[t], &r); results[t] = (unsigned long)r; }\n"
    "    return (unsigned long)b_worker((void*)9L);\n"
    "}\n";

// The thread-locals' fixture (#141).
static void check_test_driver_tier1_tls(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_tier1_units(test, clang, executable, directory, "tier1-tls", "thread-locals'", check_test_tier1_tls_header, check_test_tier1_tls_source, check_test_tier1_tls_other_source, NULL,
                                  true, 0);
}

// The segments' fixture (#1447): loads and stores in __seg_fs and __seg_gs by Tier 1, each with its segment override
// (BUSTER_IR_SEGMENT, tier1_select_segment), every function Tier 1's. Thread-locals are reached through %fs (an
// address less the thread pointer, __builtin_thread_pointer's): integers of every width, signed and unsigned, a
// double, a float, a vector of 16 bytes and one of 32 (a baseline target's two pieces, #1624), an __int128 (its halves,
// ir_wide.c), members and subscripts, compound assignments, increments, and relaxed, acquire and seq_cst atomic loads and
// stores (a seq_cst store's xchg), in a loop. A global in __seg_gs, a struct's members, is reached through %gs, whose
// base the other unit's arch_prctl sets to a window, a mapped page past it, and restores to 0.
static const char check_test_tier1_segments_header[] =
    "#include <stdio.h>\n"
    "typedef int v4si __attribute__((vector_size(16)));\n"
    "typedef double v4df __attribute__((vector_size(32)));\n"
    "struct S { long a; int b; char c[8]; };\n"
    "struct G { int count; long wide[4]; double d; v4si v; };\n"
    "extern __seg_gs struct G gs;\n"
    "long fs_run(int n);\n"
    "long gs_run(int n);\n"
    "long ss_run(int n);\n"
    "int gs_enter(void);\n"
    "int gs_leave(void);\n";

static const char check_test_tier1_segments_source[] =
    "#include \"tier1-segments.h\"\n"
    "static _Thread_local long tl[4] = { 1, 2, 3, 4 };\n"
    "static _Thread_local struct S ts = { 5, 6, \"abcdefg\" };\n"
    "static _Thread_local double td = 2.5;\n"
    "static _Thread_local float tf = 1.5f;\n"
    "static _Thread_local v4si tv = { 9, 8, 7, 6 };\n"
    "static _Thread_local v4df tw = { 1, 2, 3, 4 };\n"
    "static _Thread_local short th = -3;\n"
    "static _Thread_local signed char tc = -5;\n"
    "static _Thread_local unsigned char tu = 200;\n"
    "static _Thread_local int ta = 5;\n"
    "static _Thread_local __int128 ti = 77;\n"
    "#define FS(type, object) ((type __seg_fs*)((char*)&(object) - (char*)__builtin_thread_pointer()))\n"
    "static long rd(long __seg_fs* p, int i) { return p[i]; }\n"
    "static void wr(long __seg_fs* p, int i, long v) { p[i] = v; }\n"
    "static int member(struct S __seg_fs* s, int n) { s->b += n; s->c[n & 7] = (char)('A' + n); return s->b + s->c[(n + 1) & 7] + (int)s->a; }\n"
    "long fs_run(int n)\n"
    "{\n"
    "    long __seg_fs* p = FS(long, tl[0]);\n"
    "    double __seg_fs* d = FS(double, td);\n"
    "    float __seg_fs* f = FS(float, tf);\n"
    "    v4si __seg_fs* v = FS(v4si, tv);\n"
    "    v4df __seg_fs* w = FS(v4df, tw);\n"
    "    short __seg_fs* h = FS(short, th);\n"
    "    signed char __seg_fs* c = FS(signed char, tc);\n"
    "    unsigned char __seg_fs* u = FS(unsigned char, tu);\n"
    "    int __seg_fs* a = FS(int, ta);\n"
    "    __int128 __seg_fs* q = FS(__int128, ti);\n"
    "    wr(p, n & 3, n * 7L); *p += n; p[1]++; --p[2];\n"
    "    long r = rd(p, (n + 1) & 3) + p[1] * 3 + (p[3] <<= 1) + member(FS(struct S, ts), n);\n"
    "    *d += n; *d *= 1.5; *f *= 2.0f; *f += n;\n"
    "    v4si k = { n, n + 1, n + 2, n + 3 }; *v = *v * k; *v += k; v4si x = *v; r += x[0] + x[1] * 3 + x[2] * 5 + x[3] * 7;\n"
    "    *w *= *w; v4df y = *w; r += (long)y[n & 3];\n"
    "    *h = (short)(*h * n); r += *h + *c + *u; *c = (signed char)(*c - n); *u = (unsigned char)(*u + n); r += *c + *u;\n"
    "    *q += n; *q <<= 3; r += (long)*q + (long)(*q >> 64);\n"
    "    r += __atomic_load_n(a, __ATOMIC_RELAXED); __atomic_store_n(a, (int)r, __ATOMIC_RELAXED);\n"
    "    __atomic_store_n(a, __atomic_load_n(a, __ATOMIC_SEQ_CST) + 1, __ATOMIC_SEQ_CST); __atomic_store_n(a, __atomic_load_n(a, __ATOMIC_ACQUIRE) + 2, __ATOMIC_RELEASE);\n"
    "    for (int i = 0; i < n; i += 1) { r += p[i & 3]; p[(i + 1) & 3] ^= i; }\n"
    "    return r + tl[0] + tl[1] + tl[2] + tl[3] + ts.b + (long)td + (long)tf + tv[n & 3] + (long)tw[n & 3] + th + tc + tu + ta + (long)ti;\n"
    "}\n"
    "long gs_run(int n)\n"
    "{\n"
    "    if (gs_enter() != 0) return -1;\n"
    "    gs.count = n; gs.count++; gs.count *= 3; gs.wide[n & 3] = gs.count; gs.wide[0] += gs.wide[1]; gs.d = n * 0.5; gs.d += gs.count;\n"
    "    v4si k = { 1, 2, 3, n }; gs.v += k; v4si q = gs.v;\n"
    "    long r = gs.count + gs.wide[0] + gs.wide[n & 3] + (long)gs.d + q[3] + q[0];\n"
    "    r += --gs.count;\n"
    "    return gs_leave() != 0 ? -2 : r;\n"
    "}\n"
    "#define SEG_SS __attribute__((address_space(258)))\n"
    "static SEG_SS long sl[4] = { 4, 3, 2, 1 };\n"
    "static SEG_SS struct S ss_s = { 7, 8, \"hgfedcb\" };\n"
    "static SEG_SS double sd = 0.75;\n"
    "static long sp[4] = { 10, 20, 30, 40 };\n"
    "static v4si sv = { 1, 3, 5, 7 };\n"
    "static float sf = 3.5f;\n"
    "static short sh = 9;\n"
    "static int sa = 4;\n"
    "static __int128 si = 5;\n"
    "long ss_run(int n)\n"
    "{\n"
    "    long SEG_SS* p = (long SEG_SS*)sp;\n"
    "    v4si SEG_SS* v = (v4si SEG_SS*)&sv;\n"
    "    float SEG_SS* f = (float SEG_SS*)&sf;\n"
    "    short SEG_SS* h = (short SEG_SS*)&sh;\n"
    "    int SEG_SS* a = (int SEG_SS*)&sa;\n"
    "    __int128 SEG_SS* q = (__int128 SEG_SS*)&si;\n"
    "    sl[n & 3] += n; sl[0]++; --sl[1]; ss_s.b += n; ss_s.c[n & 7] = (char)('a' + n); sd *= 1.5; sd += n;\n"
    "    p[n & 3] = n * 5L; *p += sl[2]; p[1] <<= 1;\n"
    "    long r = sl[0] + sl[1] + sl[(n + 2) & 3] + ss_s.a + ss_s.b + ss_s.c[(n + 1) & 7] + (long)sd + p[0] + p[1] + p[n & 3];\n"
    "    v4si k = { n, 2, n, 4 }; *v += k; v4si x = *v; r += x[0] + x[1] * 3 + x[2] * 5 + x[3] * 7;\n"
    "    *f *= 0.5f; *f += n; r += (long)*f; *h = (short)(*h - n); r += *h;\n"
    "    *q += n; *q <<= 2; r += (long)*q;\n"
    "    r += __atomic_load_n(a, __ATOMIC_RELAXED); __atomic_store_n(a, (int)(r & 1023), __ATOMIC_SEQ_CST); r += __atomic_load_n(a, __ATOMIC_ACQUIRE);\n"
    "    for (int i = 0; i < n; i += 1) { r += sl[i & 3]; p[(i + 1) & 3] ^= i; }\n"
    "    return r + sp[0] + sp[1] + sp[2] + sp[3] + sv[n & 3] + (long)sf + sh + sa + (long)si;\n"
    "}\n";

static const char check_test_tier1_segments_other_source[] =
    "#include \"tier1-segments.h\"\n"
    "long syscall(long, ...);\n"
    "#include <sys/mman.h>\n"
    "__seg_gs struct G gs;\n"
    "int gs_enter(void)\n"
    "{\n"
    "    static char* window;\n"
    "    if (window == 0) window = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);\n"
    "    if (window == MAP_FAILED || (unsigned long)window < (unsigned long)&gs) return -1;\n"
    "    for (unsigned i = 0; i < sizeof(struct G); i += 1) window[i] = (char)(i * 7);\n"
    "    return (int)syscall(158, 0x1001, (unsigned long)window - (unsigned long)&gs);\n"
    "}\n"
    "int gs_leave(void) { return (int)syscall(158, 0x1001, 0UL); }\n"
    "int main(void)\n"
    "{\n"
    "    long t = 0;\n"
    "    for (int n = 0; n < 6; n += 1) t = t * 31 + fs_run(n) + gs_run(n) * 7 + ss_run(n) * 11;\n"
    "    printf(\"%ld\\n\", t);\n"
    "    return 0;\n"
    "}\n";

static void check_test_driver_tier1_segments(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_tier1_units(test, clang, executable, directory, "tier1-segments", "segments'", check_test_tier1_segments_header, check_test_tier1_segments_source,
                                  check_test_tier1_segments_other_source, NULL, true, 0);
    check_test_driver_tier1_units(test, clang, executable, directory, "tier1-segments", "segments' (x86-64-v3)", check_test_tier1_segments_header,
                                  check_test_tier1_segments_source, check_test_tier1_segments_other_source, "-march=x86-64-v3", true, 0);
}

// The stack arguments' fixture (#141): its unit by --tier1 -c, by each path with and without rematerialization, every
// function but main Tier 1's, linked with its other unit compiled by clang -O2, prints what clang -O0's build of both
// prints: the arguments past the registers reach clang's callees in their stack slots, and the parameters past them
// arrive from clang's callers.
static void check_test_driver_tier1_stack(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    char caller[512];
    char callee[512];
    char callee_object[512];
    char reference[512];
    snprintf(caller, sizeof(caller), "%s/tier1-stack.c", directory);
    snprintf(callee, sizeof(callee), "%s/tier1-stack-other.c", directory);
    snprintf(callee_object, sizeof(callee_object), "%s/tier1-stack-other.o", directory);
    snprintf(reference, sizeof(reference), "%s/tier1-stack-clang", directory);
    const bool written = parse_test_write(caller, check_test_tier1_stack_caller_source, sizeof(check_test_tier1_stack_caller_source) - 1) &&
                         parse_test_write(callee, check_test_tier1_stack_callee_source, sizeof(check_test_tier1_stack_callee_source) - 1);
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
    check_test_expect(test, ran, "clang's builds of the stack arguments' fixture run");
    static const char* const names[5] = { "tier1_pass", "tier1_take7", "tier1_take12", "tier1_many", "tier1_take64" };
    for (uint32_t rematerialize = 0; ran && rematerialize < 2; rematerialize += 1)
    {
        for (uint32_t path = 0; path < 2; path += 1)
        {
            char object[512];
            char program[512];
            snprintf(object, sizeof(object), "%s/tier1-stack-%u-%u.o", directory, rematerialize, path);
            snprintf(program, sizeof(program), "%s/tier1-stack-%u-%u", directory, rematerialize, path);
            CheckTestTier1Build build;
            const bool compiled = check_test_tier1_build_names(executable, caller, names, BUSTER_ARRAY_LENGTH(names), object, rematerialize != 0, path != 0, true, false, NULL, &build);
            check_test_expect(test, compiled, "the stack arguments' unit compiles by --tier1, every function but main Tier 1's");
            char* link_arguments[] = { (char*)clang, "-o", program, object, callee_object, NULL };
            char* link_text = NULL;
            const bool linked = compiled && check_test_driver_spawn(link_arguments, &link_text) == 0;
            free(link_text);
            char actual[256] = { 0 };
            check_test_expect(test, linked && check_test_driver_capture(program, actual, sizeof(actual)) && strcmp(actual, expected) == 0,
                              "Tier 1's stack arguments and parameters meet clang -O2's code");
            unlink(object);
            unlink(program);
        }
    }
    unlink(caller);
    unlink(callee);
    unlink(callee_object);
    unlink(reference);
}
