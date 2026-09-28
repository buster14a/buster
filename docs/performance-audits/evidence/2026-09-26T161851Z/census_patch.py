#!/usr/bin/env python3
"""Disposable census instrumentation for superlinear-work candidates.

Applies anchor-checked insertions to a pristine checkout of main ade6ac4b and
appends CENSUS_* keys to the existing BUSTER_BENCH_ALLOCATIONS construction
counters, so a counting build reports them as ir_construction.census_* in
-fsource-metrics. Every anchor must occur exactly once; the patch refuses to
apply otherwise. It never changes control flow: each insertion is a single
IR_CONSTRUCTION_RECORD statement, which normal builds preprocess away.

--lenient patches a candidate commit instead: an anchor its change removed
is skipped and reported, and CANDIDATE_EDITS place the same counters on the
replacement code, so base and candidate counts share one meaning.

usage: census_patch.py REPO_ROOT [--lenient]
"""
import sys
from pathlib import Path

COUNTERS = [
    "DBG_SEEDS", "DBG_SEED_SCAN_VISITS",
    "DBGV_FUNCTIONS", "DBGV_BLOCKS", "DBGV_UNRESOLVED", "DBGV_BLOCK_UNRESOLVED_VISITS", "DBGV_PARAMETERS",
    "PREDECL_CALLS", "PREDECL_ENTITY_VISITS",
    "PCE_FIND_CALLS", "PCE_FIND_VISITS", "PCE_CONTAINS_CALLS", "PCE_CONTAINS_VISITS", "PCE_RANGE_CALLS", "PCE_RANGE_VISITS",
    "ENUM_PENDING_CALLS", "ENUM_PENDING_VISITS",
    "TYPE_ALIGN_VISITS",
    "INIT_SLOT_STEPS", "INIT_AT_CALLS", "INIT_AT_STEPS",
    "TNP_ANON_VISITS", "TNP_TAG_CALLS", "TNP_TAG_SEARCHES", "TNP_TAG_VISITS",
    "CLEAR_CALLS", "CLEAR_COMPACTIONS", "CLEAR_COMPACTION_ROWS",
    "AGG_FIELD_PAIRS",
    "AGG_LOOKUP_SCANS", "AGG_LOOKUP_VISITS",
    "GPLACE_MODULE_VISITS", "GPLACE_ENTITY_VISITS",
    "UNPROTO_CALLS", "UNPROTO_VISITS",
    "ASM_SYMBOL_CALLS", "ASM_SYMBOL_VISITS",
    "BIND_AGG_VISITS", "CORE_STEP_DEF_VISITS", "SWITCH_PAIRS",
    "LAYOUT_UNCACHED_CALLS", "LAYOUT_UNCACHED_SEEDS",
    "CONST_REVERSE_ENTITY_VISITS", "FN_FOR_SYMBOL_VISITS", "LABEL_PATH_VISITS",
]

R = "IR_CONSTRUCTION_RECORD"

# (file, anchor, where, text). "before"/"after" place text relative to the
# whole anchor; text lines carry their own indentation.
EDITS = [
    ("src/buster/lib/compiler/debug/debug.c",
     "            IrFunction* module_function = 0;\n", "after",
     f"            {R}(CENSUS_DBG_SEEDS, 1);\n"),
    ("src/buster/lib/compiler/debug/debug.c",
     "                    IrFunction* candidate = input.module->functions + module_index;\n", "after",
     f"                    {R}(CENSUS_DBG_SEED_SCAN_VISITS, 1);\n"),
    ("src/buster/lib/compiler/codegen/machine.c",
     "        u32 debug_count = ir_function->debug_local_count;\n", "after",
     f"        {R}(CENSUS_DBGV_FUNCTIONS, 1);\n"),
    ("src/buster/lib/compiler/codegen/machine.c",
     "        u32 local_slot_count = 8u;\n", "before",
     f"        {R}(CENSUS_DBGV_UNRESOLVED, unresolved_count);\n        {R}(CENSUS_DBGV_BLOCKS, ir_function->block_count);\n"),
    ("src/buster/lib/compiler/codegen/machine.c",
     "            IrCfgBlock const* published = ir_function->published_cfg->blocks + block_index;\n"
     "            for (u32 unresolved_index = 0; unresolved_index < unresolved_count; unresolved_index += 1)\n", "before",
     f"            {R}(CENSUS_DBGV_BLOCK_UNRESOLVED_VISITS, unresolved_count + (ir_function->published_cfg->blocks[block_index].instruction_count ? unresolved_count : 0));\n"
     f"            {R}(CENSUS_DBGV_PARAMETERS, ir_function->published_cfg->blocks[block_index].parameter_count);\n"),
    ("src/buster/lib/compiler/frontend/c/c_gen.c",
     "BUSTER_C_INTERNAL bool c_ir_predeclare_labeled_automatic_locals(CIntegerIrBuilder* builder, u32 start, u32 end)\n{\n", "after",
     f"    {R}(CENSUS_PREDECL_CALLS, 1);\n"),
    ("src/buster/lib/compiler/frontend/c/c_gen.c",
     "        CEntity* value = builder->parse.entities + entity.value;\n"
     "        u32 declaration_token = value->declaration_token_plus_one ? value->declaration_token_plus_one - 1 : UINT32_MAX;\n"
     "        if (value->kind != C_ENTITY_LOCAL || value->is_static_storage || value->is_thread_local || value->is_extern ||\n", "before",
     f"        {R}(CENSUS_PREDECL_ENTITY_VISITS, 1);\n"),
    ("src/buster/lib/compiler/frontend/c/c_gen.c",
     "BUSTER_C_INTERNAL CIrPreparedControlExpression* c_ir_prepared_control_expression_find(CIntegerIrBuilder* builder, u32 open_index)\n{\n", "after",
     f"    {R}(CENSUS_PCE_FIND_CALLS, 1);\n    {R}(CENSUS_PCE_FIND_VISITS, builder->prepared_control_expression_count);\n"),
    ("src/buster/lib/compiler/frontend/c/c_gen.c",
     "BUSTER_C_INTERNAL bool c_ir_prepared_control_expression_contains(CIntegerIrBuilder* builder, u32 token_index)\n{\n", "after",
     f"    {R}(CENSUS_PCE_CONTAINS_CALLS, 1);\n"),
    ("src/buster/lib/compiler/frontend/c/c_gen.c",
     "        CIrPreparedControlExpression* expression = builder->prepared_control_expressions + index;\n"
     "        if (expression->emitted && expression->open_index < token_index && token_index < expression->close_index)\n", "before",
     f"        {R}(CENSUS_PCE_CONTAINS_VISITS, 1);\n"),
    ("src/buster/lib/compiler/frontend/c/c_gen.c",
     "BUSTER_C_INTERNAL bool c_ir_prepared_control_expression_contains_range(CIntegerIrBuilder* builder, u32 start, u32 end)\n{\n", "after",
     f"    {R}(CENSUS_PCE_RANGE_CALLS, 1);\n"),
    ("src/buster/lib/compiler/frontend/c/c_gen.c",
     "        CIrPreparedControlExpression* expression = builder->prepared_control_expressions + index;\n"
     "        if (expression->lowering && expression->open_index < start && expression->close_index >= end)\n", "before",
     f"        {R}(CENSUS_PCE_RANGE_VISITS, 1);\n"),
    ("src/buster/lib/compiler/frontend/c/c_gen.c",
     "                if (builder->parse.types[type_index].definition_start == body_open + 1)\n", "before",
     f"                {R}(CENSUS_TNP_ANON_VISITS, 1);\n"),
    ("src/buster/lib/compiler/frontend/c/c_gen.c",
     "            CScopeId reference_scope = c_parse_scope_for_token(&builder->parse, (CScopeId){0}, index + 1);\n", "after",
     f"            {R}(CENSUS_TNP_TAG_CALLS, 1);\n            {R}(CENSUS_TNP_TAG_SEARCHES, 1);\n"
     f"            {R}(CENSUS_TNP_TAG_VISITS, builder->parse.type_count);\n"),
    ("src/buster/lib/compiler/frontend/c/c_gen.c",
     "            u64 relocation_start = relocation_base + offset;\n", "before",
     f"            {R}(CENSUS_CLEAR_CALLS, 1);\n"),
    ("src/buster/lib/compiler/frontend/c/c_gen.c",
     "                CIrInitializerRelocationExtent kept = {.begin = UINT64_MAX};\n", "after",
     f"                {R}(CENSUS_CLEAR_COMPACTIONS, 1);\n                {R}(CENSUS_CLEAR_COMPACTION_ROWS, *relocation_count);\n"),
    ("src/buster/lib/compiler/frontend/c/c_gen.c",
     "        IrGlobal* global = builder->module->globals + global_index;\n"
     "        if (global->symbol.value != symbol.value)\n", "before",
     f"        {R}(CENSUS_GPLACE_MODULE_VISITS, 1);\n"),
    ("src/buster/lib/compiler/frontend/c/c_gen.c",
     "            CEntity* candidate = builder->parse.entities + candidate_index;\n"
     "            if (candidate->kind != C_ENTITY_OBJECT || candidate->scope.value != 0 || !string_equal(candidate->name, entity_value->name) ||\n", "before",
     f"            {R}(CENSUS_GPLACE_ENTITY_VISITS, 1);\n"),
    ("src/buster/lib/compiler/frontend/c/c_gen.c",
     "        IrType* candidate = builder->program->types.types + type_index;\n"
     "        if (candidate->kind != IR_TYPE_FUNCTION || !candidate->is_variadic || candidate->is_unprototyped || candidate->is_noreturn ||\n", "before",
     f"        {R}(CENSUS_UNPROTO_VISITS, 1);\n"),
    ("src/buster/lib/compiler/frontend/c/c_gen.c",
     "BUSTER_C_INTERNAL IrTypeId c_ir_unprototyped_call_type(CIntegerIrBuilder* builder, IrTypeId return_type, IrValueId* arguments, u32 argument_count)\n{\n", "after",
     f"    {R}(CENSUS_UNPROTO_CALLS, 1);\n"),
    ("src/buster/lib/compiler/frontend/c/c_gen.c",
     "                IrLabelProvenancePath* existing = destination->label_paths + existing_index;\n"
     "                if (existing->offset != offset || existing->size != size)\n", "before",
     f"                {R}(CENSUS_LABEL_PATH_VISITS, 1);\n"),
    ("src/buster/lib/compiler/frontend/c/c_parse.c",
     "    CEntityId ordinary = c_parse_lookup_entity_token(result, preprocess.spelling_base, scope, &token);\n"
     "    // Published identifiers are the common path.", "before",
     f"    {R}(CENSUS_ENUM_PENDING_CALLS, 1);\n"),
    ("src/buster/lib/compiler/frontend/c/c_parse.c",
     "            CEnumMember const* member = result->enum_members + index - 1;\n"
     "            if (member->token_index < token_index", "before",
     f"            {R}(CENSUS_ENUM_PENDING_VISITS, 1);\n"),
    ("src/buster/lib/compiler/frontend/c/c_parse.c",
     "            record = result->type_alignments[index].type_index == type.value ? result->type_alignments + index : 0;\n", "before",
     f"            {R}(CENSUS_TYPE_ALIGN_VISITS, 1);\n"),
    ("src/buster/lib/compiler/frontend/c/c_parse.c",
     "        slot += c_parse_initializer_member_is_slot(result->members + type->member_start + index);\n", "before",
     f"        {R}(CENSUS_INIT_SLOT_STEPS, 1);\n"),
    ("src/buster/lib/compiler/frontend/c/c_parse.c",
     "        u32 current = 0;\n        for (u32 field_index = 0; field_index < type->member_count; field_index += 1)\n", "before",
     f"        {R}(CENSUS_INIT_AT_CALLS, 1);\n"),
    ("src/buster/lib/compiler/frontend/c/c_parse.c",
     "            CMember* member = result->members + type->member_start + field_index;\n"
     "            if (!c_parse_initializer_member_is_slot(member))\n"
     "            {\n                continue;\n            }\n"
     "            if (current++ == slot", "before",
     f"            {R}(CENSUS_INIT_AT_STEPS, 1);\n"),
    ("src/buster/lib/compiler/frontend/c/c_parse.c",
     "                C_AGGREGATE_LOOKUP_COUNT(lookup, fallback_type_count);\n", "after",
     f"                {R}(CENSUS_AGG_LOOKUP_VISITS, 1);\n"),
    ("src/buster/lib/compiler/frontend/c/c_parse.c",
     "            bool scoped = scan_by_scope && scope.value != C_ID_UNDERLYING_INVALID;\n", "after",
     f"            {R}(CENSUS_AGG_LOOKUP_SCANS, 1);\n"),
    ("src/buster/lib/compiler/frontend/c/c_parse.c",
     "            defined = result->types[type_index].definition_start == open + 1;\n", "before",
     f"            {R}(CENSUS_BIND_AGG_VISITS, 1);\n"),
    ("src/buster/lib/compiler/frontend/c/c_parse.c",
     "                if (result->types[index].definition_start == open + 1 &&\n", "before",
     f"                {R}(CENSUS_CORE_STEP_DEF_VISITS, 1);\n"),
    ("src/buster/lib/compiler/frontend/c/c_parse.c",
     "                    bool overlaps = (low ^ sign_bit) <= (highs[previous] ^ sign_bit) && (lows[previous] ^ sign_bit) <= (high ^ sign_bit);\n", "before",
     f"                    {R}(CENSUS_SWITCH_PAIRS, 1);\n"),
    ("src/buster/lib/compiler/frontend/c/c_parse.c",
     "        pending_count = type_count;\n        pending = arena_allocate(arena, u32, pending_count + 1);\n", "before",
     f"        {R}(CENSUS_LAYOUT_UNCACHED_CALLS, 1);\n        {R}(CENSUS_LAYOUT_UNCACHED_SEEDS, type_count);\n"),
    ("src/buster/lib/compiler/frontend/c/c_parse.c",
     "                        CEntity* candidate = &result->entities[entity_index - 1];\n"
     "                        if (candidate->kind == C_ENTITY_TYPEDEF && string_equal(candidate->name, c_token_spelling(preprocess.spelling_base, preprocess.tokens[type_start])))\n", "before",
     f"                        {R}(CENSUS_CONST_REVERSE_ENTITY_VISITS, 1);\n"),
    ("src/buster/lib/compiler/ir/ir.c",
     "                valid = instruction->immediates[previous] != field_index;\n", "before",
     f"                {R}(CENSUS_AGG_FIELD_PAIRS, 1);\n"),
    ("src/buster/lib/compiler/ir/ir.c",
     "            IrFunction* function = module->functions + function_index;\n"
     "            if (function->symbol.value == symbol.value)\n            {\n                return function;\n", "before",
     f"            {R}(CENSUS_FN_FOR_SYMBOL_VISITS, 1);\n"),
    ("src/buster/lib/compiler/codegen/codegen.c",
     "    IrSymbolId result = IR_SYMBOL_ID_INVALID;\n"
     "    for (u32 symbol_index = 0; symbol_index < program->symbols.count && result.value == IR_ID_UNDERLYING_INVALID; symbol_index += 1)\n", "before",
     f"    {R}(CENSUS_ASM_SYMBOL_CALLS, 1);\n"),
    ("src/buster/lib/compiler/codegen/codegen.c",
     "        IrSymbol* symbol = &program->symbols.symbols[symbol_index];\n"
     "        String8 link_name = symbol->link_name.length ? symbol->link_name : symbol->name;\n"
     "        // A type symbol shares its spelling with a struct tag", "before",
     f"        {R}(CENSUS_ASM_SYMBOL_VISITS, 1);\n"),
]


# Candidate replacements of anchored loops. Absent on main; each applies only
# where its anchor occurs exactly once.
CANDIDATE_EDITS = [
    # Tag type-name index (#1297): every call, the calls that still search,
    # and each row the search visits.
    ("src/buster/lib/compiler/frontend/c/c_gen.c",
     "            CTypeId unique = c_parse_aggregate_unique(&builder->parse, kind, tag, &decided);\n", "after",
     f"            {R}(CENSUS_TNP_TAG_CALLS, 1);\n            {R}(CENSUS_TNP_TAG_SEARCHES, !decided);\n"),
    ("src/buster/lib/compiler/frontend/c/c_gen.c",
     "                C_AGGREGATE_TAG_SEARCH_COUNT(builder->parse.aggregate_lookup);\n", "after",
     f"                {R}(CENSUS_TNP_TAG_VISITS, 1);\n"),
]


def insert(root, path, anchor, where, text, required):
    file = root / path
    body = file.read_text()
    count = body.count(anchor)
    if count != 1:
        if required or count > 1:
            raise SystemExit(f"{path}: anchor occurs {count} times: {anchor[:80]!r}")
        return False
    replacement = text + anchor if where == "before" else anchor + text
    file.write_text(body.replace(anchor, replacement))
    return True


def main():
    root = Path(sys.argv[1])
    lenient = "--lenient" in sys.argv[2:]
    header = root / "src/buster/lib/compiler/ir/ir_construction.h"
    lines = header.read_text().split("\n")
    # The construction X-list's final entry: a candidate may have appended
    # its own counters after main's last one.
    ends = [index for index, line in enumerate(lines)
            if line.startswith("    X(") and line.endswith(")") and lines[index - 1].endswith("\\")]
    assert len(ends) == 1, f"construction list end: {ends}"
    lines[ends[0]] += "".join(f" \\\n    X(CENSUS_{name}, census_{name.lower()})" for name in COUNTERS)
    header.write_text("\n".join(lines))
    applied = 0
    for path, anchor, where, text in EDITS:
        if insert(root, path, anchor, where, text, not lenient):
            applied += 1
        else:
            print(f"skipped (anchor removed by candidate): {path}: {text.strip()[:70]}")
    for path, anchor, where, text in CANDIDATE_EDITS:
        if insert(root, path, anchor, where, text, False):
            applied += 1
            print(f"candidate insertion: {path}: {text.strip()[:70]}")
    print(f"applied {applied} insertions and {len(COUNTERS)} counters")


if __name__ == "__main__":
    main()
