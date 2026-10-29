# GNU C extensions

This is the implementation record for [#147](https://github.com/buster14a/buster/issues/147).
The language policy is [#154](https://github.com/buster14a/buster/issues/154#issuecomment-6289359306):
follow Clang, with GNU17 as the default and the selected standard's keyword rules.

## Current boundary

The parser accepts GNU syntax, records its tree, and can now supply structured
GNU attribute and asm-label metadata. `type_machine_mode` supplies the missing
machine-mode type action beside the existing layout and vector actions.
**These are frontend building blocks, not completed source-language semantics.**
The normal driver on this revision preprocesses and parses; it does not type
check or generate an object from C. Issue #147 must stay open until the source
checker and lowering actually consume these constructs and its corpus/runtime
acceptance conditions pass.

| Construct | Implemented here or already available | Remaining source integration |
| --- | --- | --- |
| GNU attribute lists | Generic names and argument trees; structured bounded records, including unknown names and empty lists | Attribute-specific argument checking and application to declarations/types |
| Statement and label attributes | Clang's grammar at block boundaries, after control heads and after labels | Applicability, warnings and effects |
| Asm labels | Separate records for declarator labels, retaining adjacent string tokens and source identity | Literal decoding, redeclaration consistency and actual linker-symbol renaming |
| `mode` | `type_machine_mode` transforms supported target types with Clang-checked results | Attribute argument resolution and declaration elaboration |
| `packed`, `aligned`, `vector_size` | Existing type/layout actions and scalar/vector layout | Applying parsed attributes to those actions |
| `typeof`, statement expressions, GNU `?:`, range designators | Existing parser grammar, with additional regression fixtures | Types, value categories, evaluation order and lowering |
| Alternate keywords, `__int128`, zero-length arrays, local labels | Existing keyword/parser/type support as applicable | The remaining checker and control-flow rules |
| `__extension__` | Parser accepts expression/declaration forms | Pedantic-diagnostic suppression through semantic checking |
| `transparent_union` | Checked as clang checks it, a conversion to the union selecting its member as clang's does; its arguments, parameters and `va_arg` pass as its first member, as clang's code passes them ([the checker guide](c-checker.md)'s "Transparent unions (#147)") | A union sized or aligned otherwise than its first member, or made transparent after a function definition where its first member passes otherwise, is refused by name |
| `gnu_inline`, `cleanup`, `returns_twice` | Parsed and retained | Checking, ABI or control-flow/emission effects |
| `always_inline`, `noinline`, `used`, `may_alias` | Parsed and retained | Propagation to the actual backend/optimizer |
| `weak`, `alias`, `visibility`, constructors/destructors | Parsed and retained; link representation has relevant primitives | Source-to-symbol/atom creation, init/fini entries and execution tests |
| `section` | Parsed and retained | Custom named-section representation and source emission |
| Inline assembly statements | Existing parser grammar, kept distinct from asm labels; the checker's operands; constraints, operand substitution, clobbers and assembly into per-unit helpers ([the inline assembly guide](c-inline-assembly.md)) | `asm goto`, naked functions, and templates that refer to symbols |

The in-progress checker in #133 and declaration processing in #129 must own
their semantic decisions. This change neither duplicates those implementations
nor advertises support through `__has_attribute` based on syntax acceptance.

## Parser corrections

A GNU attribute after a named label does not require a null statement before
the label's statement:

```c
void f(void)
{
    again: __attribute__((unused)) return;
}
```

Attributes after a control head similarly precede its statement; they do not
make a declaration legal where a statement is required. At an ordinary block
boundary, Clang distinguishes a leading attribute list that consists entirely
of statement attributes from one that starts a declaration. The parser now
models that distinction, including paired-underscore attribute spellings and
the different treatment of `__extension__` after attributes. Attribute
applicability remains semantic work: parsing an unknown or misplaced attribute
does not promise its effect.

The scalar state machine handles these irregular prefixes. The existing vector
validator sends their declarations to that exact path, and the parser tests
compare the full trees and diagnostics. The token-kind conversion and vector
grammar tables retain their interfaces. A cold reference to the intern table
lets the scalar state machine classify a prefix attribute without adding a
lookup to ordinary tokens.

## Attribute and asm-label handoff

`ParseOptions.extension_metadata` requests `Parser.gnu`, a `GnuSyntax` value
built by `c_gnu.c`. It can be combined with `tree_only`; it does not require a
postorder. Ordinary syntax-only callers leave the option false and allocate no
metadata arrays. `parse_release` releases their arena. A new parse invalidates
the previous metadata, including when the new input is rejected.

The producer is `parse_translation_unit`; the immediate consumer is
`gnu_syntax_build`. The eventual semantic consumer is #133's declaration/type
adapter. It receives:

- **Groups**, in source order, with the attribute keyword, closing token, tree
  attachment parent/role and a contiguous range of attributes.
- **Attributes**, in source order within each group, with the name token and
  half-open argument token range `[begin, end)`. The arguments exclude their parentheses. A bare name
  uses `BUSTER_PARSE_NONE` for both bounds; `name()` has equal, valid bounds.
  Nested groups have their own ranges, so the complete attribute array is
  grouped by list, not globally sorted by name-token position.
- **Asm labels**, in source order, with the keyword, syntactic attachment and
  the nonempty half-open range `[begin, end)` of adjacent string tokens. Assembly statements and
  file-scope assembly blocks never enter this array.

The attachment is a syntax-tree relationship, **not a resolved declaration
identity**. Specifier attributes can affect several declarators; nested type
attributes have their own owner. Consumers must use the declarator/type tree
and existing source-order rules. `tokens.names[name]` gives the original
`IdentId`; `tokens.sources[token]` reaches the preprocessor's spelling and
location. No string is copied or decoded by this stage. C23 `[[...]]` lists
remain in the original tree and are not reported as GNU `__attribute__` lists.

### Batching, memory and failure

Candidate discovery compares 64 one-byte token kinds at once with AVX-512;
only set bits enter the bounded list reader. The scalar twin discovers those
same candidates. The reader follows the parser's opener-to-matching-closer
relationship, so it does not rescan nested argument expressions or recurse.
It preserves names whose meaning is unknown and distinguishes explicitly empty
argument lists. Semantic evaluation must follow the original syntax/declaration
dependencies, not the order of the grouped metadata rows.

`gnu_syntax_collect` takes immutable, successful parser output and caller-owned,
disjoint, naturally aligned output arrays. Counts and capacities are separate;
there is no readable input padding or writable output padding requirement.
The final token-kind load is masked to the logical token count. The parser's
token and intern tables must outlive consumers of the records. Resolving source
spellings or locations also requires the originating preprocessor output and
source storage to remain alive and unchanged.

The measurement mode counts exact rows without writing output. `gnu_syntax_build`
uses that count to allocate exact arrays in an arena, then collects them. The
parser reserves a conservative virtual bound but commits only the measured
array bytes. Empty input creates no rows. Failed collection resets every
output count; partially written rows are invalid. Failed build rolls the
caller's arena back to its incoming mark and clears the output. The structural
checks protect every slice the reader follows; they are not a replacement for
validating an arbitrary foreign syntax tree.

No generic work scheduler or repeated spelling classification is introduced.
The records preserve group boundaries and exact exceptional work for a future
batched semantic consumer. No semantic consumer exists on this main revision,
so the benchmark does not claim end-to-end checking speedup.

## Machine-mode type action

`type_machine_mode(table, base, mode)` takes a bounded spelling and returns a
`TypeId`, or zero with `table->last_error`. Plain and paired-underscore names
are accepted. The original implementation is checked against Clang 23.1.1.

Supported scalar families include integer `QI`, `HI`, `SI`, `DI`, `TI`, `byte`,
`word`, `pointer` and `unwind_word`; real `HF`, `SF`, `DF`, `XF`, `TF`, `KF`; and
representable complex floating modes. Compatible ordinary GNU vector modes
and changes to existing vector elements use the existing vector constructor.
Signedness follows the original integer type. Enum typedef transformation
does not change the original enum tag.

This is Clang's declaration-type replacement operation: it strips existing
qualifiers and aligned typedef wrappers. The declaration checker must apply
qualifiers that occur later in elaboration. Treating this as a generic
qualifier-preserving derivation would give the wrong result. Applying a mode
directly to an enum tag belongs to enum completion in the checker.

Unknown names, incompatible categories, unsupported target representations
and oversized vector counts are distinguished. Complex `__fp16` and vectors
with complex or zero-size element counts are explicitly refused where the
current type table cannot represent a Clang result. Numeric overflow is
refused rather than reproducing Clang's wrapped vector-count corner case.
The scalar lookup is a cold declaration action; it does not establish a new
vector speedup claim.

## Verification and measurement

The existing executable exposes:

```sh
./build/compiler test-parser --clang
./build/compiler test-gnu --clang
./build/compiler test-types
./build.sh check-call-graph
./build/compiler benchmark-gnu [--only collect-scalar|collect-vector|build-scalar|build-vector|parser-only|parser-with-gnu] [--after-scalar MS] [-E's options] file.c
```

`test-parser` covers the new grammar and keyword regressions with Clang's
acceptance/rejection, scalar/vector trees and diagnostics, all 64 token-block
positions, generated inputs and deep nesting. `test-gnu` checks exact metadata
and source ranges, nested/empty/unknown attributes, asm labels versus assembly
statements, both producer/collector paths, guarded tails, capacity failures
and arena rollback. `test-types` includes independent Clang checks for machine
mode results, canonical type spellings and surrounding record layouts, along
with invalid modes and names ending at protected pages.

`benchmark-gnu` preprocesses the requested real input once, then checks the
metadata and runs 21 rotating measurement passes. It reports:

- scalar/vector collection, including candidate discovery and group decoding;
- scalar/vector build, including measurement and exact array allocation;
- the parser pipeline with and without metadata, including token conversion,
  parsing and postorder;
- time per input token, useful token throughput, retained metadata bytes and
  the parser's total fallback token share. CPU cycles are explicitly marked
  unavailable if the hardware counter cannot be read.

Preprocessing is outside the measured region. Individual semantic effects,
object symbols, runtime results and complete glibc/SQLite type checking remain
unmeasured until the missing consumers exist. Use the protected 9700X workflow
or its shared lock for target measurements, pinned to CPU 2; use separate
processes and `--after-scalar 100` to compare sustained and conditioned runs.
Conditioning uses dependent integer work between clock checks, since the
compiler can hoist vector constants into the generic collector's scalar path.
Cloud timings validate the harness and do not establish Zen 5 throughput.

## Sources and provenance

Implementation, fixtures and benchmark helpers are original project work.
Clang runs externally as an oracle; no compiler implementation or external
fixture corpus is imported. Behavioral rules were checked in the
`llvmorg-23.1.1` versions of
[`ParseStmt.cpp`](https://github.com/llvm/llvm-project/blob/llvmorg-23.1.1/clang/lib/Parse/ParseStmt.cpp)
and [`SemaDeclAttr.cpp`](https://github.com/llvm/llvm-project/blob/llvmorg-23.1.1/clang/lib/Sema/SemaDeclAttr.cpp).
The installed Clang 23.1.1 `AttrParsedAttrImpl.inc` supplied the statement
attribute spelling facts. Existing parser/type interfaces and original project
test utilities are reused under their existing terms.
