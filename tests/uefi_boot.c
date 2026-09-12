// Firmware execution fixture. UefiMain checks the firmware entry ABI, calls
// across a real stack frame and relocated initialized pointers, then prints
// an architecture-specific marker and requests EFI_RESET_SHUTDOWN.
// The small declarations below describe only the UEFI 2.x fields we use.
typedef unsigned long long EfiUintn;
typedef unsigned int EfiUint32;
typedef unsigned short EfiChar16;
typedef struct EfiTableHeader EfiTableHeader;
struct EfiTableHeader
{
    EfiUintn signature;
    EfiUint32 revision;
    EfiUint32 header_size;
    EfiUint32 crc32;
    EfiUint32 reserved;
};
typedef struct EfiTextOutput EfiTextOutput;
struct EfiTextOutput
{
    void *reset;
    EfiUintn (*output_string)(EfiTextOutput *, EfiChar16 *);
};
typedef struct EfiRuntimeServices EfiRuntimeServices;
struct EfiRuntimeServices
{
    EfiTableHeader header;
    void *before_reset[10];
    void (*reset_system)(EfiUint32, EfiUintn, EfiUintn, void *);
};
typedef struct EfiSystemTable EfiSystemTable;
struct EfiSystemTable
{
    EfiTableHeader header;
    EfiChar16 *firmware_vendor;
    EfiUint32 firmware_revision;
    void *console_in_handle;
    void *console_in;
    void *console_out_handle;
    EfiTextOutput *console_out;
    void *standard_error_handle;
    EfiTextOutput *standard_error;
    EfiRuntimeServices *runtime;
};
_Static_assert(sizeof(void *) == 8, "64-bit firmware ABI");
_Static_assert(sizeof(EfiTableHeader) == 24, "EFI_TABLE_HEADER layout");
_Static_assert(__builtin_offsetof(EfiSystemTable, console_out) == 64, "ConOut offset");
_Static_assert(__builtin_offsetof(EfiSystemTable, runtime) == 88, "RuntimeServices offset");
_Static_assert(__builtin_offsetof(EfiRuntimeServices, reset_system) == 104, "ResetSystem offset");

static volatile EfiUintn values[] = {0x11223344, 7, 0x1234};
static volatile EfiUintn *volatile relocated_data = &values[1];
static volatile EfiUintn zero_data[4];

__attribute__((noinline)) static EfiUintn firmware_call(EfiUintn a, EfiUintn b, EfiUintn c, EfiUintn d, EfiUintn e,
                                                       EfiUintn f, EfiUintn g, EfiUintn h, EfiUintn i, EfiUintn j)
{
    volatile EfiUintn frame[32];
    EfiUintn sum = 0;
    for (EfiUintn index = 0; index < 32; index += 1)
    {
        frame[index] = *relocated_data + index;
    }
    for (EfiUintn index = 0; index < 32; index += 1)
    {
        sum += frame[index];
    }
    return a + 2*b + 3*c + 4*d + 5*e + 6*f + 7*g + 8*h + 9*i + 10*j + sum + values[2];
}
static EfiUintn (*volatile relocated_call)(EfiUintn, EfiUintn, EfiUintn, EfiUintn, EfiUintn,
                                          EfiUintn, EfiUintn, EfiUintn, EfiUintn, EfiUintn) = &firmware_call;

EfiUintn UefiMain(void *image_handle, EfiSystemTable *table)
{
    EfiUintn status = 0x8000000000000015ull; // EFI_ABORTED
    if (image_handle && table && table->header.signature == 0x5453595320494249ull &&
        table->console_out && table->console_out->output_string && table->runtime && table->runtime->reset_system)
    {
        // Both pinned machines place RAM below 4 GiB, unlike the 5 GiB PE base.
        int passed = (EfiUintn)&UefiMain < 0x100000000ull && relocated_data == &values[1] && relocated_call == &firmware_call &&
                     zero_data[0] == 0 && zero_data[3] == 0 &&
                     firmware_call(1, 2, 3, 4, 5, 6, 7, 8, 9, 10) == 5765 &&
                     relocated_call(1, 2, 3, 4, 5, 6, 7, 8, 9, 10) == 5765;
        zero_data[3] = 0x4567;
        passed = passed && zero_data[3] == 0x4567;
#if defined(BUSTER_UEFI_NEGATIVE) && BUSTER_UEFI_NEGATIVE
        passed = 0;
#endif
#if defined(__x86_64__)
        EfiChar16 *marker = L"BUSTER_UEFI_BOOT_PASS x86_64\r\n";
#else
        EfiChar16 *marker = L"BUSTER_UEFI_BOOT_PASS aarch64\r\n";
#endif
        EfiUintn printed = table->console_out->output_string(table->console_out,
            passed ? marker : L"BUSTER_UEFI_BOOT_FAIL\r\n");
        if (passed && printed == 0)
        {
            // QEMU exits zero only after the guest actually requests shutdown.
            table->runtime->reset_system(2, 0, 0, 0);
        }
        table->console_out->output_string(table->console_out, L"BUSTER_UEFI_BOOT_FAIL\r\n");
        table->runtime->reset_system(2, status, 0, 0);
    }
    return status;
}
