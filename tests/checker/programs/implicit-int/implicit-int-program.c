static x = 3;
helper(a) int a; { return a + 1; }
tested(n) { register i; i = helper(x + n); return i; }
