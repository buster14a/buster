# GOT-load encoding authority (#267)

Both built-in x86-64 ELF writers use `link_x86_relax_got_load`. It now passes
section-relative bounds to `buster_x86_metadata_relax_got_load`; no linker
code recognizes or substitutes opcodes. The metadata module serially derives
MOV r64,[RIP+symbol] and LEA r64,[RIP+symbol] for all sixteen destinations,
checks their seven-byte envelope and matching PC32 offset/width/addend, and
publishes validity last. Failed derivation stays invalid. The arbitrary four
relocation bytes remain untouched; the existing ELF relocation writer patches
them afterward. This opcode-changing operation is a relaxation, not a neutral
relocation patch.

REX.X/B are ignored by both RIP-relative forms. Previously accepted redundant
bits remain accepted, but the output uses the independently derived canonical
prefix, rather than copying ignored input bits into other operand roles.
Truncated or unrecognized shapes cause no writes. Both ELF callers provide
section bounds rather than whole-image bounds, so the instruction prefix may
not cross the beginning of its section.

The scope is the existing MOV-r64 family. ALU/TEST/indirect control-transfer
GOTPCRELX conversions and absolute-immediate patch policy remain in #78.
This change neither broadens that vocabulary nor silently treats it as solved.

`tests/x86_64_got_encoding_oracle.s` independently specifies the sixteen MOV
and LEA forms. GNU as and LLVM produce identical bytes; GOTPCRELX and PC32
relocation kinds remain distinct in their objects, with fields at 3+7*i and
addend -4. The registered regression covers checked/exact agreement, every
REX/ModRM pair, every opcode byte, all destinations, redundant prefixes,
displacement boundaries, section bounds, guard bytes and failure atomicity.

Reproduce external inspection from the repository root:

```sh
as --64 tests/x86_64_got_encoding_oracle.s -o /tmp/got-gas.o
clang --target=x86_64-linux-gnu -c tests/x86_64_got_encoding_oracle.s -o /tmp/got-llvm.o
readelf -Wr /tmp/got-gas.o /tmp/got-llvm.o
objdump -drw /tmp/got-gas.o /tmp/got-llvm.o
```

Full current-tree validation and measurements are recorded separately from
this source-level contract; this document does not certify unrun gates.
