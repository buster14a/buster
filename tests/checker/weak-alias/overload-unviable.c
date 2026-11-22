long alias(int);
int target(double value) { return value; }
#pragma weak alias = target
struct Value { int member; } value;
int use(void) { return alias(value); }
int later(void) { return undeclared_later; }
