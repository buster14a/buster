# Executable padding authority (#267)

`assembly_fill_executable_padding` is the shared target-aware policy for both
`assembly_unit_directive_align` and `codegen_generate_canonical_module_attempt`.
Explicit `.p2align` / `.balign` fill bytes remain user data. Non-executable
sections retain zero fill. No target policy is inferred from byte literals.

On x86-64, metadata serially prepares and validates the one-byte NOP before
publishing a ready flag. The hot path is one `memset` per gap. A failed recipe
is never replaced by literal instruction bytes. `prewarm_all_forms` includes
this cache for the parallel test runner and other worker-lane consumers.

On AArch64, the shared encoder derives `A64_OPCODE_NOP`. The existing #228
policy is unchanged: zero bytes up to the next instruction boundary, complete
little-endian NOP words, then zero trailing fragments. Complete-word runs use
geometrically doubled copies rather than repeated general encoding calls.
Generated AArch64 function-entry gaps contain only the partial leading word,
so their prior zero fill is preserved.

The registered `executable_padding_tests` compare every offset 0..31 and gap
0..256 on both targets against independently assembled NOP bytes, including
unaligned caller buffers, guards, zero lengths, unsupported targets, null
storage, and overflowing section offsets. Existing assembly-unit regressions
continue covering directives and explicit fill, including AArch64 fall-through
code that preserves x16 when native execution is available.

Source bytes and returned recipes are not proof of execution on every target;
validation records distinguish native runs from disassembly-only checks.
