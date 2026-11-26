# 1 "system-variable.h" 1 3
int system_initialized = 1;
int system_tentative;
extern int declared_in_system;
# 10 "default-warning.c" 2
int user_initialized = 2;
int user_tentative;
int declared_in_system = 3;
# 1 "user-variable.h" 1
int user_header_initialized = 4;
int user_header_tentative;
# 20 "default-warning.c" 2
int user_end;
