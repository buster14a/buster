extern int *alias_pointer;
const int target_value = 3;
#pragma weak alias_pointer = target_value
int use(void) { return *alias_pointer; }
