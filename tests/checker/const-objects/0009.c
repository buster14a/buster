int get(void);int f(void){const int a[3]={[0 ... 2]=get(),[0]=1,[1]=2,[2]=3};enum{E=a[0]};_Static_assert(E==1,"range");return E;}
