# Apple-M1 typed direct-GPR and scalar-integer decode

The typed decode APIs in `aarch64_encoding.h` are the decode half of the
checked-in Arm projections `arm-a64-m1-gpr.generated.h` and
`arm-a64-m1-scalar-integer.generated.h`.  They do not introduce another row or
field database: each generated `arm_row_digest` is joined to exactly one row in
the canonical Arm decoder, and canonical raw decoding supplies fixed-bit and
architectural constraint validation before the family recipe is reversed.

The direct-GPR denominator is 80 canonical rows.  The scalar-integer
denominator is 72 generated rows: 71 defined rows plus the permanently
unallocated `UDF_only_perm_undef` row.  UDF remains executable for encoding as
an architectural test vector, but every typed decode path rejects it.

Form-directed entry points (`*_decode_form`) require the caller-selected
family form.  Word-first entry points (`*_decode`) first call
`buster_aarch64_canonical_decode`, so overlapping canonical words remain owned
by the canonical row selected by specificity and target features; a word is
accepted only when that selected digest belongs to the requested family.

All outputs are staged in bounded local arrays and committed only after a
recipe-specific encode-equality check.  The count pointers are output counts
and the corresponding capacities are checked before any write.  Null,
undersized, malformed, unsupported-feature, reserved, ambiguous, and
aliasing/overlap calls therefore leave every caller output byte-unchanged.
Register 31 roles are reconstructed from the generated ZR/SP metadata.  Scalar
shift and extension modifiers are canonicalized: architectural defaults are
omitted, while non-default values are returned as one present modifier.

The focused test additions enumerate all 80 GPR and all 71 executable scalar
representatives through form-directed and word-first decode, re-encode each
result, exercise UDF fail-closed behavior, and check undersized/malformed
transactionality.  The implementation is bounded C99 and uses no callbacks,
allocation, or recursion.
