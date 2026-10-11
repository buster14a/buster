# FFmpeg compatibility

[Harness index](../compatibility.md) · Owner: [#3177](https://github.com/buster14a/buster/issues/3177).

The native `test_ffmpeg` action implements the initial Linux x86-64 portable
blocker census. A passing census is a bounded configuration result, not complete
FFmpeg qualification. The issue also requires optimized dispatch, applicable
sample-based FATE, video/audio workflows, rebuild costs and approved Zen 5
performance evidence.

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
The pin's [libavutil/libm.h](https://github.com/FFmpeg/FFmpeg/blob/f46e514491172d15bd74b4abb1814cd2f05a763e/libavutil/libm.h) additionally retains a BSL-1.0 notice for its Boost-derived erf algorithm.
GPL-covered external FATE helper tools keep their separate terms even when
the selected media libraries disable GPL components.
The portable configuration disables GPL, nonfree, version3 and external
autodetection. This is license intake, not a complete enabled-file or test-tool
license audit. External FATE samples have no qualified license/hash inventory
yet and are not downloaded or executed by this action. No external source or
binary is vendored, installed, embedded or redistributed; diagnostics retain
configuration and command evidence. Buster's own license remains governed by
[LICENSES/README.md](../../../LICENSES/README.md).

## Run the census

Build the trusted Release compiler through the native driver first, following
[the build guide](../build.md). From the Buster repository root:

```sh
./build.sh test_ffmpeg --self-test /absolute/path/to/ffmpeg
./build.sh test_ffmpeg --portable /absolute/path/to/ffmpeg /absolute/path/to/fresh-results /absolute/path/to/buster/build/Release/ide
```

The output parent must exist and the result directory must be new and outside
the upstream checkout. Paths used in upstream's compiler-command word expansion
must contain only letters, digits, slash, dot, underscore and hyphen; other paths
fail explicitly. Input controls use a disposable clone to reject an incorrect
pin, changed tracked source and an injected untracked header. They never modify
the caller's input. Sample completeness controls remain unimplemented.

The action owns normal out-of-tree upstream configuration and GNU Make builds.
Unmodified upstream shell/generation scripts are external prerequisites; no
repository shell workflow replaces the native orchestration. The independent
Clang reference runs first and remains visible if Buster fails. Candidate CC
invocations go through the native driver's `--cc` mode, which forwards every
argument to `ide cc` and records each invocation separately, with an explicit Linux x86-64 baseline
and disabled debug output. It never compiles
a required candidate C object with Clang or substitutes reference objects.

External Clang owns explicitly declared target/host linking and target dependency
preprocessing. Candidate host C utilities also use the recorded Buster wrapper.
These are separate dependency stages; the result is not a native-Buster-linker
qualification. Both configurations request
Linux x86-64/generic with explicit PIC on both sides, static libraries, assembly disabled, no hardware
acceleration, no network and no external autodetection. Preserve the exact
generated configuration and command records; requested components are not
proof that configure enabled them.

| Requested cell | Initial selection |
|---|---|
| Decode | H.264, HEVC, FLAC, MP3, AAC, signed 16-bit PCM, rawvideo, PGM-YUV test input |
| Containers / input | Matroska, MOV/MP4, FLAC, MP3, AAC, WAV, image2, rawvideo, raw PCM |
| Encode | FLAC, signed 16-bit PCM, rawvideo |
| Output | Frame CRC/MD5, CRC, null, WAV, FLAC, Matroska, MP4/ISMV, image2, rawvideo |
| Filters | Format/scale and audio format/resampling/channel mapping/buffer plumbing |
| Protocols | File and pipe |
| Pending configurations | External assembly, intrinsic/optimized paths, shared libraries, hardware acceleration and third-party codecs |

Full `config.h` diffs are retained. Matching compares all component selections
and feature definitions except `CC_IDENT` and `FFMPEG_CONFIGURATION`, whose
original values remain in evidence. A mismatch fails the matched census; it
does not silently remove a failing feature.

## Correctness and evidence

Each successfully built side encodes a deterministic native-generated 8,000
sample little-endian mono PCM fixture as FLAC, decodes it to PCM, requires exact
byte equality with the original fixture, and checks ffprobe's codec, sample
rate and channel count against fixed expectations. This checks a useful lossless
workflow against an independent fixture, not only agreement between compilers.
It does not yet validate H.264/HEVC clips, seeking, frame extraction, lossy
tolerances or cross-decoding reference/candidate outputs.

The action prepares upstream's `tests/pixfmts.mak` through normal generation
before retaining `fate-list`, then runs `make fate` without external
samples, plus the pin's `fate-hw` target, and preserves test reports. At this pin
`fate-list` includes the hardware-context API test although default `fate` does
not depend on it; the explicit target keeps the list denominator intact.
Hardware acceleration is disabled; device validation remains pending. These tests form the applicable no-sample subset of
the declared configuration. The native count ledger joins every declared no-sample test to its report,
records attempted/pass/fail/not-run/skip counts, and fails missing reports.
The complete licensed/pinned sample subset is still required before acceptance. A reference failure remains a failure, not expected output to
regenerate with Buster.

Artifacts include source Git-object inventory; compiler SHA-256 identities;
argv/cwd/stdout/stderr/status for every candidate CC action and top-level stage;
generated configuration; FATE reports; and a receipt separating reference and
candidate configure/build/media/FATE outcomes. Per-command deadlines are 60
seconds for compiler/runtime probes and 900 seconds for configure/build/FATE.
Capture overflow, launch failure, timeout or incomplete process cleanup fails
the command. Hosted elapsed values are diagnostic; there is no performance
qualification or ranking.

## Hosted scheduling

[The workflow](../../../.github/workflows/ffmpeg-compatibility.yml) runs on
explicit manual dispatch, plus changes on the implementation's named opt-in
diagnostic branch `codex/3177-ffmpeg-qualification`. It does not run on PR,
merge-group or main events and adds no required check. Execution is on a
GitHub-hosted Linux runner, with machine records and diagnostics retained even
on failure. Broader scheduling needs its own runner-cost review.

## Remaining qualification

Keep #3177 open until its staged acceptance is met. In particular, implement
complete sample provenance/negative controls and sample-test counts; audit selected
components/test assets; minimize actual compiler blockers with their existing
owners; validate all required C actions and libraries; add optimized/checkasm
and declared video/audio media cells; retain independent reference artifacts;
and publish clean/edit/header rebuild, memory, code-size and throughput evidence
through the approved 9700X route. Green ordinary correctness CI cannot replace
any of those missing results.
