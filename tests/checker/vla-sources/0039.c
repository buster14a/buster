void f(void) { int a[(int)(1.0+2.0)]; _Static_assert(sizeof a==12,"still variable"); }
