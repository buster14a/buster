# GNU C extensions

This is the implementation record for [#147](https://github.com/buster14a/buster/issues/147).
The language policy is [#154](https://github.com/buster14a/buster/issues/154#issuecomment-6289359306):
follow Clang, with GNU17 as the default and the selected standard's keyword rules.

## Current boundary

The parser accepts GNU syntax, records its tree, and can supply structured
GNU attribute and asm-label metadata. `type_machine_mode` supplies the
machine-mode type action beside the layout and vector actions. The checker
([its guide](c-checker.md)) gives the constructs below their meaning, and `-c`
and the direct path lower them, or refuse by name what isn't lowered yet: the
table's last column. Each row's tests compare with clang (and GNU as), by both
paths, and run the code natively against clang's builds.

| Construct | Implemented here or already available | Remaining source integration |
| --- | --- | --- |
| GNU attribute lists | Generic names and argument trees, structured bounded records (`Parser.gnu`), and each attribute the checker knows applied as clang's Sema applies it (`c_check_attribute.c`: its subjects, its arguments' kinds and constants, ignored where clang ignores it). An attribute clang doesn't know is ignored with clang's `-Wunknown-attributes` warning and its "did you mean" suggestion ([the checker guide](c-checker.md)'s "Attributes clang doesn't know (#147)"). One on a declaration of none of its subjects gets clang's `-Wignored-attributes` warning or error, from clang's generated subject table, and so do function types' attributes on other types, `nonstring` off character arrays and `transparent_union`'s own cases (the guide's "Attributes on declarations they don't apply to (#147)"). A C23 list's place is clang's: a type's after a declarator chunk or the specifiers, with clang's warnings and errors there, and before a declarator's suffixes; GNU attributes GCC doesn't take after a definition's declarator get `-Wgcc-compat`; nonnull's indices, the calling conventions x86-64 hasn't and conflicting conventions get clang's diagnostics. A tag's attributes on its earlier declarations reach its definition's layout, those after a reference's name are its declarators', a type name's are ignored as clang ignores them, and `vector_size`'s errors are clang's (the guide's "Attributes' places, GCC's positions and their handlers' checks (#147)") | One clang knows that the checker has no rule for is taken as clang takes it where that changes nothing here (ignored, another language's, its generated handler's alone, and some whose handlers' checks are made: the guide's "Attributes without a rule"), and refused by name otherwise; argument counts, mutual exclusions and the handlers' argument errors and warnings (an alignment that is no constant or no power of 2, a section that is no string, a machine mode, a parameter index, a format, a target string, a sanitizer) are clang's, worded and placed as its own (the guide's "Arguments' errors"); a deprecated or unavailable member's use gets clang's diagnostic at a `.` or `->` (`check_input_member_uses`), at a designator or a value it takes by its position (`check_init_member_use`, an unavailable one's error whatever the warning options), and where the reader's evaluator alone reads the access (`typeof`, a file-scope array bound, an enumerator's value: #1416) |
| Statement and label attributes | Clang's grammar at block boundaries, after control heads and after labels; clang's `musttail` on a return checked by clang 23's rules and lowered to a tail call ([the checker guide](c-checker.md)'s "Tail calls (#147)"); a statement's attributes as clang's Sema takes them: fallthrough on a null statement in a switch that directly precedes a case or default label, as clang's analysis of the function's CFG has it (`check_input_fallthrough_placed`: the statement that runs next, past statements without executable ones (null statements, declarations of no variable, such as typedefs other than a variably modified type's, static assertions, tags' and functions', blocks of them and plain labels on them, `check_input_fallthrough_item`, but not a label on the case label itself); one that can't run (after a statement that never completes, with no label in between: `check_input_fallthrough_dead`; a return, break, continue or goto, a statement that always calls a function declared noreturn, inheriting it through redeclarations, a builtin or library function clang's table marks noreturn (`abort`, `exit`, `__builtin_trap`), an object whose function type is noreturn (its declaration's or its typedef's attribute, or a typeof's of a function GNU's attribute marks or of such an object or typedef: `check_input_fallthrough_noreturn_object`; an element of one, and a cast to such a typedef) or `__builtin_assume` of a false condition, a loop whose condition folds true and an `if` whose folded condition's branch, or whose two branches, never complete, conditions folding as clang's CFG builder folds them, through `&&` and `||` (`if (2-1)`, a `const int`, `x || 1`) with an `if`'s, a `while`'s and a `for`'s operands' blocks pruned one by one (`check_input_fallthrough_branches`), and with its tautology checks of comparisons (`x > 5 && x < 3`, `x || !x`, `(x & 4) == 3`, `(x < y) == 10`, `x | 2`, `x * 0`: `check_input_fallthrough_logic_tautology` and `check_input_fallthrough_tautology`; with floating literals at clang's APFloat values in the wider of `_Float16`, float, double and long double, enumerators of one enumeration, and members and elements as `Expr::isSameComparisonOperand` compares them, past the operands' implicit conversions: `check_input_fallthrough_comparison`, `check_input_fallthrough_real_tautology`, `check_input_fallthrough_same_operand_converted`), once the uses are resolved: `check_input_fallthrough_placements`) is taken where its CFG block reaches a case label through straight-line code (`check_input_fallthrough_flows`), as clang takes it with a warning, out of compound statements, an `if`'s branches and a switch's end, never a loop's body, must start with one; clang's error otherwise, but under `-w` and in a system header's function without `-Wsystem-headers`, where clang's analysis doesn't run: `check_input_fallthrough_analyzed`), GNU's `noinline` and `always_inline` ignored with clang's `-Wignored-attributes` warning at the statement ("use '[[clang::noinline]]' on statements"), clang's (and `msvc::noinline`) checked as its `CheckStmtInlineAttr` checks them (a statement without a call, and a call of a function whose declarations have `always_inline`, `noinline` or `flatten`, with its note: `check_input_statement_inline_calls`), `nomerge` and `suppress` ignored (a `nomerge` on a statement that evaluates no call, outside `sizeof`'s and `typeof`'s operands, with clang's warning: `check_input_statement_calls`; a `nomerge` of arguments clang's "takes no arguments", and `suppress`'s string literals through parentheses, as clang's parser takes them; a `suppress` it drops for a malformed argument leaves the statement a declaration where no GNU attribute is left, with clang's second error, "expected identifier or '('", at its first token: `check_input_suppress_dropped`), MSVC's `forceinline` and `forceinline_calls` ignored with clang's warning, an asm statement's C23 attributes each ignored with clang's warning and its GNU ones the parser's error, and clang's errors for the others, worded and placed as clang's (`check_input_statement_attributes`); GNU attributes after a label's colon the label's, whatever follows, with clang's errors for those whose subjects have no label (`section`, `alias`, `weakref`, `fallthrough`, `minsize`, `nomerge`, `suppress` and the rest: `check_input_label_attributes`, which looks for them by name before reading the attributes, but a `suppress` whose argument is no string literal, which clang's parser drops with its string error first, #2332) | after a GNU `suppress` whose malformed argument clang's parser drops, the errors of the declaration clang parses where the statement starts with `(` or `*` (an implicit int's declarator and Sema's errors after it: "redefinition of 'x'"), clang's "expected ')'" and its note after a non-string inside the argument's parentheses (`suppress((1))`), and the declaration's error where the parser itself rejects the argument (`suppress("a",)`, a token after a whole literal, whose "expected ')'" clang gives twice), and before a label's name `suppress`'s string error before an earlier refused attribute's subject error (`suppress(1), minsize`), which is given instead (#2332); for a fallthrough's reachability, the tautology checks of comparisons in `__bf16` or `__float128` (unknown here), and a noreturn function type a member's declaration or a conditional's arms give the callee (`s.cb()`, `(c ? f : g)()`), which only the checked types could follow |
| Asm labels | Declarator labels decoded as string literals (escapes and concatenation) and given to the declaration's symbol as its name, in `-c`'s objects and the direct path's executables, a redeclaration's the same one | None |
| `mode` | The checker's `check_input_attribute_mode` gives the declaration its type as clang's `AddModeAttr` does (its qualifiers dropped, a vector mode's or a vector base's vector, `__fp16` of HF), with its errors and `-Wdeprecated-attributes`' warning of a vector mode ([the checker guide](c-checker.md)'s "Arguments' errors"); `type_machine_mode` is the type table's own | A base of the wrong type is one error, where clang goes on to "unsupported machine mode" where both apply |
| `packed`, `aligned`, `vector_size` | Applied to declarations and types by the type table's layout and vector actions, as clang lays them out | None |
| `typeof`, statement expressions, GNU `?:`, range designators | Checked and lowered: types, value categories, evaluation order ([the checker guide](c-checker.md)) | None |
| Alternate keywords, `__int128`, zero-length arrays | Checked and lowered: the double-underscore keywords (`__signed__`, `__const__`, `__inline__`, `__typeof__` and the others), `__int128` and `unsigned __int128` (Tier 0's I128 and U128), and zero-length arrays and flexible array members, laid out as clang lays them out | None |
| Local labels (`__label__`) | Scoped as clang scopes them: a list's names are labels of its block, shadowing an outer list's and the function's, with clang's errors at its places; a statement expression's labeled last statement gives its value ([the checker guide](c-checker.md)'s "Local labels and `__auto_type` (#147)") | None: a name a block's lists declare twice and the missing labels are reported in clang's order, with the notes naming an ambiguous name's candidates; each ambiguous reference makes a label of the function's own scope, as clang's lookup does, which the body's list's names share a scope with, so that a later reference may find two and more (`check_input_label_lookup`, #1844) |
| `__auto_type` | Deduced as clang deduces it: the initializer's type, lvalue-converted and decayed (`_Atomic` kept), with the specifiers' `const` and `volatile`; through pointer and function declarators, and the same in a declaration's later declarators, as clang deduces them as C++ does (GCC refuses them), by C23 `auto`'s deduction (`check_input_c23_auto_deduce`); a variably modified one, whose sizes the initializer's own declaration evaluated (`&vla`) or its cast evaluates (#2321); a redeclaration of a variable with linkage the earlier declaration's type, as clang merges it before deducing (a `restrict` placeholder's over a restrict pointer too), and clang's "redefinition with a different type" error, with its note, where clang's `mergeTypes` refuses the merge (#1844); numeric and OpenCL address-space attributes retained at each object or pointer level, with the storage, restrict-specifier, attribute and redeclaration diagnostics (the checker guide's "Local labels and `__auto_type` (#147)"); a vector's or a mode's attribute of the placeholder clang's errors (#1844); clang's errors at its places. C23's `auto` is the language's type specifier (the checker guide's "C23 `auto` and `constexpr`") | `_Atomic` and `restrict` are the deduced type's (`_Atomic(int)`, `int *restrict`, #1844). A redeclaration of an atomic entity takes its atomic type, as clang merges it, and a cast to an atomic type deduces the atomic type (#2230). Refused by name: either through a declarator, both together, `_Atomic` of an atomic value, `restrict` of a value that is no pointer to an object (clang's `restrict int`), and other type-changing attributes besides vector, mode and address space. An incompatible address-space pointee initializer is rejected in the assignment checker's words, which name the deduced type. The reader stops at the "different type" error, as at other initialized invalid declarations, so clang's later errors aren't given, and so does it at a vector's or a mode's error, after which clang goes on. Declarators that deduce differently get clang's error at its place in the checker's words |
| Casts to a union | Checked as clang's `CheckCStyleCast` checks them: an operand of a member's unqualified type, after its lvalue conversion, initializes the first such member that is no unnamed bit-field, with `-Wgnu-union-cast` under `-pedantic` or `-Wgnu`; another type gets clang's "not present in union" error, and a cast to a struct from another type its "used type" error; a record's cast to its own type gets `-Wpedantic`'s "C99 forbids casting nonscalar type" warning, and a cast to a scalar from a record clang's "operand of type" error (#2375). Lowered as clang's code does it: the member stored into a temporary union, whose address is the cast's value, and in a static object's initializer the member's bytes ([the checker guide](c-checker.md)'s "Casts to a union (#2301)") | A function designator's decayed type in the error loses its parameters' typedef names, as buster's other diagnostics of a decayed function do; where an error follows at the same expression (a file-scope initializer's), the cast's warning comes after it, where clang gives it first |
| `__extension__` | Accepted before expressions and declarations | None: the checker gives no pedantic warning for it to suppress |
| `cleanup` | Checked as clang checks it, its scopes' jumps too, and called at each exit from its variable's scope, the named return value's after the return's cleanups as clang's NRVO has it, a `_Bool` parameter's argument the address's low bit as clang's code passes it, and a result of any class dropped ([the checker guide](c-checker.md)) | A function of an `_Atomic _Bool` parameter, whose call clang's code generation can't select, is refused by name |
| `transparent_union` | Checked as clang checks it, with its warnings where clang ignores it (a declaration of no union, an incomplete or empty union, a floating or vector first field, a field of another size or a greater alignment, with the note), and a field less aligned than the first is fine, as in clang; a conversion to the union selects its member as clang's does; its arguments, parameters and `va_arg` pass as its first member, as clang's code passes them, with a stack slot of the union's own size and alignment where an `aligned` or `packed` attribute makes them differ ([the checker guide](c-checker.md)'s "Transparent unions (#147)") | A union sized or aligned otherwise than its first member past two eightbytes or of a member passed as an aggregate, or one made transparent after a function definition or an initializer naming a function where its first member passes otherwise, is refused by name |
| `gnu_inline`, `returns_twice` | `gnu_inline`'s `extern inline` definition is no definition of the unit's, as clang's isn't (a call reaches the library's); `returns_twice` needs nothing of Tier 0's code, which keeps locals across a call in memory or callee-saved registers, as `setjmp` needs | Tier 1's code of a function that calls one (`--tier1`) isn't checked against `setjmp`'s rule |
| `always_inline`, `noinline`, `used`, `may_alias` | `used` roots its definition, which `-c` and the direct path emit; `always_inline`'s local functions are what the lowering's symbol comparison expects clang to inline; `noinline` and `may_alias` need nothing of Tier 0's code, which inlines nothing and assumes no aliasing rule | None |
| `constructor`, `destructor` | An entry of `.init_array` or `.fini_array` each, its priority's `.init_array.N` or `.fini_array.N` in `-c`'s object, in LLVM's order (stably by priority, past 65535 its default), run against clang's build ([the checker guide](c-checker.md)) | None |
| `weak` | A weak symbol, defined or undefined; clang's error without external linkage, and one after the definition ignored; one after a use binds as clang's code generation does (#1849): an undefined object's symbol has the binding of the declaration its first emitted reference names, so a reference before the weak declaration keeps it global, and a tentative one's that of its last tentative definition | Where only code clang emits at the unit's end refers to an object, and its references disagree on a late weak declaration, the binding is refused by name ([the checker guide](c-checker.md)'s "GNU's weak, constructor and destructor attributes") |
| `alias`, `ifunc`, `weakref` | An alias is a symbol of its own name, binding and visibility at its target's definition (through chains of aliases), with the definition's size and type; an ifunc an `STT_GNU_IFUNC` symbol at its resolver, always global; a weakref's references are weak ones to its target's name, or to the unit's definition of it, weak where nothing else refers to it; every alias and ifunc roots its definition, as clang emits each, in `-c`'s object and the direct path's executables (the dynamic PIE's IFUNC stubs and `IRELATIVE` relocations). Clang's code generation errors at `-c` (undefined, cyclic, of another kind, an ifunc's resolver), with their notes, and Sema's at `-fsyntax-only` (a definition that is also an alias or ifunc, weakref's linkage, context, target and subject); `__has_attribute` answers 1 ([the checker guide](c-checker.md)'s "GNU's alias, ifunc and weakref attributes (#147)") | Refused by name: a reference to an alias whose thread-local storage isn't its definition's, which clang's code makes as the alias's declaration says (an unreferenced one has its definition's symbol type, as clang's object does). Clang's code generation warnings (different function types, a section on an alias or ifunc, an alias of a weak alias) are reported at `-c` (#1849) |
| `retain` | An emitted definition keeps its section with `SHF_GNU_RETAIN`: its named section's, or one of its own, `.data.name` and the like as LLVM names it (#716's section descriptors); retain alone emits nothing, as clang 23 emits no unreferenced static with it; `__has_attribute` answers 1 ([the checker guide](c-checker.md)'s "GNU's `retain` attribute") | None |
| `common` | A tentative definition of external linkage with it, or with `-fcommon` any but `nocommon`'s, is a common symbol (`BUSTER_LINK_SYMBOL_COMMON`), as clang's `isVarDeclStrongDefinition` makes it one: `-c`'s object writes it as `SHN_COMMON`, its value its alignment, as clang's does; the units' merge keeps a name's largest common, with the largest alignment of its commons, as GNU ld and lld do, and a strong definition takes its place; an executable allocates it in `.bss`, an object in its table (`STT_OBJECT`, a hidden one local). A common of no bytes (`int x[0];`, an empty struct) has one, as LLVM's code publishes it, so that two don't share an address; an alias of a common is refused at its attribute, as clang's assembler refuses it. One that an initializer defines, a section or weak attribute, thread-local storage or internal linkage keeps a definition of its own | None: the import path's reader (`import.c`: the C runtime's objects, the lowering's comparisons of clang's objects) reads an object's global `SHN_COMMON` symbol as a common of its own atom, its size and alignment (#1849), and `link-foreign` resolves others' commons itself (`foreign.c`) |
| `counted_by`, `sized_by`, `counted_by_or_null`, `sized_by_or_null` | Clang's own (#148), on a struct's flexible array member or pointer member, wherever the member's declaration writes it: checked as clang's `CheckCountedByAttrOnField` checks it (its member's place and type, its pointee's or element's size, its count's name among the members declared before it, an enclosing record's through anonymous ones, then the ordinary scopes), with clang's errors and its element warning (`c_check_counted_by_input.c`); the member keeps its count (`TypeMember.count_kind` and `count_name`), which `__builtin_counted_by_ref` and `__builtin_dynamic_object_size` read ([the builtins guide](c-builtins-integration.md)); a count member that has a typedef's name is read as the member, as clang's lookup reads it (the parser's `parse_scalar_counted_member`, #1842); a type name read as the argument's expression, at any depth, is the parser's "unexpected type name" and then clang's "requires a non-boolean integer type argument" at the argument's first token (`ParseDiagnostic.publication` keeps clang's order), but past the argument's top-level `,`, where clang reads nothing, the checker's "expected ')'" alone (#1842); any other argument of more than a name is typed as clang's parser reads it, with the record's earlier members and the scope's names but no member in `sizeof`'s, alignof's or typeof's operands: a name that names nothing is clang's undeclared error, one of no integer type its integer error, any other its "simple declaration reference" one (`check_input_counted_argument_type`, #1842); the expression's own `-Wdivision-by-zero` at a `/` or `%` by a constant zero (`check_input_counted_division`), and its member access errors as an attribute expression's (`member reference base type 'int' is not a structure or union`, then clang's integer error), come before the attribute's own error, which completes at the argument's end; each of the attribute's own errors drops it and the unit goes on, as clang's does; `__has_attribute` answers 1 | `-Wgnu-alignof-expression` inside such an argument isn't made; a missing member's or a `->` of no pointer's error comes after the integer error, where clang gives it before; the attribute list's own errors (an argument past a `,`, none, no parentheses) still stop the unit; a counted member's type prints clang's `__counted_by(n)` sugar only as a failed conversion's source or assigned target (`check_driver_counted_type`, #1842's 3c: `'int * __counted_by(n)' (aka 'int *')`, `'int[] __counted_by(n)'`), other diagnostics print it canonically; clang's errors for a counted pointer whose pointee is incomplete where it is used are made for a read and an assignment (`check_graph_counted_uses`, #1842's 3d: "cannot use 'b->p' …", "cannot assign to 'B::p' …", with both notes), but not yet for an initializer list's element ("cannot initialize 'B::p' …"); after a parser error in a declaration the parser reads no more of it, where clang goes on (`counted_by(T n)`'s "expected ')'", a later member's attribute errors) |
| `lifetimebound`, `loader_uninitialized`, `weak_import`, `internal_linkage`, `nocf_check` | clang's (#147): `lifetimebound` checked alone; `loader_uninitialized` a definition in `.bss` without an initializer (weak where common linkage would be clang's); `weak_import` a weak reference where the unit doesn't define its entity; `internal_linkage` a local symbol; `nocf_check` ignored with clang's warning (the driver refuses `-fcf-protection`); each with its handler's errors and warnings ([the checker guide](c-checker.md)) | None: `loader_uninitialized`'s incomplete type and redefinition errors are clang's, with their notes, and an explicit `section`'s variable is in a read-only section, as clang's object has it ([the checker guide](c-checker.md)) |
| The thread-safety analysis's (`guarded_by`, `requires_capability`, `acquire_capability`, `capability`, ...) | clang's (#147): no effect, their arguments' names resolved where the attribute is and their handlers' errors ([the checker guide](c-checker.md)) | None: their arguments are complete expressions, evaluated as clang's are, and `-Wthread-safety-attributes`' warnings (off by default) are clang's ([the checker guide](c-checker.md)) |
| `section` | A function's or a variable's named section in `-c`'s objects and the direct path's executables, with the section types clang gives it (#716's section descriptors) | None |
| Inline assembly statements | Existing parser grammar, kept distinct from asm labels; the checker's operands; constraints, operand substitution, clobbers and assembly into per-unit helpers, flag outputs and `asm goto` among them, naked functions, and templates that refer to symbols ([the inline assembly guide](c-inline-assembly.md)) | The guide's "Pending" list |

The checker (#133) and the lowering (#129) own these semantic decisions; the
table records where each is implemented. `__has_attribute` answers 1 only for
the attributes whose effects are implemented (the preprocessor's
`pp_implemented_attribute`: `common` and `nocommon` among them now), never
from syntax acceptance alone.

## Parser corrections

A GNU attribute after a named label does not require a null statement before
the label's statement, and is the label's in C, as clang's parser takes it:

```c
void f(void)
{
    again: __attribute__((unused)) return;
}
```

Attributes after a control head similarly precede its statement; they do not
make a declaration legal where a statement is required. At an ordinary block
boundary, Clang distinguishes a leading attribute list that consists entirely
of statement attributes from one that starts a declaration; C23 attributes
alone are a statement's where the token after them starts no declaration
(`[[x]] y += 1;`, `parse_scalar_attributed_statement`). The parser now
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
