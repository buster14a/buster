/* A named-section fixture for source -> CAST -> unsigned conversion -> ELF. */
__attribute__((section(".buster_investigation")))
unsigned long long investigation_before(void)
{
    return 17;
}

__attribute__((section(".buster_investigation")))
unsigned long long cast_unsigned(double value)
{
    return (unsigned long long)value;
}
