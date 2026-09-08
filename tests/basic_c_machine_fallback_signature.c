// The named argument is legal on every native ABI. Win64 and Darwin AArch64
// deliberately keep variadic definitions on the canonical path, even when
// the body never reads the anonymous arguments.
int machine_fallback_signature(int value, ...)
{
    return value;
}
