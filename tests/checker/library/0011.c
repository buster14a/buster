int ov(int) __attribute__((overloadable));unsigned long strlen(const char*);int calls;
static const unsigned long n = strlen((++calls, "abcd"));
