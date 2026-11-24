#if u'a' < -1
unsigned_char16
#else
wrong_signed_char16
#endif
#if U'a' < -1
unsigned_char32
#endif
#if 'a' < -1
wrong
#else
signed_char
#endif
#if L'a' < -1
wrong
#else
signed_wchar
#endif
