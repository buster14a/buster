int object=9;int* const p=&object;int* q=p;const int k=3;const int* const r=&k;int g=*r;
const double d=3.5;int h=(int)d;
int tested(int n){return *q+g+h+n;}
