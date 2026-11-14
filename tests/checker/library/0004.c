unsigned long strlen(const char*);int strcmp(const char*, const char*);int memcmp(const void*, const void*, unsigned long);
static const unsigned long n = strlen("abcd");_Static_assert(strcmp("a", "b") < 0 && memcmp("ab", "ac", 2) == -1, "plain");
