// A stand-in libSystem for tools/test_mach_o_dylib_data_lld.sh: ld64.lld
// requires dyld_stub_binder from libSystem whenever it emits lazy stubs.
void mach_dylib_data_stub_binder(void) __asm__("dyld_stub_binder");

void mach_dylib_data_stub_binder(void)
{
}
