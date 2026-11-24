#define S(x) #x
#define T(x) S(x)
#define B(...) >= ## __VA_OPT__( )( )
T(B())
#define C(...) >= ## __VA_OPT__(x)( )
T(C())
#define D(...) x __VA_OPT__(a )y
T(D(1))
#define E(...) x #__VA_OPT__()y
T(E())
#define F(...) x ## __VA_OPT__()y
T(F())
