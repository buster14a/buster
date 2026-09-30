# Expression-query memo replay

`replay.c` is a standalone C11 diagnostic, not an alternate compiler IR. It
replays the observed query/publication stream of one scratch population owned
by `c_parse_validate_lowering_constraints`. The parent function allocates one
slot per body token, clears the population, runs semantic validators, and
rewinds its scratch arena. Only `c_parse_expression_type_query` publishes;
that function and the nested expression machine consume. No serialization,
append/reallocation, copied-model publication, or cross-body retention exists
for this population in the inspected production code.

Run the diagnostic on a standard GitHub runner:

```sh
cc -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic -fwrapv -fno-strict-aliasing -funsigned-char replay.c -o replay
./replay --self-test
./replay events.tsv > model.tsv
```

Use sanitizer builds for correctness, not for performance comparisons. This
prototype has no timer. Its allocation/poisoning and seven simultaneous copies
deliberately differ from production; executing it cannot establish compilation
time or peak compiler memory. Do not benchmark the observer or replay as if it
were an implementation candidate.

## Trace contract

Decimal unsigned columns separated by tabs or spaces; blank lines and complete
lines beginning `#` are ignored. The observer must emit only lookups with an
eligible memo slot and successful publications, in their original order:

| Event | Fields after event letter | Meaning |
|---|---|---|
| `B` | `body_token_count body_start scope_count type_count` | Allocate and clear one fresh body population. |
| `Q` | `reader slot end scope flags type_count expected_hit` | Lookup; reader 0 is nested, 1 root; type_count is the live result bound. |
| `W` | `slot end scope type flags` | Successful fact publication, including overwrite. |
| `E` | None | Destroy this body's scratch population. |

End is the absolute exclusive token index. Slot is `start - body_start`.
Flags are `VALID=1 CHECKED=2 RUNTIME=4 CONSTANT=8`; requests/publications must
have VALID set. A checked publication satisfies an unchecked request when the
other mode bits match. An unchecked publication does not satisfy a checked
request. Matching also requires equal end and scope, plus stored type strictly
below the lookup's live type_count. Failed attempts and unpublished successes
produce no `W`. Publications are not inferred from misses.

The parser rejects malformed/cardinality errors, numeric overflow, an active
body at EOF, nested bodies, events outside a body, out-of-range slots/end,
unknown flags, and body_start + body_token_count above UINT32_MAX. Zero-token
bodies are supported by the diagnostic, although production skips them.
Scope/type counts at `B` are descriptive; the result's type table can change
after `B`, so they are not truncation promises or publication bounds.

Every layout must reproduce `expected_hit` exactly; any mismatch fails the
process. That checks this memo's hit predicate and overwrite behavior, not
compiler outputs or correctness of the observer.

## Concrete representations

Let T be body_token_count, P=ceil(T/64), and R(x)=64ceil(x/64), with R(0)=0.
Scope and type remain global u32 IDs in all layouts. Struct sizes are checked
by C11 static assertions.

| Name | Physical data | Logical retained bytes | Initialization |
|---|---|---:|---|
| current16 | `{u32 end,scope,type,flags}[T]` | 16T | Contiguous memset of 16T. |
| flags_first16 | `{u32 flags,end,scope,type}[T]` | 16T | Contiguous memset of 16T; check-order control. |
| hot_cold13 | `u8 flags[T]`, `{u32 end,scope,type}[T]` | 13T | Contiguous memset of flags T only. |
| soa13 | `u8 flags[T]`, three `u32[T]` columns | 13T | Contiguous memset of flags T only. |
| blocked64 | `{u8 flags[64]; payload[64]}[P]` | 832P | Clear all 64 flags in each allocated block, including padding slots. |
| compact16 | `{u32 end,scope,type;u8 flags}[T]` | 16T | Zero only each live strided flag byte. |
| narrow12_wide16 | `{u8 flags,pad;u16 end_delta;u32 scope,type}[T]` | 12T if T<=65535, else 16T | Narrow: zero strided flags; wide: contiguous memset of 16T. |

For narrow, end_delta is `end - (body_start + slot)`. Eligible nonempty ranges
make delta positive and at most T. The entire body uses the wide flags-first
layout for T>65535. It does not narrow scope/type IDs, build translation maps,
truncate, or branch on a per-slot overflow sentinel. Queries reconstruct the
absolute end with one addition to the slot start. A production candidate would
select narrow/wide once at the body boundary; the replay's switch dispatch is
diagnostic machinery.

Flags-first candidates inspect flags before payload. All uncleared payload is
poisoned with 0xa5, and unpublished payload is never read by those candidates.
The current layout retains its production end→scope→flags→type order and its
full clear. The flags_first16 control also retains a full clear so its result
isolates lookup order from initialization. All publications write payload and
then flags. Readers/writers are serial within this body; publishing flags last
does not add a concurrency or atomicity guarantee.

Changing the u32 flags to u8 in an ordinary three-u32 AoS does not reduce its
sizeof because of trailing alignment padding. Narrowing the local end is what
makes narrow12 physically 12 bytes. Full memset of narrow12 instead of its
strided flags would initialize 12T bytes and ceil(12T/64) lines; this is a
separate implementation choice, not what the displayed `init_bytes` models.

## Cost model and interpretation

`storage_sum` is the sum of live population bytes across bodies, not peak
retained process memory. `storage_peak` is its largest per-body value. Bodies
are sequential in the replay. Compiler lanes and nesting may change aggregate
scratch peaks; other scratch allocations, arena capacity/rounding, allocator
metadata, page residency, and retained backing arenas are not counted.

`aligned_storage_*` additionally rounds each independently allocated stream
to a conceptual 64-byte allocation boundary. That model is R(16T) for wide AoS,
R(T)+R(12T) for hot/cold, R(T)+3R(4T) for SoA, 832P for blocked, and R(12T) for
narrow. This exposes small-population overhead. The actual diagnostic malloc
pointers need not be 64-byte aligned; these numbers are model offsets, not
measured address placement.

For every query the model follows the short-circuit predicate exactly. It
counts one logical load per inspected field (one flags load even though the
predicate uses it twice), its width, and whether any payload was probed. Each
publication counts its four stores. Replay-only bookkeeping reads of previous
flags to count overwrites are excluded from publication cost. Padding is not
loaded/stored. Production aggregate assignment could store padding or emit
wider accesses, so these store-byte predictions require compiler disassembly
and measurement before acceptance.

`query_stages` sums flags/end/scope/type predicate stages that execute. It is
a logical short-circuit dependency count, not a CPU critical-path measurement:
fixed addresses allow speculative/parallel loads, and branches alter execution.
`end_checks`, `scope_checks`, `flags_checks`, and `type_checks` identify the
shift of work between gates. Narrow adds absolute-end reconstruction arithmetic,
which is not valued in bytes or stages. All variants retain the same T-slot
direct addressing and successful-fact capacity; no scans or conversion passes
are added to query/publication paths. Initialization is the only full-population
work measured by the replay.

`query_lines` and `write_lines` sum the number of distinct conceptual 64-byte
lines touched within each individual event. The fields in one line coalesce;
different streams never coalesce. Straddling fields are counted on both lines.
Touches between events do not coalesce, so these totals are not distinct lines
over a function, cache misses, line fills, read-for-ownership traffic, or bytes
transferred from any cache/memory level. A warm flags stream may favor split
layouts; a flags load followed by a scattered payload may lose. SoA adds up to
three payload streams on hits. Blocked payload keeps those fields together but
retains padded slots. Narrow payload can straddle a line.

Initialization is counted separately. Current/control/wide fallback touch
ceil(16T/64) lines. Contiguous split flags touch ceil(T/64). Blocked clears P
flag lines. compact16 strided flags touch floor((16(T-1)+12)/64)+1 for T>0.
Narrow strided flags touch floor(12(T-1)/64)+1 for T>0, even though only T bytes
are zeroed; a smaller logical byte count therefore does not promise lower
initialization traffic. Diagnostic poison stores are deliberately excluded.

Storage/init advantages and touch disadvantages must be tested on compilation
workloads using an uninstrumented isolated candidate. Small bodies, mixed flags,
overwrites, misses due to differing end/scope, and interleaved nested/root
readers are retained in the trace instead of replacing them with homogeneous
scans. Replaying a single production event sequence does not model timing-driven
scheduling changes, mutation outside the memo, or allocation/serialization
costs for another population.

## Bounded self-tests

`--self-test` uses 0/1/4/65/65535/65536-token populations, checked supersets,
mixed modes, scope and end overwrites, type-bound rejection, >65535 global IDs,
the UINT32_MAX absolute-end boundary, body-wide overflow fallback, and malformed
events. This validates the diagnostic only. Hosted validation and real-trace
results must be reported separately from these synthetic facts.
