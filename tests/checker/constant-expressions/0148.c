enum { N=__builtin_choose_expr(((__int128)1<<100)|1,7,0) }; _Static_assert(N==7, "wide condition");
