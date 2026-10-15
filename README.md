# Buster

Buster is a personal monorepo. The headless C compiler and toolchain are the
maintained product. Shared GUI/media libraries and mobile-hosting source remain
unmaintained; the image browser, disk builder, ancillary component tests and
their CMake helpers have been removed.

Automatic builds and CI compile and test the compiler and its dependencies.
They do not build or test the retained graphics/UI, image/font libraries,
packaged examples or mobile application host. See the
[project catalogue](PROJECTS.md) for these boundaries.

## Find your way

| Question | Start here |
|---|---|
| How is the project website published? | [GitHub Pages integration and local preview](docs/github-pages.md) |
| What is in the monorepo? | [Projects and components](PROJECTS.md) |
| What can the compiler do? | [Compiler and toolchain](docs/projects/compiler.md) |
| How do I build and test? | [Build guide](docs/agents/build.md) and [tests/CI](docs/agents/testing.md) |
| How are PRs and merge groups validated? | [GitHub CI and no-code admission](docs/ci-github-actions.md) |
| What should change next? | [GitHub issues](https://github.com/buster14a/buster/issues) and [project/feature tracking](docs/project-tracking.md) |
| How should an agent contribute? | [AGENTS.md](AGENTS.md) |

Automatic CI classifies the complete PR or merge group. Only the reviewed files
listed in `tools/ci_no_code.c` count as prose; any other path, including other
Markdown, selects full validation. A prose-only final commit does not erase
execution-affecting changes earlier in the PR, and a prose-only merge group
queued behind code changes still runs full validation.

The `ide` target is currently headless; its historical name does not advertise a
working graphical IDE. See the compiler page for its entry point. Retained
unmaintained components have no automatic CI or compatibility promise and are
not dependencies of `ide`.

For attribution and third-party terms, see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)
and [LICENSES](LICENSES/). Do not infer a license grant for Buster's own code from
those notices.
