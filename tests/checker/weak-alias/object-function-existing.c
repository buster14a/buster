extern int alias_name;
int target_function(void) { return 3; }
#pragma weak alias_name = target_function
int use(void) { return alias_name; }
int after(void) { return undeclared_after; }
