const int k=3;int a[k];struct P{int x;};const struct P p={4};enum{E=p.x};int g=k;
int tested(int n){const int local=3;switch(n){case local:return g+E+(int)sizeof(a);default:return n+g;}}
