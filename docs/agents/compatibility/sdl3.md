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
The tool refuses a different HEAD, tracked modifications and untracked files,
then repeats the check after execution. The hosted workflow exercises a dirty-input refusal;
a separate wrong-pin negative control remains pending. Build directories are outside the
upstream checkout. Wrong-pin/dirty failures must not be repaired by patching
sources or accepting a different revision.

| Selected source/component | Verified terms at the pin | Primary evidence |
| --- | --- | --- |
| SDL first-party source | Zlib | [LICENSE.txt](https://github.com/libsdl-org/SDL/blob/7f3ae3d57459e59943a4ecfefc8f6277ec6bf540/LICENSE.txt) |
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
software rendering, PulseAudio, joysticks (without HIDAPI), haptics, POSIX threads, libc, no PCH, and portable
scalar routines (`SDL_ASSEMBLY=OFF`). It preserves `-fwrapv`,
`-fno-strict-aliasing` and `-funsigned-char`.

The predeclared exclusions are Wayland, KMS/DRM, offscreen video, OpenGL,
OpenGL ES, Vulkan/GPU, ALSA, PipeWire, JACK, sndio, OSS, cameras, HIDAPI,
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

A timed-out fixture retains its primary failure and runs a separately labelled,
bounded GDB stack/instruction diagnostic. Debugger success does not repair the
failed runtime cell.

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

## Hosted execution

Development and correctness use cloud/GitHub-hosted Linux, never the laptop
or general-purpose agent execution on the 9700X. The
[workflow](../../../.github/workflows/sdl3-qualification.yml) is explicitly
opt-in; its temporary development trigger is limited to the owned
`codex/3188-sdl3-qualification` branch. It adds no PR/merge-group/main gate.

Build the existing compiler with its native driver. Then compile the intake
tool with the hosted compiler; GCC also validates it with warnings as errors:

```sh
clang -D_DEFAULT_SOURCE -Wall -Wextra -Werror -fwrapv -fno-strict-aliasing -funsigned-char tools/sdl3_qualification.c -o /absolute/private/path/sdl3-qualification
/absolute/private/path/sdl3-qualification /absolute/pristine/SDL /absolute/buster/build/Release/ide /absolute/fresh/evidence
```

The output path must not exist: it is created privately and owned by one run.
Use the workflow's fixed Xvfb display and PulseAudio `buster_sdl3` virtual
sink, or reproduce those isolated resources on authorized cloud infrastructure.
The tools remain external prerequisites, not Buster runtime dependencies.
GNU `timeout` bounds child process groups; timeout/signal/launch failures
remain nonzero, and a stage that did not execute has status 125. A zero fixture exit without
the exact all-success verdict is refused with status 126. Oracle corruption
negative controls remain pending.

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
The outer workflow retains failed stages and machine-specification records.

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
