# Bounded regression expectation audit (#3089)

Inspected 2026-10-08 at main
`c60bbc25e53a041de2a1e5ae626b3c2e7cf5ad74`, tree
`1ad7d9e8da2d1d2de270157830fd824a9cb37a50`.
This is current-source inspection plus explicitly historical execution evidence,
not a new compiler/test run or a repository-wide defect-rate estimate. Procedure:
[testing guide](agents/testing.md#reviewing-regression-expectations).

## Exact source and test identities

All Buster links below pin that inspected revision. Git blobs identify bytes;
symbols identify the reviewed subset, not every assertion in each file.

| Family | Source/test identities |
|---|---|
| Linux namespace/context | [os.c](https://github.com/buster14a/buster/blob/c60bbc25e53a041de2a1e5ae626b3c2e7cf5ad74/src/buster/lib/os.c), blob `7c60a1fd5f8636cee41dc79daec04d74f680abd2`: `os_linux_process_status_namespace_index`, `os_linux_proc_context_select_self`, `os_linux_proc_context_select_self_test`, `os_linux_proc_context_live_self_test`, `os_linux_process_stat_parse_self_test`. [os_test.c](https://github.com/buster14a/buster/blob/c60bbc25e53a041de2a1e5ae626b3c2e7cf5ad74/src/buster/tests/os_test.c), blob `ac3f8ab6eacdc045ede0d135d510470e1167e0e8`: `os_tests`' 26-row `OsTestProcContextCase` table and live context/resource checks. |
| Function-parameter dialect | [c_test.c](https://github.com/buster14a/buster/blob/c60bbc25e53a041de2a1e5ae626b3c2e7cf5ad74/src/buster/tests/compiler/frontend/c/c_test.c), blob `a10f8201367b60415f75e9e2f22cba962a0ad46c`: registered `c_test_function_parameter_compatibility_runtime`; neighboring `c_test_function_parameter_compatibility` and `c_test_c23_empty_list_prototypes`. |
| GNU typeof declarations | Same `c_test.c` blob: registered `c_test_typeof_statement_expression_declarations`, added by [#3065](https://github.com/buster14a/buster/pull/3065). [c_parse.c](https://github.com/buster14a/buster/blob/c60bbc25e53a041de2a1e5ae626b3c2e7cf5ad74/src/buster/lib/compiler/frontend/c/c_parse.c), blob `546e0cb00a554128a1a3d98f4661c1ee327f4a99`: `c_parse_bind_block_statements`, `c_parse_typeof_statement_expression_after`, `c_parse_pending_typeof_grow`. [c_gen.c](https://github.com/buster14a/buster/blob/c60bbc25e53a041de2a1e5ae626b3c2e7cf5ad74/src/buster/lib/compiler/frontend/c/c_gen.c), blob `171b5f0f9361d06d0baf64483bcef18a68ee6f4e`: `c_ir_declarator_list_specifier_end`, `c_ir_prepare_automatic_declaration`. |

## Linux: keep corrected hierarchy expectations

**Proves:** the production context-selection seam accepts valid parsed hierarchies
and checks the caller at the innermost coordinate. Repeated IDs in different
coordinates are accepted; duplicate fields, malformed/missing IDs, depth overflow,
invalid identity and a wrong final PID remain refused. Live checks exercise procfs
open/select/close and resource balance.

**Expectation source:** [#2562](https://github.com/buster14a/buster/issues/2562)
links Linux `proc_pid_status(5)`'s ordered procfs-to-nested namespace hierarchy and
`pid_namespaces(7)`'s namespace-local numbering. Numeric equality between levels
does not imply duplicate identity. Current source selects depth minus one, checks
a positive current PID there, and preserves mount/namespace, exact-child,
leader and double-census ownership gates.

Representative sensitivity and valid-alternative evidence:

| Input and getpid | Contract result | Historical wrong helper | Current table expectation |
|---|---|---|---|
| `10041 41`, 41 | accept index 1/depth 2 | accepts | accepts |
| `41 41`, 41 | accept index 1/depth 2 | rejects | accepts |
| `10041 41 41`, 41 | accept index 2/depth 3 | rejects | accepts |
| `41 42`, 41 | refuse: caller is not final entry | unique-match algorithm would accept (source derivation) | refuses |

The [old helper](https://github.com/buster14a/buster/blob/97fb07f42b432864bee9fd38b70eb69b15252991/src/buster/lib/os.c)
at `97fb07f42b432864bee9fd38b70eb69b15252991` returns `matches == 1`.
#2562 records its extracted C11 helper execution for the first three rows:
successful compilation and direct calls reject the valid repeated-ID cases for
the intended semantic reason, without fixture build/launch failure. This is
historical negative-control evidence, not a fresh production mutation run.
The repeated-ID rows are valid alternatives to the incidental unique-number
assumption. Current table inspection independently confirms those exact accepted
index/depth values and the earlier-only-match refusal.

Historical repaired-source execution is separately bound: [run 37194112228,
job 111412434521](https://github.com/buster14a/buster/actions/runs/37194112228/job/111412434521)
checks out `c465e38a5368bacf26220dc289d172f041dbd78c`; the inspected log reports
`os_tests passed=1290 failed=0 assertions=1290`.
[#2581's receipt](https://github.com/buster14a/buster/pull/2581#issuecomment-5978964327)
identifies the 26 context rows and live controls. Their literal table is byte-identical
in the inspected current source and that repaired source. Protected integration is recorded
at `ba2edd6a3078a215bfdfa7731690d421fec76d0e` in
[the completion receipt](https://github.com/buster14a/buster/issues/2562#issuecomment-5980023359).

**Limits/disposition:** keep. No live nested PID namespace or fresh current-main
execution is claimed. The synthetic table does not prove process churn or explain
#2380's historical failure; #2380 retains that investigation.

## Function parameters: keep the repaired dialect guard; follow #2579

**Proves:** compatible array/function parameter adjustments, top-level parameter
qualifiers and legacy promotion-compatible redeclarations compile and execute.
Literal runtime expectations follow directly from the independent initialized
arrays/call arithmetic: `7, 7u, 20, 9, 17, 11, 19, 7, 23`.
Array accesses are inside the supplied objects, including the static-minimum case.
C17/GNU17 float, bool, char and short promotion conflicts remain separately refused;
the neighboring C23 fixture checks zero-parameter prototypes.

**Expectation source:** pre-C23 empty lists are non-prototypes; C23 empty lists
declare no parameters (#1860/#1306). The current literal source uses
`__STDC_VERSION__ <= 201710L` for legacy declarations. The old
`< 202311L` guard incorrectly selected legacy declarations for GCC14's draft
`202000L`, even though that dialect implements the C23 rule.
[#2579's finding](https://github.com/buster14a/buster/issues/2579#issuecomment-6057727844)
and [correction](https://github.com/buster14a/buster/issues/2579#issuecomment-6057941759)
record the independent declaration conflict and successful corrected controls.

**Limits/disposition:** keep the correction. All eight Buster dialect/allocator/
frontend profiles, eight GCC/Clang dialect/O0/O2 runtime controls, and 16 reference
promotion refusals remain in current source. Those historical successes do not
qualify every reference version. GCC13's unsupported `-std=gnu23` spelling remains
#2579's capability problem; do not normalize it by changing Buster semantics,
adapting flags silently or counting unavailable required execution as green.
No fresh oracle execution or general qualification repair was made by this audit.

## GNU typeof: retain behavioral expectations

**Proves:** operand-local declarations are bound for type resolution without
escaping their block scope. Syntax checks cover both frontend forms, with typedef,
qualifier/attribute, atomic wrapper, for-initializer, sibling and nested operands.
A separate `private_name` escape must produce an undeclared-name error naming it.
Twenty-four generated nesting layers must remain accepted. FAST/QUALITY runtime
checks reject native fallback and compare eleven initialized object values.

**Expectation source:** [GCC14.2's GNU C manual source](https://github.com/gcc-mirror/gcc/blob/04696df09633baf97cdbbdd6e9929b9d472161d3/gcc/doc/extend.texi),
`Statement Exprs` and `Typeof`, documents compound-statement local variables,
the last expression's value/type and type-only use of non-variably-modified
operands. Ordinary block scope makes the escape invalid. The fixture's values
are independently derived from its explicit initializers/assignments; e.g.
`shadowed == 9` uses the enclosing object after the operand's scope ends.
No internal binding IDs, pending-frame order or emitted instruction sequence
is an expectation, so a different correct binding algorithm remains admissible.

**Limits/disposition:** keep. This is a bounded declaration regression, not every
GNU extension, VLA side-effect rule or arbitrary-depth proof. The privacy refusal
is source-semantic evidence, not a newly executed implementation mutation.
[#3065](https://github.com/buster14a/buster/pull/3065) retains its historical execution
and integration receipts; this audit does not transfer those passes to current main
or establish pristine application acceptance (#79/#3082).

## Validation and license

No production, fixture, golden, frozen inventory, workflow or admission rule
changes are needed for these dispositions. This documentation-only slice needs
the existing Markdown-link and whitespace checks on its submitted head; compiler
builds are unnecessary. Historical execution, source derivation and current
inspection are deliberately distinguished above. New independent defects require
their own reproducible report, not silent expectation changes.

Buster first-party license is unspecified/NOASSERTION, verified from
[LICENSES/README.md at the inspected revision](https://github.com/buster14a/buster/blob/c60bbc25e53a041de2a1e5ae626b3c2e7cf5ad74/LICENSES/README.md),
blob `366189f2aa2b9f635ae1faf26a92cc68349aee46`.
The referenced GCC manual is GFDL-1.3-or-later with invariant/cover texts, verified
in [gcc.texi](https://github.com/gcc-mirror/gcc/blob/04696df09633baf97cdbbdd6e9929b9d472161d3/gcc/doc/gcc.texi).
No external implementation, fixture or manual text is imported.
