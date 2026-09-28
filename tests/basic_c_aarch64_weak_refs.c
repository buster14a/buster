// Clang reaches every extern-weak symbol through R_AARCH64_ADR_GOT_PAGE and
// R_AARCH64_LD64_GOT_LO12_NC, even under -fno-pic. One weak reference is
// defined by basic_c_aarch64_weak_refs_definition.c; the other is absent and
// must read as a null address. Hidden visibility keeps the absent ones from
// becoming dynamic imports, so the image stays static and runs under qemu.
extern int weak_present __attribute__((weak, visibility("hidden")));
extern int weak_absent __attribute__((weak, visibility("hidden")));
extern int weak_present_array[] __attribute__((weak, visibility("hidden")));
extern int weak_absent_array[] __attribute__((weak, visibility("hidden")));

int main(void)
{
    int status = 0;
    if (!&weak_present || weak_present != 42)
    {
        status |= 1;
    }
    if (&weak_absent)
    {
        status |= 2;
    }
    if (!weak_present_array || weak_present_array[1] != 7)
    {
        status |= 4;
    }
    if (weak_absent_array)
    {
        status |= 8;
    }
    return status;
}
