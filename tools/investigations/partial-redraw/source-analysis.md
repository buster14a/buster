# Partial redraw: source contract and bounded experiment design

Owner: [#1952](https://github.com/buster14a/buster/issues/1952).
Buster source anchor: main `8f67df736f13d4edc055110a7a6d619a00a22eaf`,
tree `8810c6603e47e5eecf26e1d73633619a347c70d8`.
The [frozen experiment contract](contract.md) defines the executable model.
This document contains source observations and derived proof/cost boundaries;
it contains no executed pixel results, accepted timings, or GPU-performance claim.

## Observed integration and rendering contract

[AGENTS.md](https://github.com/buster14a/buster/blob/8f67df736f13d4edc055110a7a6d619a00a22eaf/AGENTS.md),
[platform guidance](https://github.com/buster14a/buster/blob/8f67df736f13d4edc055110a7a6d619a00a22eaf/docs/agents/platform.md),
and the [graphics/UI project page](https://github.com/buster14a/buster/blob/8f67df736f13d4edc055110a7a6d619a00a22eaf/docs/projects/graphics-ui.md)
describe opt-in reusable components. The active `ide` is headless; source
presence does not establish a running graphical product or supported GPU path.

| Source and symbols at the anchor | Observation relevant to partial redraw |
|---|---|
| [rendering/internal.h](https://github.com/buster14a/buster/blob/8f67df736f13d4edc055110a7a6d619a00a22eaf/src/buster/lib/rendering/internal.h): `RenderingCommand`, `RenderingCommandStream`, `RENDERING_MAX_DRAW_COUNT` | Ordered bounded commands, resource snapshots and batches exist. Maximum count is 4096; clip depth is 64. Commands have no stable interframe identity, resource-content generation, output support, or history epoch. Vertex/index buffers carry geometry. |
| [rendering.c](https://github.com/buster14a/buster/blob/8f67df736f13d4edc055110a7a6d619a00a22eaf/src/buster/lib/rendering.c): `rendering_command_stream_begin`, `rendering_command_stream_record_rect` | Every frame resets command/batch/vertex/index counts and arenas. Only resource bindings persist. Consecutive compatible draws batch without reordering; clips, resources, targets and flushes break batches. Retained modules do not constitute an interframe display list. |
| Same file: `rendering_clip_rect_from_f32`, `rendering_command_stream_push_clip`, `rendering_command_stream_record_target` | Clips apply positive content scale, outward floor/ceil, target intersection and stack intersection. Only the backbuffer target is accepted. Each draw snapshots its effective clip and bindings. |
| Same file: `rendering_window_render_rect`, `rendering_window_render_text`; [rect.slang](https://github.com/buster14a/buster/blob/8f67df736f13d4edc055110a7a6d619a00a22eaf/src/buster/lib/shaders/rect.slang): `rect_vertex`, `ps_main`, `rect_fs` | Rectangles scale geometry independently of atlas UVs, interpolate four corner colors across two triangles, and multiply texture samples by rounded-SDF coverage. Rectangle radius is hardcoded to `5 * min(scale)`, softness to 1. Glyph geometry depends on atlas metrics and kerning. |
| [vulkan.c](https://github.com/buster14a/buster/blob/8f67df736f13d4edc055110a7a6d619a00a22eaf/src/buster/lib/rendering/vulkan.c), [metal.c](https://github.com/buster14a/buster/blob/8f67df736f13d4edc055110a7a6d619a00a22eaf/src/buster/lib/rendering/metal.c), [d3d12.c](https://github.com/buster14a/buster/blob/8f67df736f13d4edc055110a7a6d619a00a22eaf/src/buster/lib/rendering/d3d12.c): `rendering_window_frame_begin/end` | All clear the full frame. Vulkan starts `render_image` with UNDEFINED layout and CLEAR, then copies the complete image to the acquired swapchain image. Metal obtains a drawable and uses CLEAR. D3D12 clears the entire current render target. A dirty scissor alone would therefore erase unchanged pixels. |
| Same backends: rectangle pipeline blend/sampler creation | RGB is `Cs * As + Cd * (1-As)`; stored alpha is replaced by `As` through ONE/ZERO factors. This is not accumulated source-over alpha. Rectangle samplers use linear filtering and repeat; blur samplers use linear filtering and clamp. |

The CPU model preserves the observed blend algebra using declared integer
rounding, but excludes floating-point shader evaluation, bilinear/repeat
sampling, sRGB, four-color interpolation, rounded corners, SDF softness and GPU
rasterization. Its nearest 8x8 R8 coverage texture is a diagnostic abstraction,
not an assertion that Buster's atlas/shader pipeline has these semantics.

`TEXTURE_FORMAT_R8G8B8A8_SRGB` maps to true sRGB in Vulkan, but UNORM in
Metal/D3D12. That source-observed discrepancy is tracked separately in
[#1953](https://github.com/buster14a/buster/issues/1953). Compare full and partial
paths under one fixed backend/format contract; do not treat cross-backend
equality as established or repair color policy in this experiment.

## Derived pixel influence and unaffected-region proof

Let `F_i(p)` be framebuffer sample `p` after command `i`. A local draw
writes only within a conservative support `S_i`: rasterized geometry
intersected with the effective clip and target, including every possibly
nonzero coverage sample. Within it, `F_i(p) = blend(source_i(p), F_(i-1)(p))`;
outside it, `F_i(p) = F_(i-1)(p)`.

For a changed/replaced/removed command seed damage with both old and new
supports. For reordered commands conservatively seed the supports of all
affected commands; intersections may sharpen this only with an explicit proof.
Compare complete facts, not merely rectangle coordinates: geometry/UVs/colors,
effective clip, order, pipeline/blend/sampler/target interpretation, and resource
identity/content generation. A clip edit affects dependent draws under both old
and new clips until the corresponding stack boundary. Atlas metric or kerning
changes can also alter generated glyph positions and extents.

For the frozen local model, a pixel outside the seed sees the identical
ordered local operators and clear value. Induction over commands proves its
final value unchanged. Repair dirty pixels by restoring their declared clear
and evaluating every current contributor in original order. A removed command
is absent during replay, allowing the underlying scene to appear. Dirty regions
are disjoint in this experiment. Clearing their union once and then issuing
draws separately for overlapping rectangles would blend their intersection
twice. Independently clearing and fully reconstructing each patch could remain
correct, but repeats work; the mask/tile partition avoids both problems.

Transparency preserves dependence on earlier destination RGB. Occlusion pruning
is justified only where a later command provably writes coverage-one,
alpha-one values independent of prior destination, including every channel that
will be consumed later. Rounded edges, softened edges, texture coverage and
four-color interpolation prevent assuming that an entire bounding rectangle is
opaque. The bounded model deliberately performs no occlusion pruning.

Alpha-zero is not an RGBA no-op under alpha replacement: it preserves RGB but
stores alpha zero, and removing it can restore alpha 255. A minimal opaque-red
background followed by blue with alpha 128 yields `(127, 0, 128, 128)` under
the frozen integer contract; changing the background to opaque green yields
`(0, 127, 128, 128)` despite the unchanged blue overlay. Comparison must include
alpha, and support means every possible write, including alpha-only changes.

Texture changes require input dependency as well as output support. Bilinear
sampling can read neighboring texels beyond the nominal atlas rectangle;
repeat addressing can wrap to the opposite edge. Mipmaps, derivatives, anisotropy
or changed samplers would widen/change that relation. The current public API
appends textures via `rendering_texture_create`, rather than exposing mutable
texel updates, but an experiment must still freeze dimensions, format, content
generation and descriptor interpretation. A numeric handle alone is not a
content certificate. Unknown mutation, ID reuse without a generation change, or
concurrent writes invalidate that certificate.

## Blur, other nonlocal effects and history

`rendering_command_stream_record_background_blur_rounded` records the clipped
output mask and caps radius at 32. `rendering_blur_dimensions_make` uses
`ceil(width/2)` and `ceil(height/2)`. The three
`*_record_background_blur` backend functions capture the current target at
the command boundary, downsample the whole target, run horizontal convolution,
then vertically sample/composite under a rounded mask. Vulkan uses a linear
blit for downsampling; Metal/D3D12 use a sampling pass.
[blur.slang](https://github.com/buster14a/buster/blob/8f67df736f13d4edc055110a7a6d619a00a22eaf/src/buster/lib/shaders/blur.slang)
defines bounded ±radius taps and clamp sampling.

For each output pixel `p`, define `R_i(p)` as the union of original target
samples reached through final bilinear sampling, vertical taps, horizontal
sampling/taps and downsample sampling. Earlier damage `D` can affect this
blur wherever `R_i(p) intersects D`, intersected with the output mask. A blur
parameter/mask change seeds the old and new output supports directly. Propagate
that resulting damage through later commands and later framebuffer readers in
order. This is a dependency relation, not a radius in framebuffer pixels:
convolution radius is in half-resolution texels, and odd target dimensions
change the mapping. A smaller halo requires a documented sampling/raster
contract; unknown footprints require conservative whole relevant-target repair.

The previous frame's final image is not a valid pre-blur snapshot. It may contain
draws that occurred after the blur. Sampling those retained pixels while
partially replaying an earlier blur changes draw-order semantics. Supporting
partial blur therefore needs retained stage snapshots or reconstruction from
clear of the entire input dependency cone at each framebuffer-read boundary.
The first prototype excludes all framebuffer reads and invokes full redraw for
blur or any unknown/nonlocal effect.

History must certify the scene/contract revision actually stored in an owned
target and its successful completion. An acquired drawable/backbuffer may be
several frames old; repair must include changes since that buffer's own valid
epoch, or copy a complete current owned image. Resize, surface/device recreation,
clear/format/scale/shader/sampler changes, missing history, and an unsuccessful
frame invalidate history. Publish a new valid epoch only after successful full
or partial completion; failed/overflowed streams must not certify output.

Existing Vulkan frame fences, Metal `waitUntilCompleted` and D3D12 fence waits
protect current frame-resource reuse. A new preserved target also needs explicit
read/write ordering, resource lifetime and descriptor snapshot rules.
Presentation completion and rendering completion are distinct. Unchanged
frames still have submission/presentation costs if they are presented.

## Counterexamples to naive rectangle-union invalidation

| Naive rule | Counterexample and required repair |
|---|---|
| Union only new rectangles | Removal has no new rectangle; movement leaves the old location stale. Include old supports and reconstruct the underlying scene. |
| Redraw only commands whose properties changed | An unchanged translucent overlay composites a changed background. Replay the overlay over the reconstructed preceding scene. |
| Paint a changed translucent draw onto retained final pixels | Its old contribution is blended a second time. Clear/replay the region from the same baseline as full redraw. |
| Issue a draw per overlapping damage rectangle | Their intersection blends translucent content twice. Execute a disjoint damage partition. |
| Dirty only a blur's output rectangle when the blur changes | A changed background outside that rectangle can be sampled into it by an unchanged blur. Follow the input sampling relation. |
| Equal rectangle/texture handle means equal output | Atlas rebinding/generation, clip, shader, sampler or color-space changes alter output. Compare the complete contract. |
| Last frame's damage repairs every backbuffer | A reused buffer may predate several frames. Track history per buffer or use an owned current complete image. |

## Compared mechanisms, retained memory and cost

| Mechanism | CPU bookkeeping and replay | State retained between frames | Synchronization/traffic considerations |
|---|---|---|---|
| Unconditional redraw | Generate commands; clear whole target; replay all current draws. No old/new matching or damage planning. | No previous scene/dependency structure. Ordinary frame buffers/resources remain. | Existing submission/fences. Clears and full rendering may benefit from attachment optimizations and intact batching. |
| Command-level invalidation | Match bounded IDs; compare complete command/resource facts and order; union old/new supports; clear/replay contributors against a disjoint mask. Planning and retained-scene update remain even for unchanged pixels. | Previous complete bounded scene/resource facts; damage mask; valid history target/epoch. Prototype mask is bounded by 12,288 pixels. | History ordering plus normal resource/submission rules. Fragmented regions increase tests/scissors/draw pieces and may disrupt GPU batches. |
| Tile dependencies | Construct/compare ordered contributor lists and complete facts; dirty whole unequal tiles; replay their contributors. No hash-only equality proof. | Previous scene plus ordered per-tile dependencies and dirty state; history target/epoch. Prototype has 48 tiles. | Coarser repair shades more pixels but can reduce irregular scheduling. Dense overlap increases edge storage/comparisons; history costs remain. |

The common 128x96 RGBA8 history image is 49,152 bytes. Report fixed allocated
scene/dependency/mask storage separately from live dependency edges and temporary
oracle/scratch buffers. Full redraw does not intrinsically require interframe
history; the diagnostic compares common framebuffer storage fairly.

Do not scale the prototype's dense dependency matrix blindly. At 1920x1080
with 16x16 tiles there are `120 * 68 = 8160` tiles. A naive
`uint32_t[tile][4096 commands]` list allocates 133,693,440 bytes per snapshot
(127.5 MiB), or 255 MiB for old/current snapshots before framebuffers. These
are derived byte counts, not measurements. A production experiment would need
bounded packed edges, a demonstrated maximum/budget, and full-redraw fallback
when the budget is exceeded.

The existing draw stream must still be constructed/compared; reduced blended
samples do not eliminate upstream UI/draw generation. Preserving a target can
add load/store traffic, copies, barriers and synchronization. Fragmentation can
multiply replay pieces and destroy batching. CPU and GPU overlap on a real
critical path; adding standalone CPU/GPU durations does not establish frame
latency. Measure planning, submission, GPU rendering, preservation/copy traffic,
waits and end-to-end latency independently and together.

## Primary-source research and licenses

These are author/specification observations used to choose tests, not Buster
measurements or permission to import a dependency.

- [Nikolas Zimmermann, September 21, 2026](https://blogs.igalia.com/nzimmermann/posts/2026-09-21-skia-compositor-damaging/):
  the WebKit damage work records old/current painted supports and removed
  layers, accounts for backdrop effects and changed compositing state, creates
  nonoverlapping bounded repair rectangles, and tracks each swapchain buffer's
  accumulated damage. A whole-draw fast path mattered because unnecessary
  split planning caused a serious regression. Application here: test removal,
  transparency, fragmentation and dense controls before believing pixel savings;
  preserve batching and make history age explicit. The article's gains combine
  several compositor changes and are not a partial-redraw speedup prediction
  for Buster.
- [EGL_KHR_partial_update v14, April 24, 2025](https://registry.khronos.org/EGL/extensions/KHR/EGL_KHR_partial_update.txt),
  verified from Khronos' registry source, blob
  `30010c983032e9549def19b4d99fbca6ffab7bdb`: buffer repair and changed surface
  damage have different meanings. Query buffer age, set the entire repair
  region before rendering, and respect undefined-content/resize rules.
  Application here: missing/unknown history requires a full repair. This is
  background reasoning; the experiment adds no EGL backend.
- [VkPresentRegionsKHR](https://github.khronos.org/Vulkan-Site/refpages/latest/refpages/source/VkPresentRegionsKHR.html):
  incremental present is an optional presentation hint. Every presented pixel
  must still contain its desired value if the hint is ignored. Application
  here: present damage is not proof that unrendered pixels exist or are current.
- [Khronos Vulkan Guide: swapchain semaphore reuse](https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html),
  source blob `5095b0045018083433571a5e7aae7177295c9fb9`: a submission fence
  does not by itself certify presentation completion. Semaphore reuse can be
  indexed by acquired swapchain image and ordered by acquisition. Application
  here: preserve backend presentation synchronization when adding history;
  do not conflate its epoch with a frame-resource fence.

| Project/document inspected | Verified license status and scope |
|---|---|
| Buster | First-party license unspecified. [LICENSES/README.md](https://github.com/buster14a/buster/blob/8f67df736f13d4edc055110a7a6d619a00a22eaf/LICENSES/README.md) explicitly says preserved upstream terms do not select Buster's license; [#621](https://github.com/buster14a/buster/issues/621) owns the decision. |
| WebKit | Mixed/per-file terms. [WebCore/LICENSE-APPLE](https://github.com/WebKit/WebKit/blob/main/Source/WebCore/LICENSE-APPLE), verified blob `f29b41c3b2a316522d0e3493a28e96a851697b2e`, is BSD-2-Clause; it does not license every WebKit file. |
| Skia | [LICENSE](https://github.com/google/skia/blob/main/LICENSE), verified blob `e32636d5668d51ca7a938ae6cef93c795c656d7c`, is BSD-3-Clause; component-specific terms still require inspection if copying material. |
| Vulkan Guide | CC BY 4.0, verified in [LICENSE](https://github.com/KhronosGroup/Vulkan-Guide/blob/main/LICENSE), blob `52bd1459bd805f5a715e4975287d84c44201a79e`, and the chapter's SPDX header. |
| EGL/Vulkan specifications; Igalia article | Specifications carry Khronos copyright/registry terms; the article has author copyright. No article-wide reusable-code license was verified. Do not infer their license from WebKit, Skia or Vulkan Guide. |

No external code, rendering library or new dependency is adopted.

## Supported envelope, fallback and next packet

The supported **reference-model** envelope is exactly the frozen contract:
128x96, at most 64 stable unique IDs, integer half-open axis-aligned geometry
and effective clips, fixed clear, constant straight RGBA8, optional nearest
8x8 R8 immutable coverage textures, declared per-draw integer rounding, local
operations, and immutable serial inputs. Compare every RGBA byte exactly,
including replaced alpha. This establishes neither current shader equivalence
nor GPU execution.

Full repair is mandatory whenever history/contract/resource equality cannot
be proved, a command/effect falls outside the envelope, or bounded storage
overflows. If the complete input cannot be rendered, report failure rather
than publishing a partial success. A valid full render reestablishes history.

Smallest subsequent GPU packet, conditional on an available approved device:
one isolated offscreen consumer; one backend and target format; owned persistent
image/epoch; at most 64 local immutable commands; unconditional control; disjoint
dirty-scissor clear/replay; no blur. Freeze resource/sampling/blend/shader state
and numerical rules before running. The public front door currently initializes
rendering against a native window; it is not an existing public offscreen API.
A diagnostic backend seam or minimal C surface consumer may be required.
Reusing the existing shader for full-versus-dirty-scissor comparison on one
backend is a valid alternative to an integer/nearest diagnostic shader, with its
own comparator frozen before execution. Neither choice is an already runnable
graphical product. Verify full/partial output using readback
outside timed regions, then use warmed asynchronous GPU timestamps and fixed
queue depth alongside CPU planning/submission and whole-frame measurements.
Keep unchanged, sparse, dense and fragmented controls and raw paired samples.
Do not change production behavior, CI/admission policy or default dependencies.

The strongest counterargument is that a small batched renderer can already
redraw cheaply, while matching scenes, maintaining history, preserving target
contents and splitting batches add permanent CPU/memory/synchronization costs.
Even perfect local correctness and fewer shaded samples do not refute it.
The model can justify further work only inside its supported envelope; a
production go decision needs an actual consuming target and approved GPU
measurements. Absent those, the production decision remains no-go.
