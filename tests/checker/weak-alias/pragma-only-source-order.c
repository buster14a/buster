#pragma weak alias = object
int before(void) { return alias; }
int object = 1;
int after(void) { return alias; }
