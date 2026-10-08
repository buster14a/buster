# C vendor builtin metadata

The maintenance generator reproduces `c_vendor_builtin.c` and `c_vendor_builtin.h`
from a finite frozen type/name contract. Ordinary Buster builds consume the
generated C files and do not require Python, LLVM, TableGen, or a network
connection.

The source is LLVM 21.1.8, commit
`2078da43e25a4623cab2d0d60decddf709aaea28`. The provenance manifest records
the exact Git blobs for four TableGen definition files, the generic semantic
reference files, the license, and 133 resource headers. The lexical include
closure starts at `immintrin.h`, `cpuid.h`, and `x86intrin.h`; it includes
conditional branches. Intersecting exact target-builtin names with resolved
LLVM records yields 2,093 `__builtin_ia32_` spellings, plus the bare
`__rdtsc` builtin. Eighteen generic custom builtins have separate grammar/type
metadata.

Typed admission does not grant runtime lowering or a positive
`__has_builtin` result. Every call still needs argument validation. A
reachable operation needs implemented semantics or a named diagnostic.

## Reproduce or inspect

Run from the Buster repository root:

```sh
python3 tools/generate_c_vendor_builtins.py --check
python3 tools/generate_c_vendor_builtins.py --describe __builtin_ia32_sha256rnds2
```

Omit `--check` to regenerate both source files. `--output-dir` can direct
regeneration to a review directory. The generator uses the Python standard
library only.

The 87,688-byte metadata file deduplicates 114 type shapes and 868 signatures
instead of storing vendor headers or repeated full prototypes. Each type row
contains C kind, vector lane count, pointer depth, object const/volatile
qualifiers, and a target data-model selector. Each signature contains return
and parameter type indices, parameter count, and a constant-argument mask.
Each builtin row pairs an exact suffix with a signature index. Generic rows
record operation, element category, result rule, arity, type-name argument
positions, and operand constraints. `--describe` resolves an individual row
for review.

Pointers to vectors remain pointers to the qualified vector object.
`size_t` and fixed-width aliases retain their target C rank. A 256-lane
AMX vector uses ordinary integer lanes and does not require a new C type
kind. Custom builtins use explicit rules, including the two shufflevector
forms and bit_cast's first type-name argument; they are never assigned a
`void(...)` C prototype.

## Verify upstream independently

With an LLVM checkout at the pinned commit, verify every recorded source
and header blob:

```sh
python3 tools/generate_c_vendor_builtins.py --check \
  --verify-llvm-source /path/to/llvm-project
```

To replay the original resolved contracts with LLVM TableGen:

```sh
llvm-tblgen --dump-json -I /path/to/llvm-project/clang/include \
  /path/to/llvm-project/clang/include/clang/Basic/BuiltinsX86.td > /tmp/x86.json
llvm-tblgen --dump-json -I /path/to/llvm-project/clang/include \
  /path/to/llvm-project/clang/include/clang/Basic/BuiltinsX86_64.td > /tmp/x86_64.json
python3 tools/generate_c_vendor_builtins.py --check \
  --verify-upstream-json /tmp/x86.json --verify-upstream-json /tmp/x86_64.json
```

The two JSON files are maintenance inputs, not build dependencies. The
checker compares all 2,094 exact names, return/argument type shapes, arities,
and immediate masks against the frozen metadata. Schema, count, spelling,
and source-pin checks reject accidental closure changes.

Update the metadata, provenance manifest, templates, and generated outputs
together when changing the pinned contract. Keep semantic implementation
and capability decisions in the frontend lowering/capability code.

The generated descriptor data retains LLVM's
`Apache-2.0 WITH LLVM-exception` notice. The complete upstream license is
in `tools/licenses/llvm-project.txt`.
