void a1(void) __attribute__((sysv_abi));
void a2(void) __attribute__((ms_abi));
void a3(void) __attribute__((vectorcall));
void a4(void) __attribute__((regcall));
void a5(void) __attribute__((preserve_all));
void a8(void) __attribute__((no_caller_saved_registers));
void a9(void) __attribute__((intel_ocl_bicc));
void a10(void) __attribute__((swiftcall));
void a11(void) __attribute__((swiftasynccall));
void __regcall a12(void);
void __vectorcall a13(void);
void a15(void) __attribute__((__ms_abi__));
void a16(void) __attribute__((ms_abi, no_caller_saved_registers));
void a17(void) __attribute__((cdecl));
void (*b9)(void) __attribute__((intel_ocl_bicc));
void (__regcall *b12)(void);
void (* __attribute__((vectorcall)) b3)(void);
void h(void)
{
    int x1 = a1, x2 = a2, x3 = a3, x4 = a4, x5 = a5, x8 = a8, x9 = a9, x10 = a10, x11 = a11;
    int x12 = a12, x13 = a13, x15 = a15, x16 = a16, x17 = a17, y9 = b9, y12 = b12, y3 = b3;
}
