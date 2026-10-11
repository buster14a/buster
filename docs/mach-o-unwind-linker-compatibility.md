# Mach-O unwind linker compatibility

Buster supports the Mach-O `__TEXT,__eh_frame` relocation contract used by
Apple ld and LLVM `ld64.lld` 22.x. The reference LLVM implementation is
22.1.8; `.github/workflows/mach-o-unwind-lld.yml` installs LLVM 22 and records
the exact tool versions with every run.

## Object contract

A canonical CFI relocation means `target + A - P`, where `P` is the relocated
four-byte field. Mach-O represents this with an external, non-PC-relative,
32-bit `SUBTRACTOR` followed by an `UNSIGNED` relocation at the same field.
The subtrahend symbol is placed at the start `R` of the containing CIE or FDE,
and the stored addend is `A - (P - R)`. The linker therefore computes
`target - R + A - (P - R)`, exactly the canonical expression.

Record-boundary labels and internal unwind labels are not interchangeable.
Apple ld accepted Buster's former symbol at `P`, but `ld64.lld` first splits
`__eh_frame` at CFI record boundaries and rejects a section symbol inside a
record as a misaligned subsection boundary. The writer now emits only
record-boundary symbols for unwind differences. The reader retains support
for old exact-field Buster objects and converts both encodings to the same
canonical relocation.

Non-unwind AArch64 PREL32 relocations retain their exact-field subtraction
symbol and wire representation. CFI record lengths, extended lengths, field
ranges, symbol sections, relocation-pair shapes and adjusted addends are
checked before they are accepted or serialized.

## Validation

`tools/test_mach_o_unwind_lld.sh` builds a Buster subject object and an
independently Clang-compiled host object for both `x86_64-apple-macos` and
`aarch64-apple-macos`, then asks `ld64.lld` to produce final Mach-O
executables. It also links a Clang subject oracle and records `llvm-readobj`
headers, sections, relocations and symbols. A successful object parse is not
a passing result; both final links must succeed.

`tools/test_mach_o_dylib_data_lld.sh` covers the data side of the
relocation contract (#1454) for both architectures. It compiles
`tests/basic_c_mach_dylib_data_subject.c` with Buster and with Clang at `-O0`
and `-O2`, then links each object with `ld64.lld` against a Clang-built dylib
that defines the data. The check fails unless all three hold:

- At `-O0`, every symbol's `__text` relocation types match Clang's.
- Buster's image binds the dylib data in `__got`.
- The image's dyld bindings equal Clang's.

Before the GOT forms, x86-64 data was bound lazily through a call stub and
arm64 data was not bound at all.

The normal desktop CI retains independent Apple-linker coverage on the native
`macos-26-intel` and `macos-26` lanes, including link-and-run tests with an
AppleClang host, canonical object round trips and the ordinary self-hosting
suites. `compiler_driver_test_mach_libsystem_data` runs there too. It links a
Buster object that reads libSystem's `__stderrp` and `optind` once with
AppleClang (Apple ld) and once with Buster's own Mach-O linker, and runs both
images.

Local reproduction after building `build/Release/ide`:

```sh
PATH=/usr/lib/llvm-22/bin:$PATH \
  tools/test_mach_o_unwind_lld.sh build/Release/ide build/mach-o-unwind-lld
```

```sh
PATH=/usr/lib/llvm-22/bin:$PATH \
  tools/test_mach_o_dylib_data_lld.sh build/Release/ide build/mach-o-dylib-data-lld
```
