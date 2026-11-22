long alias(int);
int target(double x) { return x != 0; }
#pragma weak alias = target
_Static_assert(sizeof(alias(1)) == sizeof(long), "ordinary candidate keeps its return type");
_Static_assert(sizeof(alias(1.0)) == sizeof(int), "pragma candidate keeps its return type");
int (*chosen)(double) = alias;
