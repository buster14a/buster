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
| Linux kernel (minimal reference and blocker frontier) | [Linux kernel](compatibility/linux-kernel.md) |
| Git (Linux x86-64 opt-in build census) | [Git](compatibility/git.md) |
| musl | [musl](compatibility/musl.md) |
| musl + BusyBox (Linux diagnostic userspace) | [musl + BusyBox](compatibility/busybox.md) |
| libc-test through the musl harness | [libc-test](compatibility/libc-test.md), after [musl](compatibility/musl.md) |
| CPython | [CPython](compatibility/cpython.md) |
| QEMU (opt-in Linux x86-64 TCG census) | [QEMU](compatibility/qemu.md) |
| RAD Debugger (Linux x86-64 diagnostic campaign) | [RAD Debugger](compatibility/raddebugger.md) |
| FFmpeg (opt-in portable blocker census) | [FFmpeg](compatibility/ffmpeg.md) |
| curl/libcurl (Linux x86-64 blocker census) | [curl/libcurl](compatibility/curl.md) |
| OpenSSL build/test qualification intake | [OpenSSL](compatibility/openssl.md) |
| SDL3 (Linux x86-64 pending qualification intake) | [SDL3](compatibility/sdl3.md) |
| Clang test corpus | [Clang suite](compatibility/clang-suite.md) |

## Compiler selection

Every harness command (`test_cjson` through `test_raddebugger`) resolves the
`ide` it drives once, through `compat_ide_resolve` in `build.c`, and prints a
`COMPAT_IDE` line with the build directory, requested and effective
configuration, selection disposition and path (#1920):

- `--config Debug|Release` selects exactly `<build-directory>/<config>/ide`
  or fails before any external input is compiled; the other configuration
  and the flat tree are never substituted.
- With no `--config`, the harness takes Release, then Debug, then the flat
  `<build-directory>/ide` (reported as `effective_config=unknown`).
  `test_raddebugger` defaults to an explicit Release instead.
- `--build-directory`/`--build-dir` choose the root (default `build`). They
  select only the compiler; harness outputs stay under `build/`.

The path names a file, not a producer: it does not prove which revision or
options built that compiler.

The musl and libc-test guides form one investigation in that order; references
to preceding stages and later tests cross that boundary.

## Ordinary post-merge scheduling

Whole-project integration runs under the single [external-project owner](../external-project-ci.md)
after each main push. Its assignment table records applicable Linux workloads
and pending native macOS/Windows cells. Keep focused compiler regressions in
pre-merge CI; add future pinned project recipes to the shared OS job rather than
creating a project workflow or an arbitrary dispatch/reusable route. Project
qualification and performance evidence contracts remain separate.
