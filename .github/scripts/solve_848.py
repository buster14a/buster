from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one anchor, found {count}")
    return text.replace(old, new, 1)


gen_path = Path("src/buster/lib/compiler/frontend/c/c_gen.c")
gen = gen_path.read_text()
helper_anchor = "BUSTER_C_INTERNAL IrValueId c_ir_emit_stack_save(CIntegerIrBuilder* builder, IrSourceRange source)\n"
helper = r'''// A declaration skipped after a terminator still introduces its automatic
// object for any later label in the same scope.  Materialize only the frame
// owner at the first such use: evaluating the skipped initializer would invent
// execution, while treating a missing owner as a global invents an external
// symbol.  This is the named-label counterpart of switch-prefix locals.
BUSTER_C_INTERNAL bool c_ir_entity_is_ordinary_automatic(CIntegerIrBuilder* builder, CEntity* entity)
{
    bool result = entity && entity->kind == C_ENTITY_LOCAL && !entity->is_static_storage && !entity->is_thread_local &&
                  entity->type.value < builder->parse.type_count && builder->parse.types[entity->type.value].kind != C_TYPE_FUNCTION;
    if (result)
    {
        u32 start = entity->declaration_token_start;
        u32 end = start;
        if (start <= builder->preprocess.token_count && entity->declaration_token_count <= builder->preprocess.token_count - start)
        {
            end = start + entity->declaration_token_count;
        }
        u32 name = entity->declaration_token_plus_one ? entity->declaration_token_plus_one - 1 : UINT32_MAX;
        if (name < end)
        {
            end = name;
        }
        for (u32 index = start; index < end && result; index += 1)
        {
            CToken token = builder->preprocess.tokens[index];
            result = token.kind != C_TOKEN_IDENTIFIER ||
                     !c_token_is_well_known(builder->preprocess.spelling_base, token, C_SYMBOL_WELL_KNOWN_EXTERN);
        }
    }

    return result;
}

BUSTER_C_INTERNAL IrValueId c_ir_emit_deferred_automatic_local(CIntegerIrBuilder* builder, CEntityId entity)
{
    IrValueId result = IR_VALUE_ID_INVALID;
    CEntity* value = entity.value < builder->parse.entity_count ? builder->parse.entities + entity.value : 0;
    CIntegerIrLocal* local = value ? c_ir_find_local_by_entity(builder, entity) : 0;
    if (local)
    {
        result = local->place;
    }
    else if (value)
    {
        u32 declaration_token = value->declaration_token_plus_one ? value->declaration_token_plus_one - 1 : UINT32_MAX;
        IrTypeId local_type = value->type.value < builder->parse.type_count ? builder->c_type_ir_map[value->type.value] : IR_TYPE_ID_INVALID;
        IrType* local_type_value = ir_type_from_id(&builder->program->types, local_type);
        u32 alignment = local_type_value ? local_type_value->layout.alignment : 0;
        String8 rejection = {0};
        bool open_block = builder->function && builder->current_block.value < builder->function->block_count &&
                          !builder->function->blocks[builder->current_block.value].terminated;
        bool valid = declaration_token < builder->preprocess.token_count && local_type_value && local_type_value->layout.resolved &&
                     local_type_value->kind != IR_TYPE_VOID && open_block;
        if (valid)
        {
            valid = c_ir_alignment_evaluate(builder, value->alignment_start, value->alignment_count, alignment, &alignment, 0, &rejection) ==
                    C_IR_ALIGNMENT_RESOLVED;
        }
        if (valid)
        {
            result = c_ir_emit_local(builder, builder->preprocess.tokens[declaration_token], local_type, entity, alignment);
            local = result.value != IR_ID_UNDERLYING_INVALID ? c_ir_find_local_by_entity(builder, entity) : 0;
            c_ir_mark_local_read_only(builder, local);
        }
        if (result.value == IR_ID_UNDERLYING_INVALID && !builder->failure_message.length)
        {
            builder->failure_message = rejection.length
                                           ? rejection
                                           : string_format(builder->arena, S8("could not materialize skipped automatic local '{S8}'"), value->name);
            builder->failure_kind_plus_one = rejection.length ? C_DIAGNOSTIC_INVALID_ALIGNMENT + 1 : 0;
            builder->failure_token_index = declaration_token;
        }
    }

    return result;
}

'''
if "c_ir_emit_deferred_automatic_local" in gen:
    raise SystemExit("frontend helper already present")
gen = replace_once(gen, helper_anchor, helper + helper_anchor, "frontend helper insertion")

global_anchor = "    IrSymbolId symbol = builder->entity_symbols[entity.value];\n    CEntity* entity_value = builder->parse.entities + entity.value;\n"
global_replacement = "    CEntity* entity_value = builder->parse.entities + entity.value;\n    if (c_ir_entity_is_ordinary_automatic(builder, entity_value))\n    {\n        return c_ir_emit_deferred_automatic_local(builder, entity);\n    }\n    IrSymbolId symbol = builder->entity_symbols[entity.value];\n"
gen = replace_once(gen, global_anchor, global_replacement, "ordinary-local fail-closed guard")
gen_path.write_text(gen)

test_path = Path("src/buster/tests/compiler/ir/ir_unreachable_local_test.c")
if test_path.exists():
    raise SystemExit("regression file already exists")
test_path.write_text(r'''// A terminated statement stream skips declarations until the next label.  The
// declarations still own automatic objects, but their initializers do not run.
// Keep the check at canonical IR so it survives removal of either native emitter.
BUSTER_GLOBAL_LOCAL bool ir_unreachable_local_has_data_symbol(IrProgram* program, String8 name)
{
    bool result = false;
    for (u32 index = 0; index < program->symbols.count && !result; index += 1)
    {
        IrSymbol* symbol = program->symbols.symbols + index;
        result = symbol->kind == IR_SYMBOL_DATA &&
                 (string_equal(symbol->name, name) || string_equal(symbol->link_name, name));
    }
    return result;
}

BUSTER_GLOBAL_LOCAL UnitTestResult ir_unreachable_local_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    TargetParseResult parsed_target = target_parse_triple(S8("x86_64-unknown-linux-gnu"));
    BUSTER_TEST(arguments, parsed_target.error == TARGET_PARSE_ERROR_NONE);
    if (parsed_target.error == TARGET_PARSE_ERROR_NONE)
    {
        String8 source = S8(
            "int side_effect(void);\n"
            "int initialized(void) {\n"
            "    goto live;\n"
            "    int hidden = side_effect();\n"
            "dead:\n"
            "    return hidden;\n"
            "live:\n"
            "    return 0;\n"
            "}\n"
            "int uninitialized(void) {\n"
            "    goto live;\n"
            "    int hidden;\n"
            "dead:\n"
            "    return hidden;\n"
            "live:\n"
            "    return 0;\n"
            "}\n"
            "int reachable(int choose) {\n"
            "    int hidden = 7;\n"
            "    return choose ? hidden : 0;\n"
            "}\n");
        for (u32 mode = 0; mode < 2; mode += 1)
        {
            TemporalArena temporary = scratch_begin(&arguments->arena, 1);
            CIRLowerResult lowered = ir_complex_test_lower(temporary.arena, source, parsed_target.target, mode != 0);
            BUSTER_TEST(arguments, lowered.program && !lowered.diagnostic_count && lowered.canonical_ir_certified);
            if (lowered.program && !lowered.diagnostic_count)
            {
                IrProgram* program = lowered.program;
                IrModule* module = program->modules;
                BUSTER_TEST(arguments, ir_validate_canonical_module(program, module).error == IR_VALIDATION_NONE);
                BUSTER_TEST(arguments, !ir_unreachable_local_has_data_symbol(program, S8("hidden")));
                u32 deferred_functions = 0;
                u32 reachable_functions = 0;
                for (u32 index = 0; index < module->function_count; index += 1)
                {
                    IrFunction* function = module->functions + index;
                    bool initialized = string_equal(function->name, S8("initialized"));
                    bool uninitialized = string_equal(function->name, S8("uninitialized"));
                    bool reachable = string_equal(function->name, S8("reachable"));
                    if (initialized || uninitialized)
                    {
                        deferred_functions += 1;
                        BUSTER_TEST(arguments, function->local_count != 0);
                        BUSTER_TEST(arguments, !initialized || ir_test_opcode_count(function, IR_OPCODE_CALL) == 0);
                    }
                    if (reachable)
                    {
                        reachable_functions += 1;
                        BUSTER_TEST(arguments, function->local_count != 0);
                    }
                }
                BUSTER_TEST(arguments, deferred_functions == 2 && reachable_functions == 1);
                IrValidationResult prepared = ir_prepare_canonical_module(program, module, true);
                BUSTER_TEST(arguments, prepared.error == IR_VALIDATION_NONE);
                BUSTER_TEST(arguments, !ir_unreachable_local_has_data_symbol(program, S8("hidden")));
            }
            scratch_end(temporary);
        }
    }
    return result;
}
''')

ir_test_path = Path("src/buster/tests/compiler/ir/ir_test.c")
ir_test = ir_test_path.read_text()
include_anchor = "#include <buster/tests/compiler/ir/ir_complex_value_test.c>\n"
ir_test = replace_once(
    ir_test,
    include_anchor,
    include_anchor + "#include <buster/tests/compiler/ir/ir_unreachable_local_test.c>\n",
    "IR regression include",
)
call_anchor = "    UnitTestResult complex_values = ir_complex_value_tests(arguments);\n"
call = (
    "    UnitTestResult unreachable_locals = ir_unreachable_local_tests(arguments);\n"
    "    result.test_count += unreachable_locals.test_count;\n"
    "    result.succeeded_test_count += unreachable_locals.succeeded_test_count;\n"
    "    UnitTestResult complex_values = ir_complex_value_tests(arguments);\n"
)
ir_test = replace_once(ir_test, call_anchor, call, "IR regression registration")
ir_test_path.write_text(ir_test)
