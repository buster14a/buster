# Constant name-binding oracle

`scope_oracle.py` generates C17 programs in which one ordinary identifier (an
enumeration constant, or an object used as `sizeof obj`) is declared at several
scopes and then used in type-level and constant contexts. It also computes each
program's expected output from its own model of the C17 scope rules and the
x86-64 System V layout rules. `run.py` compiles and runs each program with a
subject compiler (normally `build/Release/ide`) under several configurations and
compares every printed value with the model, Clang and GCC.

Both files are hand-run tools outside the build graph. They use only the Python 3
standard library, `clang`, and `gcc`.

## Why an independent oracle

Every program prints labelled values, so a mismatch names the exact binding that
went wrong. The existing checks of this area all share one lookup, so none of them
can see a wrong binding:

```text
                  names inside tag bodies and type names in constant expressions
                                            |
        parse-side layout                   |                IR layout
  c_parse_type_layout                       |      c_ir_array_bound_evaluate_attempt
   -> c_parse_first_constant_entity_symbol  |       -> c_ir_constant_entity_at
      (oldest same-spelled entity)          |          -> c_ir_identifier_entity_or_lookup
                                            |             (no parser binding: scope-0 lookup,
                                            |              then the oldest-by-symbol index)
         |                                                      |
   enum/_Static_assert/offsetof constants            runtime sizeof, frame and stack layout
         \______________________ agree with each other _______/
                                    |
  existing oracles: the literal member bounds in tests/basic_c_block_scope_tag.c;
  internal parse-vs-IR agreement; metamorphic/differential grammars without shadowed
  names or block-scope tags; the self-host fixed point
```

Information from outside that mechanism is therefore required: the binding the
C17 scope rules give each use. `scope_oracle.py` computes it from an abstract
program tree without parsing the rendered C and without running a compiler. Clang
and GCC then confirm the model on every audited program.

## Run

From the repository root, after building `build/Release/ide`:

```sh
python3 tools/scope_oracle/scope_oracle.py --list-families
python3 tools/scope_oracle/scope_oracle.py audit --families all --seeds 0-299 --cc clang --cc gcc --work build/scope-oracle-audit
python3 tools/scope_oracle/run.py --oracle tools/scope_oracle/scope_oracle.py --subject build/Release/ide \
    --families D1,D2 --seeds 0-299 --work build/scope-oracle-design --json build/scope-oracle-design.json
```

Use a fresh `--work` directory. `run.py` gives each generated program one of
these verdicts:

| Verdict | Meaning |
|---|---|
| `PASS` | Every configuration printed exactly the oracle's lines. |
| `DEFECT` | Clang and GCC both reproduce the oracle exactly, and the subject does not: a mismatched value, a rejected compile, or a failed run. |
| `REFERENCE_DISAGREES` | Clang or GCC differs from the oracle. This is an oracle or generator problem, never a compiler finding. |
| `ORACLE_ERROR` | The oracle failed to render the program. |

`--configs` selects subject configurations: `default`, `nossa`, `none`,
`mirstack`, `quality`, `nofast` and `O2`. `--fault K` exports
`BUSTER_SEED_FAULT=K` for a fault-seeded test build. `MISMATCH_LABELS` lines
count wrong values by consumer: `sz`, `al`, `off`, `bound`, `esz`, `snap`, `val`,
`tn`, `td` and `bf`, as defined in the oracle's docstring.

## What the oracle covers

The families and grammar are in the oracle's docstring. In brief:

* **Design families.** D1 is one constant shadowed across file, function and
  nested-block scope, with structs at each scope. D2 is sibling functions with
  same-named local constants and local structs.
* **Held-out families.** H1 is bit-field widths, H2 `_Alignas`, H3 `sizeof obj`
  in bounds, H4 scope end and controlling-expression declarations, H5
  self-referential enumerators, and H6 unions, nested members and enumerators
  declared inside members.

Expected values follow C17 6.2.1 (scope), 6.7.2.2 (enumeration constants), 6.6,
6.5.3.4, 6.7.2.1, 6.7.5 and 7.19 (`offsetof`), together with the x86-64 System V
LP64 layout.

Excluded by design:

* bit-field record layout;
* plain-`int` bit-fields;
* alignments above 16;
* the size of enumerated types;
* VLAs, flexible and anonymous members, `long double`;
* division in constant expressions and negative values;
* tag shadowing across nested scopes.

Programs never read objects or write member arrays, and they print every
observable as a `label=value` line. Nothing is encoded in the exit status.

## Validation record (main `ade6ac4b6ecb21f30b61b656439bac476c145e2f`)

The oracle author did not read the frontend and did not run Buster. Seeded faults
came from a different author, who did not read the oracle. The two files were
frozen at the sha256 values below before any held-out family was run against
Buster:

* `scope_oracle.py`: `42e5f346…5e4121d2`
* `run.py`: `01118821…3078f0548f`

| Check | Result |
|---|---|
| Model vs Clang 18.1.3 and GCC 13.3.0 at `-O0` and `-O2` (`-std=c17 -pedantic-errors`), seeds 0–299 of all 8 families | 9600/9600 builds equal the model; Clang = GCC on 2400/2400 programs; 155,051 expected lines |
| GCC ASan+UBSan builds of the same 2400 programs | 2400/2400 clean, with output equal to the model |
| Negative control: `run.py` with Clang or GCC (`-O1`) as the subject, seeds 300–339 of all 8 families | 640/640 `PASS` |
| Seeded faults, env-selected in a test-only build (nothing committed), 60 seeds × 8 families | 5 of 8 detected: dropped `_Alignas` struct alignment, a nested-`offsetof` slip, enumerator-initializer and block-binding scope slips, and dropped tail padding in the parse layout |
| Determinism: design seeds 0–99 run twice | 200/200 identical verdicts and diffs |

The three undetected faults each have an identified cause:

* a bit-field straddle off-by-one needs bit-field record layout, which is
  excluded;
* a wrong successor for negative enumerators needs negative values, which are
  excluded;
* `sizeof` of a file-scope array is masked by R2 below, because 294/300 H3
  programs are rejected before the value can be observed.

## Findings on the pinned compiler

Results were identical under `default`, `-fno-frontend-ssa`,
`-fregister-allocator=none` and `-fregister-allocator=quality`. LLVM bitcode
output has the same layouts, so these are frontend defects that affect every
target.

| Family | Programs with a `DEFECT` verdict |
|---|---|
| D1 | 270/300 |
| D2 | 300/300 |
| H1 | 260/300 |
| H2 | 285/300 |
| H3 | 299/300 |
| H4 | 299/300 |
| H5 | 288/300 |
| H6 | 288/300 |

The design families (D1–D2) were used to build the detector. The held-out families
(H1–H6) were first run against Buster only after the freeze.

The following binding rule predicts every wrong value in D1, D2, H1 and H6:

> A name in a tag body or in a type-name bound binds the file-scope declaration if
> one exists anywhere in the translation unit, even a later one, and otherwise the
> oldest same-spelled declaration.

D1 and D2 have 0 unexplained labels among 4,030 wrong ones. In 13 D2 programs the
rule gives a zero or negative member bound: Buster silently compiles 8 of them and
rejects 5.

The detector exposes three root causes. The minimal witnesses below print the
Buster value first.

* **R1: scope-blind binding in type-level constants.**
  * A block-scope struct member bound resolves `N` to the file-scope `N`: size 1
    instead of 9.
  * Sibling functions each with a local `enum { K }` share the first function's
    `K`.
  * A local constant loses to a file-scope declaration that appears later in the
    file.
  * In `enum { M = sizeof(char[N]) }` with `N` shadowed, the parse side gives 3
    while the IR gives 5.
  * A unity build of two functions, each with a local `enum { CAP }` and a local
    `struct Buffer { int value[CAP]; }`, gives the second buffer 16 bytes while its
    loop writes 256. The program crashes with SIGSEGV.
* **R2: parse-side layout evaluates member bounds with the preprocessor
  evaluator.**
  * The layout cannot evaluate a bound of `sizeof(char[SZ])`.
  * As a result, `enum { E = sizeof(struct S) }` on such a struct is rejected as
    "not an integer constant expression".
  * In the default configuration this shape accounts for 1,064 of the 1,079
    "not an integer constant expression" rejections: in each, the rejected
    enumerator takes the size of a record whose body contains a `sizeof` bound.
  * Of the remaining 15, some are D2 bounds that R1 makes negative, some are
    nested records, and 2 are H3 `enum { snapK = sizeof obj }` snapshots.
* **R3: declarations in an `if`/`switch` controlling expression are not in
  scope.**
  * Given `if (sizeof(enum { Q = 8 })) value = Q;`, the substatement sees the
    outer `Q`, or reports "use of undeclared identifier".

This detector does not cover `_Static_assert` arithmetic: cast-free assertions
are evaluated with `#if` rules, so `_Static_assert(~0u == UINT_MAX, "")` fails.
That separate constant-context probe finding is recorded with its issue.
