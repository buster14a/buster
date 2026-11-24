unsigned long f(int n) { typedef int A[n++]; __typeof__(A) a; __typeof__(int[n++]) b; return sizeof a+sizeof b; }
