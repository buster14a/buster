static const unsigned long a = __builtin_strlen("abcd");static const int b = __builtin_strcmp("a", "b");
_Static_assert(__builtin_strcmp("a", "b") < 0 && __builtin_strcmp("b", "b") == 0, "strcmp");
_Static_assert(__builtin_strncmp("abc", "abd", 2) == 0 && __builtin_strncmp("ab", "abc", 5) == -1, "strncmp");
_Static_assert(__builtin_memcmp("ab\0c", "ab\0d", 4) == -1 && __builtin_bcmp("ab", "ab", 2) == 0, "memcmp");
_Static_assert(__builtin_strcmp("\x80", "\x01") == 1, "unsigned bytes");
_Static_assert(__builtin_wcslen(L"wide") == 4 && __builtin_wcscmp(L"\x80000000", L"\x01") == -1, "wide");
_Static_assert(__builtin_wcsncmp(L"ab", L"ac", 1) == 0 && __builtin_wmemcmp(L"ab", L"ac", 2) == -1, "wide bounded");
int c[__builtin_strlen("abc")];enum { E = __builtin_strlen("xy") };
int f(int x){switch(x){case __builtin_strlen("ab"):return 1;case __builtin_wcslen(L"abc"):return 2;}return 0;}
