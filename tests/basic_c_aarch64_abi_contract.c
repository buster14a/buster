// Cross-target controls for the AArch64 UEFI contract. Compile this ordinary C
// fixture for all six AArch64 OS rows; it must not inherit the host data model.
#if !defined(__aarch64__)
#error "AArch64 control fixture"
#endif
_Static_assert(sizeof(void *) == 8 && _Alignof(void *) == 8, "AArch64 pointer layout");
_Static_assert(_Alignof(__builtin_va_list) == 8, "AArch64 list alignment");
#if defined(_WIN32)
_Static_assert(sizeof(long) == 4 && _Alignof(long) == 4, "Windows LLP64");
_Static_assert(sizeof(long double) == 8 && _Alignof(long double) == 8, "Windows long double");
_Static_assert(sizeof(__builtin_va_list) == 8, "Windows pointer list");
#elif defined(__APPLE__)
_Static_assert(sizeof(long) == 8 && _Alignof(long) == 8, "Darwin LP64");
_Static_assert(sizeof(long double) == 8 && _Alignof(long double) == 8, "Darwin long double");
_Static_assert(sizeof(__builtin_va_list) == 8, "Darwin pointer list");
#else
_Static_assert(sizeof(long) == 8 && _Alignof(long) == 8, "AAPCS64 LP64");
_Static_assert(sizeof(long double) == 16 && _Alignof(long double) == 16, "AAPCS64 binary128 storage");
_Static_assert(sizeof(__builtin_va_list) == 32, "AAPCS64 public list");
#endif
#if defined(_WIN32) || defined(__UEFI__)
_Static_assert(__WCHAR_WIDTH__ == 16, "Windows and UEFI short wchar");
#else
_Static_assert(__WCHAR_WIDTH__ == 32, "hosted non-Windows wchar");
#endif
int aarch64_abi_contract(void)
{
    return 0;
}
