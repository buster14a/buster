# Bounded partial-redraw experiment contract

Owner: [#1952](https://github.com/buster14a/buster/issues/1952).
Source anchor: Buster main `8f67df736f13d4edc055110a7a6d619a00a22eaf`,
tree `8810c6603e47e5eecf26e1d73633619a347c70d8`.

Frozen before executing model checks or timings. This diagnostic is separate
from production rendering, the compiler, registered tests, CI policy and
performance admission.

## Question and hypotheses

H1: local immutable commands admit sound bounded incremental reconstruction.
H2: exact command damage avoids more pixel work than coarse tiles, but its CPU
bookkeeping can cost more than it saves.
H3: an ordered tile dependency list can improve sparse replay but dense or
fragmented scenes favor unconditional redraw.
H4: local correctness and reduced pixel work do not establish faster GPU frames.

The cheapest discriminator is exact CPU output versus an independent full
oracle, followed by work counts. Stop any cost verdict on an image mismatch.
Do not rerun to select a favorable result.

## Supported reference envelope and arithmetic

Target is exactly 128x96; tiles are 16x16, 48 in total. At most 64 ordered,
unique command IDs in 0..63. Geometry and effective clips are integer, half-open,
axis-aligned rectangles intersected with the target. Inputs are constant
straight RGBA8 colors and optional nearest-neighbor 8x8 R8 coverage textures
with explicit immutable ID/generation pairs. Clear is a known opaque constant.

For each covered sample let `a = (As * coverage + 127) / 255`.
For each RGB component, `out = (Cs*a + destination*(255-a) + 127) / 255`,
using unsigned integer division. Stored alpha is replaced by `a`.
An untextured draw has coverage 255. Every draw rounds independently.
This follows the observed RGB/alpha blend algebra, but does not reproduce
Buster's floating-point rounding, linear/repeat texture sampling, sRGB handling,
four-color interpolation, rounded corners or SDF softness.

No transforms, antialiasing, MSAA, framebuffer reads, blur, multiple targets,
unknown shaders, temporal effects, concurrent resource mutation or hidden state.
Frames/resource data are immutable during a call; one serial CPU model path.
No new worker pool, dependency, frontend IR or production API is introduced.

## Numerical comparison fixed before testing

Require exact equality of every RGBA byte at every one of 12,288 pixels.
No epsilon, ignored alpha, percentage mismatch allowance or visual-only pass.
Oracle: per pixel, clear then evaluate the complete ordered current scene.
Candidate: command-major region replay, clearing dirty pixels and replaying
all current contributors in the same order. Invalidation must not inspect
oracle pixels. Diagnostic changed-pixel counts are computed after planning.

First execute adversarial add/remove/move/reorder/clip/transparency/atlas-version/
history/unsupported-contract cases and 2,000 deterministic fixed-seed transitions.
Independent minimal hand goldens and deliberately broken naive invalidators
must show sensitivity. GCC and Clang agreement is corroboration, not the
definition of the integer rendering contract. ASan/UBSan checks are separate
from optimized timing.

## Compared mechanisms and bounded storage

Unconditional: clear whole target, replay all current commands; no old scene.
Command-level: match bounded stable IDs, compare complete command/resource
facts and order, union old/new clipped supports of changed commands in a
bounded pixel mask, then clear/replay current contributors only in that mask.
Tile-level: retain ordered contributing command-ID lists per tile; structurally
compare lists and complete command/resource facts (no hash-only proof); dirty
a whole tile when equivalence cannot be proved. No opaque occlusion pruning.

All damage is disjoint at execution (one mask/tile partition); a sample must
not be blended twice due to overlapping invalidation rectangles.
Report retained scene/dependency/mask storage separately from the 49,152-byte
common history framebuffer and temporary oracle/scratch storage. Fixed buffers
are worst-case allocations; observed live edge counts are not allocated bytes.

Counters include dirty/actually changed pixels, command/ID/tile membership and
comparison work, candidate coverage tests, blended samples, replayed command
pieces and dependency edges. A reduced sample count is not a frame-time verdict.

## Fallback

Full repair for invalid/unknown/stale history, clear/contract/size changes,
unsupported effects, invalid IDs/resources/geometry or capacity overflow.
A valid full render publishes history only after it completes successfully.
Invalid input that cannot be fully rendered must fail instead of certifying
partial output. Resource ID reuse without a generation change is outside the
immutable-resource contract; callers must invalidate history when that promise
cannot be made.

## CPU measurements

Standard GitHub-hosted runner only. Same optimized C binary, same scenes and
command-major replay implementation. Full mode uses its full-region fast path,
not the slower per-pixel oracle. Workloads: unchanged, sparse, dense, fragmented,
opaque-covered mutation and glyph generation change. Freeze each pair before
timing, verify output outside timed regions, then rotate full/command/tile order
with fixed repetitions. Report planning and replay separately and combined.
Include retained-scene copy/update in incremental planning.

Harness framebuffer resetting, correctness oracle, diagnostic changed-pixel
counts, process startup and artifact upload are outside timings. Real rendering
requires neither cloning oracle pixels nor this reset. Model timing is an
in-process CPU diagnostic on tiny synthetic scenes, not a representative
application, dedicated-9700X result, confidence-bounded admission or GPU result.

## GPU experiment, conditional on an approved environment

No GPU execution is authorized on the user's desktop. The existing Linux GPU
workflow validates external artifacts; optional Metal creates a pipeline without
submitting GPU commands. This does not furnish a rendering measurement route.
The dedicated 9700X gateway does not admit this experiment.

Smallest future packet: one isolated offscreen backend consumer, explicit owned
persistent target/history epoch, <=64 bounded commands, dirty-scissor clear/replay
for this restricted local set, plus an unconditional control. Exclude blur.
Do not change production defaults while establishing the premise.

Before execution fix device/driver/backend, target format/color space, shaders,
blend/sampler/AA state, resource snapshots and history age. Require same-device
full-versus-partial exact bytes for the restricted integer/nearest contract;
if a floating/filtered production contract is investigated later, declare its
tolerance from documented representation/error bounds before running it.
Never relax a failed comparator after observing outputs.

Use correctness readback outside timing and asynchronous GPU timestamp pairs
with fixed queue depth and warmed pipelines/resources. Record CPU planning/
submission, render GPU duration, preserved-target load/store/copy, synchronization
and complete frame latency. Preserve raw paired samples and null/unchanged,
sparse, dense and fragmented controls. Proposed production threshold: at least
10% paired end-to-end benefit on designated sparse workloads with a 95% interval
excluding zero, no more than 2% regression on dense/fragmented controls, no
correctness failures, and a bounded memory budget agreed for the consuming target.
These are prospective experiment criteria, not an existing Buster admission rule.

Decision: CPU exactness may admit the model envelope. Production incremental
redraw is no-go until an actual consumer and permitted GPU measurements justify
the extra retained state, synchronization and framebuffer traffic.
