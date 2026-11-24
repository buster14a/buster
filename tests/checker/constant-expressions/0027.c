typedef int T [[gnu::aligned(__builtin_constant_p((int)1.0e100+1)*16)]]; _Static_assert(_Alignof(T)==16,"raw proof");
