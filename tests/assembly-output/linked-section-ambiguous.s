# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

.text
entry:
    ret
.section entries,"awo",@progbits,entry
    .quad entry
.data
    .quad entries
.section entries,"aw",@progbits
    .quad 7
