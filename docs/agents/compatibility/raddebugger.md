# RAD Debugger compatibility

[Harness index](../compatibility.md) · Commands run from the Buster repository root.

The compatibility goal is for Buster's active C frontend and canonical native
backend to compile and link the complete graphical `raddbg` application from
an unchanged upstream checkout. Building `radbin`, `metagen`, or
`raddbg_non_graphical` is a useful diagnostic milestone; it does not establish
that the graphical debugger builds or that debugging works.

## Inputs and provenance

Use [EpicGames/raddebugger](https://github.com/EpicGames/raddebugger), pinned to
[`f6b4a38134652886239b91f940cd7a67fedf689d`](https://github.com/EpicGames/raddebugger/commit/f6b4a38134652886239b91f940cd7a67fedf689d).
The `master` branch was read at that exact commit on 2026-10-02; its commit
timestamp is 2026-10-01T22:31:18Z. The build driver must reject a different
revision or modified upstream inputs. Do not copy, vendor, replace, or patch
upstream C sources or generated files into Buster.

The root [LICENSE](https://github.com/EpicGames/raddebugger/blob/f6b4a38134652886239b91f940cd7a67fedf689d/LICENSE)
is MIT, with copyright held by Epic Games Tools. The bundled components have
their own notices; the root license alone is not a component audit.

| Component inspected at the RAD Debugger pin | Verified license | Evidence and limits |
|---|---|---|
| RAD Debugger first-party source | MIT | Root `LICENSE`; first-party source headers carry the same notice. |
| BLAKE3 | CC0-1.0 | [`src/third_party/blake3/LICENSE_CC0`](https://github.com/EpicGames/raddebugger/blob/f6b4a38134652886239b91f940cd7a67fedf689d/src/third_party/blake3/LICENSE_CC0); bundled header reports version 1.8.7. This audit does not assert additional upstream licensing alternatives. |
| xxHash | BSD-2-Clause | [`src/third_party/xxHash/LICENSE`](https://github.com/EpicGames/raddebugger/blob/f6b4a38134652886239b91f940cd7a67fedf689d/src/third_party/xxHash/LICENSE). |
| Zydis and Zycore | MIT | License grants embedded in the amalgamated [`zydis.h`](https://github.com/EpicGames/raddebugger/blob/f6b4a38134652886239b91f940cd7a67fedf689d/src/third_party/zydis/zydis.h). |
| stb_image and modified stb_sprintf | MIT OR Unlicense | License alternatives embedded in [`stb_image.h`](https://github.com/EpicGames/raddebugger/blob/f6b4a38134652886239b91f940cd7a67fedf689d/src/third_party/stb/stb_image.h) and [`stb_sprintf.h`](https://github.com/EpicGames/raddebugger/blob/f6b4a38134652886239b91f940cd7a67fedf689d/src/third_party/stb/stb_sprintf.h). The latter explicitly identifies project modifications. |
| sinfl | MIT OR Unlicense | Alternatives embedded in [`sinfl.h`](https://github.com/EpicGames/raddebugger/blob/f6b4a38134652886239b91f940cd7a67fedf689d/src/third_party/sinfl/sinfl.h). |
| Martin's memfun, bitscan, and hash snippets | Component license unverified | Inspected bundled files have no embedded license grant and the pinned tree has no license file in these component directories. `memfun.h` links to `mmozeiko/overflow`; that link alone does not establish the bundled revision or license. |
| radsort and rad_lzb_simple | Component license ambiguous | No component-specific license notice or license file was found in the inspected files/directories. Do not infer a separate grant from public availability. |
| Embedded fonts and other binary assets | Component licenses unverified | This source/build audit does not establish the licenses of `data/*.ttf` or other embedded assets. |

Buster's own license status remains as recorded in `LICENSES/README.md`; this
compatibility work does not select or change it. System libraries are external
build prerequisites, not new Buster runtime dependencies or vendored sources.

## Build contract

The authoritative inputs are upstream
[`build.sh`](https://github.com/EpicGames/raddebugger/blob/f6b4a38134652886239b91f940cd7a67fedf689d/build.sh),
[`build.bat`](https://github.com/EpicGames/raddebugger/blob/f6b4a38134652886239b91f940cd7a67fedf689d/build.bat),
and the pinned
[`builds.yml`](https://github.com/EpicGames/raddebugger/blob/f6b4a38134652886239b91f940cd7a67fedf689d/.github/workflows/builds.yml).
The Linux script accepts a `CC`/`AR` override and `clang` or `gcc` flavor.
It builds each application as a unity C translation unit.

| Linux target | Translation unit | Target-specific flags |
|---|---|---|
| `raddbg` | `src/raddbg/raddbg_main.c` | `-DLNX_WM_ICON=1` |
| `raddbg_non_graphical` | `src/raddbg/raddbg_main.c` | `-DWM_STUB=1 -DR_BACKEND=R_BACKEND_STUB` |
| `radbin` | `src/radbin/radbin_main.c` | None |
| `radlink` | `src/linker/lnk.c` | None |
| `torture` | `src/torture/torture_main.c` | Graphical library flags |
| Optional `meta` stage | `src/metagen/metagen_main.c` | Built in debug mode, then executed to regenerate source |

Generated source is committed upstream. At this pin, `build.sh` executes
`metagen` only when explicitly given `meta`; compiling the checked-in generated
source does not require a generator built by another compiler.

Linux common source flags are `-mcx16 -I../src/ -I../local/ -D_GNU_SOURCE`
plus `-D_USE_MATH_DEFINES -Dstrdup=_strdup -Dgnu_printf=printf`. Debug mode
uses `-O0 -DBUILD_DEBUG=1`; release uses `-O2 -DBUILD_DEBUG=0`. Upstream also
adds `-g` and warning controls. Clang-specific diagnostics/warning options do
not define the application's language semantics.

The graphical build obtains compiler/linker flags from `pkg-config` for
`freetype2`, `x11`, `xext`, `xfixes`, `xrandr`, `gl`, and `egl`. Common
link libraries are `pthread`, `m`, `rt`, `dl`, and `atomic`. Upstream wraps
the application inputs, `blake3.a`, and `atomic` in an ELF linker group.
Ubuntu hosted prerequisites are `pkg-config`, `libfreetype6-dev`, `libx11-dev`,
`libxext-dev`, `libxfixes-dev`, `libxrandr-dev`, `libgl1-mesa-dev`, and
`libegl1-mesa-dev`, in addition to the repository's supported host toolchain.
The non-graphical upstream target still requests those library flags.

Upstream assembles four BLAKE3 sources from
`src/third_party/blake3/blake3_{sse2,sse41,avx2,avx512}_x86-64_unix.S`
and archives them into `blake3.a`. A host assembler and archiver may build
these unchanged assembly inputs. Record their provenance separately from
the C compiler: using a host assembler does not authorize compiling any
application or bundled C implementation with Clang and labeling it Buster.

## Language and backend requirements

[`base_context_cracking.h`](https://github.com/EpicGames/raddebugger/blob/f6b4a38134652886239b91f940cd7a67fedf689d/src/base/base_context_cracking.h)
selects Clang, MSVC, or GCC from compiler macros and rejects architectures
other than x86-64. Buster's prelude at baseline
`d4ca72fb4e97ae0a410b0d03258b00495fa45e13` already defines `__clang__`,
GCC compatibility macros, and target OS/architecture macros. This selects
upstream's Clang path, including its temporary feature-macro definitions
around `immintrin.h`; absent compiler identity is not an established blocker.

[`base_core.h`](https://github.com/EpicGames/raddebugger/blob/f6b4a38134652886239b91f940cd7a67fedf689d/src/base/base_core.h)
uses `__thread`, GNU statement expressions, `__int128`, alignment and section
attributes, `#pragma pack`, variadic macros, atomics, and standard hosted C
headers. Required GNU atomic operations include load, store, exchange,
compare-exchange, add/subtract-and-fetch, and fetch-or, with sequentially
consistent and relaxed orders. Correct 128-bit compare-exchange must retain
its observable expected-value update behavior and ordering.

[`base_strings.c`](https://github.com/EpicGames/raddebugger/blob/f6b4a38134652886239b91f940cd7a67fedf689d/src/base/base_strings.c)
includes `martins_memfun/memfun.h` with `MEM_STATIC` in every main target.
That header defines SSE2, AVX2, and AVX-512 implementations and runtime
dispatch, using CPUID, XGETBV, byte swaps, bit scans, and per-function
`target` attributes for XSAVE, AVX2/BMI/BMI2/MOVBE, and
AVX512F/BW/VBMI/BMI/BMI2. These C implementations remain part of the
compilation even when BLAKE3 assembly is handled externally.

[`base_hash.c`](https://github.com/EpicGames/raddebugger/blob/f6b4a38134652886239b91f940cd7a67fedf689d/src/base/base_hash.c)
includes MD5, SHA-1, and SHA-256 implementations. Their Clang paths require
rotate builtins, SHA-NI/SSSE3 intrinsics, and target-specific function bodies.
An audit of the baseline predefined builtin table suggests additional
vendor builtin coverage may be needed; source inspection alone is not a
reproduced compiler failure.

The Linux application consumes glibc/POSIX interfaces, including threads,
signals, virtual memory, futexes, subprocesses, `ucontext`, and `ptrace`.
The GUI uses X11, FreeType, and OpenGL/EGL. This harness does not establish
portability to a new OS or libc.

## Diagnostic and acceptance boundaries

With a configured tree and a trusted Release compiler, run:

```sh
./build.sh test_raddebugger --self-test
./build.sh test_raddebugger --config Release /absolute/path/to/raddebugger
```

The hosted workflow invokes that same action through its immutable bootstrap
driver, from the Buster repository root:

```sh
"$RUNNER_TEMP/raddebugger-driver" test_raddebugger --config Release "$GITHUB_WORKSPACE/external/raddebugger"
```

The GitHub-hosted `RAD Debugger compatibility` workflow installs the upstream
platform development libraries, builds the trusted compiler, runs its self-host
fixed point and input controls, and retains the compatibility artifacts even
when an application compilation fails.

The `test_raddebugger` build-driver action attempts the pinned `raddbg`,
`raddbg_non_graphical`, `radbin`, and `torture` C unity targets with Buster
and an independent Clang oracle, preserving the original source tree.
The four upstream targets use native `-fregister-allocator=fast` and the
default frontend SSA path. Both compiler attempts use the same source,
target, definitions, and system headers. Buster spells the baseline
x86-64/CX16/SSE2 target as `-mcpu=baseline -mattr=+cx16,+sse2`;
Clang uses `-march=x86-64 -mcx16 -msse2`. Separately attributed host
Clang assembly produces the unchanged BLAKE3 objects. Successful C objects
are passed to the host Clang linker with those assembly inputs.

Artifacts belong to an isolated `build/raddebugger-<pid>-<time>` directory.
`summary.tsv` records outcomes; command, stdout, stderr, and process-status
artifacts preserve individual steps. Retain compiler revisions and
object/binary identities alongside those outcomes. An oracle-only success
or a Buster diagnostic inventory is progress toward compatibility, not a
passing Buster application build.

`intrinsic-summary.tsv` records first-party header, scalar/vector,
unsupported-operation, guarded XGETBV and SHA witnesses. The SHA fixture
runs in the default FAST/SSA mode and three separately named diagnostic modes:
MIR_STACK, NONE, and FAST with `-fno-frontend-ssa`. The diagnostic modes
also request `-fverify-codegen`; they retain the same source and independent
Clang oracle. Their success cannot replace a failing default result.
Advanced Clang runtime witnesses require the corresponding CPU and OS state;
an unavailable reference is recorded as `hardware-pending`.

When both compilers link a matching `raddbg` or `radbin` executable,
`runtime-summary.tsv` records a paired, deterministic ELF-note dump. The
full graphical `raddbg` binary uses its `--bin` path. Each result must
match the independently specified note output, empty stderr, and zero exit
status. Runtime environments omit `DISPLAY` and `WAYLAND_DISPLAY`.
This checks binary loading without establishing GUI or debugger behavior.

Tool children have a 600-second deadline; headless runtime children have a
60-second deadline. Capture is limited to 16 MiB per stdout/stderr stream.
Timeout, capture failure, truncated output, or process-cleanup failure cannot
be credited as a successful step. Ordinary probe or Buster target failures
still retain the independent Clang diagnostics.

The harness's `--self-test` mode uses isolated Git fixtures to exercise
pristine-input acceptance and wrong-revision, tracked-dirty, untracked,
ignored-input, and hidden-index-flag rejection. Git probes filter inherited
`GIT_*` overrides and require the canonical repository root to match the
directory used for compilation. Passing those controls establishes the
harness's input boundary; it does not compile the upstream application.

The completed full graphical goal requires a Buster-produced object and
linked executable from the unchanged target. Separately validate startup
and debugger behavior before describing the result as a working debugger.
GUI execution, process control, DWARF/PDB conversion, debugger functionality,
Windows x64 compilation/linking, and performance acceptance remain separate
gates until actually run. Do not present a non-graphical success, host-compiled
fallback, static parse, or accepted unsupported operation as those results.

Perform correctness runs on authorized hosted infrastructure. Do not compile,
test, or benchmark this workload on the user's laptop or benchpress/9700X
in this task. Hosted timing is diagnostic evidence, not qualified performance
acceptance.

## Existing ownership and dependencies

The guarded literal XGETBV correction is already owned by
[#1487 / PR #2258](https://github.com/buster14a/buster/pull/2258).
It covers constrained fixed-register inline assembly on baseline and
XSAVE-disabled targets; it is relevant to upstream BLAKE3's GNU inline-asm
runtime probe. `memfun.h` also uses the distinct `__builtin_ia32_xgetbv`
spelling, so merging the assembly fix does not prove that builtin works.
Reproduce the remaining gap after the existing dependency lands instead of
duplicating its implementation.

At the audit, PR #2258 remained open under the explicitly recorded stable-main
hold on [#1751](https://github.com/buster14a/buster/issues/1751#issuecomment-5951717741).
Honor that owner's integration boundary; the RAD Debugger harness must not
remove the hold, rewrite the owned branch, or change retirement authority.
Keep current validation and handoff details on the owning issue/PR rather
than maintaining live progress in this guide.
