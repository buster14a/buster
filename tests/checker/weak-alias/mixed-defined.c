int object_alias = 1;
int function_target(void) { return 2; }
#pragma weak object_alias = function_target
int first(void) { return object_alias; }
int function_alias(void) { return 3; }
int object_target = 4;
#pragma weak function_alias = object_target
int second(void) { return function_alias(); }
