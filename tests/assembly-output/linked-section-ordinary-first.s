# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.

.text
entry:
    ret
.section entries,"aw",@progbits
    .quad 7
.data
    .quad entries
.section entries,"awo",@progbits,entry
    .quad entry
