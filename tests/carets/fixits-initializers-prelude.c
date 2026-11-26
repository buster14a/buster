// The command line's -D definitions participate in the same historical lookup (#2376).
int *command_null(void) { int *value; return value; }
_Bool command_false(void) { _Bool value; return value; }
