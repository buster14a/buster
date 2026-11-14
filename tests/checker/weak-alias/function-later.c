int entry(void);
#pragma weak entry = implementation
int implementation(void) { return 4; }
int user(void) { return entry(); }
