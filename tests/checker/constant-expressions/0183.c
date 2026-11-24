typedef int T [[gnu::aligned((unsigned)(1?16:1/0))]]; _Static_assert(_Alignof(T)==16, "raw conditional");
