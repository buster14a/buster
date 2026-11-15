int real_object = 1;
extern int object_alias __attribute__((alias("real_object")));
int function_target(void) { return 2; }
#pragma weak object_alias = function_target
int first(void) { return object_alias; }
int real_function(void) { return 3; }
int function_alias(void) __attribute__((alias("real_function")));
int object_target = 4;
#pragma weak function_alias = object_target
int second(void) { return function_alias(); }
