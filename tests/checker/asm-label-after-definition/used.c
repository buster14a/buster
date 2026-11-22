int called(void) { return 1; }
int caller(void) { return called(); }
int called(void) __asm__("called_label");
int object = 4;
int *address(void) { return &object; }
extern int object __asm__("object_label");
