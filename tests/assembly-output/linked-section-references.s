# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

.data
    .quad "123patch"
    .quad "."
    .quad .
.text
entry:
    ret
.section "123patch","awo",@progbits,entry
    .quad entry
.section ".","awo",@progbits,entry
    .quad entry
.section .text.pick,"axG",@progbits,pick,comdat
pick:
    ret
.section pick,"awo",@progbits,pick
    .quad pick
