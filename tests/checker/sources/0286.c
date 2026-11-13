double f(double a, double b) { double c = a * b;
#pragma STDC FP_CONTRACT ON
 return c + 1; }
