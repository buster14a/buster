typedef int F(void);typedef F *FP;
struct S{F *p;FP q;};
void h(struct S s,FP p){int a=s.p+1;int b=p+1;int c=(F*)0+1;int d=*(F*)0+1;}
