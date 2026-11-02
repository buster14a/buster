# x86-disassembler

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## x86-64 disassembler

`src/buster/compiler/x86_64_disassembler.c` decodes bounded raw-byte slices into
semantic records with actual encoded lengths. Its tests and raw-binary command
adapter are included by `main.c`; the benchmark is a separate translation unit
in the same executable. [The disassembler notes](../../docs/x86-64-disassembler.md)
define the supported 64-bit catalogue subset and prefix restrictions.

On the approved host, with the usual per-run lock:

```sh
./build/compiler test-x86-disassembler --objdump
./build/compiler disassemble input.text
./build/compiler benchmark-x86-disassembler
./build/compiler benchmark-x86-disassembler --vector --after-scalar 200
```

The independent GNU tools check instruction boundaries and reassembled operands.
Keep hand-written semantic fixtures and guarded tails: scalar/vector equality
and re-encoding through the shared catalogue do not constitute an independent
oracle. Benchmarks separate decode from decode plus formatting and include
`--file raw.text` and `--only scalar|compact|avx512|batched`. `batched` is the
default API path; `compact` isolates validation specialization with scalar
matching. `--iterations N` and `--after-scalar MS` distinguish short bursts
after scalar work from sustained runs.
