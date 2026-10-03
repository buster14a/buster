# Graphics and UI components

[Catalogue](../../PROJECTS.md) · Area: `graphics-ui` · [Tracking](../project-tracking.md)

## Purpose and integration state

Retain reusable rendering, window, font and UI-construction components for Buster
applications. This is not a claim that the headless `ide` target is a graphical
IDE, nor a request to restore the removed custom-language editor.

## Capability entry points

| Stable feature ID | Existing source | Integration boundary |
|---|---|---|
| `graphics-ui.rendering` | [rendering.h](../../src/buster/lib/rendering.h), [rendering.c](../../src/buster/lib/rendering.c) | Retained rendering front door and backend implementations; device execution/support must be demonstrated by the consuming target. |
| `graphics-ui.raster` | [rendering_raster.h](../../src/buster/lib/rendering_raster.h) | Bounded CPU pixels and Linux/XCB presentation for the separate image-browser target; no GPU support claim. |
| `graphics-ui.windows` | Window modules described in the [platform guide](../agents/platform.md) | Native lifecycle/event/surface boundary. Android/iOS lifecycle use does not demonstrate a complete desktop UI. |
| `graphics-ui.fonts` | [truetype.h](../../src/buster/lib/truetype.h), [font_provider.h](../../src/buster/lib/font_provider.h) | TrueType has a registered headless test consumer; the current headless compiler has no production font consumer. |
| `graphics-ui.construction` | [ui_builder.h](../../src/buster/lib/ui_builder.h) | Retained UI construction API, not a supported end-user application by itself. |

The [platform/backend guide](../agents/platform.md) owns native-surface boundaries,
backend inclusion, TrueType limits and the current dependency contract. Source
presence, build integration, test coverage and product support are different facts.
Raster-image recognition and decoding has a separate [media home](media.md);
using that library from a graphical product is an explicit consumer decision.

## Validation and future consumers

Use the existing `truetype_tests` and dependency checks described in that guide
for font changes; keep geometry/error limits in one place rather than copying them
here. A new graphical consumer must explicitly register its dependencies, document
its runnable entry point and supported backend/platform combinations, and report
actual rendering/lifecycle validation. Do not add unused UI or font dependencies
to the compiler merely to make this area appear active.

`./build.sh build --config Release -t test_ui_utf8` runs a separate headless UI
component executable when tests and libc are enabled on desktop targets. It uses
the production `ui_core` module to check Unicode scalar text-event activation and
underline draw commands, including UTF-8 width/scalar boundaries and per-byte
replacement of malformed input. Native rendering calls are counted and must
remain unused. Desktop `test_all` and `test_units` include this component gate;
the compiler has no added UI, window, or rendering dependency. The retained
`ui_test.c` suite is not registered by this target, and these headless checks do
not establish device rendering or a supported graphical application.

Create a feature issue for a concrete application workflow or component behavior,
not a speculative checklist claiming that a future editor/viewer already exists.

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
`./build.sh build --config Release -t test_ui_slider` runs the focused
`ui_slider_tests` module against the real `ui_core`/`ui_builder` front doors and
an inert native renderer boundary. Desktop `test_all` and `test_units` include
this component when tests and libc are enabled; mobile and tests-disabled graphs
omit it. It covers the completed hit tree and reordered widget builds without
adding UI dependencies to the compiler. The broad retained `ui_tests` suite is
unregistered; these component checks do not establish device rendering support.

## CPU raster image presentation

The separately registered `rendering_raster` component borrows caller-owned
RGBA8 source and canvas storage only for a draw/present call. It uses nearest
sampling, all eight reported Exif orientations, and straight-alpha composition
over a checkerboard. Canvas admission is capped at 4096 per axis and 64 MiB;
rejected admission leaves the canvas unchanged. It applies no ICC/gamma
conversion. Native presentation currently admits Linux XCB TrueColor visuals
with 16-, 24- or 32-bit pixels; other native surfaces fail explicitly.

`test_rendering_raster` checks independent headless orientation, alpha, pan,
zoom and admission goldens. `test_rendering_raster_native` requires a real
X server (Xvfb qualifies for this software backend), reads server pixels back,
and exercises window events, resize and repeated shutdown. The separate
unavailable-display invocation validates recoverable connection failure.
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
length is a hint and cannot allocate storage. The browser continues opting out
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

`test_rendering_raster_native` exercises actual X-server XDND negotiation,
direct/INCR transfer, decoded-path ownership, cancellation and shutdown, plus
synthetic XIM reducer boundaries and actual poll scope teardown. Its ordinary
native invocation also requires a real XIM provider on a second XCB connection,
using the server API in the already linked `libxcb-imdkit`. Two independently
negotiated provider cycles exercise UTF-8 and Compound Text commits through
normal bounded polling with nonzero input contexts. Independent Unicode golden
bytes, producer-payload mutation/release, scratch clobbering and text reads after
complete client/provider shutdown check caller-arena ownership. UTF-8 controls
also admit 4096 raw bytes, refuse 4097 bytes and malformed input, and recover with
a subsequent valid commit. Asynchronous commits followed by ordered XIM SYNC
replies distinguish consumed refusals from missing provider traffic.

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

The separately registered [native image browser](image-browser.md) consumes
window lifecycle/events/title updates and CPU raster presentation. Its first
platform, file/decode ownership, loading policy and executable launch live in
the application guide. Retained UI construction, fonts and Vulkan rendering
remain independent components rather than unused application dependencies.
