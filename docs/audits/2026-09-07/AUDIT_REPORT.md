# buster — codebase audit, 6 September 2026

Revision audited: `d0ecad0d` (`tests: integrate runtime boundary regressions on desktop hosts`), cloned from `github.com/buster14a/buster`.

**Rebased 2026-09-07 onto `44a936a8`** (10 commits later). Four cited files had changed; the tree was rebuilt and every runnable reproducer re-run at that revision. All of them still reproduce — none of these defects were fixed by the intervening commits. Line numbers in this report and in `issues/` refer to `44a936a8`.

Baseline: Debug and Release both build clean with clang 18. `ide test` is green — **322668/322668 unit tests, 39/39 module tests**. Every defect below is therefore in territory the existing suite does not reach.

Scope note: the `2026-09-06` bundle already in `docs/audits/` covered the eBPF emitter, Wasm alignment, and runtime file/string boundaries. Those were excluded here.

**23 findings. 17 reproduced first-hand against the built compiler**, marked ✅. The rest come from subsystem passes and are marked accordingly. Ranked by severity.

---

## S1 — Silent miscompilation of valid C

The worst class in a compiler: correct C in, wrong answer out, no diagnostic. All seven were reproduced by differential test against clang 18 on the default target and allocator.

### 1. ✅ Enums are always `int` — every enumerator outside 32-bit range is corrupted

`src/buster/lib/compiler/frontend/c/c_gen.c:42391` (`c_lower_to_ir`), same hardcode at `:24173` (`c_ir_emit_integer_value`).

An enum without a C23 fixed underlying type is unconditionally mapped to `s32_type`, instead of picking the type from the enumerator range.

```c
enum Flags { F_NONE = 0, F_HIGH = 0x80000000 };
enum B { BB = 4294967296LL };
printf("%d %lld %zu\n", F_HIGH > 0, (long long)BB, sizeof(enum B));
```

| | `F_HIGH > 0` | `BB` | `sizeof(enum B)` |
|---|---|---|---|
| clang | 1 | 4294967296 | 8 |
| **ide** | **0** | **0** | **4** |

`BB` collapses to zero. Any enum used as a bit-flag set with bit 31 set — a very common pattern — silently compares as negative.

**Fix:** derive the underlying type from the enumerator range (`unsigned int` / `long` / `unsigned long long`). The rule is already written down for enum bit-fields at `c_gen.c:42751`; reuse it.

### 2. ✅ Signed `__int128` division and remainder are wrong for every negative dividend

`src/buster/lib/compiler/codegen/codegen.c:15088` (`codegen_generate_canonical_module_attempt`).

The `sign_xor` row (`XOR R9, R8`, emitted at `:15133`) overwrites R9 — which holds the **divisor's** sign mask — with the combined result sign, *before* the divisor-magnitude rows at `:15139–15142` read R9. So the divisor is negated iff the two signs differ, rather than iff the divisor is negative.

```c
volatile long long a = -100, b = 7;
__int128 x = a, y = b;
printf("q=%lld r=%lld\n", (long long)(x/y), (long long)(x%y));
```

clang: `q=-14 r=-2` — **ide: `q=0 r=-100`**. Divisor 7 becomes 2^128−7, the restoring loop terminates immediately, quotient is 0 and remainder is the whole dividend.

`tests/basic_c_int128.c:66-70` misses it because its only assertion is `q*37 + r == n`, which `q=0, r=n` satisfies trivially. The AArch64 sibling `codegen_canonical_a64_i128_divide` (`codegen.c:8104`) is correct — it keeps the divisor sign in x16 and the combined sign in x17.

**Fix:** emit `sign_xor`/`save_sign` *after* `divisor_high_sbb`. Pure reordering — R9 then still carries the divisor's own sign mask through the negation. Worth strengthening that test to compare against exact expected values.

### 3. ✅ Every negative floating constant converted to an integer folds to −1

`src/buster/lib/compiler/frontend/c/c_gen.c:39410` (`c_ir_constant_cast`).

`(u64)source.floating` is undefined behaviour for a negative value. Clang lowers it to `vcvttsd2usi`, which returns `0xFFFFFFFFFFFFFFFF`.

```c
static const long long L = (long long)(-2147483648.0);
static const int T[3] = {(int)-1.5, (int)-2.5, (int)2.5};
```

| | `L` | `T` |
|---|---|---|
| clang | −2147483648 | `-1, -2, 2` |
| **ide** | **−1** | **`-1, -1, 2`** |

The same line also drops the high limb for `__int128` targets: `static __int128 F = (__int128)1.5e30;` yields `0x…00000000FFFFFFFFFFFFFFFF`.

**Fix:** convert through `s64` when the value is negative, range-check against the target type, and populate `integer_high` for 128-bit targets.

### 4. ✅ `_Bool` in an aggregate initializer keeps only bit 0 — and Debug rejects valid C outright

`src/buster/lib/compiler/frontend/c/c_gen.c:37855` (`c_ir_constant_initializer_store_integer_leaf`).

The literal fast path masks the value to the member's `bit_width` instead of comparing against zero. `_Bool` has `bit_width == 1`, so every **even** nonzero constant becomes `false`.

```c
struct S { _Bool b; };
struct S s = { 2 };
_Bool a[3] = {2, 256, 1};
```

clang: `b=1 a=1,1,1` — **Release ide: `b=0 a=0,0,1`**.

Two different failure modes from one bug: the Debug build instead **aborts the compiler** on this valid C — `assertion failed at c_gen.c:37995 in c_ir_constant_initializer_check_literal_leaf`. That differential gate is `#if !BUSTER_OPTIMIZE`, so Release is silent and wrong while Debug refuses to compile.

**Fix:** store `value != 0` when `child->kind == IR_TYPE_BOOLEAN` (C 6.3.1.2), or exclude boolean children from `c_ir_constant_initializer_leaf_class`.

### 5. ✅ Unsigned constants ≥ 2^63 initializing a float member become negative

`src/buster/lib/compiler/frontend/c/c_gen.c:37903` (`c_ir_constant_initializer_fold_integer_leaf`).

The INTEGER_TO_FLOAT arm converts through `c_ir_integer_signed_value` — an `s64` — regardless of the literal's signedness.

```c
struct F { double d; };
struct F f = { 18446744073709551615ULL };
```

clang: `1.8446744073709552e+19` — **ide: `-1`**. Same for `float` members and for `9223372036854775808ULL`. Debug aborts at `c_gen.c:37995` instead.

**Fix:** route this arm through `c_ir_constant_integer_to_float`, which already handles unsigned values and rounds at the destination precision.

### 6. ✅ A `char[]` member initialized by a string literal gets one garbage byte, when the initializer also has a nested brace

`src/buster/lib/compiler/frontend/c/c_gen.c:20848` (`c_ir_lower_nested_compound_literal_step`).

The unbraced string literal is brace-elided into element 0 instead of being recognised as a whole-array string initializer.

```c
struct Point { int x, y; };
struct Cfg { struct Point origin; char name[16]; int flags; };
struct Cfg c = { .origin = {3,4}, .name = "widget", .flags = 7 };
```

clang: `name='widget' len=6` — **ide: `name='\xef' len=1`**.

The trigger is narrow but the shape is ordinary: it fires only when the initializer contains a nested brace or designator that routes it to the nested walker. `{ .name = "widget" }` on its own is correct, and the `static` version is correct — which is exactly why the tests miss it. The positional form `{ {1,2}, "cd" }` fails identically.

**Fix:** take the string branch before the aggregate-descent `while` loop — `c_ir_tokens_are_string_literals(...)` + `c_ir_emit_string_range_typed` + `c_ir_emit_store_place` — as the task-level branch at `c_gen.c:20605` already does for a braced string.

### 7. ✅ `__int128` array/struct static initializers drop the high 64 bits

`src/buster/lib/compiler/frontend/c/c_gen.c:35234` and `:35503` (`c_ir_constant_initializer_bytes_legacy_core`).

`c_ir_constant_store_bits(..., bits, sign_extend)` takes a single `u64`, so a 128-bit member is written as its low 64 bits sign-extended and `converted.integer_high` is discarded.

```c
__int128 g4[2] = { ((__int128)1<<100), (__int128)0xffffffffffffffffULL };
```

`.data` via `objdump -s`:

```
clang:  00000000 00000000 00000000 10000000     <- 2^100
        ffffffff ffffffff 00000000 00000000     <- 2^64-1
ide:    00000000 00000000 00000000 00000000     <- 0
        ffffffff ffffffff ffffffff ffffffff     <- -1
```

Both elements wrong: one loses its value entirely, the other is sign-extended into a different number. The scalar form `__int128 g1 = ((__int128)1<<100);` is correct — it takes the `IR_GLOBAL_INITIALIZER_BYTES` path — so only aggregate members are affected.

**Fix:** give the aggregate leaf writer a 128-bit path that stores `converted.integer` and `converted.integer_high` when `type->layout.size == 16`.

---

## S2 — The assembler emits wrong machine code

Verified by assembling with `ide cc -c` and disassembling the output with `objdump`. These are on APX / AVX-512 paths — directly on the Zen 5 target line.

### 8. ✅ APX EVEX hardcodes `pp = 0` and `vvvv = 0` — instructions collide and decode as invalid

`src/buster/lib/compiler/assembly/x86_64_metadata.c:6984` (in `buster_x86_metadata_emit_form_to_scratch`).

`p1 |= apx_evex_fixed_width_no_w ? (0x7c | pp) : 0x7c;` pins `vvvv=1111` and `pp=00` for every APX EVEX form that is not SCC and not ND, discarding both the mandatory prefix and the NDS source register. `p2` at `:6991` likewise pins `V'=1`, and the ND branch at `:6982` also omits `| pp`.

```asm
g: adcx %rax, %rcx
h: adox %rax, %rcx
```

```
clang:  66 48 0f 38 f6 c8    adcx %rax,%rcx
        f3 48 0f 38 f6 c8    adox %rax,%rcx
ide:    62 f4 fc 08 66 c8    wrssq %rcx,(bad)     <- both
        62 f4 fc 08 66 c8    wrssq %rcx,(bad)     <- identical bytes
```

`adcx` and `adox` assemble to *the same bytes*, and those bytes are not a valid instruction. This needs no extended register — plain `%rax`/`%rcx` on any APX target. The same defect collides `shlx`/`sarx`/`shrx`, `pdep`/`pext`, and corrupts `andn`, `blsi`, `mulx`, `rorx` whenever an r16–r31 operand forces EVEX promotion.

**Fix:** OR `pp` into `p1` in all three branches, and encode `vvvv`/`V'` from `vvvv_index` whenever the pattern has a vvvv-bound operand — not only when `pattern.has_nd`.

### 9. ✅ EVEX broadcast operands use the wrong disp8 scale — wrong memory address

`src/buster/lib/compiler/assembly/x86_64_metadata.c:5860` (`buster_x86_metadata_emit_tuple_scale`, used at `:6021`/`:6026`).

The compressed-disp8 scale N is derived from `form->tuple_kind` and vector length only; the broadcast state (`query.attributes.broadcast_elements`) is never passed into `buster_x86_metadata_emit_address`. A Full-tuple broadcast therefore scales by 64/32/16 instead of the element size 4/8.

```asm
vaddps 64(%rax){1to16}, %zmm1, %zmm2
```

```
clang:  62 f1 74 58 58 50 10    vaddps 0x40(%rax){1to16},%zmm1,%zmm2
ide:    62 f1 74 58 58 50 01    vaddps 0x4(%rax){1to16},%zmm1,%zmm2
```

The load comes from the wrong address — off by a factor of 16 here. Any displacement that is a nonzero multiple of the vector byte width is affected; non-multiples fall back to disp32 and are correct, which is why the tests pass.

**Fix:** pass the broadcast flag into `buster_x86_metadata_emit_address` and return the element size from `emit_tuple_scale` when EVEX.b is set.

### 10. ✅ REX2 forms lose `W` when the width comes only from the memory operand — 64-bit ops become 32-bit

`src/buster/lib/compiler/assembly/x86_64_metadata.c:6729-6730` (`scalar_memory_width_rex_w`).

The rule "a folded memory width is a REX.W authority" is gated on `PREFIX_LEGACY || PREFIX_REX` and omits `PREFIX_REX2` — which is exactly what an r16–r31 base or index forces.

```asm
addq $1, (%r16)
```

```
clang:  d5 18 83 00 01    addq $0x1,(%r16)
ide:    d5 10 83 00 01    addl $0x1,(%r16)
```

A 4-byte read-modify-write where an 8-byte one was written; the upper half of the target is left stale. The same applies to `adcq sbbq subq andq orq xorq cmpq testq incq decq negq notq mulq divq idivq shlq shrq sarq rolq rorq rclq rcrq btq btsq btrq btcq` with any EGPR memory operand, and to `lock addq`. Two ad-hoc patches already exist for exactly IMUL `F7 /5` and MOV at `:6708-6716`, which is why only those two are right.

**Fix:** add `|| form.prefix_kind == BUSTER_X86_METADATA_PREFIX_REX2` to `scalar_memory_width_rex_w`, and drop the two per-iclass workarounds.

### 11. AT&T `s` suffix treated as a generic 32-bit size suffix — `bts` assembles as `bt`

`src/buster/lib/compiler/assembly/assembly.c:11154-11158` (`assembly_x86_metadata_suffix_alias`). *Subsystem pass; bytes disassembled there, not re-run by me.*

`suffix == 's' ? 32` maps any mnemonic ending in `s` to base+32-bit. When the exact `bts` form is rejected (memory operand of unspecified width), the fallback strips the `s`, resolves `bt`, and emits BT's `/4` extension instead of BTS's `/5`:

```
ide:    d5 90 ba 27 03    btl  $0x3,(%r23)     <- reads the bit, never sets it
gas:    d5 90 ba 2f 03    btsl $0x3,(%r23)
```

The alias also makes the assembler accept non-instructions as 32-bit ops (`adds`, `cmps`, `movs` with immediate operands), which gas rejects.

**Fix:** gate the `s`/`t` suffix widths on the x87 alias table rather than applying them to every mnemonic.

### 12. x86-64 rejects `__int128` ↔ float casts and 16-byte `_Atomic` structs

`codegen.c:11604` and `codegen.c:12764`. *Subsystem pass.*

`double d = (double)someI128;` fails with `cc: error: C code generation failed with error 2 … opcode 27` — valid C rejected outright. AArch64 has `codegen_canonical_a64_i128_to_float` / `..._float_to_i128` (`codegen.c:8202`/`8381`); x86-64 has no counterpart. Separately, a 16-byte `_Atomic` *struct* is rejected by the CMPXCHG16B path's `IR_TYPE_INTEGER` test even though the identical-layout `_Atomic unsigned __int128` compiles and runs.

These are hard errors rather than silent wrong answers, which is why they rank below the S1 set.

---

## S3 — Memory safety and robustness in the compiler

### 13. ✅ Systemic: `BUSTER_CHECK` is `__builtin_unreachable()` in Release, so validation guards are not guards

`src/buster/lib/os.h:288-292`:

```c
#if BUSTER_OPTIMIZE
#define BUSTER_CHECK(ok) ((void)(BUSTER_UNLIKELY(!(ok)) ? (BUSTER_UNREACHABLE(), 0) : 0))
#else
#define BUSTER_CHECK(ok) ((void)(BUSTER_UNLIKELY(!(ok)) ? (os_fail_message(S8("assertion failed")), 0) : 0))
#endif
```

That is the correct semantic for an *invariant* the code has already established. It is the wrong one wherever `BUSTER_CHECK` is currently doing duty as a **bounds or error check on data the program does not control** — there, the shipping build does not merely skip the check, it grants the optimizer permission to assume the bad case cannot happen and delete the surrounding handling.

This is the common root of findings 14, 16 and 17 below, and of the capacity pre-checks in the object readers. It is worth a deliberate sweep separating the two uses — an invariant assertion from a validating check — rather than fixing each site as it is found.

### 14. ✅ 18 bytes of malformed C hang the compiler forever

`src/buster/lib/compiler/frontend/c/c_parse.c:4967`, with the pop at `:4884-4890` (`c_parse_infer_initializer_array_count_core`).

```c
struct i e[]={>};
```

Both Debug and Release spin indefinitely (killed at 20s and 120s). The brace-elision branch advances the parent to `frame->cursor = value_end`, then pushes a `borrowed` child at `.cursor = designator.value_start`; when the child is immediately popped as exhausted, the pop copies the child's **unadvanced** cursor back into the parent, undoing the advance. `struct i` is incomplete, so `c_parse_initializer_type_slots` returns 0, the child pops on the first iteration, and the loop oscillates between frame counts 1 and 2 with the cursor pinned at 7. Same for `struct s e[]={()};` and `struct i e[]={<};`.

A build that touches an unlucky generated file never terminates.

**Fix:** on popping a `borrowed` frame, propagate its cursor only when it advanced (`if (cursor > frames[frame_count-1].cursor)`); or refuse to push a borrowed frame whose type has zero slots.

### 15. ✅ Out-of-bounds arena write from repeated typedef-name statements

`src/buster/lib/compiler/frontend/c/c_parse.c:11722` (`c_parse_bind_identifier_entity`).

A typedef name at statement start is bound twice — once as a declaration-specifier at `:13996`, again as a plain identifier use at `:14058` when the declaration parse falls through — but `identifier_use_capacity` is sized `identifier_count + 1` at `:15420`.

```c
typedef int B; void n(void){ B{}B{}B{}B{}B{}B{}B{}B{} }
```

Debug: `assertion failed at c_parse.c:11720`. Release: that check is `__builtin_unreachable()` (finding 13), so the write proceeds past the allocation, scaling linearly with input — an instrumented 20000-repeat run recorded ~240 KB written past the end, straight into the immediately following bump allocations `identifier_use_by_token`, `token_classes` and `diagnostics` (`:15480-15487`). Corrupted `token_classes` bytes are read back by `c_parse_token_class`.

The input is invalid C (clang rejects it too), so this is a malformed-input path, not a valid-program path — but it is a genuine unchecked write in the shipping configuration, and the corruption is silent.

**Fix:** skip the second bind when `declaration_type_start == index` and the specifier bind already ran.

### 16. ✅ `#import` writes into `once_paths` with no capacity check

`src/buster/lib/compiler/frontend/c/c_source.c:7898` (`c_preprocess`).

```c
once_paths[once_path_count++] = include_path;   // no bound
```

The sibling `#pragma once` writer at `:5868` does check (`*context.once_path_count < context.once_path_capacity`); this one does not. The array is sized `source.length + 1` from the **root** file only (`:7512`), so a small root that imports a header containing many distinct `#import` lines overruns it — an instrumented run with a 12-byte root and 20000 imports recorded ~320 KB of `String8{pointer,length}` pairs written past the end. No diagnostic, no crash.

**Fix:** guard the write exactly as line 5868 does, and size the array from the include closure rather than the root length.

### 17. ✅ A failed `write(2)` is counted as 18446744073709551615 bytes written

`src/buster/lib/os.c:1280` (`os_file_write_partially`), loop at `:1293` (`os_file_write`).

```c
BUSTER_CHECK(result > 0);
return (u64)result;
```

In Release that check is `__builtin_unreachable()`, so `write()` returning −1 is returned as `UINT64_MAX` and added straight into the progress counter, which wraps. `os_file_write` returns `void`; `file_write()` still reports success and the process exits 0.

Two demonstrated shapes: a transient failure after a partial write drops the remainder silently (200000-byte write on a pipe → 195904 bytes lost, exit status 0); a partial write followed by one failure resumes at the wrong offset and duplicates a byte (256-byte buffer → 257 bytes on disk). This is the only write path for object files, executables and archives.

**Fix:** give `os_file_write_partially` a failure-flagged result and `os_file_write` a `bool` return that `file_write`/`file_copy` check.

### 18. Arena: commit failure returns a pointer into uncommitted memory; non-granular reservations abort

`src/buster/lib/arena.c:44-49` (`arena_allocate_commit`). *Subsystem pass, probes run there.*

Two distinct problems in one function. First, `os_commit`'s return value only decides whether to advance `os_position`; the failure is otherwise ignored, and the inline bump then publishes `position` and returns a non-null pointer into memory the OS refused to commit — first write segfaults. Second, the commit target is rounded up to `granularity` and then bounded against `reserved_size`, so an allocation ending exactly at a non-granular `reserved_size` fails a bound the inline bump never applied — Debug aborts, Release commits past the reservation. `arena.c:36-40` explicitly documents non-granular reservations as supported.

**Fix:** report commit failure through `os_fail` (the file's own "allocation must not return null" contract), and clamp the commit target with `BUSTER_MIN(align_forward(...), reserved_size)`.

### 19. `file_read` returns empty for any source that does not report a size

`src/buster/lib/file.c:229-241`. *Subsystem pass.* The buffer is sized from `fstat` and the read is skipped entirely when `st_size == 0`, so a FIFO, `/proc` file, character device or `ide cc <(...)` process substitution compiles as an **empty translation unit and succeeds**. Relatedly, `os_file_read_partially` (`os.c:1330-1349`) converts a read error to `0`, which the caller cannot distinguish from EOF — a mid-file `EIO` yields a silently truncated source or object.

**Fix:** fall back to a grow-and-read loop when the reported size is 0; distinguish error from EOF in the read path.

---

## S4 — Debug information and driver output

### 20. ✅ DWARF range lists double-count the text base — no function is findable by address

`src/buster/lib/compiler/dwarf/dwarf.c:1060` (`dwarf_model_emit_ranges`), with `:1801-1806` (`dwarf_build_model`).

Range-list entries are emitted with `.address = true` relocations (absolute addresses) while the CU's `DW_AT_low_pc` is *also* an absolute relocated address. Per DWARF-4 §2.17.3 that low_pc is the base every `DW_AT_ranges` entry is added to, so consumers add the text base twice.

```
$ ide cc -g -c dw.c -o dw.o && ld -o dw_gnu -e main dw.o
$ llvm-dwarfdump --verify dw_gnu
error: DIE address ranges are not contained in its parent's ranges:
  DW_TAG_compile_unit  DW_AT_low_pc (0x0000000000401000)
  DW_TAG_subprogram "main"  DW_AT_ranges ([0x0000000000802030, 0x0000000000802078))
Errors detected.
```

`main`'s code is at 0x401030; its DIE claims 0x802030. Every subprogram, lexical block and inline site is unreachable by address in gdb and lldb. A single unlinked `.o` verifies clean, which is why this survived. `.debug_loc` lists at `:1143-1156` have the identical defect.

**Fix:** emit the CU's `DW_AT_low_pc` as a literal 0 with no address relocation (LLVM's convention when children use `DW_AT_ranges`), or keep the range entries CU-relative.

### 21. CodeView: `LF_FIELDLIST` length overflows `u16` at ~2500 members

`src/buster/lib/compiler/codeview/codeview.c:238` (`codeview_type_record_end`). *Subsystem pass.* The record length is `(u16)(count - offset - 2)` with no overflow check, and `LF_FIELDLIST` (`:541-565`) accumulates every member into one record with no `LF_INDEX` continuation split. A struct with 2500 `int` members produces a ~135 KB payload whose length field reads 3930 (135010 mod 65536); `llvm-readobj --codeview-merged-types` reports "The CodeView record is corrupted" and every subsequent type record is parsed out of member-name text. 2400 members is fine.

**Fix:** split field lists at ~64 KB into continuation records chained with `LF_INDEX`; fail the build rather than truncate if a single record still exceeds `UINT16_MAX`.

### 22. CodeView: `S_GPROC32` `pEnd` offsets are wrong after the PDB split, and the scope stack is quadratic

`codeview.c:734` and `codeview.c:369` (`codeview_build_legacy`). *Subsystem pass.*

`pEnd` is patched with an offset into the whole `.debug$S` blob, but `pdb_split_codeview` (`pdb.c:252-256`) puts only the concatenated `DEBUG_S_SYMBOLS` payloads into the module stream — so every `pEnd` is too large by the stripped signature and subsection headers, and a DIA/MSVC reader following it walks into the C13 line-table region. Separately, `codeview_emit_scope_tree` allocates its frame stack `arena_allocate(..., scope_count + 1)` on **every call**, once per function, giving O(function_count × scope_count) of never-released arena; the DWARF path allocates once at `dwarf.c:1531`.

**Fix:** patch `pEnd`/`pParent` with the running module-symbol offset; hoist the frame stack to one allocation per `codeview_build_legacy`.

### 23. ✅ `-c` without `-o` writes the object next to the source, not into the working directory

`src/buster/lib/compiler/driver/driver.c:3401` (`compiler_driver_default_object_path`), duplicated at `:3765`.

The default object path replaces the extension of the *full input path* instead of taking its basename:

```
$ cd outdir && ide cc -c ../srcdir/a1.c
   srcdir/a1.o exists? YES     outdir/a1.o exists? no
```

gcc and clang both write `./a1.o`. Out-of-tree builds deposit objects into the source tree, two configurations of the same tree overwrite each other's objects, and compiling from a read-only checkout fails outright.

**Fix:** strip the directory prefix as well as the extension.

---

## Areas examined and found clean

Worth recording, since it is a real result and tells you where not to spend effort next:

- **`object.c` / `link.c` / `jit.c` — genuinely hardened.** Every on-disk count, offset and size in the ELF, COFF, Mach-O and archive readers is validated against `bytes.length` before use; string-table reads are bounds-scanned; relocation values are range-checked before truncation (PC32/PLT32/GOTPCREL against `INT32` bounds, AArch64 CALL26/JUMP26/ADRP through validity-flagged helpers); addend arithmetic uses overflow-safe helpers; REL vs RELA is handled correctly; the JIT enforces W^X. ~2900 mutated objects and archives plus ~4000 crafted malformed archives across all six targets produced zero crashes and zero ASan reports. This is the strongest code in the repo.
- **The AVX-512 compaction lexer.** 1.2M randomized inputs through a `c_lex` vs `c_lex_reference` differential harness — tokens, shapes, metrics, checkpoints, diagnostics — produced zero mismatches, and the tail/lookahead bounds hold by hand-check.
- **Register allocation under pressure.** ~800 generated programs across three generators × four allocators, plus hand-written swap/parallel-copy, 40-live-value and deep-expression cases: clean. Also clean: 64-byte ZMM values live across calls, SysV struct/float/mixed argument and return ABI, varargs including aggregates and `va_copy`, over-aligned locals, VLAs, >300 KB frames with page probing, computed goto, TLS, `-fPIC`, long double.
- **AArch64 encoding.** 3230 instructions checked against clang, zero mismatches; branch and PC-relative range checks are properly bounded.
- **Bitfields and layout.** Packing and access including 32/64-bit-wide and packed fields, struct/union/over-aligned/FAM/VLA layout and `offsetof`, integer promotions and usual arithmetic conversions, runtime float↔int conversions, float constant folding: all clean.
- **`string.c`, `integer.c`, `hash.c`, `simd.h` fallbacks** under ASan+UBSan with adversarial inputs: clean.

## Two structural observations

**The Debug/Release divergence is a liability.** Findings 4 and 5 abort the Debug compiler on valid C while Release silently miscompiles it; findings 15, 17 and 18 are contained in Debug and are memory-unsafe in Release. The `#if !BUSTER_OPTIMIZE` differential gates are doing real work — they caught these — but they gate *aborts*, not *corrections*, so the configuration you ship is the one with no defence. Finding 13 is the root.

**Self-hosting cannot catch any of the S1 defects.** `test_self_host` requires stage-1 and stage-2 to be byte-identical, and a compiler that miscompiles enums identically in both generations still reaches a fixed point. Byte-identity proves determinism, not correctness. Every S1 finding above passes self-host by construction — differential testing against a reference compiler is the gate that would have caught them, and there is currently no such gate in CI.

---

*Reproducers for every ✅ finding are one-file C or `.s` inputs quoted inline above; each was run against `build/Debug/ide` and `build/Release/ide` at `d0ecad0d` and compared with clang 18.*
