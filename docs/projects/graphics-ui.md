# Graphics and UI components

Status: **preserved, unmaintained source**. This area has no default build,
compiler-test, fuzz, analyzer or automatic CI coverage. The ancillary test
sources and CMake helpers have been removed; the shared library sources remain.
Earlier test descriptions are historical references, not runnable current-tree
commands. Reconnecting a consumer requires an explicit maintenance decision.

[Catalogue](../../PROJECTS.md) · Area: `graphics-ui` · [Tracking](../project-tracking.md)

## Purpose and integration state

Retain reusable rendering, window, font and UI-construction components for Buster
applications. This is not a claim that the headless `ide` target is a graphical
IDE, nor a request to restore the removed custom-language editor.

## Capability entry points

| Stable feature ID | Existing source | Integration boundary |
|---|---|---|
| `graphics-ui.rendering` | [rendering.h](../../src/buster/lib/rendering.h), [rendering.c](../../src/buster/lib/rendering.c) | Retained rendering front door and backend implementations. Vulkan admits at most 16 texture slots per renderer and returns the invalid `UINT32_MAX` index when full; texture binding ignores invalid indices and retains the prior binding. Textures still have no per-texture replacement/release and live until renderer teardown. Device execution/support must be demonstrated by the consuming target. |
| `graphics-ui.raster` | [rendering_raster.h](../../src/buster/lib/rendering_raster.h) | Retained bounded CPU pixels and Linux/XCB presentation library; the image-browser application and raster test targets are removed. No GPU support claim. |
| `graphics-ui.windows` | Window modules described in the [platform guide](../agents/platform.md) | Native lifecycle/event/surface boundary. Android/iOS lifecycle use does not demonstrate a complete desktop UI. |
| `graphics-ui.fonts` | [truetype.h](../../src/buster/lib/truetype.h), [font_provider.h](../../src/buster/lib/font_provider.h) | TrueType test sources are removed; the current headless compiler has no production font consumer. |
| `graphics-ui.construction` | [ui_builder.h](../../src/buster/lib/ui_builder.h) | Retained UI construction API, not a supported end-user application by itself. |

## Texture storage and sampling

`TEXTURE_FORMAT_R8_UNORM` stores one linear unorm channel. The retained font
atlas currently packs white RGB and glyph coverage in alpha into
`TEXTURE_FORMAT_R8G8B8A8_SRGB`, which stores four 8-bit channels: sampled RGB
decodes from sRGB to linear values, while alpha remains unorm. Vulkan uses
`VK_FORMAT_R8G8B8A8_SRGB`, D3D12 uses `DXGI_FORMAT_R8G8B8A8_UNORM_SRGB` for
both the texture resource and sampled view, and Metal uses
`MTLPixelFormatRGBA8Unorm_sRGB` for the sampled texture. These texture mappings
do not select a render-target or presentation color space; those remain separate
backend contracts.

The removed `test_rendering_texture_formats` target checked the shared
channel/encoding contract and selected native format mapping without creating
a device or submitting GPU commands. It did not establish executed
sampling or cross-device pixel equivalence.

The [platform/backend guide](../agents/platform.md) owns native-surface boundaries,
backend inclusion, TrueType limits and the current dependency contract. Source
presence, build integration, test coverage and product support are different facts.
Raster-image recognition and decoding has a separate [media home](media.md);
using that library from a graphical product is an explicit consumer decision.

## Validation and future consumers

The TrueType, rendering, window and UI suites and their component targets have
been removed, including `test_ui_utf8`, `test_ui_scale`, `test_ui_slider` and
the raster/native tests. Neither `test_all` nor `test_units` includes them.
Their [pre-removal guide](https://github.com/buster14a/buster/blob/02a6414e020f5d549122197c3a7c8407df5bed76/docs/projects/graphics-ui.md)
records the former coverage and work-count controls. Test names mentioned
below describe that historical coverage only.

A new graphical consumer must explicitly register its dependencies, provide
focused tests, document its runnable entry point and supported backend/platform
combinations, and report actual rendering/lifecycle validation. Keep font
geometry/error limits in the [platform guide](../agents/platform.md). Do not add
unused UI or font dependencies to the compiler to activate this area.

## Slider input contract

`ui_slider` reserves Left/Right for five-percent value adjustments while focused,
including focus acquired through Tab or a pointer press. Values clamp to the
supplied endpoints; Tab and vertical arrows remain focus-navigation inputs.
`UI_BoxFlag_OwnsHorizontalArrows` gives other value widgets the same ownership
policy without making them text editors. Disabled or active-focus-disabled
sliders do not accept keyboard value changes.

A completed slider click reads its own release coordinate from
`UI_Signal.left_click_position`. Pointer moves later in the same event list
still update hover state, but do not change that committed value. A slider with
live left-button capture continues following the current pointer, including
outside its bounds; an outside release ends capture without reporting a click.
The removed `ui_slider_tests` module covered completed-hit-tree and reordered
widget builds through the real UI front doors and an inert renderer. No current
component target exercises this contract.

## CPU raster image presentation

The retained, unregistered `rendering_raster` component borrows caller-owned
RGBA8 source and canvas storage only for a draw/present call. It uses nearest
sampling, all eight reported Exif orientations, and straight-alpha composition
over a checkerboard. Canvas admission is capped at 4096 per axis and 64 MiB;
rejected admission leaves the canvas unchanged. It applies no ICC/gamma
conversion. Native presentation currently admits Linux XCB TrueColor visuals
with 16-, 24- or 32-bit pixels; other native surfaces fail explicitly.

The removed `test_rendering_raster` checked independent headless orientation,
alpha, pan, zoom and admission goldens. The removed native test required a real
X server (Xvfb qualified for this software backend), read server pixels back,
and exercised window events, resize and repeated shutdown. A separate
unavailable-display invocation checked recoverable connection failure.
Headless raster checks do not prove native transfer; XCB readback proves CPU
presentation and native lifecycle, with no GPU-rendering claim. These targets
do not add graphics dependencies to `ide`.

XCB initialization rejects error-bearing connections before native setup.
Shutdown consumes and clears the connection; repeated shutdown is safe.

`wm_window_set_title` admits validated UTF-8 metadata titles up to 4096 bytes
through checked XCB requests. Other native backends report unsupported.
`wm_poll_events_bounded` currently admits Linux XCB batches of 1–32 native
events, retaining unread events for a later call. It requires at least the
reported minimum remaining, already committed event-arena space before removing
each event. That allowance includes the existing 16 MiB file-drop path budget,
the slice array, alignment and event overhead. Library-internal native
allocations and the separate XDND transfer arena are outside this allowance.
The legacy full-drain API retains its existing contract.

Linux/XCB consumers that do not accept drops may set
`WmWindowCreate.disable_file_drop`. This omits XDND advertisement and ignores
addressed XDND messages before source watching, property reads or transfer
state changes. The default accepts bounded drops. Extended XDND type negotiation
requests at most 256 atoms (1024 payload bytes) per property reply and admits/scans
at most 1024 atoms across the entire list. A list above that work limit is refused
before accepting a match in its fetched prefix. The three inline advertised
types retain their constant-work fast path.

XDND direct and incremental payloads share a 16 MiB logical limit. The first
nonempty accepted chunk creates one precommitted, non-pooled staging arena:
16 MiB for its stable buffer plus a separate 64 KiB allowance for arena/page
storage. Reservation or commitment refusal rejects the transfer recoverably;
subsequent chunks append without replacement buffers. Completion, cancellation,
superseding negotiation and shutdown release this owner. INCR's advertised
length is a hint and cannot allocate storage. The former browser opted out
of drops; these enabled-consumer bounds do not alter its workflow.

XIM callbacks borrow the active poll's arena/list only during native polling;
the callback destination is cleared before return. Raw commit input is limited
to 4096 bytes before UTF-8/compound-text conversion, output to 16384 UTF-8 bytes,
and each poll to 32 conversion attempts and 64 KiB of published text. Rejected
conversion output still charges an attempt. Publication checks validated UTF-8
and already committed space for both the copied text and aligned event before
mutating the arena; a refusal emits no partial event. Conversion-library
allocations remain native-library owned and outside the event arena. These
limits do not establish a general bound on all XIM protocol-library activity.

CREATE_IC publication marks a window pending before `xcb_xim_create_ic`,
because the library processes its request queue synchronously and a send
failure or immediate reply can run the completion callback before the call
returns; that callback's transition is never overwritten. Only a direct refusal
(nothing queued) clears pending and advances the input-style attempt, once, and
the per-window loop never issues a second request while one is pending or a
context exists. Provider disconnect is handled by the `disconnected` callback:
xcb-imdkit frees queued requests without completion callbacks, so Buster clears
every window's IC, pending CREATE_IC, style attempt, negotiated styles,
style-query state, open state and provider event masks while retaining windows,
focus and arena ownership. Automatic reconnect then rebuilds contexts.

The removed `test_rendering_raster_native` exercised actual X-server XDND negotiation,
direct/INCR transfer, decoded-path ownership, cancellation and shutdown, plus
synthetic XIM reducer boundaries and actual poll scope teardown. Its ordinary
native invocation also requires a real XIM provider on a second XCB connection,
using the server API in the already linked `libxcb-imdkit`. Independently
negotiated provider cycles exercise UTF-8 and Compound Text commits through
normal bounded polling with nonzero input contexts. Independent Unicode golden
bytes, producer-payload mutation/release, scratch clobbering and text reads after
complete client/provider shutdown check caller-arena ownership. UTF-8 controls
also admit 4096 raw bytes, refuse 4097 bytes and malformed input, and recover with
a subsequent valid commit. Asynchronous commits followed by ordered XIM SYNC
replies distinguish consumed refusals from missing provider traffic. A third
UTF-8 cycle closes the provider after a commit, requires the client to
invalidate its session and contexts, keeps polling with the provider missing
without a fabricated context, then restarts a provider under the same
advertisement name and requires a rebuilt nonzero context, a correct commit and
teardown of both provider sessions. Synthetic controls drive the CREATE_IC
publication helpers for immediate failure, accepted asynchronous, immediate
success and refused requests, and the registered disconnect callback against a
window with a pending CREATE_IC.

Provider handshake, commit and teardown phases each have a 5-second deadline,
a 2048-pass cap, and bounded work per pass (64 provider events, 32 client events).
Missing providers or failed handshakes fail explicitly; no opt-in environment
variable skips this gate. These fixture bounds do not bound every underlying
X-server request or native-library allocation. The first-party fixture copies no
external implementation and adds no production dependency: its public server
API was inspected at [xcb-imdkit 1.0.9, commit 44f5c821](https://github.com/fcitx/xcb-imdkit/blob/44f5c8219bcae9e6afc2391dc50486efcf0bdf06/src/imdkit.h),
whose component notice is `LGPL-2.1-only`; independent Compound Text bytes follow
the [X Consortium Compound Text 1.1 contract](https://xorg.freedesktop.org/archive/current/doc/xorg-docs/ctext/ctext.html).
Hosted execution remains the required evidence for the provider gate under
[#2197](https://github.com/buster14a/buster/issues/2197); broader desktop input
method interoperability is outside this fixture's scope.

Required window-arena allocation failure releases native initialization and
returns failure. The existing arena reservation fault seam exercises this
recovery before subsequent successful native lifecycles.

The removed [native image browser](image-browser.md) consumed window lifecycle,
events, title updates and CPU raster presentation. Its historical workflow and
ownership reference is linked from the application guide. Retained UI
construction, fonts and Vulkan rendering remain independent unmaintained
components.
