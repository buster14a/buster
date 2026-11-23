long alias(int);
int target(double value) { return value; }
#pragma weak alias = target
long (*integer_pointer)(int) = alias;
int (*double_pointer)(double) = alias;
