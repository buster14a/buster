# x86-intrinsics

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- `src/buster/compiler/x86_intrinsics_test.c` is `test-x86-intrinsics` (#136):
  the compiler's code of the headers' x86 intrinsics whose instructions no
  host of the tests runs (XOP's, AVX-NE-CONVERT's, AVX10.2's, SHA512's, SM3's,
  SM4's, MOVRS's, AVX512-BMM's, the extensions' system instructions' and the
  leaves' of Key Locker, WAITPKG and RTM) against clang -O0's. A source takes
  each case's address, both compilers compile it (this one by both paths into
  the same bytes), and each case's function is decoded
  (`x86_64_disassembler.c`) and followed through `x86_semantics.c`'s effects,
  every register's and stack byte's value an expression of the arguments, both
  paths of a conditional jump or `xbegin`, each with its conditions: its
  result, stores and effects (an instruction that writes no register or memory,
  and no flag or is a system instruction; a prefetch's address) must be the
  same expressions in both objects, path by path (Key Locker's encryptions
  with clang's conditions negated: clang's code stores them the other way from
  Intel's manual, its own header and GCC's code), controls that run natively
  must agree too, and an intrinsic's two orders of operands must differ.
  `--cases FILE` takes other cases, `--option FLAG` adds a flag to every
  compile and `--print NAME` prints a case's traces; a second run compares
  bf16's arithmetic in a unit whose target has AVX10.2 (`--march diamondrapids
  --option -mno-apxf --cases tests/x86-intrinsics/avx10.2-units.txt`). The
  checker guide's "Intrinsics no host runs" has the details.
  Its AVX512-BMM byte-bit-reversal cases cover all three widths, unmasked,
  merged and zeroed. Byte product reductions in the native vector fixtures
  cover 4, 8, 16, 32 and 64 bytes against an independent modulo-256 loop and
  clang; they widen to packed words and split only between complete halvings.
