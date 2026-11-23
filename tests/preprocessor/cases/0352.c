#define c(x) x
#define d(e) e
#define h d("s") c(
#ident h
#ident "t" _Pragma("message(\"m\")")
#ident "u" __COUNTER__
__COUNTER__
