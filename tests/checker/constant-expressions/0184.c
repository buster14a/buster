typedef int T [[gnu::aligned(sizeof(int[2+sizeof(char[2])]))]]; _Static_assert(_Alignof(T)==16, "raw type");
