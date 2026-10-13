# FFmpeg compatibility

[Harness index](../compatibility.md) · Initial harness: [#3177](https://github.com/buster14a/buster/issues/3177) · Optimized profile: [#4078](https://github.com/buster14a/buster/issues/4078).

The native `test_ffmpeg` action provides matched Linux x86-64 portable and
assembly-enabled software profiles. A passing run is a bounded configuration,
media and no-sample FATE result, not complete FFmpeg qualification. Production
CPU dispatch, licensed sample FATE, video/audio workflows, rebuild costs and
approved Zen 5 performance evidence remain separate gates.

## Immutable input and license intake

Use an external, complete, clean checkout of FFmpeg 7.1.3 at
[`f46e514491172d15bd74b4abb1814cd2f05a763e`](https://github.com/FFmpeg/FFmpeg/commit/f46e514491172d15bd74b4abb1814cd2f05a763e).
The annotated upstream tag `n7.1.3` resolves to this commit. The driver rejects
another HEAD, tracked modifications, untracked/ignored files, sparse checkouts
and hidden index flags. It compares the worktree to the immutable commit again
after execution. Generated files and objects belong outside the input checkout.

The pin's [LICENSE.md](https://github.com/FFmpeg/FFmpeg/blob/f46e514491172d15bd74b4abb1814cd2f05a763e/LICENSE.md)
declares LGPL-2.1-or-later for the default combination. It separately identifies
MIT/X11/BSD-style components, IJG notices in JPEG-derived files, optional GPL
parts and GPL-covered testing tools including `tests/checkasm/*`,
`tests/tiny_ssim.c` and `libswresample/tests/swresample.c`.
The pin's [libavutil/libm.h](https://github.com/FFmpeg/FFmpeg/blob/f46e514491172d15bd74b4abb1814cd2f05a763e/libavutil/libm.h)
also retains a BSL-1.0 notice for its Boost-derived erf algorithm.
GPL-covered external FATE helper tools keep their terms even when the selected
media libraries disable GPL components. Both profiles disable GPL, nonfree and
version3 components plus external autodetection. The optimized profile also
builds and runs upstream `tests/checkasm/checkasm`; this test-tool licensing is
separate from the selected media libraries' default LGPL-2.1-or-later
combination. The pinned [checkasm source](https://github.com/FFmpeg/FFmpeg/blob/f46e514491172d15bd74b4abb1814cd2f05a763e/tests/checkasm/checkasm.c)
contains its own license notices. This is license intake, not a complete
enabled-file or test-tool license audit. External FATE samples have no
qualified license/hash inventory yet and are not downloaded or executed by
this action. No external source or binary is vendored, installed, embedded or
redistributed. Buster's own license remains governed by
[LICENSES/README.md](../../../LICENSES/README.md).

## Run the harness

Build the trusted Release compiler through the native driver first, following
[the build guide](../build.md). The first-party no-input test is:

```sh
./build.sh test_ffmpeg --harness-self-test
```

For a pinned external checkout, the harness supports these opt-in commands:

```sh
./build.sh test_ffmpeg --self-test /absolute/path/to/ffmpeg
./build.sh test_ffmpeg --portable /absolute/path/to/ffmpeg /absolute/path/to/fresh-results /absolute/path/to/buster/build/Release/ide
./build.sh test_ffmpeg --optimized /absolute/path/to/ffmpeg /absolute/path/to/fresh-results /absolute/path/to/buster/build/Release/ide /absolute/path/to/nasm
```

The output parent must exist and the result directory must be new and outside
the upstream checkout. Paths used in upstream's compiler-command word expansion
must contain only letters, digits, slash, dot, underscore and hyphen; other paths
fail explicitly. The optimized profile requires an absolute NASM executable
path. Input controls use a disposable clone to reject an incorrect pin, changed
tracked source and an injected untracked header. They never modify the caller's
input. Sample completeness controls remain unimplemented.

The action owns normal out-of-tree upstream configuration and GNU Make builds.
Unmodified upstream shell/generation scripts are external prerequisites; no
repository shell workflow replaces the native orchestration. The independent
Clang reference runs first and remains visible if Buster fails. Candidate CC
invocations go through the native driver's `--cc` mode, which forwards every
argument to `ide cc` and records each invocation separately, with an explicit
Linux x86-64 baseline and disabled debug output. It never compiles a required
candidate C object with Clang or substitutes reference objects.

External Clang owns explicitly declared target/host linking and target
dependency preprocessing. Candidate host C utilities also use the recorded
Buster wrapper. These are separate dependency stages; the result is not a
native-Buster-linker qualification.

## Matched configurations

Both profiles request Linux x86-64/generic with explicit PIC, static libraries,
no hardware acceleration, no network and no external autodetection. The
portable control adds `--disable-asm`, `--disable-inline-asm` and
`--disable-x86asm`. The optimized profile enables those three features
explicitly and passes `--x86asmexe=<absolute NASM path>`. Both configurations
are assembled by one shared argument builder so their requested codec, parser,
container, filter and protocol selections match.

| Requested cell | Selection |
|---|---|
| Decode | H.264, HEVC, FLAC, MP3, AAC, signed 16-bit PCM, rawvideo, PGM-YUV test input |
| Containers / input | Matroska, MOV/MP4, FLAC, MP3, AAC, WAV, image2, rawvideo, raw PCM |
| Encode | FLAC, signed 16-bit PCM, rawvideo |
| Output | Frame CRC/MD5, CRC, null, WAV, FLAC, Matroska, MP4/ISMV, image2, rawvideo |
| Filters | Format/scale and audio format/resampling/channel mapping/buffer plumbing |
| Protocols | File and pipe |
| Still pending | Shared libraries, hardware acceleration, third-party codecs and production CPU-dispatch evidence |

Full `config.h` diffs are retained. Matching compares all component selections
and feature definitions except `CC_IDENT` and `FFMPEG_CONFIGURATION`, whose
original values remain in evidence. A mismatch fails the matched census; it
does not silently remove a failing feature.

For the optimized profile, `feature-manifest.tsv` requires
`ARCH_X86_64`, `HAVE_INLINE_ASM`, `HAVE_X86ASM`,
`HAVE_INTRINSICS_SSE2` and `CONFIG_RUNTIME_CPUDETECT`, plus
`HAVE_MMXEXT_INLINE` and `HAVE_SSSE3_INLINE`. It requires every
`HAVE_AESNI_EXTERNAL`, `HAVE_AMD3DNOW_EXTERNAL`,
`HAVE_AMD3DNOWEXT_EXTERNAL`, `HAVE_AVX_EXTERNAL`, `HAVE_AVX2_EXTERNAL`,
`HAVE_AVX512_EXTERNAL`, `HAVE_AVX512ICL_EXTERNAL`, `HAVE_FMA3_EXTERNAL`,
`HAVE_FMA4_EXTERNAL`, `HAVE_MMX_EXTERNAL`, `HAVE_MMXEXT_EXTERNAL`,
`HAVE_SSE_EXTERNAL`, `HAVE_SSE2_EXTERNAL`, `HAVE_SSE3_EXTERNAL`,
`HAVE_SSE4_EXTERNAL`, `HAVE_SSE42_EXTERNAL`, `HAVE_SSSE3_EXTERNAL` and
`HAVE_XOP_EXTERNAL` generated from this pin's `ARCH_EXT_LIST_X86_SIMD`. A
missing assembler or inline-assembly probe fails the profile instead of
silently accepting a reduced tier set. These are build/configuration floors;
they do not claim that every instruction tier executes on the host. It also
requires the generated `X86ASM` and `DEPX86ASM` entries in
`ffbuild/config.mak` to equal the requested NASM path, which catches
configure's fallback to another assembler. The run retains NASM's version
output and SHA-256 identity. The SSE2 macro is only FFmpeg's configure probe;
it does not prove that every intrinsic lowered to the intended instructions.

## Correctness and evidence

Each successfully built side encodes a deterministic native-generated 8,000
sample little-endian mono PCM fixture as FLAC, decodes it to PCM, requires exact
byte equality with the original fixture, and checks ffprobe's codec, sample
rate and channel count against fixed expectations. This checks a useful
lossless workflow against an independent fixture. It does not yet validate
H.264/HEVC clips, seeking, frame extraction, lossy tolerances or cross-decoding
reference/candidate outputs.

The optimized profile builds the pinned `checkasm` target and verifies that
`tests/checkasm/x86/checkasm.o` exists. It runs checkasm once without `--bench`
so the upstream suite exercises every compiled applicable family on CPU tiers
safely detected by FFmpeg. The parser fails on missing, malformed, duplicate,
failed, empty or zero-test summaries. It also requires passing `[OK]` records
for the selected H.264, HEVC, FLAC and scale floor families. The full raw
stdout/stderr and a family ledger are retained.

Upstream's summary counts unique optimized function pointers, not every test
case or the complete implementation denominator. Family `[OK]` rows are
grouped reports, not named implementation witnesses. A per-compiler checkasm
status is `not-run` until the checkasm executable is invoked; an earlier
configure, library build or checkasm-target build failure stays `not-run`,
with a matching not-run ledger. The receipt reports whether the two aggregate
pointer counts are equal, different or not run. A difference leaves that
comparison unqualified and does not identify a compiler fault; equality is
diagnostic only and does not prove the same functions or implementations ran.
These checks compare optimized functions to C references inside each compiler's
build; they do not prove cross-compiler equivalence or production dispatch. The
receipt therefore records `production_dispatch=not-run` and
`per_function_denominator=unknown`.

The harness prepares upstream's `tests/pixfmts.mak` through normal generation
before retaining `fate-list`, then runs `make fate` without external samples,
plus the pin's `fate-hw` target, and preserves reports. At this pin `fate-list`
includes the hardware-context API test although default `fate` does not depend
on it; the explicit target keeps the list denominator intact. Hardware
acceleration is disabled; device validation remains pending. The native count
ledger joins every declared no-sample test to its report, records
attempted/pass/fail/not-run/skip counts, and fails missing reports. A reference
failure remains a failure, not output to regenerate with Buster. The complete
licensed/pinned sample subset is still required before acceptance.

Artifacts include source Git-object inventory; compiler SHA-256 identities;
argv/cwd/stdout/stderr/status for every candidate CC action and top-level stage;
generated configurations; feature manifests; NASM version/hash provenance;
checkasm logs and counts; FATE reports; and a receipt separating reference and
candidate configure/build/checkasm/media/FATE outcomes. Per-command deadlines
are 60 seconds for compiler/runtime probes and 900 seconds for
configure/build/FATE. Capture overflow, launch failure, timeout or incomplete
process cleanup fails the command. Hosted elapsed values are diagnostic; there
is no performance qualification or ranking.

## Hosted scheduling

FFmpeg has no workflow of its own. The no-input `--harness-self-test` runs in
both existing native-driver lint blocks; it checks configuration
construction, the complete pinned x86 feature floor, tool identity, checkasm
summary and count controls without an external checkout or build. Full portable
or optimized external builds are reserved for
the approved shared main-only external-projects workflow under
[main-only scheduling](../../external-project-ci.md) (#3501). This change adds
no PR, merge-group, branch-push or manual full-build workload. A qualified
pinned recipe still needs its own runner-cost review.

## Remaining qualification

Keep #3177 and #4078 open until staged acceptance is met. The assembly-enabled
profile is an initial harness milestone. Production CPU-dispatch witnesses,
complete per-function implementation denominators and full selected-component
checkasm ownership still need evidence. Remaining gates include complete
sample provenance/negative controls and sample-test counts; enabled-component
and test-asset licensing; existing compiler blockers with their owners; all
required C actions and libraries; declared video/audio media cells; independent
reference artifacts; clean/edit/header rebuild, memory and code-size evidence;
and throughput evidence. Under #4013, eligible build/resource/native-host
claims use the admitted alternative route; ISA-width, instruction-scheduling,
branchless-selection and explicit Zen 5 throughput claims require the relevant
approved hardware experiment. Green ordinary correctness CI cannot replace
those missing results.
