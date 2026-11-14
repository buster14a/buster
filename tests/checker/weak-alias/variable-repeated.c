int target_value = 3;
extern int alias_value;
#pragma weak alias_value = target_value
extern int alias_value;
int alias_value;
