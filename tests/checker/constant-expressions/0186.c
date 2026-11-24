typedef int T [[gnu::aligned(sizeof(__typeof__(long double)))]]; _Static_assert(_Alignof(T)==16, "raw typeof");
