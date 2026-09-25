// The subject is compiled by both Buster and the independent host compiler.
// A separately compiled host checks its results and both directions of the ABI.
typedef unsigned __int128 U128;
typedef __int128 S128;

float x64_u128_to_f32(U128 value) { return (float)value; }
double x64_u128_to_f64(U128 value) { return (double)value; }
long double x64_u128_to_f80(U128 value) { return (long double)value; }
float x64_s128_to_f32(S128 value) { return (float)value; }
double x64_s128_to_f64(S128 value) { return (double)value; }
long double x64_s128_to_f80(S128 value) { return (long double)value; }

U128 x64_f32_to_u128(float value) { return (U128)value; }
U128 x64_f64_to_u128(double value) { return (U128)value; }
U128 x64_f80_to_u128(long double value) { return (U128)value; }
S128 x64_f32_to_s128(float value) { return (S128)value; }
S128 x64_f64_to_s128(double value) { return (S128)value; }
S128 x64_f80_to_s128(long double value) { return (S128)value; }
