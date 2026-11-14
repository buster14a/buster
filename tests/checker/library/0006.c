unsigned long strlen(const char*);unsigned long strlen(int) __attribute__((overloadable));
static const unsigned long n = strlen("abcd");_Static_assert(strlen("abc") == 3, "ordinary first");
