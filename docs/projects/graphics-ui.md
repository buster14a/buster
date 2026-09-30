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

## Validation and future consumers

Use the existing `truetype_tests` and dependency checks described in that guide
for font changes; keep geometry/error limits in one place rather than copying them
here. A new graphical consumer must explicitly register its dependencies, document
its runnable entry point and supported backend/platform combinations, and report
actual rendering/lifecycle validation. Do not add unused UI or font dependencies
to the compiler merely to make this area appear active.

Create a feature issue for a concrete application workflow or component behavior,
not a speculative checklist claiming that a future editor/viewer already exists.
