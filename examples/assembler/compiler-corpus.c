// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original input for GCC/Clang -S compatibility checks. No generated assembly is checked in.

extern long corpus_external(long value);

long corpus_initialized = 37;
long corpus_zero;
const char* corpus_message = "compiler assembly";
static const long corpus_table[] = {11, -17, 23, 41};

long corpus_extend(signed char a, unsigned char b, short c, unsigned short d, int e)
{
    return (long)a + (long)b + (long)c + (long)d + (long)e;
}

long corpus_load(const signed char* a, const unsigned char* b, const short* c, const unsigned short* d, const int* e)
{
    return (long)*a + (long)*b + (long)*c + (long)*d + (long)*e;
}

long corpus_loop(const long* values, unsigned count)
{
    long total = 3;
    for (unsigned i = 0; i < count; i += 1)
    {
        total = total * 7 + values[i];
    }
    return total;
}

long corpus_divide(long value, long divisor)
{
    return value / divisor + value % divisor;
}

long corpus_choose(long a, long b)
{
    return a < b ? a - 19 : b + 23;
}

long corpus_call(long value)
{
    long result = corpus_external(value + corpus_initialized);
    corpus_zero = result;
    return result + corpus_table[(unsigned long)value & 3];
}

const char* corpus_string(void)
{
    return "compiler assembly";
}

const long* corpus_address(void)
{
    return corpus_table;
}
