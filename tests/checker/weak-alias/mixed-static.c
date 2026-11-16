static int object_alias = 1;
int function_target(void) { return 2; }
#pragma weak object_alias = function_target
int use(void) { return object_alias; }
