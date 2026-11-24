#pragma clang diagnostic push
#pragma clang diagnostic warning "-Wunused-variable"
int in_scope(void) { int unused; return 1; }
#pragma clang diagnostic pop
int outside(void) { int unused; return 2; }
#pragma clang diagnostic warning "-Wunused-local-typedef"
int local_type(void) { typedef int T; return 3; }
