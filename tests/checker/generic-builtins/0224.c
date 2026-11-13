int g(int);
enum { E = __builtin_invoke(g, 1) };
