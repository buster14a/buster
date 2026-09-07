# Compatibility harnesses

[Agent instructions](../../AGENTS.md) · Commands run from the repository root.

Read the guide for the harness you are changing. It records required external
inputs, immutable upstream pins, commands, oracles, artifacts, and known gaps.
Do not copy or patch upstream sources into this repository. Reproduce historical
counts and failures before using them as current evidence.

Build the Release `ide` first using the [build guide](build.md). Never run
`generate` while a harness uses the same build directory: it recreates that
directory and removes the compiler and in-progress outputs.

| Harness | Guide |
|---|---|
| cJSON | [cJSON](compatibility/cjson.md) |
| zlib | [zlib](compatibility/zlib.md) |
| Lua | [Lua](compatibility/lua.md) |
| yyjson | [yyjson](compatibility/yyjson.md) |
| stb | [stb](compatibility/stb.md) |
| LZ4 | [LZ4](compatibility/lz4.md) |
| SQLite | [SQLite](compatibility/sqlite.md) |
| sbase | [sbase](compatibility/sbase.md) |
| DoomGeneric | [DoomGeneric](compatibility/doom.md) |
| QuickJS and Test262 | [QuickJS](compatibility/quickjs.md) |
| musl | [musl](compatibility/musl.md) |
| libc-test through the musl harness | [libc-test](compatibility/libc-test.md), after [musl](compatibility/musl.md) |
| CPython | [CPython](compatibility/cpython.md) |

The musl and libc-test guides form one investigation in that order; references
to preceding stages and later tests cross that boundary.
