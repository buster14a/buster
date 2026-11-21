int twice(void) { return 1; }
int twice(void) __asm__("twice_label");
int twice(void) __asm__("twice_label");
int pair(void) { return 2; }
int pair(void) __asm__("pair_label"), single(void) __asm__("single_label");
int labelled(void) __asm__("kept_label");
int labelled(void) { return 3; }
int labelled(void) __asm__("kept_label");
int caller(void) { return twice() + pair() + single() + labelled(); }
