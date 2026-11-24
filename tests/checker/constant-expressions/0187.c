int x __attribute__((aligned(32))); typedef int T [[gnu::aligned(__alignof__((x)))]]; _Static_assert(_Alignof(T)==32, "raw object");
