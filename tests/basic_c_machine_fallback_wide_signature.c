// The former 32-byte Win64 signature rejection is now a strict MIR success.
// At baseline it crosses the boundary as two indirect sixteen-byte references.
typedef unsigned long long MachineFallbackWideVector __attribute__((vector_size(32)));

int machine_fallback_wide_signature(MachineFallbackWideVector value)
{
    return (int)value[0];
}
