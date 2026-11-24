int f(void){
#pragma clang diagnostic ignored "-Wshift-overflow"
int a=3<<31;int b=1<<3+1;return a+b;}
