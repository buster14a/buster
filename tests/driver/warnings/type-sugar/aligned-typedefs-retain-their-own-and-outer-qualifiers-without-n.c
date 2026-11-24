typedef const int CI __attribute__((aligned(8)));
typedef int *P __attribute__((aligned(32)));
void f(void){volatile CI *a=1;const P b=1;P c=1;char *d=1;}
