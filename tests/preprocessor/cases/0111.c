#define L changed
#define u changed
#define U changed
#define u8 changed
#define e changed
#define S_(...) #__VA_ARGS__
#define S(...) S_(__VA_ARGS__)
S($L"x") S($u"x") S($U'x') S($u8"x") S(a$L"x") S($u8'x') S($1.e) S(x$1.5) S(1$e+5)
