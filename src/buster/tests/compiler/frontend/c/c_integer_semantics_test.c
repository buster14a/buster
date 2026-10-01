// Included by c_test.c. One integer semantics across the compile-time phases:
// each fixture is a source whose integer answer the parser's typed evaluator,
// the static-assertion route, lowering's constant evaluator, lowering's
// value queries or the constant emitter used to compute differently. The
// arithmetic itself is checked exhaustively by ir_integer_test.c; these pin
// that every phase consumes it with C's conversions.

BUSTER_GLOBAL_LOCAL Target c_integer_semantics_target(void)
{
    // LP64: `-1L < 1U` holds and pointers are 64 bits.
    return (Target){.cpu_arch = CPU_ARCH_X86_64, .cpu_model = CPU_MODEL_BASELINE, .os = OPERATING_SYSTEM_LINUX};
}

BUSTER_GLOBAL_LOCAL u32 c_integer_semantics_diagnostics(Arena* arena, String8 source, CIRLowerResult* lowered_out, CParseResult* parse_out)
{
    CPreprocessResult preprocess = {0};
    CParseResult parse = {0};
    CIRLowerResult lowered = c_test_lower_source(arena, source, S8("integer-semantics.c"), c_integer_semantics_target(), &preprocess, &parse);
    if (lowered_out)
    {
        *lowered_out = lowered;
    }
    if (parse_out)
    {
        *parse_out = parse;
    }
    return (u32)(preprocess.diagnostic_count + parse.diagnostic_count + lowered.diagnostic_count);
}

BUSTER_GLOBAL_LOCAL IrGlobal* c_integer_semantics_global(IrProgram* program, String8 name)
{
    IrGlobal* result = 0;
    for (u32 module_index = 0; program && module_index < program->module_count; module_index += 1)
    {
        IrModule* module = program->modules + module_index;
        for (u32 index = 0; !result && index < module->global_count; index += 1)
        {
            IrSymbol* symbol = ir_symbol_from_id(&program->symbols, module->globals[index].symbol);
            if (symbol && string_equal(symbol->name, name))
            {
                result = module->globals + index;
            }
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult c_test_integer_semantics_agreement(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TemporalArena temporary = scratch_begin(&arguments->arena, 1);
    Arena* arena = temporary.arena;

    // A shift count at or above the promoted left width has no value in
    // every phase: the enumerator (parser) used to accept `1 << 40` as 0 and
    // `1 << 0x100000001LL` as 2 while the static initializer (lowering)
    // refused both.
    String8 rejected[] = {
        S8("enum { E = 1 << 40 };"),
        S8("enum { E = 1 << 0x100000001LL };"),
        S8("enum { E = (unsigned short)0 >> 63 };"),
        S8("static int s = 1 << 40;"),
        S8("static int s = 1 << 0x100000001LL;"),
        S8("_Static_assert((1 << 32) != 0, \"undefined shift\");"),
        S8("enum { E = 1 / 0 };"),
        // A valid assertion's arithmetic is C's (usual arithmetic
        // conversions), not preprocessing's intmax_t (GitHub #1238).
        S8("_Static_assert(-1 < sizeof(int), \"must fail\");"),
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(rejected); index += 1)
    {
        u32 count = c_integer_semantics_diagnostics(arena, rejected[index], 0, 0);
        if (!count)
        {
            BUSTER_TEST_ERROR(S8("accepted: {S8}\n"), rejected[index]);
        }
        BUSTER_TEST(arguments, count != 0);
    }
    CParseResult failed_parse = {0};
    c_integer_semantics_diagnostics(arena, S8("_Static_assert(-1 < sizeof(int), \"must fail\");"), 0, &failed_parse);
    BUSTER_TEST(arguments, failed_parse.diagnostic_count == 1);
    if (failed_parse.diagnostic_count == 1)
    {
        BUSTER_TEST(arguments, failed_parse.diagnostics[0].kind == C_DIAGNOSTIC_STATIC_ASSERT_FAILED);
        BUSTER_STRING_TEST(arguments, failed_parse.diagnostics[0].message, S8("static assertion failed: \"must fail\""));
    }

    String8 accepted[] = {
        S8("_Static_assert(0u - 1 == 4294967295u, \"unsigned wrap\");"),
        S8("_Static_assert(~0U == 4294967295u, \"complement\");"),
        S8("_Static_assert(~32u == 4294967263u, \"complement\");"),
        S8("_Static_assert(2 - 33u == 4294967265u, \"usual conversion\");"),
        S8("_Static_assert(-1L < 1U, \"LP64 usual conversion\");"),
        S8("_Static_assert(0xffffffff + 1 == 0, \"unsigned int literal\");"),
        S8("int main(void) { _Static_assert(0u - 1 == 4294967295u, \"block scope\"); return 0; }"),
        // The typed evaluator's sizeof(identifier) sizes the object a block
        // scope binds, not an outer typedef of the same spelling; the
        // enumerator sends the assertion to the deferred typed check.
        S8("typedef void *string; enum { N = 256 };"
           "int probe(void) { char string[N]; (void)string; _Static_assert(sizeof(string) == N, \"\"); return 0; }"),
        // Signed overflow keeps the wrapped value in every phase and width.
        S8("enum { E = 1 << 31, F = -1 << 1 }; static int s = 1 << 31; _Static_assert(E == -2147483647 - 1 && F == -2, \"\");"),
        S8("enum { E = (-2147483647 - 1) / -1, F = (-9223372036854775807LL - 1) / -1 == (-9223372036854775807LL - 1) };"
           "_Static_assert(E == -2147483647 - 1 && F, \"\");"),
        S8("static long long q = (-9223372036854775807LL - 1) / -1;"),
        // A zero-valued integer constant expression built through a signed
        // widening is a null pointer constant (GitHub #1347).
        S8("int check(void) { int *p = 0; return !(p == ((signed char)-1 + 1)); }"),
        S8("int check(void) { int o = 7; int *p = &o; return !(p != ((short)-3 + 3)); }"),
        S8("typedef union __attribute__((transparent_union)) { int *i; void *v; } pointer_argument;"
           "int accept(pointer_argument p) { return p.i != 0; } int check(void) { return accept((signed char)-1 + 1); }"),
    };
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(accepted); index += 1)
    {
        u32 count = c_integer_semantics_diagnostics(arena, accepted[index], 0, 0);
        if (count)
        {
            BUSTER_TEST_ERROR(S8("rejected: {S8}\n"), accepted[index]);
        }
        BUSTER_TEST(arguments, count == 0);
    }
    // The control: a nonzero integer is still no null pointer constant.
    BUSTER_TEST(arguments, c_integer_semantics_diagnostics(arena, S8("int check(void) { int *p = 0; return p == ((signed char)1 + 1); }"), 0, 0) != 0);

    // A static `(T *)integer` holds the run-time conversion's image: the
    // integer extended by its own signedness to the pointer width.
    CIRLowerResult pointers = {0};
    BUSTER_TEST(arguments, c_integer_semantics_diagnostics(arena, S8("void *p = (void *)-1; void *q = (void *)(unsigned)-1; void *r = (void *)-2L;"),
                                                           &pointers, 0) == 0);
    String8 pointer_names[] = {S8("p"), S8("q"), S8("r")};
    u64 pointer_images[] = {UINT64_MAX, UINT64_C(0xffffffff), UINT64_MAX - 1};
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(pointer_names); index += 1)
    {
        IrGlobal* global = c_integer_semantics_global(pointers.program, pointer_names[index]);
        u64 image = 0;
        bool bytes = global && global->initializer_kind == IR_GLOBAL_INITIALIZER_BYTES && global->bytes.length == 8;
        for (u32 byte = 0; bytes && byte < 8; byte += 1)
        {
            image |= (u64)global->bytes.pointer[byte] << (byte * 8);
        }
        BUSTER_TEST(arguments, bytes && image == pointer_images[index]);
    }

    // `__builtin_choose_expr` reads the whole 128-bit condition.
    CIRLowerResult choose = {0};
    BUSTER_TEST(arguments,
                c_integer_semantics_diagnostics(arena, S8("int pick(void) { return __builtin_choose_expr((unsigned __int128)1 << 64, 11, 22); }"),
                                                &choose, 0) == 0);
    u32 elevens = 0;
    u32 twenty_twos = 0;
    // Every integer constant the producer emits lies in its type's range,
    // including bit-field clear masks built at 64 bits for narrow units.
    CIRLowerResult fields = {0};
    BUSTER_TEST(arguments, c_integer_semantics_diagnostics(arena,
                                                           S8("struct S { unsigned char a : 3, b : 5; unsigned c : 13, d : 19; } s;"
                                                              "void store(int x) { s.b = x; s.d = x; s.a = 1; }"),
                                                           &fields, 0) == 0);
    u32 noncanonical = 0;
    u32 masks = 0;
    CIRLowerResult programs[] = {choose, fields};
    for (u32 program_index = 0; program_index < BUSTER_ARRAY_LENGTH(programs); program_index += 1)
    {
        IrProgram* program = programs[program_index].program;
        for (u32 module_index = 0; program && module_index < program->module_count; module_index += 1)
        {
            IrModule* module = program->modules + module_index;
            for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
            {
                IrFunction* function = module->functions + function_index;
                for (u32 row = 0; row < function->instruction_count; row += 1)
                {
                    IrInstruction* instruction = function->instructions + row;
                    u32 width = ir_integer_type_width(ir_type_from_id(&program->types, instruction->canonical_type));
                    IrInteger value = {0};
                    if (instruction->opcode == IR_OPCODE_CONSTANT_INTEGER && width && ir_integer_constant_decode(instruction, width, &value))
                    {
                        noncanonical += !ir_integer_constant_canonical(instruction, width);
                        elevens += program_index == 0 && value.low == 11;
                        twenty_twos += program_index == 0 && value.low == 22;
                        masks += program_index == 1 && width < 64 && ir_integer_sign_bit(value, width);
                    }
                }
            }
        }
    }
    BUSTER_TEST(arguments, elevens == 1 && twenty_twos == 0);
    BUSTER_TEST(arguments, masks != 0 && noncanonical == 0);
    scratch_end(temporary);
    return result;
}
