"""One-use A04 patch transport, excluded from the implementation branch.

Refuse an unexpected baseline or patch anchor. This does not build or test Buster.
"""
from pathlib import Path
import hashlib

SOURCE = Path('src/buster/tests/compiler/metamorphic/metamorphic_test.c')
DOC = Path('docs/metamorphic-testing.md')

def checked_text(path, expected):
    data = path.read_bytes()
    actual = hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest()
    if actual != expected:
        raise SystemExit(f'{path}: unexpected blob {actual}, expected {expected}')
    return data.decode('utf-8')

def replace(text, old, new, count=1):
    found = text.count(old)
    if found != count:
        raise SystemExit(f'patch anchor expected {count} matches, found {found}: {old[:100]!r}')
    return text.replace(old, new)

code = checked_text(SOURCE, '92670fc262d34119b1dcd3cd802ab47924f31291')
doc = checked_text(DOC, '25a7177d1ae601bf10efe7f4f2aa580610a8afde')
code = replace(code, '// bounded, unsigned-integer grammar and its independently evaluated oracle.', '// bounded unsigned-integer grammar, aggregate/call relations and host oracle.')
code = replace(code, '#define BUSTER_META_TRANSFORM_COUNT 9u', '#define BUSTER_META_TRANSFORM_COUNT 11u')
code = replace(code, '#define BUSTER_META_EMPTY_PASTE 256u', '#define BUSTER_META_EMPTY_PASTE 256u\n#define BUSTER_META_AGGREGATES 512u\n#define BUSTER_META_OUTLINE 1024u\n#define BUSTER_META_REFERENCE_COUNT 4u')
code = replace(code, '    String8 clang;\n    String8 node;', '    String8 clang;\n    String8 gcc;\n    String8 reference_compiler;\n    bool reference_optimized;\n    String8 node;')
code = replace(code, 'BUSTER_GLOBAL_LOCAL String8 meta_source(Arena* arena, MetaSpec spec, u32 mask, bool single_function)', '''BUSTER_GLOBAL_LOCAL u32 meta_target_transform_mask(MetaTarget target, u32 mask)
{
    // The bounded eBPF test interpreter has no local-call execution contract.
    // Retain every other requested relation; report this exclusion in meta_run.
    return target.backend == META_EBPF ? mask & ~BUSTER_META_OUTLINE : mask;
}

BUSTER_GLOBAL_LOCAL String8 meta_source(Arena* arena, MetaSpec spec, u32 mask, bool single_function)''')
code = replace(code, r'''    meta_append(&text, S8("unsigned long long metamorphic(unsigned long long x, unsigned long long y) {\n"));''', r'''    // Match the u64 evaluator by value range, not by an assumed sizeof/ABI.
    meta_append(&text, S8("typedef char meta_u64_width[(~0ULL == 18446744073709551615ULL) ? 1 : -1];\n"));
    String8 product = meta_name(arena, S8("product"), mask);
    if (mask & BUSTER_META_OUTLINE)
    {
        meta_append(&text, string_format(arena,
            S8("unsigned long long {S8}(unsigned long long left, unsigned long long right) {{\nreturn left * right;\n}}\n"), product));
    }
    meta_append(&text, S8("unsigned long long metamorphic(unsigned long long x, unsigned long long y) {\n"));''')
code = replace(code, '    a = meta_operand(arena, a, mask);\n    b = meta_operand(arena, b, mask);', r'''    if (mask & BUSTER_META_AGGREGATES)
    {
        // Explicit nested initialization defines both elements. Observe members,
        // never padding, and keep all reads within the two-element array.
        String8 aggregate = meta_name(arena, S8("aggregate"), mask);
        String8 copy = meta_name(arena, S8("aggregate_copy"), mask);
        meta_append(&text, S8("struct MetaValues { unsigned long long lane[2]; };\n"));
        meta_append(&text, string_format(arena, S8("struct MetaValues {S8} = {{{{ {S8}, {S8} }}}};\n"), aggregate, a, b));
        meta_append(&text, string_format(arena, S8("struct MetaValues {S8} = {S8};\n"), copy, aggregate));
        a = string_format(arena, S8("{S8}.lane[0]"), copy);
        b = string_format(arena, S8("{S8}.lane[1]"), copy);
    }
    a = meta_operand(arena, a, mask);
    b = meta_operand(arena, b, mask);''')
code = replace(code, r'''        meta_append(&text, string_format(arena, S8("{S8} = {S8} + ({S8} * {S8});\n"), value, value, left, right));''', r'''        // Call operands are pure unsigned expressions: their unspecified
        // evaluation order cannot change the result or any observable state.
        String8 term_value = mask & BUSTER_META_OUTLINE ? string_format(arena, S8("{S8}({S8}, {S8})"), product, left, right)
                                                       : string_format(arena, S8("({S8} * {S8})"), left, right);
        meta_append(&text, string_format(arena, S8("{S8} = {S8} + {S8};\n"), value, value, term_value));''')
code = replace(code, '        argv[count++] = reference ? context->clang : context->compiler;', '        argv[count++] = reference ? context->reference_compiler : context->compiler;')
code = replace(code, '        argv[count++] = S8("-O0");', '        argv[count++] = reference && context->reference_optimized ? S8("-O2") : S8("-O0");')
code = replace(code, '    if (context->clang.length)\n    {\n        for (u32 seed_index', r'''    for (u32 reference = 0; !budget_expired && reference < BUSTER_META_REFERENCE_COUNT; reference += 1)
    {
        context->reference_compiler = reference < BUSTER_META_REFERENCE_COUNT / 2 ? context->clang : context->gcc;
        context->reference_optimized = (reference & 1u) != 0;
        String8 optimization = context->reference_optimized ? S8("-O2") : S8("-O0");
        if (!context->reference_compiler.length)
        {
            if (context->full)
                string_print(S8("METAMORPHIC_REFERENCE_UNAVAILABLE compiler={S8} optimization={S8}\n"),
                             reference < BUSTER_META_REFERENCE_COUNT / 2 ? S8("clang") : S8("gcc"), optimization);
            continue;
        }
        u32 reference_start = result.reference_pairs;
        for (u32 seed_index''')
code = replace(code, r'''                    string_print(S8("METAMORPHIC_REFERENCE_FAILURE seed={u32} mask={u32} compiler={S8}\n"), first_seed + seed_index, mask, context->clang);''', r'''                    string_print(S8("METAMORPHIC_REFERENCE_FAILURE seed={u32} mask={u32} compiler={S8} optimization={S8}\n"),
                                 first_seed + seed_index, mask, context->reference_compiler, optimization);''')
code = replace(code, r'''        string_print(S8("METAMORPHIC_REFERENCE pairs={u32} failed={u32} compiler={S8}\n"), result.reference_pairs, result.failures, context->clang);''', r'''        string_print(S8("METAMORPHIC_REFERENCE pairs={u32} failed={u32} compiler={S8} optimization={S8}\n"),
                     result.reference_pairs - reference_start, result.failures, context->reference_compiler, optimization);''')
start = code.index('    for (u32 target_index = 0;', code.index('MetaSummary meta_run('))
before, targets = code[:start], code[start:]
targets = replace(targets, '        for (u32 mode = 0; !budget_expired && mode < CODEGEN_REGISTER_ALLOCATOR_MODE_COUNT; mode += 1)', r'''        u32 target_mask = meta_target_transform_mask(target, transform_mask);
        if (target_mask != transform_mask)
            string_print(S8("METAMORPHIC_TRANSFORMS_UNAVAILABLE target={S8} mask={u32} reason=local-calls-not-interpreted\n"),
                         target.name, transform_mask & ~target_mask);
        for (u32 mode = 0; !budget_expired && mode < CODEGEN_REGISTER_ALLOCATOR_MODE_COUNT; mode += 1)''')
targets = replace(targets, 'u32 mask = transform == BUSTER_META_TRANSFORM_COUNT ? transform_mask : (1u << transform) & transform_mask;', 'u32 mask = transform == BUSTER_META_TRANSFORM_COUNT ? target_mask : (1u << transform) & target_mask;')
code = before + targets
code = replace(code, '        result.clang = executable_resolve_in_path(arena, S8("clang"));', '        result.clang = executable_resolve_in_path(arena, S8("clang"));\n        result.gcc = executable_resolve_in_path(arena, S8("gcc"));')
code = replace(code, 'UnitTestResult metamorphic_tests(UnitTestArguments* arguments)\n{\n    UnitTestResult result = meta_preprocessor_tests(arguments);', r'''BUSTER_GLOBAL_LOCAL UnitTestResult meta_generator_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    u32 seeds[] = {0, 1, 42, UINT32_MAX};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(seeds); index += 1)
    {
        TemporalArena temporary = arena_begin_temporal(arguments->arena);
        MetaSpec spec = meta_spec(seeds[index]);
        String8 base = meta_source(arguments->arena, spec, 0, false);
        BUSTER_TEST(arguments, !string_equal(base, meta_source(arguments->arena, spec, BUSTER_META_AGGREGATES, false)));
        BUSTER_TEST(arguments, !string_equal(base, meta_source(arguments->arena, spec, BUSTER_META_OUTLINE, false)));
        for (u32 transform = 0; transform <= BUSTER_META_TRANSFORM_COUNT; transform += 1)
        {
            u32 mask = transform == BUSTER_META_TRANSFORM_COUNT ? BUSTER_META_ALL_TRANSFORMS : 1u << transform;
            String8 first = meta_source(arguments->arena, spec, mask, false);
            String8 second = meta_source(arguments->arena, meta_spec(seeds[index]), mask, false);
            BUSTER_TEST(arguments, string_equal(first, second));
        }
        // Invalid token pastes are diagnostic-only inputs, never executables.
        // A valid adjacent-token control prevents universal rejection passing.
        String8 valid = string_format(arguments->arena,
            S8("#define JOIN(a,b) a ## b\nint JOIN(v,{u32})(void) {{ return 0; }}\n"), seeds[index]);
        String8 invalid = string_format(arguments->arena,
            S8("#define JOIN(a,b) a ## b\nint f(void) {{ return JOIN(+,{u32}); }}\n"), seeds[index]);
        for (u32 layout = 0; layout < 2; layout += 1)
        {
            CPreprocessResult accepted = c_preprocess(arguments->arena, layout ? meta_layout(arguments->arena, valid) : valid, (CPreprocessOptions){0});
            CPreprocessResult rejected = c_preprocess(arguments->arena, layout ? meta_layout(arguments->arena, invalid) : invalid, (CPreprocessOptions){0});
            BUSTER_TEST(arguments, accepted.error_count == 0);
            BUSTER_TEST(arguments, rejected.error_count != 0);
        }
        arena_set_position(arguments->arena, temporary.position);
    }
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(meta_targets); index += 1)
    {
        u32 expected = meta_targets[index].backend == META_EBPF ? BUSTER_META_ALL_TRANSFORMS & ~BUSTER_META_OUTLINE : BUSTER_META_ALL_TRANSFORMS;
        BUSTER_TEST(arguments, meta_target_transform_mask(meta_targets[index], BUSTER_META_ALL_TRANSFORMS) == expected);
    }
    return result;
}

UnitTestResult metamorphic_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = meta_preprocessor_tests(arguments);
    UnitTestResult generated = meta_generator_tests(arguments);
    result.passed += generated.passed;
    result.failed += generated.failed;''')
# Test-result field names are confirmed against the repository header below;
# avoid assuming the aggregate's public layout in this transport.
header = Path('src/buster/tests/test.h').read_text()
if 'u64 passed;' not in header and 'u32 passed;' not in header:
    # The ordinary result combiner is discovered from current source rather
    # than guessing field names. Abort publication until it is supplied.
    raise SystemExit('confirm UnitTestResult fields before publishing')

# Keep prose adjacent to the existing contracts, rather than adding another guide.
doc = replace(doc, 'Both Clang reference and Buster invocations preserve', 'Both independent reference and Buster invocations preserve')
doc = replace(doc, 'targets. Overflow is unsigned modular arithmetic. There are no external calls,', 'targets; a generated value-range assertion enforces that contract. Overflow is\nunsigned modular arithmetic. Pure local helper calls are allowed; there are no external calls,')
doc = replace(doc, '| 256 | Spell local names through empty-prefix token pasting | `META_NAME(,local)` must expand to the same identifier. This is the permanent issue #220 regression. |', '| 256 | Spell local names through empty-prefix token pasting | `META_NAME(,local)` must expand to the same identifier. This is the permanent issue #220 regression. |\n| 512 | Materialize and copy an aggregate | Explicitly initialize a two-element unsigned array inside a struct, copy the struct, and read only its initialized elements, never padding. |\n| 1024 | Outline products into a helper | A nonrecursive, pure, same-translation-unit call computes the same unsigned product. Argument evaluation order is unobservable. |')
doc = replace(doc, 'The default is ten pairs per seed.', 'The default is twelve pairs per seed and target/mode (eleven for eBPF).')
doc = replace(doc, 'an eight-bit exit status. eBPF checks the same full-width values directly. If\nClang is available, the wider campaign first compiles and executes each generated\npair with Clang. A failed reference check stops the campaign rather than promoting\nan invalid generator case as a Buster compiler bug. Absence of Clang means the\nindependent compiler check and LLVM execution are unavailable, not successful.', 'an eight-bit exit status. eBPF checks the same full-width values directly. The\nwider campaign first compiles and executes each generated pair with available\nClang and GCC commands, each at `-O0` and `-O2`, using C11 and identical semantic\nflags. A failed reference check stops the campaign rather than promoting an\ninvalid generator case as a Buster compiler bug. Missing reference commands are\nreported explicitly, not counted as passes. Missing Clang also prevents LLVM\nexecution. Save both compiler versions: a command named `gcc` may actually be\nApple Clang, so its name alone does not establish an independent implementation.\n\nThe ordinary module also checks deterministic rendering for seeds 0, 1, 42 and\n`UINT32_MAX`, every individual relation and their composition. Seeded valid and\ninvalid token-paste controls run both plain and reformatted, using only\n`c_preprocess`; invalid sources are never linked or executed. These assert the\npreprocessor diagnostic contract, not a runtime result for invalid C.')
doc = replace(doc, 'The eBPF interpreter supports the generated subset and has bounded instruction\nexecution and checked stack accesses.', 'The eBPF interpreter supports the generated subset and has bounded instruction\nexecution and checked stack accesses. It does not implement local calls: only\nmask 1024 is excluded for that row, with an explicit\n`METAMORPHIC_TRANSFORMS_UNAVAILABLE` record. All previously supported relations\nand aggregate materialization still run. This is a coverage exclusion, not an\nexecution pass for the call relation.')
doc = replace(doc, 'across all native allocator modes: 40\npairs, 80 compilations/executions', 'across all native allocator modes: 48\npairs, 96 compilations/executions')
doc = replace(doc, 'subset of mask 511', 'subset of mask 2047')
doc = replace(doc, '`METAMORPHIC_REFERENCE` reports independent-Clang checks.', '`METAMORPHIC_REFERENCE` reports the actual reference command, optimization level\nand pair count; `METAMORPHIC_REFERENCE_UNAVAILABLE` reports missing commands.')
SOURCE.write_text(code)
DOC.write_text(doc)
print('A04 patch applied to exactly two implementation files')
