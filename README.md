# Buster

This is the current compiler, formerly `buster-fast`, with GitHub repository ID
`1421805693`. The maintained previous implementation is
[`buster14a/buster-legacy`](https://github.com/buster14a/buster-legacy), repository
ID `1071732997`. Each project retains its own history, issues and pull requests.

Raw benchmark evidence and measurement history live in
[buster14a/buster-performance](https://github.com/buster14a/buster-performance/tree/main/docs/measurements).
New logs, captures, receipts and measurement harnesses belong there under
`docs/measurements/`; this repository keeps design summaries and evidence links.

A C compiler under development for Linux/x86-64, with its implementation tuned
for AMD Zen 5. The lexer, x86 encoder, lane base layer, ELF object writer and
first static-executable path are implemented; a complete C compilation
pipeline is still being built.

Build with Clang on the intended host:

```sh
./build.sh
./build/compiler
```

The build produces one executable, `build/compiler`, using `-march=native`.
Its stage 1 link prefers an installed Wild, then mold, then the system linker.
Set `BUSTER_LINKER=system|wild|mold|lld|bfd` or an absolute executable path to
choose explicitly. The build installs nothing and propagates linker failures.
See [AGENTS.md](AGENTS.md) for test commands and the shared 9700X host lock,
[the lexer notes](docs/c-lexer.md),
[the encoder design](docs/x86-64-encoder.md), and
[the object writer and linker design](docs/object-writer-and-linker.md).

The [assembly frontend](docs/assembler.md) assembles its documented Intel/AT&T
subset into ELF objects with AVX-512 and scalar paths. Try
`./build/compiler assemble -o /tmp/sum.o examples/assembler/sum.s`;
the assembly notes include a runnable caller and validation commands.

The [x86-64 disassembler](docs/x86-64-disassembler.md) decodes its documented
catalogue subset from raw bytes with AVX-512 and scalar paths. Run
`./build/compiler disassemble input.text` for an Intel-syntax listing.

## License and contributions

Except where component-specific terms apply, original material is licensed
under **Apache-2.0 WITH LLVM-exception**.
[LICENSE](LICENSE) contains the complete text;
[LICENSING.md](LICENSING.md) defines its scope, compiler-output rules and
distribution requirements. Imported and adapted portions retain the terms
listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Commercial use, proprietary products and component reuse are permitted subject
to the applicable licenses. Payment, sponsorship and upstream contributions
are voluntary. Merely compiling independently authored source does not place
that source or its output under the compiler's license. Copying compiler
components or incorporating support code requires checking those components'
terms; the LLVM exception has a specific, limited scope.

Read [CONTRIBUTING.md](CONTRIBUTING.md) before submitting work. Contributors keep
their copyrights and certify their right to contribute using [DCO 1.1](DCO).
