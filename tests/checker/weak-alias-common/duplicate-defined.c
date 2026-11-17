int common_target = 1;
int ignored_common;
#pragma weak common_alias = common_target
#pragma weak common_alias = ignored_common
int first(void) { return 1; }
int second(void) { return 2; }
int third(void) { return 3; }
