#pragma STDC FP_CONTRACT OFF
double f(double a, double b) {
#pragma STDC FP_CONTRACT ON
 return a * b + 1; }
#pragma STDC FP_CONTRACT DEFAULT
double g(double a) { if (a) {
#pragma STDC FP_CONTRACT OFF
 return a; } return 0; }
#pragma STDC FP_CONTRACT MAYBE
double x;
