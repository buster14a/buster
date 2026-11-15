int implementation(void) { return 4; }
int entry(void);
#pragma weak entry = implementation
int first(void) { return entry(); }
int second(void) { return entry(); }
