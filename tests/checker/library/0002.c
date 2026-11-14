static const char s[] = "abc";static const unsigned long n = __builtin_strlen(s);
_Static_assert(__builtin_strlen("abc" + 1) == 2 && __builtin_strcmp(&"abc"[1], "bc") == 0, "offsets");
int f(void){static const char t[] = "xyz";return (int)__builtin_strlen(t);}
