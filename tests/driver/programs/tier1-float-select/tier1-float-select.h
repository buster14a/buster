// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

double fsel_count(const int* p, long n);
double fsel_values(const double* p, const double* q, long n);
float fsel_int(float a, float b, int c);
double fsel_less(double a, double b, double x, double y);
double fsel_greater(double a, double b, double x, double y);
double fsel_at_most(double a, double b, double x, double y);
double fsel_at_least(double a, double b, double x, double y);
double fsel_equal(double a, double b, double x, double y);
double fsel_unequal(double a, double b, double x, double y);
double fsel_not_less(double a, double b, double x, double y);
double fsel_not_equal(double a, double b, double x, double y);
float fsel_float_less(float a, float b, float x, float y);
float fsel_float_not_greater(float a, float b, float x, float y);
float fsel_float_unequal(float a, float b, float x, float y);
double fsel_mixed(float a, float b, double x, double y);
double fsel_triangle(double s, double x, int c);
double fsel_clamp(double x);
double fsel_nested(double x);
double fsel_magnitude(double x);
double fsel_reused(const double* p, long n);
double fsel_guarded(int c, const double* p);
double fsel_divided(double a, double b, int c);
double fsel_choice(int a, int b, double x, double y, double z);
double fsel_convert_double(int c, int x);
double fsel_convert_double_reverse(int c, int x);
double fsel_convert_unsigned(int c, unsigned x);
double fsel_convert_double_constants(int c);
float fsel_convert_float_constants(int c);
double fsel_convert_mixed_signedness(int c);
double fsel_convert_double_logical(int a, int b);
float fsel_convert_float_logical(int a, int b);
double fsel_convert_count(const int* p, long n);
double fsel_convert_effect(int a, volatile int* p);
float fsel_convert_effect_float(int a, volatile int* p);
float fsel_round_float(int c, int x);
double fsel_round_double(int c, long x);
float fsel_guarded_float_convert(int c, int x);
double fsel_guarded_double_convert(int c, long x);
float fsel_round_float_constant(int c);
double fsel_round_double_constant(int c);
float fsel_round_narrow_float(int c);
double fsel_round_narrow_double(int c);
#ifdef __AVX512F__
double fsel_masked(const int* p, const int* q, int c, double x, double y, unsigned* out);
#endif
