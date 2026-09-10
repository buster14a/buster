// Win64 still excludes over-aligned aggregate parameters from machine selection.
// Darwin AArch64 still excludes variadic definitions. Keep both signature
// failures explicit as supported shapes move into the machine backend.
#if defined(_WIN32) && defined(__x86_64__)
struct MachineFallbackParameter { _Alignas(32) long long first; };
int machine_fallback_signature(struct MachineFallbackParameter value, ...)
{
    return (int)value.first;
}
#else
int machine_fallback_signature(int value, ...)
{
    return value;
}
#endif
