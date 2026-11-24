typedef int T [[gnu::aligned(sizeof(int(*)(int)))]]; _Static_assert(_Alignof(T)==8,"raw function pointer");
