#pragma once
#pragma GCC system_header
#pragma STDC FP_CONTRACT ON
#define P(x) _Pragma(#x) after
P(omp parallel for)
#pragma push_macro("X")
#define X 1
#pragma pop_macro("X")
X
