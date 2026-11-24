typedef int I [[gnu::aligned(1<<4)]]; _Static_assert(_Alignof(I)==16,"ICE");
