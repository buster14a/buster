# x86-64 forwarding-stub encoding authority (#267)

This is the forwarding-runtime slice of issue #267, not completion of the
whole encoding-authority migration. GOT relaxation, implicit padding and
source-assembler sizing remain separate work. TLS remains the #268 slice.

## Ownership and consumer closure

`link_forwarding_runtime_object` is the shared builder for
`link_elf_libc_runtime_object` and `link_windows_libc_runtime_object`.
The two raw x86 arrays previously encoded XOR ESI/ESI, XOR EDX/EDX and JMP
rel32 independently of the XED-derived metadata encoder. They are removed.

`buster_x86_metadata_emit_forwarding` now owns a prepared ABI recipe. Its
first serial use asks the checked metadata encoder to emit typed 32-bit XOR
operands and a symbolic 32-bit relative JMP. There are no copied opcode
constants, generated form IDs, or independent register/ModRM packing rules.
The symbolic target is essential: numeric zero could allow a short branch.

Preparation validates both XOR lengths, the total and branch lengths, one
PC32 relocation, a four-byte field ending at the branch's end, and addend -4.
It publishes bytes and descriptor only after every check succeeds. Success
and failure are cached, with the readiness flag published last. The public
API rejects null storage, invalid kinds and insufficient byte capacity
without changing either output. The borrowed output regions must not overlap.
The relocation's symbol is deliberately empty; symbol identity remains the
object producer's responsibility.

`buster_x86_metadata_prewarm_all_forms` prepares this recipe before worker
lanes begin. Its existing test-only readiness census counts the new cache.
Ordinary decoder prewarm does not eagerly prepare this optional recipe.

The linker obtains one template and descriptor before allocating its object,
then copies the prepared body per stub. It consumes the descriptor's offset
and addend, retaining its existing object-format relocation kind and symbol
policy. Preparation failure returns an empty `OBJECT_ERROR_INVALID_INPUT`
object. No literal x86 fallback remains in this producer.

| Consumer | Stub size | Relocation offsets for the two stubs | Target policy |
|---|---:|---|---|
| ELF x86-64 | 9 bytes | 5, 14 | Zero RSI/RDX via 32-bit XOR; tail-forward to `__cxa_atexit` / `__cxa_at_quick_exit`. |
| UCRT x86-64 | 5 bytes | 1, 6 | Pass arguments unchanged; tail-forward to `_crt_atexit` / `_crt_at_quick_exit`. |

Both stubs remain weak, globally visible function definitions. The imported
symbols remain undefined global functions. AArch64 output is unchanged: its
12-byte ELF and 4-byte Windows bodies and JUMP26 relocation policy are not
part of the x86 authority migration. Target addresses, object records and
other non-instruction payloads remain linker data.

The canonical-machine authority-site registry is not expanded: this recipe
is a linker-object consumer, not a canonical-machine emission site. The
repository-wide inventory links here rather than treating its narrower
five-file census as universal proof.

## Regression coverage

`x86_64_forwarding_test.c` is registered in the normal CMake and unity-test
lists. It checks both models and invalid enum boundaries, capacities 0-16,
all 64 output alignments within a guarded buffer, null outputs, relocation
fields, preservation of every untouched byte, checked/exact agreement, and
both OS policies on x86-64 and AArch64. Byte expectations are independent
constants backed by the assembler fixture, not encoder-generated expectations.

The Linux x86-64 non-sanitized test also executes both ELF stubs through the
production object JIT. Nonzero high halves in RSI/RDX verify full-register
zeroing, and a returned checksum checks preservation of the first argument
and caller return address. Its sanitizer guard matches the existing native
JIT test policy; byte/object/atomicity checks remain enabled under sanitizers.

`tests/x86_64_forwarding_encoding_oracle.s` uses real instruction mnemonics
and undefined external targets, never `.byte` instruction literals. Assemble
with GNU as and Clang's LLVM integrated assembler:

```sh
as --64 tests/x86_64_forwarding_encoding_oracle.s -o /tmp/forward-gas.o
clang --target=x86_64-linux-gnu -c tests/x86_64_forwarding_encoding_oracle.s -o /tmp/forward-llvm.o
readelf -Wr /tmp/forward-gas.o /tmp/forward-llvm.o
objdump -drw /tmp/forward-gas.o /tmp/forward-llvm.o
```

Both tools emit the expected 18-byte and 10-byte sections, relocation offsets
5/14 and 1/6, and addends -4. Their ELF records use PLT32 for external jumps;
this is not silently equated with Buster's format-neutral PC32 descriptor or
used to change the linker's pre-existing object relocation policy. The
comparison proves instruction bytes, field positions, widths and addends.

## Gates and measured tradeoff

The accompanying performance audit records the exact tested source mixture,
raw samples, first-use cost and cached object-construction cost. It does not
claim end-to-end compiler throughput or a current-main self-host fixed point.
The focused runner passes serially and on four prewarmed lanes, including
ASan/UBSan runs. An isolated three-byte-XOR fault injection rejects the ABI
envelope and confirms unchanged output and no partial object allocation.

The full current-main suite, optimization/register-allocation matrix, native
Windows execution and self-host fixed point still require the normal build
and CI gates. An unavailable or resource-limited gate is not a passing gate.
