#define F(x, ...) #__VA_OPT__(a x+b)
F(, 1)
#define G(x, ...) #__VA_OPT__(a x+b x-c)
G(, 1)
#define K(x, ...) #__VA_OPT__(a x) y
K(, 1)
