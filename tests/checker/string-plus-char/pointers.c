// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

typedef char Letter;
extern char text_array[32];

const char* pointer_right(const char* p) { return p + 'x'; }
const char* pointer_left(const char* p) { return 'x' + p; }
const char* pointer_parenthesized(const char* p) { return (p) + 'x'; }
const signed char* pointer_signed(const signed char* p) { return p + 'x'; }
const unsigned char* pointer_unsigned(const unsigned char* p) { return p + 'x'; }
const volatile char* pointer_volatile(const volatile char* p) { return p + 'x'; }
const Letter* pointer_typedef(const Letter* p) { return p + 'x'; }
const char* pointer_atomic_load(const char* _Atomic p) { return __c11_atomic_load(&p, 0) + 'x'; }
const char* pointer_atomic_type_load(_Atomic(const char*) p) { return __c11_atomic_load(&p, 2) + 'x'; }
_Atomic(char)* pointer_atomic_pointee(_Atomic(char)* p) { return p + 'x'; }
const char* pointer_conditional(const char* p, int n) { return (n ? p : p + 1) + 'x'; }
const char* array_pointer(void) { return text_array + 'x'; }
unsigned long pointer_size(const char* p) { return sizeof(p + 'x'); }
const char* character_group(const char* p) { return p + ('x'); }
const char* character_expression(const char* p, int n) { return p + (n ? 'x' : 'y'); }
const int* integer_pointer(const int* p) { return p + 'x'; }
const short* short_pointer(const short* p) { return p + 'x'; }
const char* pointer_subtracted(const char* p) { return p - 'x'; }
const char* pointer_compound(const char* p) { p += 'x'; return p; }
