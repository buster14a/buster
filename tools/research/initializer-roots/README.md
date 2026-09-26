# Initializer function-root census (#1403)

This is a disposable, observational experiment. It changes no production
emission policy and provides no performance acceptance result.

Source anchor: main `ade6ac4b6ecb21f30b61b656439bac476c145e2f`, tree
`4c5306221fdb22fccc929b55e333163742de17d0`.

`c_lower_to_ir_with_options` scans identifiers in file-scope object
initializers and resolves them in the ordinary file-scope namespace. Its
function-reference scan does not exclude member identifiers following `.` or
`->`, although the neighboring object-reference scan does. A live table with
`.trigger = 7` can therefore seed the function worklist for an unrelated static
function named `trigger`. The existing worklist then retains its transitive
dependencies. A renamed member provides an independent source control.

The probe records those marks and replays the existing function closure with
only those member-token marks removed. Alias, outside-body, cleanup,
constructor/destructor and external-definition roots remain. It does not
attempt global dead stripping, change semantic validation, reinterpret other
identifier roles or bypass canonical validation. The wider internal-global
reachability hypothesis remains separate and unmeasured.

The function and body-token counters describe eligibility before signature
checks and body lowering; they are structural upper bounds. Synthetic object
symbol tables independently check actual emitted functions. They are not
instructions saved, elapsed time, resident memory, or an artifact-equivalence
proof for a future production fix.

The workflow supplies the pristine baseline after its fixed-point check. The
runner freezes baseline and counter compilers built serially through the
existing native build driver, restores the pristine source before any measured
input is compiled, and compares output hashes and source metrics for each
identical input. The compiler unity source, repository fixtures, pinned cJSON
and SQLite are real-input controls. Synthetic function chains at fixed sizes
isolate the mechanism; actual function-pointer, alias and constructor roots
must remain live. Native synthetic programs must execute successfully.

Run only on an authorized hosted executor after the baseline fixed point:

```sh
python3 tools/research/initializer-roots/run_probe.py \
  --driver /absolute/path/to/native-build-driver \
  --out /absolute/path/to/evidence \
  --sqlite /absolute/path/to/sqlite-amalgamation-3530400 \
  --cjson /absolute/path/to/cjson
```

`probe.patch` is applied only to the diagnostic build, then reverted. The
source patch must never be merged as production code. The temporary workflow
will be removed after retaining the completed run at an immutable commit.

## Decision rules

- Missing observation records, changed artifacts/metrics for identical inputs,
  failed fixture compilation/execution, or a failed positive-root control
  invalidates the relevant inference.
- A synthetic transitive-chain result establishes mechanism, not real-world
  materiality. Report newly retained functions and body tokens with their full
  per-input denominators.
- If real inputs have negligible newly retained work, reject this subcase as a
  major compiler-throughput opportunity. A small correctness/maintenance repair
  may still be useful under its own scope.
- Any production change must preserve actual function pointers, aliases,
  constructors/destructors, explicit section/retention and assembly obligations,
  diagnostics and self-hosting. An observational replay does not prove that
  removing roots is sufficient to repair symbol publication.
- No wall-time or RSS claim comes from this run. Performance evaluation, if
  justified, requires the approved 9700X matched-build route.

Current progress and final disposition belong on
<https://github.com/buster14a/buster/issues/1403>.
