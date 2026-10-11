# Compiler embedding example

The SDK is opt-in and is not part of the normal `ide` build. Generate a
separate build tree, build and install the static library, then configure this
standalone consumer:

```sh
./build.sh generate --build-directory build-sdk --cc clang --config Release \
  --no-sanitize --no-fuzz --no-lto --no-include-tests --linker DEFAULT \
  -- -DBUSTER_COMPILER_SDK=ON
./build.sh build --build-directory build-sdk --config Release -t buster_compiler
cmake --install build-sdk --prefix "$PWD/build-sdk/install" --config Release
cmake -S tools/embed_example -B build-embed-example \
  -DCMAKE_PREFIX_PATH="$PWD/build-sdk/install" -DCMAKE_BUILD_TYPE=Release
cmake --build build-embed-example --config Release
./build-embed-example/buster_embed_example
```

The public source API is `src/buster/lib/compiler/embed/embed.h`. It copies
source and option strings during `buster_embed_compile`. A module owns its
diagnostics, object bytes, and loaded code. Release every module before
destroying its compiler. Returned symbol addresses remain valid only until
module release, and imported function/data addresses must remain valid during
every execution that can reach them. Release must not race generated code
execution.

`BUSTER_EMBED_API_VERSION` versions the public source API. It deliberately
makes no stable ABI promise; rebuild clients with the matching installed
header and library.

Compilation can target foreign triples for object inspection, but only
host-native objects can be loaded by the JIT. Native execution is unsandboxed.
The loader refuses nonempty initializer/finalizer arrays and TLS; it does not
register debug or unwind metadata. The host must permit executable memory. On
macOS the loader uses `MAP_JIT` with the platform's write-protection mode and
entitlement/policy requirements; its existing one-region contract does not
support repeated direct JIT use in one process. Sanitizer builds are not a
supported execution configuration.

Explicit data imports support both ordinary C access and initialized pointers
on the native Linux ELF and Apple Mach-O targets:

| Target triple | Direct `host_scale` read/write | Initialized pointer to `host_scale` |
| --- | --- | --- |
| `x86_64-unknown-linux-gnu` | `OBJECT_RELOCATION_X86_64_PC32` in text | `OBJECT_RELOCATION_ABSOLUTE64` in data |
| `aarch64-unknown-linux-gnu` | `OBJECT_RELOCATION_ABSOLUTE64` in text | `OBJECT_RELOCATION_ABSOLUTE64` in data |
| `aarch64-apple-darwin` | `OBJECT_RELOCATION_AARCH64_MACH_PAGE21` and `OBJECT_RELOCATION_AARCH64_MACH_PAGEOFF12` in text | `OBJECT_RELOCATION_ABSOLUTE64` in data |

The loader patches absolute references with the supplied address. Relative
references constrain the JIT image placement: PC32 and PREL32 need a signed
32-bit displacement, and AArch64 PAGE21 needs a signed 21-bit page distance.
The loader uses non-fixed allocation hints and checks the returned mapping;
it never replaces host mappings or copies imported data. Host mutations and
generated writes therefore reach the same object, and `&host_scale` retains
the supplied pointer identity. Imports whose relative ranges have no common
available image placement return `JIT_FAILED` with the JIT capacity diagnostic.
Absolute pointer forms have no relative-distance requirement. Other
unsupported external-data relocation kinds remain refused.

Included headers are read from the filesystem using the compiler's normal
include search. Virtual header overlays are outside this bounded facade and
remain the boundary described by issue #1946.

The compiler handle serially prewarms shared tables. Use one handle and its
modules from one caller thread; the facade is serial and does not create a
persistent lane gang. Embedders that create their own lane gang must call the
internal `compiler_parallel_prewarm` before parallel compiler use. Loaded
generated functions may execute on other threads, subject to the imported
address and release lifetime rules.

The external example and workflow validate Linux x86-64 only. `embed_tests`
run wherever the repository's `ide` tests run. Android, iOS, and fuzz SDK
configurations are rejected; other platform/toolchain combinations are not
qualified by the external example.
