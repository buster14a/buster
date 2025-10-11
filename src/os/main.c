#include <lib.h>
#  define LIMINE_API_REVISION 3
#include <limine.h>

[[noreturn]] EXPORT void _start();

#define LIMINE_REQUEST __attribute__((used, section(".limine_requests"))) LOCAL volatile

LIMINE_REQUEST LIMINE_BASE_REVISION(3);

LIMINE_REQUEST struct limine_bootloader_info_request bootloader_info_request = {
    .id = LIMINE_BOOTLOADER_INFO_REQUEST,
    .revision = LIMINE_API_REVISION,
};

LIMINE_REQUEST struct limine_executable_cmdline_request executable_cmdline_request = {
    .id = LIMINE_EXECUTABLE_CMDLINE_REQUEST,
    .revision = LIMINE_API_REVISION,
};

LIMINE_REQUEST struct limine_firmware_type_request firmware_type_request = {
    .id = LIMINE_FIRMWARE_TYPE_REQUEST,
    .revision = LIMINE_API_REVISION,
};

LOCAL constexpr u64 stack_size = MB(2);

LIMINE_REQUEST struct limine_stack_size_request stack_size_request = {
    .id = LIMINE_STACK_SIZE_REQUEST,
    .revision = LIMINE_API_REVISION,
    .stack_size = stack_size,
};

LIMINE_REQUEST struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST,
    .revision = LIMINE_API_REVISION,
};

LIMINE_REQUEST struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = LIMINE_API_REVISION,
};

LIMINE_REQUEST struct limine_paging_mode_request paging_mode_request = {
    .id = LIMINE_PAGING_MODE_REQUEST,
    .revision = LIMINE_API_REVISION,
    .mode = LIMINE_PAGING_MODE_X86_64_4LVL,
    .max_mode = LIMINE_PAGING_MODE_X86_64_4LVL,
    .min_mode = LIMINE_PAGING_MODE_X86_64_4LVL,
};

LIMINE_REQUEST struct limine_mp_request mp_request = {
    .id = LIMINE_MP_REQUEST,
    .revision = LIMINE_API_REVISION,
    .flags = LIMINE_MP_X2APIC,
};

LIMINE_REQUEST struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST,
    .revision = LIMINE_API_REVISION,
};

LIMINE_REQUEST struct limine_entry_point_request entry_point_request = {
    .id = LIMINE_ENTRY_POINT_REQUEST,
    .revision = LIMINE_API_REVISION,
    .entry = &_start,
};

LIMINE_REQUEST struct limine_executable_file_request executable_file_request = {
    .id = LIMINE_EXECUTABLE_FILE_REQUEST,
    .revision = LIMINE_API_REVISION,
};

LIMINE_REQUEST struct limine_module_request module_request = {
    .id = LIMINE_MODULE_REQUEST,
    .revision = LIMINE_API_REVISION,
};

LIMINE_REQUEST struct limine_rsdp_request rsdp_request = {
    .id = LIMINE_RSDP_REQUEST,
    .revision = LIMINE_API_REVISION,
};

LIMINE_REQUEST struct limine_smbios_request smbios_request = {
    .id = LIMINE_SMBIOS_REQUEST,
    .revision = LIMINE_API_REVISION,
};

LIMINE_REQUEST struct limine_efi_system_table_request efi_system_table_request = {
    .id = LIMINE_EFI_SYSTEM_TABLE_REQUEST,
    .revision = LIMINE_API_REVISION,
};

LIMINE_REQUEST struct limine_efi_memmap_request efi_memmap_request = {
    .id = LIMINE_EFI_MEMMAP_REQUEST,
    .revision = LIMINE_API_REVISION,
};

LIMINE_REQUEST struct limine_date_at_boot_request date_at_boot_request = {
    .id = LIMINE_DATE_AT_BOOT_REQUEST,
    .revision = LIMINE_API_REVISION,
};

LIMINE_REQUEST struct limine_executable_address_request executable_address_request = {
    .id = LIMINE_EXECUTABLE_ADDRESS_REQUEST,
    .revision = LIMINE_API_REVISION,
};

LIMINE_REQUEST struct limine_dtb_request dtb_request = {
    .id = LIMINE_DTB_REQUEST,
    .revision = LIMINE_API_REVISION,
};

LIMINE_REQUEST struct limine_bootloader_performance_request bootloader_performance_request = {
    .id = LIMINE_BOOTLOADER_PERFORMANCE_REQUEST,
    .revision = LIMINE_API_REVISION,
};

__attribute__((used, section(".limine_requests_start")))
LOCAL volatile LIMINE_REQUESTS_START_MARKER;
__attribute__((used, section(".limine_requests_end")))
LOCAL volatile LIMINE_REQUESTS_END_MARKER;

#define QEMU_EXIT_PORT 0xF4
#define QEMU_EXIT_SUCCESS 0x00000000
#define QEMU_EXIT_FAILURE 0x00000010

LOCAL void outb(u16 port, u8 value)
{
    asm volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

LOCAL u8 inb(u16 port)
{
    u8 ret;
    asm volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

[[noreturn]] LOCAL void qemu_exit(int code)
{
    outb(QEMU_EXIT_PORT, code);
    while(1){}
}

[[noreturn]] EXPORT void _start()
{
    if (LIMINE_BASE_REVISION_SUPPORTED == false)
    {
        qemu_exit(QEMU_EXIT_FAILURE);
    }

    if ((framebuffer_request.response == 0) | (framebuffer_request.response->framebuffer_count < 1))
    {
        qemu_exit(QEMU_EXIT_FAILURE);
    }

    let framebuffer = framebuffer_request.response->framebuffers[0];

    for (u64 i = 0; i < 5000; i++)
    {
        volatile u32 *fb_ptr = framebuffer->address;
        fb_ptr[i] = 0xffffff;
    }

    bool hang = false;
    while (hang)
    {
    }

    qemu_exit(QEMU_EXIT_SUCCESS);
}
