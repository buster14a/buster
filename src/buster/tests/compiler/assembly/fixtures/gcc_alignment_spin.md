# GCC loop-alignment fixture (#3740)

`gcc_alignment_spin.s` is the untouched output of GCC 16.2.1 20260810
(x86-64 Linux) for the adjacent first-party C source, copied to `spin.c`
before invocation:

```sh
mkdir -p work/gcc-alignment
cp src/buster/tests/compiler/assembly/fixtures/gcc_alignment_spin.c work/gcc-alignment/spin.c
gcc -O2 -S work/gcc-alignment/spin.c -o work/gcc-alignment/spin.s
cmp work/gcc-alignment/spin.s src/buster/tests/compiler/assembly/fixtures/gcc_alignment_spin.s
as work/gcc-alignment/spin.s -o work/gcc-alignment/spin.o
objdump -d work/gcc-alignment/spin.o
```

GNU as 2.47 places the volatile load/store body at byte 16, the loop test
at byte 20, and RET at byte 26. The regression retains Buster's existing single-byte x86 NOP padding;
its exact text is embedded in the portable test so mobile runners need no
repository filesystem. The test checks the same instruction offsets and branch displacements rather than
requiring GNU's multibyte NOP encodings.

`gcc_pause_alignment_spin.c` and `.s` retain the original issue's pristine
PAUSE specimen. `compiler_driver_test_gcc_pause_listing` assembles it through
the public driver at the baseline target, covering both the maximum-skip
directive and the `rep nop` spelling reported on
[#3732](https://github.com/buster14a/buster/issues/3732#issuecomment-6122548068).
The test embeds the byte-identical listing so mobile runners do not need a
repository checkout at runtime; the adjacent files retain its provenance.
GNU as places PAUSE at byte 16, the volatile load at byte 18 and RET at byte 24;
the regression checks the same instruction bytes, offsets and branch
displacements with Buster's existing single-byte NOP padding. Both listings
are untouched GCC `-O2 -S` output, generated from their adjacent C inputs copied
to `spin.c` before invocation. The PAUSE listing's SHA-256 is
`1bc029f2963b1734f4979f43289daa766954925b67abd5fc1835da95e1d674e5`.

Buster's first-party license remains unselected per
[LICENSES/README.md](../../../../../../LICENSES/README.md).
GCC and GNU binutils are oracle tools, licensed GPL-3.0-or-later as reported
by the installed tools' version notices. No external implementation source or
runtime code was imported; the fixture is generated from the adjacent input.
