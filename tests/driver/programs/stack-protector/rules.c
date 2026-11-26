void use(void*);
struct S { int a; char b[4]; };
struct T { int a; int b; };
struct U { char big[16]; };
int f_char8(void) { char b[8]; use(b); return 0; }
int f_char7(void) { char b[7]; use(b); return 0; }
int f_int4(void) { int b[4]; use(b); return 0; }
int f_int_noescape(void) { int b[4] = { 1, 2, 3, 4 }; return b[1]; }
int f_addr(void) { int x; use(&x); return x; }
int f_struct_arr(void) { struct S s; use(&s); return s.a; }
int f_struct_noarr(void) { struct T t; use(&t); return t.a; }
int f_struct_big(void) { struct U u; use(&u); return 0; }
int f_vla(int n) { char b[n]; use(b); return 0; }
int f_alloca(int n) { char* b = __builtin_alloca(n); use(b); return 0; }
int f_alloca_const(void) { char* b = __builtin_alloca(8); use(b); return 0; }
int f_plain(int x) { return x + 1; }
int f_addr_noescape(int x) { int* p = &x; return *p; }
int f_param_addr(int x) { use(&x); return 0; }
int f_char_ptr_local(void) { char* p = 0; use(&p); return 0; }
int f_struct_nested(void) { struct { struct S s; } w; use(&w); return 0; }
int f_nested_big(void) { struct { int x; struct { char n[9]; } in; } s; use(&s); return 0; }
int f_union_arr(void) { union { char c[2]; int i; } u; use(&u); return 0; }
int f_union16(void) { union { int i; char c[16]; } u; use(&u); return 0; }
int f_2d(void) { char b[4][4]; use(b); return 0; }
int f_array_of_records(void) { struct { char c[8]; } s[2]; use(s); return 0; }
int f_bool(void) { _Bool b[8]; use(b); return 0; }
int f_uchar(void) { unsigned char b[8]; use(b); return 0; }
int f_compound(void) { use((char[16]){ 0 }); return 0; }
int f_string(void) { char s[] = "hello world"; use(s); return 0; }
int f_volatile_array(void) { volatile char b[16]; b[0] = 1; return b[0]; }
__attribute__((no_stack_protector)) int f_before(void);
int f_before(void) { char b[16]; use(b); return 0; }
__attribute__((no_stack_protector)) int f_exempt(int x) { char b[64]; use(b); return x; }
int f_callee(int);
int f_musttail(int x) { char b[16]; use(b); __attribute__((musttail)) return f_callee(x); }
