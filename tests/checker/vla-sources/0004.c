unsigned long f(int n) { __typeof__(int[n++]) a; return sizeof a; }
