long alias(int);
int target(double value) { return value; }
#pragma weak alias = target
_Static_assert(sizeof(alias(1)) == sizeof(long), "the original int candidate returns long");
_Static_assert(sizeof(alias(1.0)) == sizeof(int), "the pragma double candidate returns int");
