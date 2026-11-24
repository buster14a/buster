typedef int __attribute__((warn_unused_result)) WI;
WI (*fp)(void);
void f(void) { fp(); }
