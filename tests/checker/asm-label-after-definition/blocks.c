void earlier(void) { extern int inherited(void) __asm__("inherited_label"); }
int inherited(void) { return 1; }
int inherited(void) __asm__("inherited_label");
int defined(void) { return 2; }
void later(void) { extern int defined(void) __asm__("block_label"); }
void plain(void) { extern int defined(void); }
int defined(void) __asm__("file_label");
