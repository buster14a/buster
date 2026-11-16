extern int alias_tls;
__thread int target_tls = 3;
#pragma weak alias_tls = target_tls
int first(void) { return alias_tls; }
extern long alias_section __attribute__((section("special")));
int target_value = 5;
#pragma weak alias_section = target_value
int second(void) { return alias_section; }
