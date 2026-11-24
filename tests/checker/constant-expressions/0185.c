typedef long double T; typedef int I [[gnu::aligned(sizeof(T))]]; _Static_assert(_Alignof(I)==16, "raw typedef");
