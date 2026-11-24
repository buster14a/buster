#define F(x) __VA_OPT__(x)
#define G __VA_OPT__
#define H(...) __VA_OPT__(__VA_OPT__())
#define I(...) __VA_OPT__ x
#define J(...) __VA_OPT__
#define K(...) __VA_OPT__(## a)
#define L(...) __VA_OPT__((a ##))
#define M(...) __VA_OPT__(a ##)
#define N(...) #__VA_ARGS__ __VA_OPT__(a ## b)
