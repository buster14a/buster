#if !defined(__UEFI__)
#error "UEFI target macro is missing"
#endif

#if __STDC_HOSTED__ != 0
#error "UEFI must be a freestanding C environment"
#endif

_Static_assert(__WCHAR_WIDTH__ == 16, "UEFI wchar_t must be 16 bits");
_Static_assert(sizeof(void *) == 8, "UEFI target must use PE32+");

#if defined(__x86_64__)
_Static_assert(sizeof(long) == 4, "x86-64 UEFI uses LLP64");
_Static_assert(sizeof(long double) == 8, "x86-64 UEFI uses the Microsoft long double model");
#elif defined(__aarch64__)
_Static_assert(sizeof(long) == 8, "AArch64 UEFI uses LP64");
_Static_assert(sizeof(long double) == 16, "AArch64 UEFI uses the AAPCS64 long double model");
#else
#error "unsupported UEFI architecture"
#endif

typedef unsigned long long EFI_STATUS;
typedef void *EFI_HANDLE;
typedef struct EFI_SYSTEM_TABLE EFI_SYSTEM_TABLE;

EFI_STATUS UefiMain(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *system_table);

EFI_STATUS (*firmware_entry_address)(EFI_HANDLE, EFI_SYSTEM_TABLE *) = &UefiMain;

EFI_STATUS UefiMain(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *system_table)
{
    return image_handle == (EFI_HANDLE)system_table;
}
