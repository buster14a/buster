#pragma weak run = first
#pragma weak run = first
int first(void) { return 1; }
int before(void) { return run(); }

int second(int x) { return x; }
#pragma weak run = second
int (*saved)(int) = run;
int both(void) { return run() + run(2); }

int run(int);
int after(void) { return run() + run(3); }
