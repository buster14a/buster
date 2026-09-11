<!-- buster-audit-2026-09-07:x86-apx-evex-prefix-fields -->

**Task for an agent.** Read `AGENTS.md` first. Audited on 2026-09-07 against a full clone of `main` at [`d0ecad0d`](https://github.com/buster14a/buster/commit/d0ecad0ddd6ecc676e20fda5694d33176880a90b) ("tests: integrate runtime boundary regressions on desktop hosts"). The tree was built with clang 18 in Debug and Release using the documented hosted-CI bootstrap exception (`clang -Isrc -Wall -Werror ... build.c`), because TCC is unavailable on the audit host. `ide test` was green at 322668/322668 unit tests and 39/39 module tests before auditing, so nothing here is caught by the existing suite. Release self-host was not run: it needs TCC for the trusted bootstrap. **Rebased onto [`44a936a8`](https://github.com/buster14a/buster/commit/44a936a8):** the line numbers below are current as of that commit, and the reproduction was re-run there — the defect still occurs.

**Priority:** P1 - demonstrated emission of invalid machine code.
**Evidence:** reproduced first-hand in this audit against `build/Debug/ide` and `build/Release/ide`, differentially against clang 18.

## What is wrong

The APX EVEX prefix builder pins the mandatory-prefix and NDS-source fields to zero:

```c
p1 |= apx_evex_fixed_width_no_w ? (0x7c | pp) : 0x7c;
```

`0x7c` sets `vvvv = 1111` (register 0) and `pp = 00`. For every APX EVEX form that is not SCC and not ND - and not on the small `apx_evex_fixed_width_no_w` whitelist - both the mandatory prefix and the vvvv-bound source register are discarded. `p2` at `:6991` likewise pins `V' = 1`, and the ND branch at `:6982` omits `| pp` as well.

Because `pp` is what distinguishes several instructions that share an opcode, distinct instructions assemble to identical bytes.

**Source locations**

- src/buster/lib/compiler/assembly/x86_64_metadata.c:6984 (`buster_x86_metadata_emit_form_to_scratch`, entered at :6177)
- src/buster/lib/compiler/assembly/x86_64_metadata.c:6991 (V' field)
- src/buster/lib/compiler/assembly/x86_64_metadata.c:6982 (ND branch)

## Reproduction and measured evidence

```asm
    .text
    .globl g
g:  adcx %rax, %rcx
    ret
    .globl h
h:  adox %rax, %rcx
    ret
```

`ide cc -c -march=diamondrapids`, disassembled with `objdump -d`:

```
clang 18 : 66 48 0f 38 f6 c8    adcx %rax,%rcx
           f3 48 0f 38 f6 c8    adox %rax,%rcx
ide      : 62 f4 fc 08 66 c8    wrssq %rcx,(bad)     <- adcx
           62 f4 fc 08 66 c8    wrssq %rcx,(bad)     <- adox, identical bytes
```

Two different instructions produce the same bytes, and those bytes are not a valid instruction. This needs no extended register - plain `%rax`/`%rcx` on any APX target reaches it.

With an r16-r31 operand forcing EVEX promotion, the same defect collides `shlx`/`sarx`/`shrx` (all three become `bextr`), collides `pdep`/`pext` (both become `bzhi`), and corrupts `andn`, `blsi`, `mulx` and `rorx`. Verified against binutils 2.42 `objdump` and llvm-objdump, which agree.

## Required fix

OR `pp` into `p1` in all three branches, and encode `vvvv` and `V'` from `vvvv_index` whenever the pattern has a vvvv-bound operand - not only when `pattern.has_nd` is set. The `apx_evex_fixed_width_no_w` whitelist then becomes unnecessary for the `pp` half and should be re-examined.

## Validation / definition of done

Extend the x86 encoding differential to cover APX EVEX forms with a mandatory prefix and with a vvvv-bound source: at minimum `adcx`, `adox`, `shlx`, `sarx`, `shrx`, `pdep`, `pext`, `andn`, `blsi`, `mulx`, `rorx`, each with and without extended registers. Compare emitted bytes against clang or gas rather than against the assembler's own decoder.

The existing `X86_SOURCE_REGISTER_CENSUS` cannot see this: it exercises only register-only forms with no mandatory prefix and no vvvv operand.

## Existing work / scope

Section S2 of the 2026-09-07 audit. Directly on the Zen 5 / APX target line. Separate from the census `gap=581` figure, which is unimplemented coverage rather than wrong encoding.
