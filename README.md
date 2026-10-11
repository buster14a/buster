# Buster

Buster is a personal monorepo for applications, shared C components, development
tools, and experiments. The from-scratch C compiler and toolchain is one project
in that collection, not the boundary of what belongs here.

Start with the [project catalogue](PROJECTS.md) to find each project's source,
features, consumers, and documentation. Existing components are not all packaged
products: retained source and standalone tools have explicit integration limits.

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

Automatic CI classifies the complete PR or merge group. A prose-only final
commit does not erase execution-affecting changes earlier in the PR, and a
prose-only merge group queued behind code changes still runs full validation.

The `ide` target is currently headless; its historical name does not advertise a
working graphical IDE. See the compiler page for its entry point. Adding another
application does not require making it a compiler feature or linking its modules
into `ide`.

For attribution and third-party terms, see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)
and [LICENSES](LICENSES/). Do not infer a license grant for Buster's own code from
those notices.
