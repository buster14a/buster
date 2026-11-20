#!/usr/bin/env bash
set -euo pipefail
mkdir -p "$1"
cd "$1"
as --64 -o entry.o <<'ASM'
.globl _start
.type _start,@function
_start:
 mov $60,%eax
 mov $42,%edi
 syscall
.section .note.GNU-stack,"",@progbits
ASM
as --64 -o empty.o <<'ASM'
.section .note.GNU-stack,"",@progbits
ASM
for kind in object function; do
    {
        echo '.data'
        echo '.globl strong_ref'
        printf '.type strong_ref,@%s\n' "$kind"
        echo '.weak weak_api'
        echo '.type weak_api,@function'
        echo '.quad strong_ref,weak_api'
        echo '.section .note.GNU-stack,"",@progbits'
    } | as --64 -o "shared-$kind.o"
    for n in 256 512; do
        {
            echo '.data'
            for ((i=0; i<n; i++)); do
                printf '.weak ref_%u\n.type ref_%u,@%s\n.quad ref_%u\n' "$i" "$i" "$kind" "$i"
            done
            echo '.section .note.GNU-stack,"",@progbits'
        } | as --64 -o "$kind-$n.o"
    done
done
as --64 -o edge-a.orig.o <<'ASM'
.data
.weak duplicate,duplicate_object,duplicate_function,weak_tls,plain,hidden_weak,defined_data
.type duplicate_object,@object
.type duplicate_function,@function
.type weak_tls,@tls_object
.type hidden_weak,@object
.type defined_data,@function
.hidden hidden_weak
.quad duplicate,duplicate_object,duplicate_function
.long weak_tls@TPOFF
.quad plain,hidden_weak,defined_data
.section .note.GNU-stack,"",@progbits
ASM
objcopy --redefine-sym duplicate_object=duplicate edge-a.orig.o edge-a.first.o
objcopy --redefine-sym duplicate_function=duplicate edge-a.first.o edge-a.o
as --64 -o edge-b.o <<'ASM'
.data
.weak duplicate,other_weak,plain
.type duplicate,@function
.type other_weak,@object
.quad duplicate,other_weak,plain
.section .note.GNU-stack,"",@progbits
ASM
as --64 -o edge-c.o <<'ASM'
.data
.globl defined_data
.type defined_data,@object
defined_data: .quad 12
.weak other_weak
.type other_weak,@function
.quad other_weak
.section .tdata,"awT",@progbits
.globl defined_tls
.type defined_tls,@tls_object
defined_tls: .long 11
.section .note.GNU-stack,"",@progbits
ASM
as --64 -o archive-root.o <<'ASM'
.data
.weak late_ref
.quad pull_member,late_ref
.section .note.GNU-stack,"",@progbits
ASM
as --64 -o archive-member.o <<'ASM'
.data
.globl pull_member
.type pull_member,@object
pull_member: .quad 1
.local private_anchor
private_anchor: .quad pull_member
.weak late_ref
.type late_ref,@function
.quad late_ref
.section .note.GNU-stack,"",@progbits
ASM
ar rcs later.a archive-member.o
as --64 -o bad-undef.o <<'ASM'
.data
.quad missing_alpha,missing_beta
.section .note.GNU-stack,"",@progbits
ASM
for name in alpha beta; do
    {
        echo '.data'
        printf '.globl bad_%s\nbad_%s: .long 0\n' "$name" "$name"
        printf '.reloc bad_%s,R_X86_64_GOT32,defined_data\n' "$name"
        echo '.section .note.GNU-stack,"",@progbits'
    } | as --64 -o "bad-$name.o"
done
{
    echo '.data'
    echo '.weak early'
    echo '.type early,@object'
    echo '.quad early'
    for ((i=0; i<256; i++)); do
        printf '.globl tail_%u\n.type tail_%u,@object\ntail_%u: .byte 0\n' "$i" "$i" "$i"
    done
    echo '.section .note.GNU-stack,"",@progbits'
} | as --64 -o sparse.o
