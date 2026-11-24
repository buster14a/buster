typedef int A __attribute__((ext_vector_type(2)));typedef int B __attribute__((ext_vector_type(4)));typedef double C __attribute__((vector_size(32)));
static int calls;static A side(void){calls++;A a={0};return a;}
int tested(int n){A a={n};B b={n};C c={0};unsigned long r=__builtin_vectorelements(a,b)*1000+__builtin_vectorelements(b,a)*100+
__builtin_vectorelements(side(),b,c)*10+__builtin_vectorelements(B);return (int)r+calls*100000+n;}
