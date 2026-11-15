int target_value = 3;
int alias_value = 4;
#pragma weak alias_value = target_value
int user(void) { return alias_value; }
