extern int external(int);
int function(int x) { return external(x + 7); }
int (*pointer)(int) = function;
