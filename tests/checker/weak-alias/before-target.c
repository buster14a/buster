int entry(void);
#pragma weak entry = implementation
int user(void) { return entry(); }
int implementation(void) { return 4; }
