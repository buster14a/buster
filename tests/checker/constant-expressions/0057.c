enum { N=1?7:1/0 }; int a[N]; _Static_assert(sizeof a==28, "conditional");
