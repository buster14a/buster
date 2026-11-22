int common_target;
#pragma weak common_alias = common_target
int first(void) { return common_target + 1; }
int second(void) { return common_target + 2; }
int third(void) { return common_target + 3; }
