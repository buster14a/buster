void f(int *,int count,void callback(int *),int *last);
void g(const int *p){f(p,0,0,p);}
