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

The independent GNU tools check instruction boundaries and reassembled operands:
`as` and `objdump` from binutils 2.46 or later, and the `objcopy` that extracts
the reassembled bytes (any version), are probed once (2.47 is the qualified
9700X version); a missing or older one prints `skipped: needs as >= 2.46, found
2.42` (or `objdump`, `objcopy`, `not found on PATH` or `--version exited with
status N`), runs the rest of the command, and exits 77 for the unavailable
oracle. The raw `--file` oracle needs only `objdump`. A present pair's wrong listing
still fails. Keep hand-written semantic fixtures and guarded tails:
scalar/vector equality
and re-encoding through the shared catalogue do not constitute an independent
oracle. Benchmarks separate decode from decode plus formatting and include
`--file raw.text` and `--only scalar|compact|avx512|batched`. `batched` is the
default API path; `compact` isolates validation specialization with scalar
matching. `--iterations N` and `--after-scalar MS` distinguish short bursts
after scalar work from sustained runs.
