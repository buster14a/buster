extern long alias_value;
int target_value = 3;
#pragma weak alias_value = target_value
int first(void) { return alias_value; }
int second(void) { return alias_value; }
int after(void) { return undeclared_after; }
