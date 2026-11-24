int put(int*,int); int f(int *p,int n){return (unsigned char)((n<128)?(*p=n),1:put(p,n));}
