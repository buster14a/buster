# Raw #508 Census Import Boundary

The service-side B-to-correctness entry accepts held descriptors for #508's
support declaration, `inputs.tsv`, and `rows.tsv`. It projects every raw object
row into the prepared B rows before importing the matched build. The row
projection checks the complete fixed target, frontend, PIC, and allocator
matrix; zero-based row and group order; fixture path, recipe, and compile
obligation against the authenticated source ledger; and the approved #508
canonical JSON identity digest for each row. The `inputs.tsv` projection joins
every line to the pinned support declaration, validates byte count, SHA-256,
role, and compile obligation, and checks exact path-based `fixture_recipe` and
`fixture_flags` values from #508's approved recipe table. Each held file must
remain a read-only, close-on-exec, single-link regular file owned by root or
the effective user, with no write bits. `rows.tsv` is capped at 64 MiB and
`inputs.tsv` at 1 MiB.

The `buster_hash_64` value is checked as a decimal and covered by the input
file digest, but is not recomputed from source bytes in this importer slice.
The byte count and SHA-256 values are joined to the independently pinned
support declaration.

The expected raw input and row digests come from separate compiled profile
keys, `census-inputs-sha256` and `census-rows-sha256`. The digest in
`BqRetirementPrepared` is checked against the row pin and is never treated as
authority. The raw files are independently hashed and their stable inodes are
checked while read. The same compiled profile independently pins the support
declaration used to derive the subject population, source digests, and compile
obligations.

The production profile
`profiles/native-retirement-performance-v1.blocked` currently contains neither
`census-inputs-sha256` nor `census-rows-sha256`, so the production entry fails
closed before build import. Adding either pin requires the independently
authenticated #508 artifact and a compiled profile update; this change does
not add or infer either pin. The checked-in support declaration currently
contains 411 subjects, which project to 78,912 object rows at 192 rows per
subject.

This boundary authenticates the raw row matrix and identity/configuration
fields represented by #508's approved row identity. It does not authenticate
validator-derived eligibility, `configuration_sha256`, validator receipts,
required checks, or semantic oracles. The checked-in profile and inspected
#508 contract do not provide a validator-output digest or an approved per-row
`configuration_sha256` serializer. Those inputs therefore still require
independent authority and validator replay; the current B caller-provided
eligibility and configuration values remain outside this boundary. The
production recipe remains blocked; this projection is not a full-corpus or
eligibility claim.

The private fixture in `retirement_prepare_tests.c` exercises a positive
three-subject miniature pinned-profile join with a hard-coded independently
computed `inputs.tsv` digest, including a C23 recipe and a retained control.
It rejects missing or altered input pins, an altered prepared row digest, a
caller identity mismatch, descriptors without `FD_CLOEXEC`, changed input
bytes, input recipes or source SHA values even after those files are given new
test-only pins, and raw row mutations to allocator, ordinal, recipe, or fixture
path even when each changed file is given a matching miniature profile digest.
