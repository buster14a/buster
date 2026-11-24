#define CAT(a,b) a##b
#define S(x) #x
CAT(R,"(x)") CAT(L,R"(y)") CAT(u8,R"(z)") S(R"(a
b)") S(R"x(e"f\)x") S(LR"(\
)")
