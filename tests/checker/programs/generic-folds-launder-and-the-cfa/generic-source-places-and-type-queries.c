#line 7 "dir/places.c"
static unsigned long long h;static void mix(unsigned long long v){h=h*31+v;}
static void mixs(const char* s){while(*s)mix((unsigned char)*s++);mix(0);}static const char* who(void){return __builtin_FUNCTION();}
#define HERE __builtin_LINE()
typedef int V __attribute__((vector_size(16)));typedef float W __attribute__((ext_vector_type(3)));
static const char* pf=__builtin_FILE();static const char* pn=__builtin_FILE_NAME();static const char* pu=__builtin_FUNCTION();
static unsigned line=__builtin_LINE();enum{E=__builtin_LINE(),C=__builtin_COLUMN()};_Static_assert(__builtin_LINE()==12,"");
int tested(int n){h=(unsigned)n;mixs(pf);mixs(pn);mixs(pu);mix(line);mix(E);mix(C);mix(HERE);mix(__builtin_COLUMN());
mixs(who());mixs(__builtin_FILE());V v={n};mix(__builtin_vectorelements(v));mix(__builtin_vectorelements(W));
mix(__builtin_omp_required_simd_align(double));mix(sizeof(__builtin_FILE()));mix(_Generic(__builtin_LINE(),unsigned:1,default:2));
mix(_Generic(__builtin_FILE(),char*:3,default:4));mix(_Generic(__builtin_vectorelements(v),unsigned long:5,default:6));return (int)(h^(h>>32));}
