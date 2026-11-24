typedef int A __attribute__((aligned(16))), B __attribute__((aligned(2))); _Static_assert(_Alignof(A)==16 && _Alignof(B)==2,"separate");
