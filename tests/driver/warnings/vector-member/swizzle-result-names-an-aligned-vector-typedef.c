typedef float v4 __attribute__((ext_vector_type(4)));
typedef float v2 __attribute__((ext_vector_type(2), aligned(32)));
void f(v4 v, const v4 c) { (void)&v.xx; (void)&c.xx; (void)v.xx.z; }
