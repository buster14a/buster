__attribute__((opencl_global)) int *g;
__attribute__((opencl_local)) int *l;
long f(void) { return g - l; }
