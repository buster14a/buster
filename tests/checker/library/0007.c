unsigned long strlen(int) __attribute__((overloadable));unsigned long strlen(const char*);
static const unsigned long n = strlen("abcd");_Static_assert(strlen("abc") == 3, "ordinary last");
