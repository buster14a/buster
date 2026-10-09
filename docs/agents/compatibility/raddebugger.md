# RAD Debugger compatibility

[Harness index](../compatibility.md) · Commands run from the Buster repository root.

The qualification goal is for Buster's active C frontend and canonical native
backend to compile and link the complete graphical `raddbg` application from
an unchanged upstream checkout, start its GUI, and exercise real debugging.
The opt-in harness has a Windows x86-64 CodeView/PDB qualification path and a
Linux x86-64 DWARF diagnostic path. Linux results remain experimental and are
qualified only for the targets, compiler pairings, and debugger observations
actually recorded by a run. Building `radbin`, `metagen`, or
`raddbg_non_graphical` does not establish that the graphical debugger builds
or that debugging works.

## CI scheduling

[The compatibility workflow](../../../.github/workflows/raddebugger-compatibility.yml)
runs only on pushes to `main` and checks out the exact triggering `github.sha`
(#3086). PRs, merge groups, other branches and manual dispatch do not start it.
Each main push retains its own run during merge bursts. The hosted runner,
pinned upstream input, negative controls, independent reference, and failure
artifacts remain the compatibility evidence. This post-merge diagnostic is
outside `CI complete` and pre-merge admission; failures need follow-up on the
landed commit. The `--debugger` option remains opt-in. Keep routine scheduling
main-only; do not add PR, merge-group, or manual-dispatch triggers for this
qualification.

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

## Windows x86-64 build inputs

The Windows path uses the same pinned upstream C unity units as the Windows
build scripts: `src/raddbg/raddbg_main.c` for the graphical debugger and
`src/com_shim/com_shim_main.c` for the command-line launcher. Buster and the
independent Clang reference compile both units. The Buster target is
`x86_64-pc-windows-msvc` with its baseline x86-64/CX16/SSE2 settings; Clang
uses the same target and architecture settings plus CodeView debug output.
The pinned upstream checkout remains pristine; all objects, executables,
PDBs, and generated platform inputs are written to the external harness
output directory.

Both C compilers receive `-nostdinc`, the selected Clang resource include
directory from `clang -print-resource-dir`, and each semicolon-separated
Visual Studio `INCLUDE` directory as an explicit system include. The Windows
SDK and Visual C++ headers are therefore attributable to the hosted VS x64
tool environment rather than to implicit host header search. The routine
requires the x64 SDK `INCLUDE` and `LIB` environment and the matching Clang,
`llvm-rc`, `llvm-ml`, `llvm-lib`, and `llvm-readobj` tools.

The logo resource is compiled by `llvm-rc`. Four unchanged x86-64 Windows
BLAKE3 MASM sources are assembled by `llvm-ml` and archived by `llvm-lib`.
The first-party C objects and those attributed platform inputs are linked by
host Clang with LLD. `/DEBUG` produces a PDB beside each executable; the
linker stage requires both the executable and its PDB. `llvm-readobj` checks
the AMD64 COFF machine and the expected entry-point symbol. These resource,
assembler, archive, and linker roles are recorded separately from Buster's
C compilation.

The `.com` smoke copies the matching Buster-built or Clang-built
`com_shim.exe` as `raddbg.com` beside that compiler's `raddbg.exe`. It invokes
the upstream `--bin` route and checks the owned shim child's bounded exit
status. The Windows console stream is not an oracle for this smoke. A separate
Clang-built COFF fixture and `llvm-readobj` provide the object-format check;
the GUI session below provides the debugger behavior check.

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

## Opt-in qualification and acceptance boundaries

The command is opt-in on both platforms. From a configured Buster tree, the
same `test_raddebugger --debugger` action is available through the Unix and
PowerShell entry points:

```sh
./build.sh test_raddebugger --self-test
./build.sh test_raddebugger --config Release --debugger /absolute/path/to/raddebugger
```

```powershell
.\build.ps1 test_raddebugger --self-test
.\build.ps1 test_raddebugger --config Release --debugger C:\path\to\raddebugger
```

A hosted Linux qualification run bootstraps the same build driver and invokes
it under `xvfb-run -a`. A Windows x64 run initializes the Visual Studio x64
SDK and pinned LLVM tools, builds the trusted compiler, then invokes that
same `test_raddebugger --config Release --debugger <path>` action. Preserve
the routine main-only workflow schedule. Hosted setup and qualification logs
and the isolated output tree should remain available when a compile or session
fails.

The ordinary action compiles the pinned Linux `raddbg`,
`raddbg_non_graphical`, `radbin`, and `torture` C unity targets with Buster and
an independent Clang reference. The four targets use native
`-fregister-allocator=fast` and default frontend SSA. Compiler attempts use
the same source, target, definitions, and system headers. Buster spells the
baseline x86-64/CX16/SSE2 target as `-mcpu=baseline -mattr=+cx16,+sse2`;
Clang uses `-march=x86-64 -mcx16 -msse2`. Upstream's Linux BLAKE3 assembly is
built by the separately attributed host assembler and archive tooling, then
linked with the C objects by host Clang. A successful reference build,
non-graphical target, or binary-format check does not establish Buster's
graphical application or debugging behavior.

On Windows x64, the action compiles the exact pinned C unity units
`src/raddbg/raddbg_main.c` and `src/com_shim/com_shim_main.c` with Buster and
Clang. The first-party C fixture is also compiled by both. Both compilers use
the x86-64 MSVC target and the same SDK header directories explicitly passed
from `INCLUDE`, plus Clang's resource headers. `llvm-rc` builds the logo
resource, `llvm-ml` assembles the unchanged Windows BLAKE3 MASM inputs,
`llvm-lib` archives them, and host Clang+LLD links with `/DEBUG`. A Windows
link step counts only when both the executable and its PDB exist. These
resource, assembler, archiver, and linker tools are recorded separately from
the Buster C compile. The `.com` smoke runs the adjacent upstream shim through
`--bin` and checks its child exit status; captured console output is not its
oracle.

The opt-in debugger fixture is
[`tests/raddebugger_debuggee.c`](../../../tests/raddebugger_debuggee.c). Buster
and Clang compile it with `-g -O0`; each linked program first runs directly,
outside the debugger, with display variables removed. The control requires
zero exit status, empty stderr, and the exact output line:

```text
RADDEBUGGER_DEBUGGEE main=329 worker=337 values=2,3,5 record=17,5,257,7,11,13 thread=joined
```

The Windows fixture writes stdout in binary mode so this byte-exact marker
uses LF on Windows as well as Linux. It creates and joins one worker thread
(pthreads on Linux, a Win32 thread on Windows), and its source markers place
breakpoints in the outer and inner calls and worker entry. The debugger checks
array elements, aggregate fields and bitfields, locals before and after a
step, thread selection, and caller-frame evaluation.

The four debugger/debuggee cells run trust-first, in this order: Clang debugger
with Clang debuggee, Clang debugger with Buster debuggee, Buster debugger with
Clang debuggee, and Buster debugger with Buster debuggee. Each cell gets a
distinct IPC port and isolated user, project, log, and session directory. The
C supervisor's self-test must emit its exact success marker before a GUI
result can count. `debugger-summary.tsv`, command transcripts, per-session
logs, and captured fixture output retain the observations and failures.
Timeouts, truncated capture, IPC errors, or failed cleanup cannot count as a
passing cell.

On Linux, `--debugger` requires a non-empty `DISPLAY` and fails closed if the
X11 display cannot be opened. The hosted route uses Xvfb. The C supervisor
observes a mapped RAD Debugger X11 window, forks the GUI process, verifies
that it owns the requested TCP listener, and sends commands through the
pinned `raddbg --ipc --ipc_port:<port> <command>` client. The Linux
tracer uses ptrace exit-kill cleanup behavior; the supervisor also asks the
debugger to terminate its target, bounds process-group termination, and
requires the observed debuggee PID to disappear. The Linux IPC protocol does
not expose the debuggee's raw exit code to this supervisor; the debugger
session oracle is the unique fixture output marker plus process disappearance.
The separate direct-run control checks the real child exit status.

On Windows, the session starts the GUI with an isolated RAD project whose
target configuration names the exact debuggee and dedicated stdout/stderr
capture files. The supervisor checks a visible window owned by the launched
RAD process and verifies that the same process owns the requested listener.
It sends pinned IPC commands over loopback TCP, identifies stopped threads by
their Windows thread and process IDs, and confines the debugger and its
descendants in a kill-on-close Job Object. After continuing the debuggee, it
requires exactly one fixture marker, waits for the owned process handle to
signal, checks the real process exit code is zero, and verifies that Job
Object cleanup leaves no live descendants.

Linux/DWARF remains an experimental diagnostic route. Report only the exact
targets, compiler pairings, output marker, source locations, and debugger
observations present in a run's artifacts. The supported Windows
x64/CodeView/PDB harness route is described here, but this guide makes no
claim that any current run has passed. Full compatibility requires successful
Buster C compilation and linking from the unchanged pinned input plus all four
passing debugger cells for the requested platform; do not infer it from a static parse,
reference-only success, headless smoke, or partial set of observations.

Harness artifacts are isolated under `build/raddebugger-<pid>-<time>` on
Linux and `build/raddebugger-windows-<pid>-<time>` on Windows, outside the
upstream checkout. The harness's input self-test uses isolated Git fixtures
for pristine-input acceptance and wrong-revision, tracked-dirty, untracked,
ignored-input, and hidden-index-flag rejection. Git probes filter inherited
`GIT_*` overrides and require the canonical repository root to match the
directory used for compilation. These controls establish the input boundary;
they do not compile the application.

`intrinsic-summary.tsv` records first-party header, scalar/vector,
unsupported-operation, guarded XGETBV, and SHA witnesses. The SHA fixture runs
in default FAST/SSA mode and separately named diagnostic modes MIR_STACK,
NONE, and FAST with `-fno-frontend-ssa`; these request `-fverify-codegen` and
cannot replace a failing default result. Advanced Clang runtime witnesses
require the corresponding CPU and OS state; an unavailable reference is
recorded as `hardware-pending`. On Linux, `runtime-summary.tsv` records paired
ELF-note dumps for matching `raddbg` or `radbin` binaries; on Windows,
`llvm-readobj` records the AMD64 COFF and entry-point checks. These checks do
not establish GUI behavior.

Run correctness qualification on authorized hosted infrastructure. Hosted
runtime measurements are diagnostic only; performance acceptance has not been
established by this harness and must remain a separate goal. Do not compile,
test, or benchmark this workload on the user's laptop or benchpress/9700X in
this task.

## Existing ownership and dependencies

The guarded literal XGETBV correction is owned under
[#1487 / PR #2258](https://github.com/buster14a/buster/pull/2258). It covers
constrained fixed-register inline assembly on baseline and XSAVE-disabled
targets, relevant to upstream BLAKE3's GNU inline-asm runtime probe.
`memfun.h` also uses the distinct `__builtin_ia32_xgetbv` spelling, so the
assembly correction does not prove that builtin works. Track dependency state
and handoff through [#1751](https://github.com/buster14a/buster/issues/1751)
and the owning PR. Reproduce any remaining gap after that dependency lands;
do not duplicate its implementation or alter its integration and retirement
authority. Keep live validation and ownership updates on those issue/PR
threads rather than in this guide.
