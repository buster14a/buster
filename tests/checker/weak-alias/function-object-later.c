int alias_name(void);
#pragma weak alias_name = target_value
int before(void) { return alias_name(); }
int target_value = 3;
int after(void) { return alias_name(); }
