# Scout ledgers behind audit 2026-09-25T215352Z

Four read-only scout agents ran against main `d9e736e2cf08804a2c603d153e9fb608f18c5c49`
(tree `ae9f274b6768b023fe6e0ef80a161e14b290e0a2`). All figures are callgrind `Ir` from the
x86-64-v3 observation build (`ide-obs`, SHA-256
`b33526b8d871877ed6f9306007b346ca3f8fdd5ecad24a8f55b6f3ad211ac0e1`) unless stated; none is a
timing. Raw callgrind files, probe patches and worktrees stayed in the session container and
are not retained here; the tables below are the retained record.

## A. Used-versus-prepared census of the x86-64 prewarm

Disposable read bitmaps in `machine_x86_64.c` and `x86_64_metadata.c` (probe binary
`6e990e0a53c79a91eae2f547169b2357c0537755d96ea519b8873e3ab2891629`) counted what each
workload reads after the prewarm prepared it. Workloads: `tiny` one function, `basic`
`tests/basic_c_operations.c`, `string` `src/buster/lib/string.c` non-unity, `hello`
stdio + link, `self` the stage-1 unity compile.

| table | tiny | basic | string | hello | self |
|---|---:|---:|---:|---:|---:|
| shape-cache queries needed / registered | 6/336 | 25/336 | 29/336 | 8/336 | 62/336 |
| shape entries hit at emission | 0/267 | 9/267 | 10/267 | 0/267 | 35/267 |
| fixed-template rows read | 7/1466 | 53/1466 | 56/1466 | 10/1466 | 195/1466 |
| opcode-map rows read | 3/97 | 48/97 | 51/97 | 5/97 | 80/97 |
| dense GPR tables read (#124 owns the preparer) | 3/64 | 45/64 | 47/64 | 4/64 | 61/64 |
| variable-memory entries read | 0/6912 | 15/6912 | 31/6912 | 0/6912 | 362/6912 |
| x86 form records read, full prewarm / oracle prewarm | 1479/184 | 1479/593 | 1479/711 | 1480/496 | 1480/990 |
| NUL-distance entries read (of 1,726,254), full / oracle | 3012/476 | 3012/1242 | 3012/1416 | 3015/1012 | 3015/2053 |
| coverage records read (of 11,013) | 0 | 0 | 0 | 0 | 0 |

Oracle replay (prepare only the entries the same workload reads; all outputs `cmp`-identical):
upper bounds of deletable preparation are tiny 89.0 M (71%), hello 79.5 M (56%), basic
45.3 M (29%), string 42.3 M (15%), self about 0.14%. This is an upper bound: the keep lists
come from the measured workload itself. Estimated unread-but-prepared work outside the shape
cache: NUL-distance fill 16.35 M (99.8% of entries unread), variable-memory tables about
14.1 M (`machine_x64_exact_prepare_variable_memory_encoding_table`, separate from the #124
GPR preparer), forms/operands base64 decode about 8 M, coverage blob decode 1.05 M (never
read: coverage gating reads `form.coverage_class`).

`-fcompile-jobs=N` under valgrind exits 1 because each translation unit reserves a 32 GiB
`MAP_NORESERVE` arena that valgrind refuses; natively the same invocations succeed.

## B. Cost of compiling the generated assembly-metadata headers (self-host)

Stage-1 self-compile under callgrind: 24,810,589,023 Ir; output identical to the trusted
Release build (`a3fadc98f19a5ff9f3362be67a12e6c5c45f762709d000d8d7146e59554ebfbb`).
Attribution by a gen-only translation unit minus control and by a "hollow" stage-1 whose
generated string literals were emptied in scratch copies (sources untouched); the two methods
agree within 0.03% on the literal share.

| phase | stage-1 Ir | generated-header share |
|---|---:|---:|
| preprocess + lex | 1,584.8 M | 549.8 M |
| parse | 509.1 M | 257.3 M |
| semantic | 8,434.1 M | 2,969.8 M |
| lowering | 5,775.2 M | 1,353.4 M |
| IR prep / codegen | 8,041.5 M | 11.7 M |
| object / DWARF | 394.9 M | 21.7 M |
| total | 24,810.6 M | 5,163.8 M (20.8%) |

Verified in the same profile by the integrator: `c_parse_validate_initializer_shape`
2,389.3 M inclusive, of which 2,053.0 M re-enters `c_parse_infer_initializer_array_count_core`
(2,924 calls) after `c_parse_infer_file_array_bounds` already ran the same core in its cheaper
mode (2,691 calls, 398.0 M); `c_parse_initializer_expression_constraint` 1,726.3 M over
579,119 calls; `c_ir_count_quoted` 733.5 M over 58,384 calls against 35,444
`c_ir_decode_quoted` calls. `aarch64-syntax.generated.h` (562 M of the share) is read only by
tests. Peak RSS of stage-1 with the literals emptied fell by about 64 MB.

## D. Materialize-then-delete in the middle end (stage-1)

Stage-1 `-v`: 459,614 block parameters created, 425,754 removed. 324,344 (70.6%) are
created inside `c_ir_ssa_finish` by predecessor walks; `c_ir_ssa_finish` is 933.8 M (3.77%).
Locals written once, at their declaration and outside the entry block, account for 194,927
parameters and 51% of the finish-time lookup steps; 1,823 of them survive. A 30-line
`c_gen.c` prototype that forwards same-block declaration initializers (guarded against labels,
case labels and unreachable-to-reachable edges) removed 75,119,698 Ir (0.303%), byte-identical
on stage-1 in four `-fcanonical-*` modes, 714 fixture objects and `string.c`; the repository
gates were not run on it and it is not part of this change. FAST fold/DCE delete 177,638
constants mostly created only to be folded (constant casts, constant truth tests, 8,192
void-call placeholders); the emission-time folding upper bound is about 1.0% and was not
prototyped. `IR_LOCAL_PROMOTION` is not redundant (`-fno-canonical-local-promotion` changes
stage-1 output); #856/#857 stay rejected.
