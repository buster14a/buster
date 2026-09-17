# GOT-reference encoding authority (#267, #78)

Both built-in x86-64 ELF writers use `link_x86_relax_got_reference`. It passes
section-relative bounds, the relocation's addend and the site's psABI spelling
to `buster_x86_metadata_relax_got_reference`; no linker code recognizes or
substitutes opcodes. The metadata module serially derives every conversion the
psABI table (B.2) names, checks each one's envelope and field, and publishes
validity last. Failed derivation stays invalid for the whole vocabulary rather
than leaving a partial one behind. The relocated four bytes are never touched
here; the existing ELF relocation writer patches them afterward. These are
opcode-changing relaxations, not neutral relocation patches.

## The spelling decides the shape

A relocation names a field, not an instruction boundary. The bytes in front of
that field belong to the instruction only because the producer's promise says
how many there are, so the three GOT relocations stay three
`ObjectRelocationKind`s rather than one:

| ELF type | Kind | Bytes before the field | Vocabulary |
|---|---|---|---|
| 9 `R_X86_64_GOTPCREL` | `X86_64_GOTPCREL` | promises nothing | never decoded; the link fails by symbol unless a real GOT slot exists |
| 41 `R_X86_64_GOTPCRELX` | `X86_64_GOTPCRELX` | opcode and ModRM | every row with no REX prefix |
| 42 `R_X86_64_REX_GOTPCRELX` | `X86_64_REX_GOTPCRELX` | one REX prefix more | every REX-prefixed row |
| 43 `R_X86_64_CODE_4_GOTPCRELX` | `X86_64_CODE_4_GOTPCRELX` | REX2, opcode and ModRM | MOV, TEST and ALU rows using r16-r31 |

Collapsing 41 and 42 makes `8b 05` and `48 8b 05` the same site, and the byte
in front of a non-REX load is then read as a REX prefix it never had. Roughly
one byte in twenty is in `40..4f`, so that reading rewrites a neighbouring
instruction rather than failing. `object_relocation_kind_is_x86_got` answers
the parts that are genuinely shared: one rip-relative 32-bit field, addend -4,
and a value taken from the slot holding the symbol's address.

## The vocabulary

Each row is a source operand shape and the direct instruction that replaces
it, both encoded through checked metadata:

| GOT load | Replacement | Patched field |
|---|---|---|
| `MOV r64, [RIP+got]` | `LEA r64, [RIP+symbol]` | rip-relative, addend kept |
| `MOV r32, [RIP+got]` | `MOV r32, imm32` | the address, sign-extended |
| `ADD/OR/ADC/SBB/AND/SUB/XOR/CMP r32\|r64, [RIP+got]` | the same operation against `imm32` | the address, sign-extended |
| `TEST [RIP+got], r32\|r64` | `TEST r32\|r64, imm32` | the address, sign-extended |
| `CALL/JMP [RIP+got]` | `CALL/JMP rel32` | rip-relative, addend kept |
| `PUSH [RIP+got]` | `PUSH imm32` | the address, sign-extended |

A row survives derivation only when the replacement fills the source's exact
byte count with its own relocated field at the same offset, so the relocation
offset the linker already holds keeps pointing at the field. A replacement the
encoder makes shorter is front-padded with the derived one-byte NOP instead of
moving the field; `ld` shifts the relocation by a byte for the same cases,
which this cannot do and does not need to.

An absolute replacement answers with the symbol's address itself, so the -4
that aimed a rip-relative field at a GOT slot has no part in that value: the
module refuses any other addend and writes nothing, and the linker patches
with addend 0. Rows are grouped by prefix width and every row inside a group
is checked to accept a disjoint set of bytes, so a site never has two
readings. REX.X and REX.B name no operand in a RIP-relative form; previously
accepted redundant bits remain accepted, but the output is the independently
derived canonical prefix rather than those bits moved into another role. Both
ELF callers provide section bounds rather than whole-image bounds, so an
instruction may not cross the beginning of its section.

## Derivation cost

A row costs an encoder emission, and the vocabulary is 323 of them, so
`buster_x86_metadata_got_prepare` takes a tier mask the way
`buster_x86_metadata_tls_prepare` does. Plain `R_X86_64_GOTPCREL` is never decoded because it promises no
instruction boundary. This compiler's own `-fPIC` MOV-r64 loads are written as
`R_X86_64_REX_GOTPCRELX`; the wider vocabulary is derived only when an object
actually carries a relaxable spelling.
Selection scans a mnemonic's candidates and is the expensive half, so a class
selects once, on its highest register -- whose operands need every REX bit the
class can use, and which therefore never selects a shorter accumulator form
the other registers could not encode -- and the remaining rows only emit
through that form. Where the SDM defines both an accumulator and a ModRM
encoding, that makes the replacement the one LLVM writes.

Measured on this tree, best of three runs of thirty links each (Release,
`clang` host, one small object): an image whose GOT references are all plain
`GOTPCREL` costs what it did before, and one carrying relaxable spellings
pays about 3 ms once for the full vocabulary -- on a link that previously
failed outright.

## Oracle

`tests/x86_64_got_encoding_oracle.s` independently specifies every source
shape and the direct instruction each becomes. GNU as and LLVM agree on all of
them except the ALU immediates, where GNU as takes the accumulator short form
(`add $imm, %eax` as `05 id`) and LLVM the ModRM form (`81 c0 id`). Both are
SDM encodings of the same instruction, so the registered regression checks
membership in that legal set rather than one byte string, and builds the set
from the SDM's own rules for prefix, opcode and ModRM. Relocation kinds stay
distinct in both assemblers' objects, with every GOT field at -4.

The regression covers every row and register, checked/exact agreement for the
MOV/LEA family, redundant REX bits, field contents, section bounds, guard
bytes, failure atomicity, the refusal of a non `-4` addend for an absolute
conversion, and the one case the spelling exists for: a non-REX site whose
preceding byte is `48` converts as itself and leaves that byte alone.
For plain type 9, it separately exhausts all 256 values in each of the three
byte positions before the field -- including every `40..4f` candidate -- and
requires `PATCH_NONE` with byte-identical output.

Reproduce external inspection from the repository root:

```sh
as --64 tests/x86_64_got_encoding_oracle.s -o /tmp/got-gas.o
clang --target=x86_64-linux-gnu -c tests/x86_64_got_encoding_oracle.s -o /tmp/got-llvm.o
readelf -Wr /tmp/got-gas.o /tmp/got-llvm.o
objdump -drw /tmp/got-gas.o /tmp/got-llvm.o
```

`tests/basic_c_pic.c` carries the end-to-end case: built `-fPIC -O2` by the
configured external compiler, both GCC and Clang narrow an address to 32 bits
there and emit the non-REX `mov`/`sub` GOT loads, which the driver suite links
and runs.

Full current-tree validation and measurements are recorded separately from
this source-level contract; this document does not certify unrun gates.
