enum { A=4 }; typedef int T [[gnu::aligned(A << 2)]]; _Static_assert(_Alignof(T)==16, "raw binding");
