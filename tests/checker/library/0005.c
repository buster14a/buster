int ov(int) __attribute__((overloadable));double ov(double) __attribute__((overloadable));
unsigned long strlen(const char*);int strcmp(const char*, const char*);int memcmp(const void*, const void*, unsigned long);
static const unsigned long n = strlen("abcd");_Static_assert(strcmp("a", "c") == -1, "ordinary");
enum { length = strlen("abc") };int a[strlen("abc")];
int f(int x){switch(x){case memcmp("ab", "ad", 2):return length;}return 0;}
