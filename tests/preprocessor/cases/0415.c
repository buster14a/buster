#define F(x, ...) ((x) /* fc */ + __VA_ARGS__) // trailing
#define S(x) #x
#define P a /* in */ ## b
#define O /* only */
int y = F(1, 2 /* arg */);
S(5 /* str */ 6) O P;
F(3, // line
  4) end
O
