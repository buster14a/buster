typedef int T [[gnu::aligned(1 << __builtin_ctz(16))]]; _Static_assert(_Alignof(T)==16, "raw builtin");
