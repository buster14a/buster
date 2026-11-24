#define CALL(f) f()
#define OUT(x) CALL(x)
#define F() missing
int a=OUT(F);
