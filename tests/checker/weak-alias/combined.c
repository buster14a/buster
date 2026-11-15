int entry(void);
#pragma weak entry = implementation
int implementation(void) { return 4; }
int user(void) { return entry(); }
extern int alias_value;
#pragma weak alias_value = target_value
int target_value = 3;
int alias_value;
int independent = undeclared_after_alias;
