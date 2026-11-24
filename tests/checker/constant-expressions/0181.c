typedef int T [[gnu::aligned(1 << 4)]]; _Static_assert(_Alignof(T)==16, "raw arithmetic");
