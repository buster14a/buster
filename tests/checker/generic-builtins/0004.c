#define T(e, t) _Static_assert(_Generic((e), t: 1, default: 0), #e);
void f(char* x, void* p, const void* q, int v)
{
T(__builtin_readcyclecounter(), unsigned long long) T(__builtin_readsteadycounter(), unsigned long long)
T(__builtin_thread_pointer(), void*) T(__builtin_stack_address(), void*) T(__builtin_dwarf_sp_column(), unsigned int)
__builtin_debugtrap(); __builtin___clear_cache(x, x + 4); __builtin_assume_separate_storage(p, q);
__builtin_assume_dereferenceable(q, 8); __builtin_memcpy_inline(p, q, 13); __builtin_memset_inline(p, v, sizeof(int) * 2);
}
