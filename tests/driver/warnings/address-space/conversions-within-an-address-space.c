__attribute__((opencl_global)) int *gl;
__attribute__((opencl_generic)) int *ge;
__attribute__((opencl_global_device)) int *gd;
__attribute__((address_space(1))) int *p;
const __attribute__((address_space(1))) int *cp;
int *q;
void f(int c) {
  ge = gl; gl = gd; ge = gd;
  q = (int *)p; p = (__attribute__((address_space(1))) int *)q;
  (void)(p == q); (void)(p < q); (void)(gl == ge);
  (void)(c ? gl : gl); (void)(c ? ge : gl); (void)(c ? p : 0);
  long d = ge - gl;
}
