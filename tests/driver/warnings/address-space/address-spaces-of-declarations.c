#define AS1 __attribute__((address_space(1)))
AS1 int a;
int b __attribute__((address_space(2)));
int * __attribute__((address_space(3))) c;
__attribute__((opencl_global)) int g;
__attribute__((opencl_global_device)) int gd;
__attribute__((opencl_global_host)) int gh;
__attribute__((opencl_local)) const int *l;
typedef __attribute__((address_space(4))) int T4;
T4 t4[3];
int __attribute__((address_space(5))) __attribute__((address_space(5))) twice;
__seg_gs int *gs;
__seg_fs int *fs;
__attribute__((address_space(0))) int zero;
__attribute__((address_space(8388580))) int highest;
AS1 int *f(void) { return &a; }
AS1 int (*h(void))[3];
