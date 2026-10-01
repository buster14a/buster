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
