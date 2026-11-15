extern int alias_name;
#pragma weak alias_name = target_function
int before(void) { return alias_name; }
int target_function(void) { return 3; }
int after(void) { return alias_name; }
