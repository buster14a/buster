enum { N=sizeof(__fp16) }; _Static_assert(N==2,"half storage"); typedef int T [[gnu::aligned(sizeof(__fp16))]]; _Static_assert(_Alignof(T)==2,"raw half");
