// The direct emitter supports narrow Win64 vectors; their MIR signature
// transport remains a separate capability, independent of argument count.
typedef unsigned long long MachineFallbackVector __attribute__((vector_size(8)));

int machine_fallback_signature(MachineFallbackVector value)
{
    return (int)value[0];
}
