# Local toolchain license scope

The pinned TinyCC bootstrap source is commit `0fb54300b56512754221d80adda85ddb9815bceb` of [TinyCC](https://github.com/TinyCC/tinycc/tree/0fb54300b56512754221d80adda85ddb9815bceb). Its README says LGPL and points to `COPYING`; that file identifies LGPL version 2.1, while legacy headers in `tcc.c`, `libtcc.c`, and `tcc.h` say version 2 or later. This records those observed source statements without assigning one uniform strict SPDX version. TCC was a local `build.c` bootstrap tool; no TinyCC implementation was imported into Buster.

Buster's first-party license grant remains unselected under #621 ([`LICENSES/README.md`](../../../../LICENSES/README.md)). The installed Clang/LLVM license is SPDX `Apache-2.0 WITH LLVM-exception` ([`LICENSES/llvm-LICENSE.txt`](../../../../LICENSES/llvm-LICENSE.txt)). No external implementation or dependency was imported for this integration validation. This is not a package-by-package distribution-license audit.
