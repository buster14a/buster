// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

long g;
int ga[4];
enum E { Z, O };

int narrow(void) { if ((char)(long)&g) return 1; }
int word(void) { if ((int)(long)&g) return 1; }
int array(void) { if ((unsigned short)(long)ga) return 1; }
int real(void) { if ((double)(long)&g) return 1; }
int either(int x) { if ((char)(long)&g || x) return 1; }
int neg(void) { if (!(char)(long)&g) return 1; }
int nested(int x) { if (!((long)(char)(long)&g && x)) return 1; }
int loop(void) { while ((enum E)(long)&g) return 1; }
int unknown(int x) { if ((char)x) return 1; }
int address(void) { if ((long)&g) return 1; }
int boolean(void) { if ((_Bool)&g) return 1; }
int pointer(void) { if ((void*)(long)&g) return 1; }
int wider(void) { if ((__int128)(long)&g) return 1; }
int integer(void) { if ((char)1) return 1; }
int floating(void) { if (!(int)0.5) return 1; }
int left(int x) { if ((char)(1 || x)) return 1; }
int right(int x) { if ((double)(x || 1)) return 1; }
int inverted(int x) { if ((long)!(x && 0)) return 1; }
int twice(int x) { if ((long)(char)(x || 1)) return 1; }
int arm(void) { return (char)(long)&g ? 1 : 1 / 0; }

// The checker can follow this numeric cast even where the backend cannot yet emit its body.
static __attribute__((unused)) int wide_half(void) { if ((char)(_Float16)(unsigned _BitInt(257))1) return 1; }

#pragma STDC FENV_ACCESS ON
int query_byte(char* p) { return (char)__builtin_object_size(p, 0) ? 1 : 1 / 0; }
int fp_exact(void) { if ((float)256) return 1; }
int fp_zero(void) { if ((double)0) return 1; }
int fp_widen(void) { if ((double)(float)42) return 1; }
int fp_narrow_exact(void) { if ((float)(double)16777218) return 1; }
int fp_comma(int x) { if ((double)((void)x, 42)) return 1; }
int fp_left(int x) { if ((double)(1 || x)) return 1; }
int fp_right(int x) { if ((float)(x || 1)) return 1; }
int fp_neither(int x) { if ((long double)(x && 0)) return 1; }
int fp_addr(void) { if ((float)(long)&g) return 1; }
int fp_bool_addr(void) { if ((double)(_Bool)(long)&g) return 1; }
int fp_complex(void) { if ((float _Complex)256) return 1; }
int fp_real_again(void) { if ((float)(double _Complex)42) return 1; }
int fp_round_trip(int x) { if ((int)(double)((void)x, 42)) return 1; }
int fp_not(void) { if (!(float)42) return 1; }
int fp_wide_negative(void) { if ((double)-(_BitInt(257))42) return 1; }
int fp_wide_zero(void) { if ((double)(_BitInt(257))0) return 1; }
