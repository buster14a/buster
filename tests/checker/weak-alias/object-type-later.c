extern long alias_value;
#pragma weak alias_value = target_value
long before(void) { return alias_value; }
int target_value = 3;
int after(void) { return alias_value; }
