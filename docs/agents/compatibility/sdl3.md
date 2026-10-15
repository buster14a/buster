# SDL3 qualification

[Harness index](../compatibility.md) · [Milestone #3188](https://github.com/buster14a/buster/issues/3188)

This is an opt-in Linux x86-64 compatibility campaign for external SDL3.
It does not add SDL to Buster's platform layer, compiler or application
dependencies. The native intake tool is `tools/sdl3_qualification.c`;
`tools/fixtures/sdl3_fixture.c` is an independent C client. Buster's existing
native `build.c` driver still configures and builds the trusted compiler.

## Immutable source and license intake

Use SDL 3.2.28 at
[`7f3ae3d57459e59943a4ecfefc8f6277ec6bf540`](https://github.com/libsdl-org/SDL/tree/7f3ae3d57459e59943a4ecfefc8f6277ec6bf540).
The tool refuses Git environment redirection, replacement refs, sparse checkout,
hidden index flags, different HEAD, tracked modifications, ignored and untracked files.
It additionally compares every regular file's unfiltered raw Git object hash and
executable mode against the immutable, replacement-disabled pinned tree, independently
of the index, then repeats the checks after execution. Historical hosted intake
executed untracked, ignored, assume-unchanged, skip-worktree, sparse, environment,
replacement and wrong-pin refusals plus a final clean control; the exact run and
retained artifact are recorded below. Build directories are outside the
upstream checkout. Wrong-pin/dirty failures must not be repaired by patching
sources or accepting a different revision.

| Selected source/component | Verified terms at the pin | Primary evidence |
| --- | --- | --- |
| SDL first-party source | Zlib | [LICENSE.txt](https://github.com/libsdl-org/SDL/blob/7f3ae3d57459e59943a4ecfefc8f6277ec6bf540/LICENSE.txt) |
| HIDAPI (required by upstream virtual-joystick configuration) | BSD-3-Clause OR GPL-3.0 OR original HIDAPI permissive terms; upstream allows user choice, BSD-3-Clause selected for this external intake | [HIDAPI LICENSE.txt](https://github.com/libsdl-org/SDL/blob/7f3ae3d57459e59943a4ecfefc8f6277ec6bf540/src/hidapi/LICENSE.txt), [LICENSE-bsd.txt](https://github.com/libsdl-org/SDL/blob/7f3ae3d57459e59943a4ecfefc8f6277ec6bf540/src/hidapi/LICENSE-bsd.txt) |
| Software renderer YUV conversion | BSD-3-Clause | [yuv2rgb/LICENSE](https://github.com/libsdl-org/SDL/blob/7f3ae3d57459e59943a4ecfefc8f6277ec6bf540/src/video/yuv2rgb/LICENSE) |
| Bundled fdlibm functions | Sun permissive notice-preservation terms; exact aggregate SPDX mapping unverified | [e_sqrt.c](https://github.com/libsdl-org/SDL/blob/7f3ae3d57459e59943a4ecfefc8f6277ec6bf540/src/libm/e_sqrt.c), per-file notices remain authoritative |
| Internal qsort | Zlib, Gareth McCaughan attribution | [SDL_qsort.c](https://github.com/libsdl-org/SDL/blob/7f3ae3d57459e59943a4ecfefc8f6277ec6bf540/src/stdlib/SDL_qsort.c) |
| dlmalloc fallback / PDClib strtok fallback | Public-domain/CC0 notices embedded in the files; libc selection does not erase provenance | [SDL_malloc.c](https://github.com/libsdl-org/SDL/blob/7f3ae3d57459e59943a4ecfefc8f6277ec6bf540/src/stdlib/SDL_malloc.c), [SDL_strtokr.c](https://github.com/libsdl-org/SDL/blob/7f3ae3d57459e59943a4ecfefc8f6277ec6bf540/src/stdlib/SDL_strtokr.c) |
| CRC32 and Murmur3 adaptations | Embedded public-domain statements plus SDL notices | [SDL_crc32.c](https://github.com/libsdl-org/SDL/blob/7f3ae3d57459e59943a4ecfefc8f6277ec6bf540/src/stdlib/SDL_crc32.c), [SDL_murmur3.c](https://github.com/libsdl-org/SDL/blob/7f3ae3d57459e59943a4ecfefc8f6277ec6bf540/src/stdlib/SDL_murmur3.c) |
| Upstream test source | Separate permissive grant; historical public-domain declaration; do not assign the library's Zlib terms to every test | [test/LICENSE.txt](https://github.com/libsdl-org/SDL/blob/7f3ae3d57459e59943a4ecfefc8f6277ec6bf540/test/LICENSE.txt), [test/COPYING](https://github.com/libsdl-org/SDL/blob/7f3ae3d57459e59943a4ecfefc8f6277ec6bf540/test/COPYING) |
| Upstream copied test resources | Per-asset terms: sample.wav has specific distribution permission, sword.wav CC-BY-4.0, AlDraw bitmaps public domain; other asset-specific verification remains pending | Same test license; no broad asset redistribution claim |
| X11/XTest, PulseAudio, glibc, compiler/build tools and server dependencies | External distribution package licenses, retain installed-package identities; full transitive license audit remains pending | Pinned upstream [Linux dependency guide](https://github.com/libsdl-org/SDL/blob/7f3ae3d57459e59943a4ecfefc8f6277ec6bf540/docs/README-linux.md), runner package copyright files |

The first-party fixture generates its own pixels and sample waveform; it
uses no upstream font/image/audio assets. No vendoring, distribution release,
embedding or Buster relicensing is authorized by this harness. Buster's grant
remains as recorded in [LICENSES/README.md](../../../LICENSES/README.md).

## First configuration

The profile is declared before compilation and identical for both compilers:
Debug, shared SDL3 and SDL3_test, X11 with directly linked system libraries,
software rendering, PulseAudio, joysticks, virtual joysticks, HIDAPI, haptics, POSIX threads, libc, no PCH, and portable
scalar routines (`SDL_ASSEMBLY=OFF`). It preserves `-fwrapv`,
`-fno-strict-aliasing` and `-funsigned-char`.

The predeclared exclusions are Wayland, KMS/DRM, offscreen video, OpenGL,
OpenGL ES, Vulkan/GPU, ALSA, PipeWire, JACK, sndio, OSS, cameras, HIDAPI joystick drivers/libusb,
sensors, udev, io_uring, D-Bus and IBus. These are outside the first
configuration, not backends disabled in response to a failure. Dummy video
and audio remain compiled as controls. Upstream CTest's own default dummy
drivers form a separate control cell; the native fixture insists on X11 and
PulseAudio and refuses driver substitution.

Profile revision 2 enables the joystick subsystem for both compilers. The
initial joystick-disabled reference profile built successfully but failed six
upstream automation tests that require virtual joysticks or their subsystem
dependencies (338 pass, 6 fail, 11 skip). That historical run is retained; its
failure is not attributed to Buster. No hardware joystick claim follows from
this correction. Profile revision 3 also enables haptics in both compilers:
the unmodified `main_testInitQuitSubSystem` explicitly tests that subsystem.
Every preceding failure remains recorded; tests are not filtered to hide it.
Profile revision 4 enables HIDAPI because pristine upstream CMake makes
`SDL_VIRTUAL_JOYSTICK` depend on it; disabling HIDAPI had implicitly disabled
the virtual driver, even with the main joystick subsystem enabled. HIDAPI
joystick device drivers and libusb remain explicitly disabled for both
compilers. The BSD license option is recorded above.

A timed-out fixture retains its primary failure and runs a separately labelled,
bounded GDB polling-breakpoint diagnostic. The debugger stops after 100
poll calls and captures the returned register and caller instructions; it
is an instrumented diagnostic, not an uninterrupted hang stack. Debugger success does not repair the
failed runtime cell. A separate, SDL-free mixed-compiler diagnostic calls
a Clang-compiled boolean callee from Clang, GCC and Buster clients. The client
supplies a nonnull pointer; the callee returns whether it is null. This checks
a literal false verdict without SDL/server state and retains callee disassembly.
It is a diagnostic, not a substitute for the full native workload.

Declared upstream build targets are `SDL3-shared`, `testautomation`,
`testatomic`, `testthread`, `testsem` and `testerror`.
The five CTest executables are the automated-test denominator; individual
automation-suite pass/fail/skip counts and reasons remain in its log.
Interactive test applications, other platforms and hardware backends remain
outside this denominator.

Both builds use pristine upstream CMake/Ninja. The candidate compiler is the
native intake executable with `CMAKE_C_COMPILER_ARG1=--cc`; the adapter passes
every argument unchanged to `ide cc` and records length-delimited argv and
working directory. It does not set compiler IDs, fabricate probe successes,
filter flags, replace failing C objects, or preseed configuration. A configure
failure prevents candidate build/tests and is reported separately.

## Cloud execution and scheduling

Development and correctness use cloud/GitHub-hosted Linux, never the laptop
or general-purpose agent execution on the 9700X. Ordinary whole-project
execution belongs to the single [main-only external-project owner](../../external-project-ci.md).
SDL has no qualified input recipe or native assignment there yet. This intake
adds no SDL workflow, branch-push/manual dispatch, PR gate or merge-group workload.
After the pinned recipe qualifies, its owner must add it to that existing Linux
OS job and assignment table; preserve the pending macOS/Windows cells.

The following is the environment contract for an authorized cloud intake,
not an additional Actions scheduling entry. Run from the Buster checkout root
with a frozen candidate compiler built by its native driver. Use private source,
server and evidence paths owned by one run. Retain the Buster revision, compiler
and harness hashes, installed package versions, machine/image, complete
commands and server logs with every result.

External prerequisites are Clang, GCC, CMake, Ninja, Git, pkg-config, GNU
timeout, objdump, sha256sum, GDB, Xvfb and PulseAudio. The selected Ubuntu
backend packages are `libx11-dev`, `libxext-dev`, `libxcursor-dev`,
`libxfixes-dev`, `libxi-dev`, `libxrandr-dev`, `libxss-dev`,
`libxtst-dev`, `libpulse-dev`, `pulseaudio`, `pulseaudio-utils`
and `xvfb`. Record their actual versions; they are external prerequisites,
not Buster runtime dependencies. The upstream input must be a complete clean
Git checkout at the pin above, without sparse checkout, hidden index flags,
replacement refs or Git environment redirection.

Build the existing compiler with its native driver. Compile the intake tool
with the hosted compiler; GCC also validates it with warnings as errors:

```sh
clang -D_DEFAULT_SOURCE -Wall -Wextra -Werror -fwrapv -fno-strict-aliasing -funsigned-char tools/sdl3_qualification.c -o /absolute/private/path/sdl3-qualification
gcc -D_DEFAULT_SOURCE -Wall -Wextra -Werror -fwrapv -fno-strict-aliasing -funsigned-char tools/sdl3_qualification.c -o /absolute/private/path/sdl3-qualification-gcc
/absolute/private/path/sdl3-qualification --check /absolute/pristine/SDL /absolute/fresh/source-check
/absolute/private/path/sdl3-qualification /absolute/pristine/SDL /absolute/buster/build/Release/ide /absolute/fresh/evidence
```

Before the last command, set `DISPLAY=:93`, `SDL_VIDEO_DRIVER=x11`,
`SDL_AUDIO_DRIVER=pulseaudio` and `CMAKE_BUILD_PARALLEL_LEVEL=2`.
Start an isolated `Xvfb :93 -screen 0 800x600x24 -nolisten tcp` server and
PulseAudio with `--exit-idle-time=-1`. Load `module-null-sink` with
`sink_name=buster_sdl3 rate=48000 channels=1`, make `buster_sdl3` the
default sink and retain `pactl list sinks` plus both server logs. Ensure
these selected resources are ready before execution and shut down only
resources owned by that run.

Each source-check/evidence output path must not exist: the tool creates it
privately. GNU `timeout` bounds child process groups; timeout/signal/launch
failures remain nonzero, and a stage that did not execute has status 125.
A zero fixture exit without the exact all-success verdict is refused with
status 126. Oracle corruption negative controls remain pending.

## Independent checks

The fixture checks these operations, with each result printed separately:

- Open an X11 window, resize from 64×64 to 80×60, and check the returned size.
- Draw four independently specified colors, check every SDL readback pixel,
  present, then independently check every native X11 window pixel.
- Inject a key through XTest and observe its actual SDL scancode, key and window.
  A separately tagged SDL event checks union layout, callback userdata and
  removal of callback authority.
- Exchange a known integer through an SDL mutex/condition/thread, check its
  calculated value, and validate the thread's exit status.
- Convert a known S16 stream to F32 at fixed rate and gain; compare every
  output sample to the predeclared mathematical values and require no remainder.
- Play a known float waveform through PulseAudio and use the independent
  libpulse-simple monitor to require 64 consecutive checked output samples.
- Destroy streams, renderer/window and SDL lifetime; require `SDL_WasInit(0)==0`.

Compile the fixture independently with Clang and Buster. Both link through the
named trusted external linker driver against each SDL build. Retain separate
trusted-client/reference-SDL, Buster-client/reference-SDL,
trusted-client/Buster-SDL and Buster-client/Buster-SDL outcomes. Linking with a
trusted external linker is explicitly attributed; it cannot justify replacement
C objects or a claim that Buster's own shared-library linker was qualified.

Xvfb/software pixels are real X11 presentation evidence, not GPU evidence.
XTest input is server-injected input, not physical-keyboard evidence.
Monitor samples prove transport through a virtual sink, not audible output.
The actual hardware/device cells remain pending. Upstream dummy controls and
unavailable cells must never be promoted into full application qualification.

## Evidence and acceptance limits

Each child has lossless argv, stdout/stderr, status and a hosted diagnostic
process-tree high-water RSS observation. Compiler/tool SHA-256, upstream tree,
installed package identities, CMake cache/logs, generated inputs, compilation
database, objects, libraries and fixture executables stay in the private
evidence directory. Input pin/pristine checks repeat after execution.
The execution owner must retain failed stages and machine-specification records.

Build command count, successful C actions and distinct source count are separate
denominators; do not infer them from the number of executables that link.
A successful configure is not a successful build. A successful library or
fixture is not completion of all upstream tests or of #3188.

Clean/source-edit/header-edit development-loop measurements, artifact
code/debug size, full target census, and applicable exact-candidate Ryzen 7
9700X performance evidence remain required by #3188. Hosted timings/RSS are
diagnostics and cannot support a performance ranking. No benchmark request or
performance-validation conclusion is made by this initial campaign.

Exact revisions, hosted runs, actual counts, observed blockers and remaining
gates are recorded on #3188 and its PR; historical reports are not current
compatibility claims.

## Historical evidence and current qualification frontier

The original [intake38134480483](https://github.com/buster14a/buster/actions/runs/38134480483)
used harness head `b52b04f6df4e662e8b67f67c83368eb6680e569a` and
compiler anchor `abb1c5afbcf55e120d83283ffbad402fc75bd80a`.
Eight input-refusal controls and the final clean/raw-object controls passed.
Reference CTest was 5/5, with automation 355 total / 344 pass / 0 fail / 11 skip.
Its original dependency-flag and foreign-Boolean blockers are historical:
#3465 and #3613 subsequently merged. Artifact11700936379 has archive SHA-256
`c6e511e6dbb69204e4df470645a9cca87fef907c7b148c094e0c27be89cccb1f`
and expires 2026-10-25; full raw archival beyond retention remains pending.

The [later unchanged intake38222238640](https://github.com/buster14a/buster/actions/runs/38222238640)
is a separate composition: diagnostic head `da64acdb117e88ce6595b71de0b84567637752b5` over main
`e13523e94128f980799e9a7e64663e4568b93cb0`, retaining the same
upstream pin and intake/fixture bytes. The #3653 owner reports the exact
[discovery records and following blockers](https://github.com/buster14a/buster/issues/3653#issuecomment-6120520220).
Do not promote those results to a different candidate head or treat the
historical per-project diagnostic workflow as the current scheduling contract.

The recorded candidate configure exits 0 and finds the multiarch X11/Xext
libraries after #3673 and #3648. It still fails the XOpenDisplay and
HAVE_XFIXES_H link checks on full-path `.so` operands
([#3712](https://github.com/buster14a/buster/issues/3712)), leaving
`SDL_X11_XFIXES=OFF` against the reference's ON. Configure success therefore
does not establish a matching profile. Candidate build exits 1 on
`-idirafter` ([#3676](https://github.com/buster14a/buster/issues/3676)).
Candidate-library tests and both candidate-library ABI cells remain unexecuted.
Artifact11734742728 has archive SHA-256
`64da018e1d41bd5dd5645cb91775b68c9a44345e338b1dac7d9e3d1714f4b50b`;
the owner records fourteen-day retention. These are reported observations,
not a new exact-head execution by this guide update.

Both blockers were repaired on main by #3715 and #3722. A later local cloud-container
intake, with the unchanged tool and fixtures, ran on PR merge head `56423abbb13a7526d3a775591d7c0be9690c49ef`
(main `897275fb9e3c6d92eab85cacc1641ecb5b51a939`). It found the
XOpenDisplay and HAVE_XFIXES_H checks succeeding and a feature summary
identical to the reference (`SDL_X11_XFIXES=ON`). The remaining probe
differences are compile-only `-Wl,--enable-new-dtags`, `-fcolor-diagnostics`
and `-fcomment-block-commands` refusals ([#4041](https://github.com/buster14a/buster/issues/4041)).
The candidate build passes `-idirafter` and produces 259 of the reference's
265 objects. Six still fail: `-std=c90` in four test targets
([#1418](https://github.com/buster14a/buster/issues/1418)), the `"%rax"`/`"%rcx"`
clobber spelling in `SDL_cpuinfo.c` ([#1415](https://github.com/buster14a/buster/issues/1415)),
and an unknown MMX builtin in Clang's `mmintrin.h` reached from `SDL_stb.c`
([#1419](https://github.com/buster14a/buster/issues/1419)).
Candidate tests and candidate-library ABI cells remain unexecuted. That run
was not the main-only hosted route; its results are diagnostic, and exact
records are on #3449.

Keep #3188 open until the matched candidate build/tests, four ABI cells,
negative-oracle controls, complete C-action/target/license closure,
durable raw archival and required cost/performance evidence are complete.
Hardware, GPU and physical audible-output cells remain pending.
