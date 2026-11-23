int target(int named_parameter) { return named_parameter; }
#pragma weak alias = target
int use(void) { return alias(); }
