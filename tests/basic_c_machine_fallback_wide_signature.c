// An explicit remaining 32-byte signature gap supplies the diagnostic control.
// The original narrow signature fixture is retained as a strict success.
typedef unsigned long long MachineFallbackWideVector __attribute__((vector_size(32)));

int machine_fallback_wide_signature(MachineFallbackWideVector value)
{
    return (int)value[0];
}
