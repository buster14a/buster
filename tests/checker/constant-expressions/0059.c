enum { N=(0 && (1/0)) + (1 || (1/0)) }; int a[N]; _Static_assert(sizeof a==4, "short circuit");
