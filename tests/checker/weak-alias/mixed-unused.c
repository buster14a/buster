extern long different_type;
int object_target = 1;
#pragma weak different_type = object_target
int different_kind(void);
#pragma weak different_kind = object_target
extern int object_alias;
int function_target(void) { return 2; }
#pragma weak object_alias = function_target
