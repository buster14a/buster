int alias_name(void);
int target_value = 3;
#pragma weak alias_name = target_value
int use(void) { return alias_name(); }
int after(void) { return undeclared_after; }
