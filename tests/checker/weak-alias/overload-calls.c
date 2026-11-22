long alias(int);
int target(double value) { return value; }
#pragma weak alias = target
long use_integer(void) { return alias(1); }
int use_double(void) { return alias(1.0); }
