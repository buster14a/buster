// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Compare libgcc's live lookup at a function's first and second bytes with
// the exact FDE selected by its emitted PT_GNU_EH_FRAME table.
#define _GNU_SOURCE
#include <inttypes.h>
#include <link.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unwind.h>

struct dwarf_eh_bases
{
    void *tbase;
    void *dbase;
    void *func;
};

extern const void *_Unwind_Find_FDE(const void *, struct dwarf_eh_bases *);

static int find_header(struct dl_phdr_info *info, size_t size, void *data)
{
    (void)size;
    const unsigned char **header = data;
    if (info->dlpi_name[0] == '\0')
    {
        for (unsigned i = 0; i < info->dlpi_phnum; i += 1)
        {
            if (info->dlpi_phdr[i].p_type == PT_GNU_EH_FRAME)
            {
                *header = (const unsigned char *)(info->dlpi_addr + info->dlpi_phdr[i].p_vaddr);
            }
        }
    }
    return *header != NULL;
}

static _Unwind_Reason_Code frame(struct _Unwind_Context *context, void *argument)
{
    unsigned *count = argument;
    *count += _Unwind_GetIP(context) != 0;
    return _URC_NO_REASON;
}

__attribute__((noinline, aligned(16))) static unsigned leaf(void)
{
    unsigned count = 0;
    _Unwind_Backtrace(frame, &count);
    return count;
}

__attribute__((noinline, aligned(128))) static unsigned middle(void)
{
    return leaf() + 1;
}

__attribute__((noinline, aligned(32))) static unsigned outer(void)
{
    return middle() + 1;
}

int main(void)
{
    const unsigned char *header = NULL;
    dl_iterate_phdr(find_header, &header);
    const unsigned char encoding[] = { 1, 0x1b, 3, 0x3b };
    int ok = header != NULL && memcmp(header, encoding, sizeof(encoding)) == 0;
    uint32_t count = 0;
    if (ok)
    {
        memcpy(&count, header + 8, sizeof(count));
    }
    const uintptr_t functions[] = { (uintptr_t)leaf, (uintptr_t)middle, (uintptr_t)outer };
    unsigned lookups = 0;
    for (unsigned f = 0; f < sizeof(functions) / sizeof(functions[0]); f += 1)
    {
        uintptr_t expected = 0;
        for (uint32_t i = 0; i < count; i += 1)
        {
            int32_t pair[2];
            memcpy(pair, header + 12 + 8ull * i, sizeof(pair));
            if ((uintptr_t)header + (intptr_t)pair[0] == functions[f])
            {
                expected = (uintptr_t)header + (intptr_t)pair[1];
            }
        }
        for (unsigned off = 0; off < 2; off += 1)
        {
            struct dwarf_eh_bases bases = { 0 };
            const void *fde = _Unwind_Find_FDE((const void *)(functions[f] + off), &bases);
            lookups += expected != 0 && (uintptr_t)fde == expected && (uintptr_t)bases.func == functions[f];
        }
    }
    const unsigned depth = outer() - 2;
    printf("runtime: %" PRIu32 " FDEs, %u/6 exact libgcc lookups, %u backtrace frames\n", count, lookups, depth);
    return ok && lookups == 6 && depth >= 5 ? 0 : 1;
}
