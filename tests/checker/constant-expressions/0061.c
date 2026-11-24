typedef short T; T object; enum { N=sizeof(T)+sizeof object }; int a[N]; _Static_assert(sizeof a==16, "bindings");
