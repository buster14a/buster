int a(int x){return !x|1;}
int b(int x){return !x^1;}
int c(int x,int y){return x&!y;}
int d(int x,int y){return !x&!y;}
int e(int x,_Bool y){return !x&y;}
int f(_Bool x){return !x&1;}
int g(int x){return (!x)&1;}
int h(int x){return !(!x)&1;}
int i(int x,int y,int z){return !(x==y)<z;}
int j(int x,int y){return !x<(_Bool)y;}
int k(int x,_Bool y){return !x&+y;}
int l(int x,int y){return !x&(y=(x==1));}
