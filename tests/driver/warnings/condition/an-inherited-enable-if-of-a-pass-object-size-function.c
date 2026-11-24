int r(void *p __attribute__((pass_object_size(0)))) __attribute__((enable_if(p != 0, "nn")));
int r(void *p __attribute__((pass_object_size(0))));
int s(void *p __attribute__((pass_object_size(0)))) __attribute__((enable_if(1, "one")));
int s(void *p __attribute__((pass_object_size(0))));
void t(void) { char b[4]; r(b); s(b); }
