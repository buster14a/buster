# Meaning preservation investigation — 2026-09-25

## Disposition

Two reproduced constant-evaluator root families were published, not eighteen independent discoveries:

- [#1224: logical folding certifies effectful left operands as constants](https://github.com/buster14a/buster/issues/1224). [Completed independent IR evidence](https://github.com/buster14a/buster/issues/1224#issuecomment-5839018722).
- [#1225: truth conversion confuses scalar places/unknown values with known booleans](https://github.com/buster14a/buster/issues/1225). [Completed independent IR evidence](https://github.com/buster14a/buster/issues/1225#issuecomment-5839023996).

The three nested forms which also execute an unevaluated operand belong partly to the already-tracked preparation root. Their 48 native extra-evaluation observations were [handed to #181's active owner](https://github.com/buster14a/buster/issues/181#issuecomment-5838907405), not filed as three new issues. No production patch or broad rewrite was attempted. All source and generated bindings remain unchanged.

## Source and execution identities

| Role | Commit | Tree |
|---|---|---|
| Initial main / broad screen production | `d9e736e2cf08804a2c603d153e9fb608f18c5c49` | `ae9f274b6768b023fe6e0ef80a161e14b290e0a2` |
| Refreshed main / native confirmation / IR follow-up production | `ade6ac4b6ecb21f30b61b656439bac476c145e2f` | `4c5306221fdb22fccc929b55e333163742de17d0` |
| Broad-screen transport | `5a37714d8b0dd51c690302371f1d7fc2e2e804e5` | `e12beeca85fce9925cc311ba3415d84cb9d187df` |
| Native-confirmation transport | `1fd03ba5ab986e72fac44034fbdc03ff1a7645c8` | `35682e7fcf8285f11e9c27da080af33e34e8557a` |
| IR-follow-up transport | `56e932a36079edc5394ad016be56002d063e5f8a` | `6c8a39cccee6495d6ed458693450f774d62a4db4` |

Main was re-read after the completed experiments and still resolved to `ade6ac4b6ecb21f30b61b656439bac476c145e2f`. These are pinned source executions, not PR test-merge or merge-group evidence. The transport checkout supplies only the experiment. Each correctness job creates a detached worktree at the independently pinned production commit and builds there.

Both production pins have `c_gen.c` blob `ede2de412850975123da3f4a473f017591655a75`. The intervening merge #981 changes shared-promotion work, not this frontend blob; the confirmation nevertheless rebuilt and executed the refreshed pin instead of merely borrowing earlier results.

All executions used standard GitHub-hosted Ubuntu 26.04.1 x86-64. Clang 21.1.8 and GCC 15.2.0 were the independent C references. Each Buster binary was built and executed on the same hosted executor. No copied CPU-specific binary was executed elsewhere. No desktop, SSH, dedicated benchmark host, timing, RSS or PMU experiment was used.

Actual investigation tooling: GPT-6 Astra Pro; connected GitHub for source/history/issues/branches/Actions; container tools for source inspection, archive checksums and result processing; Python standard-library observation scripts and C subjects on GitHub Actions; primary GCC/WG14 documentation. No callable subagent executor was available after capability discovery. The scout roles below are sequential work by one model, **not independent subagents or independent agent review**. Clang/GCC provide independent implementation comparisons.

## Scout ranking and falsification

The scout roles were: map semantic facts and their consumers; derive independent expected outcomes and controls; search current/history for duplicate roots; reserve a different alignment hypothesis. Their deliverables are the source traces in the issues, the source-generating grammar, the full result table below, and the history/ownership disposition.

| Rank after the screen | Hypothesis | Impact / plausible reach | Evidence and falsification cost | Disposition |
|---|---|---|---|---|
| 1 | Known logical result confused with constant eligibility | Silent wrong predicate; guarded runtime fallback loses required effects | Direct source shortcut plus short-circuit asymmetry; two-line discriminator | Deepened to #1224; full native family and independent IR execution |
| 2 | Place/unknown truth confused with known scalar truth | Wrong static initializers and false constant-p answers | Two caller transitions into the same unsafe helper; folded/runtime discriminator | Deepened to #1225; separate from common-arm-type fix #219 |
| Reserved distinct lead | Object/member explicit alignment lost in expression alignment query | Incorrect alignment-query value | Three screen mismatches; typedef-alignment control passes | Preserved as limited scout evidence, not claimed part of either root or a completed investigation |
| Lowered after control | f32 arithmetic retains excess precision | Potential static/runtime disagreement | Known #154 repair found; matched static/runtime cases now pass | Hypothesis falsified for the tested pair; no refile |
| Known/deprioritized | Target shifts/promotions, initializers, VLA/selective preparation | Broad possible reach but many active or repaired roots | Current source and matching issues identify ownership/duplicates | Avoided competing work; nested preparation evidence extended #181 |

The scout also observed a null-pointer constant-global truth mismatch. Its possible pointer/symbol initialization cause was not reduced and is **not** merged into the scalar-integer proof for #1225. Real-package incidence and target-wide prevalence were not measured. A lookup-cost audit's synthetic counts do not establish prevalence of these semantic failures.

The initial screen had 24 sources and four profiles: Clang O0, GCC O0, Buster FAST O0 with each frontend. All 96 compilations succeeded. All 48 reference executions passed; Buster had 22 passing rows, 22 exit-1 rows and four exit-3 rows. Screen alignment cases 19/20 report 4 instead of 64 for aligned local/global int objects, case 21 reports 1 instead of 32 for an aligned char member, while the typedef-aligned case 22 reports 64 correctly. Float cases 23/24 both pass. Replay the exact source with `scout.c` and `run.py` at the screen transport revision.

## Minima and semantic contracts

### #1224

```c
static volatile int value;
int main(void) { return __builtin_constant_p(value && 0); }
```

Expected exit 0, observed 1. The [GCC constant-p contract](https://gcc.gnu.org/onlinedocs/gcc/Other-Builtins.html) requires zero for effectful expressions and leaves the predicate argument unevaluated. A known final zero does not make the live volatile read effect-free. `value && 0` is therefore different from `0 && effect()`.

The fully executed `macro_fallback.c` defines:

```c
#define PICK(e) (__builtin_constant_p(e) ? 0 : ((void)(e), 1))
```

For `PICK(mark() && 0)`, expected value/hits is 1/1; observed 0/0. The missing call is in the subsequently selected runtime fallback, not an obligation to execute the builtin's own operand. `__builtin_choose_expr` gives the same observable failure.

First divergence: `c_ir_constant_apply_binary` at `c_gen.c:45156-45212` turns an unknown live left operand into an integer constant because the right operand fixes the result. `c_ir_emit_prepared_call_step` at 18873-18879 then interprets a known value as constant-p eligibility. Upstream call/volatile-query normalization had correctly retained UNKNOWN. Preserve the invariant: **knowing a result does not prove required evaluation may be discarded**.

### #1225

```c
static const int zero = 0;
static int folded = zero ? 3 : 4;
int main(void) { return folded != 4; }
```

Expected stored value 4, observed 3; expected exit 0, observed 1. This uses the accepted **GNU17 constant-expression extension**: the identifier is not asserted to be an ISO C17 integer constant expression. All three implementations accept this input under the explicit selected dialect.

A different consumer of the same unsafe truth contract:

```c
static volatile int value;
int main(void) { return __builtin_constant_p(!value); }
```

Expected exit 0, observed 1. Ordinary runtime negation is a passing control.

`c_ir_constant_identifier` at 43932-44002 correctly produces an LVALUE with the object's type and symbol. `c_ir_constant_truth` at 43645-43679 interprets LVALUE symbol-validity as truth and UNKNOWN as false. `c_ir_constant_apply_operator` SELECT at 46308-46356 tests the condition without normalization, so the object's identity replaces its stored zero. `c_ir_constant_apply_unary` at 45065-45136 checks UNKNOWN before normalization but not after `c_ir_constant_normalize` at 44062-44078 can produce UNKNOWN; NOT then makes `!truth(UNKNOWN)` into known 1. These are two callers of an unsafe truth-query contract, not two independent discoveries per expression.

Preserve the invariant: **only a known scalar value can justify a known Boolean; places must undergo the appropriate value conversion and unknownness must survive it**. Retain distinct array/function decay and genuine address constants. The already-fixed #219 common result-arm type must remain intact.

Underlying language rules: [WG14 N1570](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf) 5.1.2.3p2, 6.3.2.1p2, 6.5.13/14 and 6.5.15p4. This is the C11 draft, not mislabeled as the published C17 standard; GNU builtin/extension behavior is additionally governed by the selected GNU17 contract. Counter changes are only zero to one, with later full-expression observations. Neither minimum depends on overflow, unspecified evaluation order, aliasing, padding, pointer representation, lifetime escape or a race.

## Full native confirmation

`confirm.py` generated 34 related programs. All **748** native compilations and executions completed: 34 sources times 22 profiles.

- References: Clang/GCC each at O0, O2 and O1+ASan+UBSan with no recovery: **204/204 pass**, including 68 sanitized reference executions.
- Buster: NONE/MIR_STACK/FAST/QUALITY times frontend SSA/memory times O0/O2: **544** executions; 256 pass, 208 exit 1, 80 exit 3. All compilations succeed; no timeout or runtime signal occurs.

These are focused experimental rows, not registered test-suite counts. Eighteen failing source variants include related expressions and duplicate reductions; they are not eighteen bugs. All sixteen Buster profiles have the same per-case outcome below. All six reference profiles exit zero in every row.

| Case | Buster exit | Observed actual / hits | Expected actual / hits |
|---|---:|---|---|
| call_and | 1 | 1 / 0 | 0 / 0 |
| call_or | 1 | 1 / 0 | 0 / 0 |
| volatile_and | 1 | 1 / 0 | 0 / 0 |
| volatile_or | 1 | 1 / 0 | 0 / 0 |
| nested_logic | 3 | 1 / 1 | 0 / 0 |
| arithmetic_consumer | 3 | 1 / 1 | 0 / 0 |
| cast_consumer | 3 | 1 / 1 | 0 / 0 |
| comma_left | 1 | 1 / 0 | 0 / 0 |
| local_volatile | 0 | 0 / 0 | 0 / 0 |
| indirect_call | 0 | 0 / 0 | 0 / 0 |
| predicate_return | 1 | 1 / 0 | 0 / 0 |
| macro_fallback | 3 | 0 / 0 | 1 / 1 |
| choose_expr_fallback | 3 | 0 / 0 | 1 / 1 |
| volatile_conditional | 1 | 1 / 0 | 0 / 0 |
| volatile_not | 1 | 1 / 0 | 0 / 0 |
| volatile_double_not | 1 | 1 / 0 | 0 / 0 |
| conditional_const_zero | 1 | 3 / 0 | 4 / 0 |
| conditional_const_nonzero | 0 | 3 / 0 | 3 / 0 |
| not_const_zero | 0 | 1 / 0 | 1 / 0 |
| runtime_const_zero | 0 | 4 / 0 | 4 / 0 |
| runtime_volatile_conditional | 0 | 9 / 0 | 9 / 0 |
| runtime_volatile_not | 0 | 1 / 0 | 1 / 0 |
| conditional_call_control | 0 | 0 / 0 | 0 / 0 |
| direct_call_control | 0 | 0 / 0 | 0 / 0 |
| comma_control | 0 | 0 / 0 | 0 / 0 |
| direct_volatile_control | 0 | 0 / 0 | 0 / 0 |
| short_circuit_and_control | 0 | 1 / 0 | 1 / 0 |
| short_circuit_or_control | 0 | 1 / 0 | 1 / 0 |
| runtime_call_and_control | 0 | 0 / 1 | 0 / 1 |
| runtime_call_or_control | 0 | 1 / 1 | 1 / 1 |
| literal_conditional_control | 0 | 4 / 0 | 4 / 0 |
| minimal_predicate | 1 | no stdout | expected exit 0 |
| minimal_static_conditional | 1 | no stdout | expected exit 0 |
| minimal_not | 1 | no stdout | expected exit 0 |

The local-volatile and indirect-call passing forms bound the claim: this evaluator refuses those queries earlier. The nested_logic/arithmetic_consumer/cast_consumer rows contain both a wrong predicate result and the separately owned #181 extra-evaluation defect; they are not isolated single-cause witnesses.

## Independent IR evidence

The initial confirmation's sixteen attempted exports were rejected because the observer mistakenly combined native-only `-fverify-codegen` with `-emit-llvm`. This setup failure is retained in the raw confirmation artifact. No semantic claim is based on those rejected commands.

The corrected `export.py` follow-up rebuilt the same production pin and used the supported LLVM invocation. All **16 exports, decodes, links and preprocessing commands succeed**. All six control executions pass. The ten executions of five failing witnesses reproduce their native outcomes through Clang's independent backend.

Both frontend forms of minimal_predicate and minimal_not decode to:

```llvm
@value = internal global i32 0, align 4

define i32 @main() {
  ret i32 1
}
```

Both forms of minimal_static_conditional include:

```llvm
@zero = internal constant i32 0, align 4
@folded = internal global i32 3, align 4
```

Its main loads folded, compares it with 4 and returns 1. Macro_fallback already branches on literal true into the zero branch; the live fallback call is unreachable. Choose_expr_fallback emits no call to mark from main. Both print `actual=0 expected=1 hits=0 expected_hits=1` and exit 3. In contrast, runtime_call_and_control has a live call, prints `actual=0 expected=0 hits=1 expected_hits=1`, and exits 0. The comma and conditional-call predicate controls also pass. The meaning is therefore wrong before Buster native machine selection or allocation, not an allocator artifact.

## Replay

All commands below are for an authorized hosted correctness executor. The production checkout, build driver and IDE must be built on that executor. The documented hosted bootstrap is the Clang exception, not canonical TCC evidence:

```sh
git worktree add --detach /tmp/meaning-production ade6ac4b6ecb21f30b61b656439bac476c145e2f
cd /tmp/meaning-production
clang -Isrc -Wall -Werror -Wno-unused-function -Wno-unused-variable -g build.c -o /tmp/meaning-build
/tmp/meaning-build generate --cc clang --ci --linker DEFAULT
/tmp/meaning-build build --config Release -t ide
```

From the investigation checkout, with BUSTER_IDE pointing to that exact binary:

```sh
"$BUSTER_IDE" cc -target x86_64-linux -std=gnu17 \
  -fwrapv -fno-strict-aliasing -funsigned-char -g0 -O0 \
  -fverify-codegen -fregister-allocator=fast -ffrontend-ssa \
  -fno-machine-fallback tools/investigations/meaning-seams-20260925/witnesses/minimal_predicate.c -o /tmp/probe
/tmp/probe
```

NONE omits `-fno-machine-fallback`; the three MIR allocators retain it. Reference commands use `clang --target=x86_64-linux-gnu` or `gcc -m64`, the same input-language flags and source, without Buster-only options. Sanitized references use `-O1 -fsanitize=address,undefined -fno-sanitize-recover=all`.

Full matrix and the historical initial export failures:

```sh
python3 -B tools/investigations/meaning-seams-20260925/confirm.py "$BUSTER_IDE" /tmp/confirmation
```

The experiment exits nonzero when it observes mismatches; a red job is not infrastructure failure or passing acceptance. It preserves exact argv/status/stdout/stderr and binaries for every row. The subject-language flags are explicitly selected independently of flags used to build Buster.

Corrected independent-IR replay (the committed witnesses directory contains the eight selected subjects):

```sh
python3 -B tools/investigations/meaning-seams-20260925/export.py "$BUSTER_IDE" tools/investigations/meaning-seams-20260925/witnesses /tmp/ir-evidence
```

For a single source, use `ide cc ... -emit-llvm source.c -o module.bc` without native allocator/verification options, then `clang -S -emit-llvm -x ir module.bc -o module.ll`, `clang -O0 module.bc -o program`, and run program.

## Raw retention and identity

| Experiment | Run | Artifact | ZIP SHA-256 | Verified manifest files |
|---|---:|---:|---|---:|
| Broad screen | 36182953864 | 10884514732 | `5aa66338566887bd606134629dbe7e2b83d3ed6e528d7fd4ccf59e2558a78e43` | 690 |
| Native confirmation | 36183831330 | 10885452801 | `0d9278ce16181de95a463858766b9aab150b028ff4347f2633aee3126e7e0bd3` | 5337 |
| Corrected IR | 36184618980 | 10885437414 | `7a8b6711480fccc9dd73209a808390b027fe8581d73e35763ebe43f8e3135845` | 328 |

Compiler SHA-256, by fresh hosted build:

```text
screen:       3d17cf697c95c54af3a17c68a0c6c59732dbbaab9f000de875412e5fd0f2fc6b
confirmation: 57cc0f0ec6876bf8d591ea5eddb663da6cfbb4dc84da00e47bad686eceb20e80
IR follow-up: d3952b9bfd41d3348a13adbd9d452aadcf04ccd82bc33542324dc51b64b706d2
```

The three minimum source SHA-256 values:

```text
minimal_predicate.c:          54f35cd368e66b515f61ed574ae7c3ee7107c54e478219b18ae9e5b81592bc09
minimal_static_conditional.c: b9da3fdddd9d19bbad57b6db16f9e35928c28a39f5308ff3c2460d3c00618221
minimal_not.c:                89ef3e79cd6c0938653238c75f1cbc059edc83b50f17ccad5be1f151670b7797
```

Actions artifacts expire October 25, 2026; retain the ZIPs for complete raw attempts. This committed record preserves the grouped complete native matrix, decisive raw IR and observations, root causes, source generators, exact minima and replay independently of Actions retention. It is not a substitute for the full raw archive when examining individual commands.

## Coverage gap, ownership and limits

Current `c_test.c:26668-26705` tests zero calls and canonical validity for older direct/nested/indirect/comma predicate shapes. A wrong known answer still emits zero calls. `tests/basic_c_builtin_memory.c:43-51` tests result bits for older shapes but not the live-left/right-annihilator seam. Static tests exercise literal/computed-value conditions and #219's result-arm conversions, not the scalar-place truth boundary. Repairs must test result values, constant eligibility and evaluation events separately.

All-state duplicate searches used the full builtin spelling, producer/consumer symbols and symptoms. #180's completed comma/call-preparation repair is preserved. #219's common-result-type repair is present and distinct. #153/#154 shift/float repairs and contemporary initializer, atomic and VLA issues were treated as controls or owned work, not rediscovered. Search coverage does not rule out unpublished or unindexed work.

Only the session-owned `research/meaning-seams-20260925-d9e7-astra` branch was written, with investigation sources and its branch-specific hosted workflow. The active preparation investigator's branch, cutover/deletion/service/admission/deployment work and generated bindings were untouched. No merge, closure, deployment, protection change or production fix occurred. No new project dependency was added; generated subjects are C, and disposable observation scripts use the already installed Python standard library.

Not run: full registered test_all, canonical TCC bootstrap, Debug/sanitized Buster, self-host acceptance, foreign-target execution, historical bisection or representative-package incidence. Sanitized reference programs are not a sanitized Buster build. No speedup, performance acceptance or 9700X qualification is claimed. An eventual production repair requires its own exact-head ordinary suite, sanitizer and self-host validation.