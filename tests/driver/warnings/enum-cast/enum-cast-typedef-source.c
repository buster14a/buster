typedef enum { TA, TB } TAnon;
typedef enum { UA, UB } UAnon;
void f(void){ TAnon a; UAnon u; a = u; u = a; (void)a;(void)u; }
void g(void){ TAnon a = UA; UAnon u = TB; (void)a; (void)u; }
typedef enum { VA } VAnon, VAlias;
VAlias h(UAnon x) { return x; }
