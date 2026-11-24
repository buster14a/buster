#define F(a, b) b a
F(__COUNTER__, __COUNTER__)
#define G(a, ...) __VA_OPT__(x) a
G(__COUNTER__, __COUNTER__)
#define H(a, b) #a b a
H(__COUNTER__, __COUNTER__)
#define K(a, ...) [__VA_OPT__(a)]
K(__has_include)
K(_Pragma("ppf"))
