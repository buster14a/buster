int calls;int get(void){calls+=1;return 9;}
int tested(int n){const int a[3]={[0 ... 2]=get(),[0]=1,[1]=2,[2]=3};enum{E=a[0]};return E+a[1]+a[2]+calls+n;}
