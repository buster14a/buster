# Opt-in Wild external linking

This guide owns Wild-specific build/link support. The C build driver remains the construction policy owner; see [build.md](build.md). The generic `generate --linker WILD` → `CMAKE_LINKER_TYPE=WILD` path already exists. No additional selector, default change or native-linker rerouting is needed.

## Qualified configuration

The hosted qualification uses Linux x86-64 / Clang 21.1.8 and Linux AArch64 / Clang 18.1.3, with CMake 4.4.3, Wild 0.10.0 and mold 2.42.1. ARM qualification is a runtime/debug smoke campaign, not a performance or sanitizer matrix. Install an optional Wild executable named `wild` on PATH. CMake's Clang rule invokes `--ld-path=wild`; the recorded command and executable checksum, rather than the configure option alone, establish which linker ran.

For a debug-information-preserving application build:

```sh
./build.sh generate --build-directory build-wild --cc clang --linker WILD --no-fuzz --no-lto -- -DCMAKE_EXE_LINKER_FLAGS=-Wl,--no-gc-sections
./build.sh build --build-directory build-wild --config Debug -t ide
```

Use the existing C driver entry point for the host environment. The evaluation invokes its compiled executable directly. Debug remains non-unity under current repository policy. Release unity/non-unity are separate graphs. Wild stays optional: omitting the selector preserves the existing mold/default selection policy; removing Wild does not affect an ordinary build.

Wild 0.10.0 enables section GC by default, unlike mold 2.42.1. The explicit no-GC flag is necessary for the strict full-DWARF qualification demonstrated here. Targets with their own required GC, such as `hot_reload`, are separately qualified: both linkers pass its runtime lifecycle checks, but both fail full-output `llvm-dwarfdump --verify` under GC. Do not claim that configuration preserves the strict full-DWARF contract. Sanitized Debug qualification covers the seven affected modules named in the [evidence report](../performance-audits/2026-10-04T185601Z.md), not every sanitizer/test mode.

## Diagnostics and scope

| Configuration | Evidence / support boundary |
| --- | --- |
| Linux x86-64, Clang 21.1.8, CMake 4.4.3, Wild 0.10.0, no LTO | Actual executable/archive/shared links, runtime, source breakpoints/unwinding, independent DWARF verification and affected sanitized regressions exercised. |
| Linux AArch64, Clang 18.1.3, CMake 4.4.3, Wild 0.10.0 | Hosted ide build/runtime, full DWARF, actual source/unwind and Buster-produced object external links PASS. ARM performance/sanitizer/LTO matrix NOT RUN. |
| GCC 15.2.0 with explicit WILD | CMake rejects this combination; hosted C-driver generation exits nonzero. No fallback. |
| Missing explicit Wild path or unsupported Wild flag | Clang/linker exits nonzero; diagnostic and status retained. |
| CMake before 4.4 | Native WILD linker type is unavailable upstream; unsupported here, not an older-CMake backport. |
| GCC 16+, Windows/macOS external Wild, other versions | NOT RUN for this qualification. Do not infer support from upstream listings. Existing Windows/macOS defaults remain unchanged. |
| LTO, full sanitizer matrix, physical-host acceptance | NOT RUN / outside this evaluation. |
| GC-enabled hot_reload full-DWARF verification | FAIL with both linkers; runtime lifecycle PASS. Explicitly unsupported strict-debug configuration. |

An explicit linker request must fail when unavailable or unsupported. Do not catch the failure and substitute a different linker. Optional research workflows install independent validation tools and build upstream executables only in the hosted runner's temporary directory; they are not required Buster dependencies.

## Buster-produced objects

The external linker used to construct `ide` is distinct from Buster's native linker. Demonstrate interoperability explicitly:

```sh
build-wild/Debug/ide cc -g -c main.c -o main-buster.o
build-wild/Debug/ide cc -g -c probe.c -o probe-buster.o
clang --ld-path=wild main-buster.o probe-buster.o -Wl,--no-gc-sections -o external-probe
./external-probe
```

This leaves `ide cc` native linking unchanged. The hosted fixture uses Buster-generated objects once, then links the identical files with each external linker and checks runtime, debug information and debugger stack/source behavior. Its small size does not rank large Buster-produced applications.

## Evaluation and recommendation

Retain optional support and existing defaults. Named hosted workloads show modest edit-build savings and some default-thread direct-link savings, with warm single-thread regressions and no demonstrated clean-build improvement. See the report for exact identities, policies, raw samples, uncertainties, failures and host limits. Fixed build IDs, no-fork and ICF controls in the research harness are measurement controls; they are not a new production default.
