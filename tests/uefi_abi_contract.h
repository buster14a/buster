#ifndef BUSTER_TEST_UEFI_ABI_CONTRACT_H
#define BUSTER_TEST_UEFI_ABI_CONTRACT_H

// Native UEFI ABI contract. PE/COFF does not select the Windows C ABI on
// AArch64. Keep this fixture shared by the structural and real firmware gates.
_Static_assert(__STDC_HOSTED__ == 0, "UEFI is freestanding");
_Static_assert(__WCHAR_WIDTH__ == 16, "UEFI wchar_t width");
_Static_assert(sizeof(void *) == 8 && _Alignof(void *) == 8, "UEFI pointer layout");
_Static_assert(_Alignof(__builtin_va_list) == 8, "UEFI va_list alignment");

struct UefiAbiRecord
{
    char prefix;
    long integer;
    long double wide;
    char suffix;
};
struct UefiAbiListRecord
{
    volatile unsigned long long before;
    __builtin_va_list list;
    volatile unsigned long long after;
};

#if defined(__aarch64__)
#if !defined(__CHAR_UNSIGNED__)
#error "AAPCS64 plain char is unsigned"
#endif
_Static_assert(sizeof(long) == 8 && _Alignof(long) == 8, "AArch64 UEFI is LP64");
_Static_assert(sizeof(long double) == 16 && _Alignof(long double) == 16, "AAPCS64 binary128 storage");
_Static_assert(sizeof(__builtin_va_list) == 32, "AAPCS64 public va_list extent");
_Static_assert(__builtin_offsetof(struct UefiAbiRecord, integer) == 8, "AAPCS64 long record offset");
_Static_assert(__builtin_offsetof(struct UefiAbiRecord, wide) == 16, "AAPCS64 wide record offset");
_Static_assert(__builtin_offsetof(struct UefiAbiRecord, suffix) == 32, "AAPCS64 record suffix");
_Static_assert(sizeof(struct UefiAbiRecord) == 48 && _Alignof(struct UefiAbiRecord) == 16, "AAPCS64 record layout");
_Static_assert(__builtin_offsetof(struct UefiAbiListRecord, after) == 40, "AAPCS64 list record extent");
_Static_assert(sizeof(struct UefiAbiListRecord) == 48, "AAPCS64 list record size");
#elif defined(__x86_64__)
_Static_assert(sizeof(long) == 4 && _Alignof(long) == 4, "x86-64 UEFI is LLP64");
_Static_assert(sizeof(long double) == 8 && _Alignof(long double) == 8, "Microsoft long double storage");
_Static_assert(sizeof(__builtin_va_list) == 8, "Microsoft x64 pointer list extent");
_Static_assert(__builtin_offsetof(struct UefiAbiRecord, integer) == 4, "LLP64 long record offset");
_Static_assert(__builtin_offsetof(struct UefiAbiRecord, wide) == 8, "LLP64 wide record offset");
_Static_assert(__builtin_offsetof(struct UefiAbiRecord, suffix) == 16, "LLP64 record suffix");
_Static_assert(sizeof(struct UefiAbiRecord) == 24 && _Alignof(struct UefiAbiRecord) == 8, "LLP64 record layout");
_Static_assert(__builtin_offsetof(struct UefiAbiListRecord, after) == 16, "Microsoft list record extent");
_Static_assert(sizeof(struct UefiAbiListRecord) == 24, "Microsoft list record size");
#else
#error "unsupported UEFI architecture"
#endif
_Static_assert(__builtin_offsetof(struct UefiAbiListRecord, list) == 8, "list is immediately after its guard");

// Consume all integer argument registers and continue onto the caller's stack.
// Copy a partially consumed list; both cursors must then advance independently.
// The adjacent guards detect writes beyond the public list object.
__attribute__((noinline)) unsigned long long uefi_abi_variadic(unsigned long long marker, ...)
{
    struct UefiAbiListRecord original;
    struct UefiAbiListRecord copied;
    original.before = copied.before = 0x123456789abcdef0ULL;
    original.after = copied.after = 0xfedcba9876543210ULL;
    __builtin_va_start(original.list, marker);
    unsigned long long sum = marker + __builtin_va_arg(original.list, unsigned long long);
    __builtin_va_copy(copied.list, original.list);
    unsigned long long copy_sum = sum;
    for (unsigned long long index = 2; index <= 12; index += 1)
    {
        sum += index * __builtin_va_arg(original.list, unsigned long long);
    }
    for (unsigned long long index = 2; index <= 12; index += 1)
    {
        copy_sum += index * __builtin_va_arg(copied.list, unsigned long long);
    }
    __builtin_va_end(copied.list);
    __builtin_va_end(original.list);
    int intact = original.before == 0x123456789abcdef0ULL && original.after == 0xfedcba9876543210ULL &&
                 copied.before == 0x123456789abcdef0ULL && copied.after == 0xfedcba9876543210ULL;
    return intact && sum == copy_sum ? sum : 0;
}

#if defined(__aarch64__)
// Independent AAPCS64 producer: the fixed prototype reserves five outgoing
// stack slots. This leaf overwrites x0-x7 and those slots itself, then tail-calls
// the C consumer without moving SP or LR. It needs no frame/unwind record.
unsigned long long uefi_abi_reference_caller(unsigned long long, unsigned long long, unsigned long long,
    unsigned long long, unsigned long long, unsigned long long, unsigned long long, unsigned long long,
    unsigned long long, unsigned long long, unsigned long long, unsigned long long, unsigned long long);
unsigned long long uefi_abi_reference_receiver(unsigned long long, ...);
__asm__(
    ".text\n"
    ".p2align 2\n"
    ".globl uefi_abi_reference_caller\n"
    "uefi_abi_reference_caller:\n"
    "movz x0, #17\n"
    "movz x1, #1\n"
    "movz x2, #2\n"
    "movz x3, #3\n"
    "movz x4, #4\n"
    "movz x5, #5\n"
    "movz x6, #6\n"
    "movz x7, #7\n"
    "movz x9, #8\n"
    "str x9, [sp]\n"
    "movz x9, #9\n"
    "str x9, [sp, #8]\n"
    "movz x9, #10\n"
    "str x9, [sp, #16]\n"
    "movz x9, #11\n"
    "str x9, [sp, #24]\n"
    "movz x9, #12\n"
    "str x9, [sp, #32]\n"
    "b uefi_abi_variadic\n"
    ".p2align 2\n"
    ".globl uefi_abi_reference_receiver\n"
    "uefi_abi_reference_receiver:\n"
    "movz x9, #17\n"
    "eor x10, x0, x9\n"
    "movz x9, #1\n"
    "eor x11, x1, x9\n"
    "orr x10, x10, x11\n"
    "movz x9, #2\n"
    "eor x11, x2, x9\n"
    "orr x10, x10, x11\n"
    "movz x9, #3\n"
    "eor x11, x3, x9\n"
    "orr x10, x10, x11\n"
    "movz x9, #4\n"
    "eor x11, x4, x9\n"
    "orr x10, x10, x11\n"
    "movz x9, #5\n"
    "eor x11, x5, x9\n"
    "orr x10, x10, x11\n"
    "movz x9, #6\n"
    "eor x11, x6, x9\n"
    "orr x10, x10, x11\n"
    "movz x9, #7\n"
    "eor x11, x7, x9\n"
    "orr x10, x10, x11\n"
    "movz x9, #8\n"
    "ldr x11, [sp, #0]\n"
    "eor x11, x11, x9\n"
    "orr x10, x10, x11\n"
    "movz x9, #9\n"
    "ldr x11, [sp, #8]\n"
    "eor x11, x11, x9\n"
    "orr x10, x10, x11\n"
    "movz x9, #10\n"
    "ldr x11, [sp, #16]\n"
    "eor x11, x11, x9\n"
    "orr x10, x10, x11\n"
    "movz x9, #11\n"
    "ldr x11, [sp, #24]\n"
    "eor x11, x11, x9\n"
    "orr x10, x10, x11\n"
    "movz x9, #12\n"
    "ldr x11, [sp, #32]\n"
    "eor x11, x11, x9\n"
    "orr x10, x10, x11\n"
    "orr x0, xzr, x10\n"
    "ret\n");
#endif

__attribute__((noinline)) static int uefi_abi_contract_test(void)
{
    volatile __WCHAR_TYPE__ wchar_minus_one = (__WCHAR_TYPE__)-1;
    int passed = wchar_minus_one == 65535 && uefi_abi_variadic(17, 1ULL, 2ULL, 3ULL, 4ULL, 5ULL, 6ULL,
                                   7ULL, 8ULL, 9ULL, 10ULL, 11ULL, 12ULL) == 667;
#if defined(__aarch64__)
    passed = passed && uefi_abi_reference_caller(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0) == 667;
    passed = passed && uefi_abi_reference_receiver(17, 1ULL, 2ULL, 3ULL, 4ULL, 5ULL, 6ULL,
                                                   7ULL, 8ULL, 9ULL, 10ULL, 11ULL, 12ULL) == 0;
    passed = passed && uefi_abi_reference_receiver(17, 1ULL, 2ULL, 3ULL, 4ULL, 5ULL, 6ULL,
                                                   7ULL, 8ULL, 9ULL, 10ULL, 11ULL, 13ULL) != 0;
#endif
    return passed;
}

#endif
