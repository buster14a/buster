extern int common_alias __attribute__((alias("common_target")));
int common_target;
int first(void) { return common_target + 1; }
int second(void) { return common_target + 2; }
int third(void) { return common_target + 3; }
