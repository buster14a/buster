extern long alias_value;
int target_value = 3;
#pragma weak alias_value = target_value
int ignored __attribute__((noreturn));
int first(void) { return alias_value + undeclared_inside; }
int ignored_again __attribute__((noreturn));
int after(void) { return undeclared_after; }
