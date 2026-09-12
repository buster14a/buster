// The original narrow signature rejection is now a strict MIR success.
// Keep its source and function identity while the wider control stays separate.
typedef unsigned long long MachineFallbackVector __attribute__((vector_size(8)));

int machine_fallback_signature(MachineFallbackVector value)
{
    return (int)value[0];
}
