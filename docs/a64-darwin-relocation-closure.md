# Darwin AArch64 relocation closure

This tranche owns the object, native-link, and JIT relocation boundary.  The
exact AArch64 object-relocation denominator is the 14 enum members below
(common `ABS64` is listed separately because it is shared by both targets):

`CALL26`, `JUMP26`, `PREL32`, `MACH_PAGE21`, `MACH_PAGEOFF12`,
`MACH_GOT_LOAD_PAGE21`, `MACH_GOT_LOAD_PAGEOFF12`,
`MACH_TLVP_PAGE21`, `MACH_TLVP_PAGEOFF12`, `PE_TLS_INDEX_ADRP`,
`PE_TLS_INDEX_LO12`, `PE_TLS_OFFSET12`, `TLSLE_ADD_TPREL_HI12`, and
`TLSLE_ADD_TPREL_LO12`.

| Path | Supported | Explicitly excluded |
| --- | --- | --- |
| Mach-O writer/reader | `CALL26/JUMP26`, `PREL32`, `ABS64`, `PAGE21/PAGEOFF12`, `GOT_LOAD PAGE21/PAGEOFF12`, `TLVP PAGE21/PAGEOFF12` | TLSLE/PE TLS are non-Mach-O formats |
| JIT | `ABS64`, native `PC32`, `CALL26/JUMP26`, `PREL32`, `PAGE21/PAGEOFF12`, and `GOT_LOAD PAGE21/PAGEOFF12` relaxed to the target's `ADRP`/`ADD`; bound external data is accepted for direct and GOT page/absolute relocations | TLVP and all TLS forms (`JIT_ERROR_TLS_UNSUPPORTED`); unresolved external data without a binding remains `JIT_ERROR_EXTERNAL_DATA` |
| Native Darwin link | local `CALL26/JUMP26`, `PREL32`, `ABS64`, `PAGE21/PAGEOFF12`, and TLVP descriptor forms; `GOT_LOAD PAGE21/PAGEOFF12` against an import's bound `__got` slot or relaxed to a local definition; `ABS64` against an import in a `__DATA` section (`__data`, `__const`, initializer and thread-local templates) as a dyld `BIND_TYPE_POINTER` with its addend (#3547). A direct page reference, or an absolute one in code, to imported data is refused | external TLVP without a local descriptor/resolver and TLSLE; out-of-range branches return `LINK_ERROR_RELOCATION` because this layout has no safe insertion point for a veneer pool |

Mach-O relocation type numbers remain unchanged: `2` branch/call, `3`
`PAGE21`, `4` `PAGEOFF12`, `5` `GOT_LOAD_PAGE21`, `6` `GOT_LOAD_PAGEOFF12`,
`8` TLVP `PAGE21`, and `9` TLVP `PAGEOFF12`. Codegen reaches every symbol the
module does not define through the GOT pair (#1454); a direct page pair to a
dylib symbol resolves to page zero under ld64.lld. The GOT pair takes no
addend, and its low half must be the 64-bit `LDR` of the slot.
Scattered addend records are accepted for all five page/branch families,
including TLVP, and malformed instruction shapes, alignment, scale, and
checked addend overflow fail before a patch is committed.

Mach-O arm64 `PREL32` is represented by the canonical `SUBTRACTOR`/`UNSIGNED`
pair. Its source offset and section layout must be 4-byte aligned and its
stored signed addend must fit `int32_t`; the writer rewrites the relocated word
even when that addend is zero. A bare arm64 32-bit `UNSIGNED` record is rejected
for regular sections (debug `ABS32` remains supported). Callers that construct
`ObjectFile` values directly still need valid section and symbol indexes;
writer and JIT preflight reject malformed PREL32
alignment/range inputs, while serialized readers check section-base arithmetic
before mapping relocations.

## Verification snapshot

On the Linux x86-64 host with Clang 22.1.8:

| Configuration | Full assertions | Object | JIT | Link |
| --- | ---: | ---: | ---: | ---: |
| Debug | 272,520 | 541 | 114 | 314 |
| Release | 272,520 | 541 | 114 | 314 |
| ASAN/UBSAN | 270,936 | 538 | 98 | 272 |

The three owned production translation units also pass strict GCC, Clang, and
Zig C17 syntax compilation. The checked-in A64 canonical source snapshot used by
the surrounding M1 work is SHA-256
`8485c5c61835d5394d325757ab2964890e8bdfea304c6faa8fd4c23e4c7aabec`.

LLVM 22.1.8's `llvm-mc -triple arm64-apple-macos` emitted the canonical
`SUBTRACTOR`/`UNSIGNED` PREL32 pair, and the Buster Darwin linker accepted that
object. The Release `test_self_host` fixed-point and benchmark gate also pass.
