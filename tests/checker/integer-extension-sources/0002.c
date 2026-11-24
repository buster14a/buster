int a[((1<<32)==(-2147483647-1))*3]; typedef int I [[gnu::aligned(((1<<33)==(-2147483647-1))*16)]]; _Static_assert(sizeof a==12 && _Alignof(I)==16,"folded bound and attribute");
