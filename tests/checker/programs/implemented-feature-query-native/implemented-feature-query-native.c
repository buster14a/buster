#if !__has_attribute(aligned) || !__has_attribute(vector_size) || !__has_attribute(section) || !__has_attribute(visibility)
#error implemented declaration attributes must be advertised
#endif
#if !__has_feature(c_static_assert) || !__has_feature(c_alignas) || !__has_feature(c_alignof) || !__has_feature(c_generic_selections) || !__has_feature(c_atomic) || !__has_feature(c_thread_local)
#error implemented C11 features must be advertised
#endif
#if !__has_builtin(__builtin_expect) || !__has_builtin(__builtin_popcount) || !__has_builtin(__builtin_add_overflow) || !__has_builtin(__builtin_memcpy) || !__has_builtin(__c11_atomic_fetch_add) || !__has_builtin(__atomic_fetch_xor) || !__has_builtin(__sync_fetch_and_add) || !__has_builtin(__builtin_fabs) || !__has_builtin(__builtin_constant_p) || !__has_builtin(__builtin_choose_expr) || !__has_builtin(__builtin_types_compatible_p)
#error implemented source builtins must be advertised
#endif
#if !__has_constexpr_builtin(__builtin_bswap32) || !__has_constexpr_builtin(__builtin_popcount)
#error source constant evaluation must be advertised
#endif
typedef int v4si __attribute__((vector_size(16)));typedef int wide __attribute__((aligned(32)));_Static_assert(sizeof(v4si)==16 && _Alignof(wide)==32,"attribute effects");_Static_assert(__builtin_popcount(31)==5 && __builtin_bswap32(0x01020304U)==0x04030201U,"builtin constants");__attribute__((section(".query_data"),visibility("hidden"))) int seed=7;_Thread_local int local=3;int tested(int n){_Alignas(32) int a[4]={n,2,3,4};int b[4];_Atomic(int) value=5;int other=1,out=0;__builtin_memcpy(b,a,sizeof(a));int old=__c11_atomic_fetch_add(&value,n,5);int prior=__atomic_fetch_xor(&other,3,5);other+=prior;prior=__sync_fetch_and_add(&other,2);other+=prior;int overflow=__builtin_add_overflow(n,9,&out);int picked=_Generic(n,int:3,default:9);int constant=__builtin_choose_expr(__builtin_types_compatible_p(int,int),11,17)+__builtin_constant_p(42);double magnitude=__builtin_fabs((double)(n%97));local=n%13;return b[0]+(int)__builtin_popcount((unsigned)n)+__builtin_expect(out,0)+old+value+other+overflow+picked+constant+(int)magnitude+seed+local;}