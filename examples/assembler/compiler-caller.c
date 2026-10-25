// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Independently computed results for compiler-corpus.c, linked to each assembled object.

#include <string.h>
#include <stdint.h>
#include <unwind.h>

extern long corpus_initialized;
extern long corpus_zero;
extern const char* corpus_message;
extern long corpus_extend(signed char, unsigned char, short, unsigned short, int);
extern long corpus_load(const signed char*, const unsigned char*, const short*, const unsigned short*, const int*);
extern long corpus_loop(const long*, unsigned);
extern long corpus_divide(long, long);
extern long corpus_choose(long, long);
extern long corpus_call(long);
extern const char* corpus_string(void);
extern const long* corpus_address(void);
int main(void);

static unsigned corpus_frames;
static unsigned corpus_unwound;

static _Unwind_Reason_Code corpus_trace(struct _Unwind_Context* context, void* argument)
{
    unsigned* frames = argument;
    *frames += _Unwind_GetIP(context) != 0;
    const uintptr_t region = _Unwind_GetRegionStart(context);
    corpus_unwound |= region == (uintptr_t)corpus_call ? 1u : 0;
    corpus_unwound |= region == (uintptr_t)main ? 2u : 0;
    return *frames < 8 ? _URC_NO_REASON : _URC_END_OF_STACK;
}

long corpus_external(long value)
{
    corpus_frames = 0;
    corpus_unwound = 0;
    _Unwind_Backtrace(corpus_trace, &corpus_frames);
    return value * 3 - 5;
}

int main(void)
{
    const signed char a = -117;
    const unsigned char b = 241;
    const short c = -30001;
    const unsigned short d = 60003;
    const int e = -1000003;
    const long values[] = {5, -8, 12};
    int failed = corpus_extend(a, b, c, d, e) != -969877;
    failed |= corpus_load(&a, &b, &c, &d, &e) != -969877;
    failed |= corpus_loop(values, 0) != 3 || corpus_loop(values, 3) != 1230;
    failed |= corpus_divide(-101, 7) != -17 || corpus_divide(101, -7) != -11;
    failed |= corpus_choose(-8, 10) != -27 || corpus_choose(10, -8) != 15;
    failed |= corpus_initialized != 37 || corpus_zero != 0;
    failed |= corpus_call(2) != 135 || corpus_zero != 112;
    failed |= corpus_frames < 4;
    failed |= corpus_unwound != 3;
    failed |= strcmp(corpus_message, "compiler assembly") != 0 || strcmp(corpus_string(), corpus_message) != 0;
    const long* table = corpus_address();
    failed |= table[0] != 11 || table[1] != -17 || table[2] != 23 || table[3] != 41;
    return failed;
}
