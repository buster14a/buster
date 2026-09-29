# CI LLVM selection

Native CI prefers the newest stable upstream LLVM/Clang over the compiler the
hosted runner image happens to ship. `tools/ci_llvm.py` runs as the
`Install latest stable LLVM` step in the `test` (combination matrix) and
`native` jobs on every Linux and Windows lane, before any compiler step.

- **Selection.** The step lists the `llvm/llvm-project` GitHub releases and
  picks the highest `llvmorg-X.Y.Z` release that is neither a draft nor a
  prerelease and already publishes this lane's archive (`LLVM-*-Linux-X64`,
  `LLVM-*-Linux-ARM64`, `clang+llvm-*-{x86_64,aarch64}-pc-windows-msvc`). A new
  release is therefore adopted automatically once its binaries are uploaded;
  there is no pin to refresh. Releases before 22.1.0 are rejected because
  that is the first stable release carrying the AVX10 host-detection fix
  (issue #1501, `cmake/NativeTargetCompatibility.cmake`).
- **Integrity.** The archive must match the SHA-256 digest and size GitHub
  records for the release asset; nothing is extracted before that check.
- **Footprint.** Only the Clang driver, lld, the LLVM binutils replacements
  (`llvm-ar`, `llvm-ranlib`, `llvm-objdump`, ...), DLLs next to them, and the
  `lib/clang` resource tree (headers and sanitizer runtimes) are written, about
  0.8 GiB instead of the full ~12 GiB distribution. `.tar.zst` is preferred
  when `zstd` is on `PATH`; otherwise `.tar.xz` is decoded in Python.
- **Use.** The bin directory is appended to `GITHUB_PATH` and exported as
  `BUSTER_CI_LLVM_BIN`/`BUSTER_CI_LLVM_VERSION`. Windows steps prepend
  `BUSTER_CI_LLVM_BIN` after entering the Visual Studio developer shell, so
  neither Visual Studio's bundled clang nor the image's
  `C:\Program Files\LLVM` wins; the existing default-target assertion still
  guards the AArch64 runner. The installer fails unless `clang --version`
  reports the selected release.
- **macOS.** The macOS lanes keep Xcode's AppleClang, which is the platform
  compiler the project supports there; upstream LLVM also publishes no
  x86-64 macOS archive.
- **Reproduction.** Set `BUSTER_CI_LLVM_VERSION=X.Y.Z` to install one exact
  release instead of the newest.

Special-purpose consumers keep their own pins: the GPU profiles use the
apt-locked LLVM 18 described in [ci-apt-inputs.md](ci-apt-inputs.md), and the
analyzer, UEFI, and other workflows still use the image compiler.
