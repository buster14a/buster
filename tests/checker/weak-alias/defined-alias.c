int implementation(void) { return 4; }
int entry(void) { return 9; }
#pragma weak entry = implementation
int user(void) { return entry(); }
