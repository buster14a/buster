# Clang 23.1.1 used for the cloud refinement

The cloud refinement uses the official LLVM **23.1.1** Linux X64 release,
matching the release number printed by the existing 9700X CI runner. The
runner's log does not include its compiler hash or source revision, so this
does not establish that the two compiler binaries are identical.

## Official archive

- [Release page](https://github.com/llvm/llvm-project/releases/tag/llvmorg-23.1.1)
- [Linux X64 archive](https://github.com/llvm/llvm-project/releases/download/llvmorg-23.1.1/LLVM-23.1.1-Linux-X64.tar.zst)
- [Official release metadata](https://api.github.com/repos/llvm/llvm-project/releases/tags/llvmorg-23.1.1)
- Archive SHA256, verified against the release metadata:
  `b7ddbabd70fa1d206948bc83f59e59aa84eaf4cd09b6b89cc3ece28177710a6f`
- Local archive: `/tmp/buster-LLVM-23.1.1-Linux-X64.tar.zst`.

The archive was extracted into `/tmp/buster-clang23.1.1` under the shared
`/tmp/buster-simd-305.lock`. No system package or repository source was
changed by installation.

## Compiler identity

Executable: `/tmp/buster-clang23.1.1/bin/clang`.

```text
clang version 23.1.1 (https://github.com/llvm/llvm-project 6dfe1677ab8dffbc6ec13d53a1e0215d75147689)
Target: x86_64-unknown-linux-gnu
Thread model: posix
InstalledDir: /tmp/buster-clang23.1.1/bin
```

Compiler binary SHA256:
`b2d843a7b195b6988eeb6a344725f21ac2e5bec80234ee96f12a4729e63efbda`.

The same installation provides `llvm-objdump` and `llvm-mca`. Official
apt.llvm.org packages for Noble already supplied 23.1.4 when this toolchain
was provisioned; the pinned release archive provides the requested 23.1.1.

## Scheduling-model scope

LLVM 23.1.1's
[X86 target definition](https://github.com/llvm/llvm-project/blob/llvmorg-23.1.1/llvm/lib/Target/X86/X86.td#L2224-L2225)
maps `znver5` to **`Znver4Model`**, with Zen 5 instruction features and tuning
flags. The installed `llvm-mca -mcpu=znver5` likewise reports `Zn4*` resources.
Its resource model is therefore not a faithful full-width Zen 5 model; this
work makes no Zen 5 throughput claim from `llvm-mca` predictions. Compiler
assembly inspection and measured target execution are separate evidence.
