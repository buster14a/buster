typedef unsigned long long EFI_STATUS;
typedef void *EFI_HANDLE;
typedef struct EFI_SYSTEM_TABLE EFI_SYSTEM_TABLE;

EFI_STATUS uefi_archive_value(void);

EFI_STATUS UefiMain(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *system_table)
{
    return uefi_archive_value() + (image_handle == (EFI_HANDLE)system_table);
}
