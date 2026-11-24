enum { A=1, B=3, V=7 }; int values[]={[A]=V,[B]=V+1}; _Static_assert(sizeof values==16, "inferred after initializer"); int f(void){enum {A=2,B=4}; int local[]={[A]=V,[B]=V}; return sizeof local==20;}
