// AAPCS64 puts 1.0 in d0 and 11 in x1. Windows variadic lowering instead
// puts them in x1 and x2; a PE/COFF target triple cannot stand in for AAPCS64.
extern int uefi_llvm_receiver(int marker, ...);
int uefi_llvm_caller(void)
{
    return uefi_llvm_receiver(7, 1.0, 11UL);
}
