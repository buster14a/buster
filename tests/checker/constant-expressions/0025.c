typedef int T [[gnu::aligned(sizeof(int(int)))]]; _Static_assert(_Alignof(T)==1,"raw function");
