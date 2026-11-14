#define T(e, t) _Static_assert(_Generic((e), t: 1, default: 0), #e);
void f(void* p, int v)
{
T(__builtin_extend_pointer(p), unsigned long long) __builtin_unwind_init(); __builtin_init_dwarf_reg_size_table(p);
__builtin_set_flt_rounds(v); __builtin_set_flt_rounds(3.0);
if (v) __builtin_verbose_trap("category", "message");
if (v > 1) __builtin_verbose_trap(__func__, (("x" "y")));
if (v > 2) __builtin_verbose_trap((const char*)"a", u8"b");
}
