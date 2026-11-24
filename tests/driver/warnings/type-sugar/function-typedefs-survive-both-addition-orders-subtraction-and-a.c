typedef int F(void);typedef F G;
G g;F f;
void h(void){int a=f+1;int b=1+f;int c=f-1;int d=g+2;int e=&f;int z=*&f+3;int (*raw)(void)=f;int w=raw+1;}
