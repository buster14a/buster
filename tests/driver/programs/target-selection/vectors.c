typedef int v4si __attribute__((vector_size(16)));
__attribute__((target("no-avx512vl"))) static int scalar_only(int x){return x*5;}
static int sum(int n){v4si a={n,1,2,3},b={4,5,6,7};a=a+b;return a[0]+a[3];}
int main(void){volatile int n=1;return sum(n)==15?0:1;}
