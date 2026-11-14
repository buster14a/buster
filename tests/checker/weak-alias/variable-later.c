extern int alias_value;
#pragma weak alias_value = target_value
int target_value = 3;
int alias_value;
