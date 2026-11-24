void f(void) { int a[(1,3)]; _Static_assert(sizeof a==12,"still variable"); }
