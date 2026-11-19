// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Write an ELF assembler input with N FDEs in a chosen order. Even an empty
// table has a real CIE, so the normal link emits its 12-byte header.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    unsigned long parsed = 0;
    char *end = NULL;
    if (argc == 3)
    {
        parsed = strtoul(argv[1], &end, 10);
    }
    int result = 2;
    if (end != NULL && end != argv[1] && *end == 0 && parsed <= 65536 &&
        (strcmp(argv[2], "sorted") == 0 || strcmp(argv[2], "reverse") == 0 || strcmp(argv[2], "ties") == 0))
    {
        const unsigned count = (unsigned)parsed;
        const int sorted = strcmp(argv[2], "sorted") == 0;
        const int ties = strcmp(argv[2], "ties") == 0;
        puts(".text\n.globl _start\n.type _start,@function\n_start:\n"
             "mov $60,%eax\nxor %edi,%edi\nsyscall\n.size _start,.-_start\n.p2align 4");
        for (unsigned i = 0; i < count; i += 1)
        {
            printf(".Lfunction%u:\nret\n.fill 15,1,0x90\n", i);
        }
        puts(".section .eh_frame,\"a\",@progbits\n.p2align 3\n.Lcie:\n"
             ".long .Lcie_end-.Lcie_body\n.Lcie_body:\n.long 0\n.byte 1\n.asciz \"zR\"\n"
             ".uleb128 1\n.sleb128 -8\n.byte 16\n.uleb128 1\n.byte 0x1b\n"
             ".byte 0x0c,7,8,0x90,1\n.balign 4,0\n.Lcie_end:");
        for (unsigned i = 0; i < count; i += 1)
        {
            const unsigned function = sorted ? i : count - 1 - i;
            printf(".long 2f-1f\n1:\n.long .-.Lcie\n.long .Lfunction%u-.\n.long 16\n"
                   ".uleb128 0\n.balign 4,0\n2:\n", ties ? function / 2 : function);
        }
        puts(".long 0\n.section .note.GNU-stack,\"\",@progbits");
        result = fflush(stdout) == 0 ? 0 : 1;
    }
    return result;
}
