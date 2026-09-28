# Projects and components

[Home](README.md) · [Tracking conventions](docs/project-tracking.md) · [Source map](docs/agents/project.md)

Buster is the umbrella. These entries identify coherent projects and shared
components in the existing tree; they are not a source-directory reorganization,
a package manifest, or a list of everything Buster may eventually contain.

The **Area ID** is the stable routing key for work. A capability's home is the
linked project documentation; its implementation tasks belong in GitHub issues.
An entry establishes where to look, not a blanket support or test-pass claim.

## Applications and tools

| Area ID | Project | Existing source | Features and integration boundary |
|---|---|---|---|
| `compiler` | C compiler and toolchain | [Compiler modules](src/buster/lib/compiler/) and [headless entry point](src/buster/apps/ide/ide.c) | [Capabilities and contracts](docs/projects/compiler.md). `ide` is headless; C is the active source frontend. |
| `disk-tools` | Disk-image tooling | [disk_builder.c](src/buster/apps/disk_builder.c) | [Source scope and limitations](docs/projects/disk-tools.md). Standalone source, not a default CMake target. |

## Shared components

| Area ID | Component | Existing source | Features and consumers |
|---|---|---|---|
| `foundation` | Memory, strings, numeric/byte helpers and SIMD | [Shared library sources](src/buster/lib/) | [Foundation capabilities](docs/projects/foundation.md). Shared primitives used by compiler/runtime code; not a separately packaged SDK. |
| `platform` | OS, files and process services | [os.h](src/buster/lib/os.h), [file.h](src/buster/lib/file.h), [entry_point.h](src/buster/lib/entry_point.h) | [Platform capabilities](docs/projects/platform.md). Runtime and tool consumers; platform-specific contracts remain explicit. |
| `graphics-ui` | Rendering, windows, fonts and UI construction | [Shared library sources](src/buster/lib/) | [Graphics/UI boundaries](docs/projects/graphics-ui.md). Retained modules, plus specific test/lifecycle consumers; no current graphical product is implied. |

## Development infrastructure

| Area ID | Component | Existing source | Contract and consumers |
|---|---|---|---|
| `build-test` | Build driver, tests, CI and developer tooling | [build.c](build.c), [CMakeLists.txt](CMakeLists.txt), [tests](tests/), [tools](tools/), [.github](.github/) | [Build](docs/agents/build.md), [tests/CI](docs/agents/testing.md), and [research lifecycle](docs/agents/research.md). Repository development and validation. |
| `bench-service` | Queued benchmark execution service | [Service sources](tools/bench_service/) | [Service contract](tools/bench_service/README.md). A service implementation is not proof of installation, host qualification or performance acceptance. |
| `repository` | Repository-wide documentation and coordination | [Agent entry point](AGENTS.md) and [documentation](docs/) | [Project/feature tracking](docs/project-tracking.md). Cross-cutting work without a more specific owning area. |

## Experiments and preservation

An experiment starts with a local README or a short section in its owning
project: purpose, actual way to run it, and known limitations. Give it a separate
catalogue entry when it develops independent consumers or maintained scope; do
not create an empty project for every idea. Use `Kind: Research` on the owning
issue rather than treating every experiment as an unrelated area.

[DORMANT_CUSTOM_COMPILER.md](DORMANT_CUSTOM_COMPILER.md) and the retained `.bbb`
fixtures are historical preservation material, not an active frontend or a
commitment to restore it. Listing them does not reactivate builds, tests or CI.

## Keeping the catalogue useful

Add or change an entry when a project's purpose, source home, consumers or
integration boundary changes. Keep its ID stable across display-name or directory
changes. Small utilities can use their README as the entire feature page. Split
an area only when independently actionable work or consumers justify it.

Keep priorities, active branches, ownership claims, blockers and run results on
the owning issues/PRs and their GitHub Project view, not in this file. Shared work
has one primary area and linked consumers; do not duplicate a task per consumer.
