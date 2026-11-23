struct S{int member}; struct S s; _Static_assert(sizeof s==sizeof(int),"member");
