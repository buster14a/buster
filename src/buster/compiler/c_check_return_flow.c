// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Source return-warning facts for a structurally and semantically checked graph. Calls return unless their visible
// declaration or source callable type proves otherwise. These facts affect only the warning walk, never emission or
// graph validity. The rare callable-type walk shares the reader's typedef/typeof provenance with fallthrough warnings.

#define BUSTER_CHECK_RETURN_STOPS 1u
#define BUSTER_CHECK_RETURN_UNKNOWN 2u

// The shared callee resolver is defined with the target-feature consumer later in c_check_program.c. It reads the
// call's checked designator/overload, not the program's later-populated object-to-declaration table.
static uint32_t check_target_feature_noreturn_object(const CheckProgram* program, uint32_t node, uint32_t* name);
static bool check_target_feature_intrinsic_noreturn(const CheckProgram* program, uint32_t node);
static bool check_target_feature_library_noreturn(const CheckProgram* program, const PPOptions* options, const uint32_t* type_names,
                                                  uint32_t object, uint32_t use);
static bool check_target_feature_file_scope(const Parser* parser, uint32_t token);

typedef struct CheckProgramReturnAnnotation CheckProgramReturnAnnotation;
struct CheckProgramReturnAnnotation
{
    uint32_t declaration;
    uint32_t end;
    uint32_t next;
    uint8_t fact;
};

typedef struct CheckProgramReturnContracts CheckProgramReturnContracts;
struct CheckProgramReturnContracts
{
    const PPOptions* options;
    const uint8_t* names;
    uint32_t* first;
    CheckProgramReturnAnnotation* annotations;
    uint32_t annotation_count;
    uint32_t type_names[2];
    bool library_types;
    bool callable_types;
};

// An annotation's lexical scope end. A file declaration has no enclosing body/block, so its end is the unit's.
static uint32_t check_program_return_scope_end(const Parser* parser, uint32_t token)
{
    const uint32_t count = parser != NULL ? parser->tokens.count : 0;
    uint32_t result = count;
    uint32_t at = token < count ? parser->tree.parents[token] : count;
    for (uint32_t steps = 0; at < count && result == count && steps < count; steps += 1)
    {
        const uint8_t role = parser->tree.roles[at];
        result = role == PARSE_ROLE_CLOSE_BODY || role == PARSE_ROLE_CLOSE_BLOCK || role == PARSE_ROLE_CLOSE_COMPOUND ? at : count;
        at = parser->tree.parents[at];
    }
    return result;
}

// analyzer_noreturn ends Clang's warning CFG but does not set FUNCTION_NORETURN. An ignored noreturn annotation can
// also have reached that flag before its placement was refused. Keep these rare annotations by entity and source
// declaration, including their scope end, so a later or closed sibling declaration cannot change an earlier call.
// The same sparse scan admits the indirect type walk only in units that actually spell a GNU noreturn type.
// Only a token with attributes can have either, so the walk visits the attributes column's tokens in order (check_input_attributed_next: the vector path skips
// 256 tokens without attributes a load, the scalar path, its twin, visits every group).
static bool check_program_return_unknowns(const CheckProgram* program, Arena* scratch, CheckProgramReturnContracts* contracts, bool scalar)
{
    const CheckInput* input = program->input;
    const uint32_t count = input->token_count;
    bool ready = true;
    for (uint32_t group = program->identifiers != NULL && check_sparse_wide_present(input->attributes) ? check_input_attributed_next(input->attributes, 0, count, scalar) : count; ready && group < count;
         group = check_input_attributed_next(input->attributes, group + 16, count, scalar))
    {
        for (uint32_t lanes = check_input_attributed_group(input->attributes, group, count, scalar); ready && lanes != 0; lanes &= lanes - 1)
        {
            const uint32_t token = group + (uint32_t)__builtin_ctz(lanes);
            const CheckInputAttributes* attributes = check_input_attributes_at(input, token);
            const bool function = input->declaration_kinds != NULL && input->declaration_kinds[token] == C_BINDING_FUNCTION;
            uint8_t fact = 0;
            for (uint32_t a = 0; ready && attributes != NULL && a < attributes->count; a += 1)
            {
                const CheckInputAttribute* attribute = attributes->records + a;
                const String8 name = check_input_attribute_spelling(program->identifiers, attribute->name);
                const bool kept = !attribute->ignored && !attribute->subject_ignored;
                const bool analyzer = kept && check_input_attribute_equal(name, "analyzer_noreturn");
                const bool ignored = attribute->kind == CHECK_ATTRIBUTE_NORETURN && (attribute->ignored || attribute->subject_ignored);
                fact |= analyzer ? BUSTER_CHECK_RETURN_STOPS : ignored ? BUSTER_CHECK_RETURN_UNKNOWN : 0;
                contracts->callable_types |= kept && attribute->kind == CHECK_ATTRIBUTE_NORETURN && !attribute->keyword &&
                                             (!attribute->standard || attribute->name_token != attribute->first);
            }
            const uint32_t object = function && input->objects != NULL ? input->objects[token] : 0;
            const bool marked = fact != 0 && object != 0 && object <= count;
            if (marked && contracts->first == NULL)
            {
                contracts->first = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1);
                ready = contracts->first != NULL;
                if (ready)
                {
                    memset(contracts->first, 0xff, ((uint64_t)count + 1) * sizeof(uint32_t));
                }
            }
            if (ready && marked)
            {
                CheckProgramReturnAnnotation* record = BUSTER_ARENA_PUSH_ARRAY(scratch, CheckProgramReturnAnnotation, 1);
                ready = record != NULL;
                if (ready)
                {
                    *record = (CheckProgramReturnAnnotation){ .declaration = token, .end = check_program_return_scope_end(program->parser, token),
                                                              .next = contracts->first[object], .fact = fact };
                    contracts->annotations = contracts->annotation_count == 0 ? record : contracts->annotations;
                    contracts->first[object] = contracts->annotation_count++;
                }
            }
        }
    }
    return ready;
}

// The names that can have a library noreturn contract. This is a candidate filter, not the answer: the call still
// checks its declaration's linkage, the unit's builtin policy and any named types the builtin needs (#1709).
// The scalar answer, and the twin of the table below: a search of the catalogue by name.
static uint8_t check_program_return_library_search(const InternTable* identifiers, uint32_t name)
{
    const BuiltinIntrinsicInfo* info = builtin_intrinsic_lookup((const char*)intern_bytes(identifiers, name), intern_length(identifiers, name));
    const bool unknown = info != NULL && memchr(info->attributes, 'r', info->attributes_length) != NULL;
    return unknown ? BUSTER_CHECK_RETURN_UNKNOWN : 0;
}

// The catalogue's entries with the 'r' attribute, in its order. test-checker's return marks check that these are all
// of them, so a regenerated catalogue that changes the set fails there.
static const BuiltinIntrinsicName check_program_return_library_entries[] =
{
#define BUSTER_CHECK_RETURN_NAME(n) { n, sizeof(n) - 1 },
    BUSTER_CHECK_RETURN_NAME("_Exit")
    BUSTER_CHECK_RETURN_NAME("__builtin_abort")
    BUSTER_CHECK_RETURN_NAME("__builtin_eh_return")
    BUSTER_CHECK_RETURN_NAME("__builtin_longjmp")
    BUSTER_CHECK_RETURN_NAME("__builtin_trap")
    BUSTER_CHECK_RETURN_NAME("__builtin_unreachable")
    BUSTER_CHECK_RETURN_NAME("__builtin_verbose_trap")
    BUSTER_CHECK_RETURN_NAME("__fastfail")
    BUSTER_CHECK_RETURN_NAME("__int2c")
    BUSTER_CHECK_RETURN_NAME("__ud2")
    BUSTER_CHECK_RETURN_NAME("_exit")
    BUSTER_CHECK_RETURN_NAME("_longjmp")
    BUSTER_CHECK_RETURN_NAME("abort")
    BUSTER_CHECK_RETURN_NAME("exit")
    BUSTER_CHECK_RETURN_NAME("longjmp")
    BUSTER_CHECK_RETURN_NAME("siglongjmp")
#undef BUSTER_CHECK_RETURN_NAME
};

// One byte per id of `identifiers`: BUSTER_CHECK_RETURN_UNKNOWN at the ids that spell an entry above, zero elsewhere,
// so a call reads its answer instead of searching the catalogue. NULL if the scratch arena has no room; the calls then
// search, with the same answers.
static uint8_t* check_program_return_library_names(const InternTable* identifiers, Arena* scratch)
{
    const uint32_t count = identifiers != NULL ? identifiers->count : 0;
    uint8_t* names = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, count) : NULL;
    if (names != NULL)
    {
        memset(names, 0, count);
        for (uint32_t e = 0; e < BUSTER_ARRAY_LENGTH(check_program_return_library_entries); e += 1)
        {
            const BuiltinIntrinsicName* entry = check_program_return_library_entries + e;
            const IdentId name = intern_find(identifiers, (const uint8_t*)entry->name, entry->length);
            names[name] = name != 0 && name < count ? BUSTER_CHECK_RETURN_UNKNOWN : names[name];
        }
    }
    return names;
}

// The rare annotations the call's declaration inherits. The first declaration of an entity carries its annotations
// into later declarations even if it was a block extern. An added annotation in a block does not leak to a sibling.
// A later annotation never changes an earlier declaration.
static uint8_t check_program_return_annotation(const CheckProgram* program, const CheckProgramReturnContracts* contracts, uint32_t object, uint32_t declaration)
{
    uint8_t result = 0;
    const uint32_t first = contracts->first != NULL ? contracts->first[object] : BUSTER_CHECK_NONE;
    const bool file = first < contracts->annotation_count && check_target_feature_file_scope(program->parser, declaration);
    for (uint32_t a = first; a < contracts->annotation_count; a = contracts->annotations[a].next)
    {
        const CheckProgramReturnAnnotation* annotation = contracts->annotations + a;
        const bool visible = annotation->declaration <= declaration &&
                             (annotation->declaration + 1 == object || annotation->end >= declaration || file);
        result |= visible ? annotation->fact : 0;
    }
    return result;
}

// Only a library entry whose encoding needs J/SJ asks for these names. The first such call collects them once; calls
// of abort/exit and units without longjmp's named types add no token pass.
static void check_program_return_library_types(const CheckProgram* program, CheckProgramReturnContracts* contracts)
{
    static const char* const names[] = { "jmp_buf", "sigjmp_buf" };
    const CheckInput* input = program->input;
    const Parser* parser = program->parser;
    for (uint32_t n = 0; n < BUSTER_ARRAY_LENGTH(names); n += 1)
    {
        const IdentId name = intern_find(program->identifiers, (const uint8_t*)names[n], (uint32_t)strlen(names[n]));
        for (uint32_t t = 0; name != 0 && contracts->type_names[n] == BUSTER_CHECK_NONE && t < input->token_count; t += 1)
        {
            const bool found = input->declaration_kinds[t] == C_BINDING_TYPEDEF && parser->tokens.names[t] == name &&
                               check_target_feature_file_scope(parser, t);
            contracts->type_names[n] = found ? t : contracts->type_names[n];
        }
    }
    contracts->library_types = true;
}

// A call through a source callable type. Ordinary unannotated units never enter this walk. Each branch of a
// conditional must retain noreturn (a null alternative keeps the other one's type); an explicit cast takes its own
// source type, so casting a noreturn pointer to an ordinary pointer drops the contract. This walks only the callee's
// type-bearing expression, not its evaluated arguments or the enclosing statement.
static uint8_t check_program_return_indirect(const CheckProgram* program, uint32_t root)
{
    const CheckInput* input = program->input;
    const CheckGraph* graph = &input->graph;
    const CheckStream* stream = program->stream;
    const Parser* parser = program->parser;
    const uint32_t count = input->token_count;
    uint32_t pending[64];
    uint32_t depth = 0;
    uint32_t node = root;
    uint8_t result = BUSTER_CHECK_RETURN_STOPS;
    bool walking = true;
    for (uint32_t steps = 0; walking && result != 0 && steps < graph->count; steps += 1)
    {
        const CheckOp op = node < graph->count ? graph->ops[node] : CHECK_OP_COUNT;
        const uint32_t token = node < graph->count ? graph->tokens[node] : count;
        uint32_t next = BUSTER_CHECK_NONE;
        uint32_t declaration = BUSTER_CHECK_NONE;
        if (op == CHECK_OP_DEREFERENCE || op == CHECK_OP_ADDRESS || op == CHECK_OP_SELECT || op == CHECK_OP_CONVERT || op == CHECK_OP_STATEMENT_VALUE ||
            op == CHECK_OP_ASSIGN)
        {
            next = graph->operand0[node];
        }
        else if (op == CHECK_OP_SUBSCRIPT)
        {
            const uint32_t left = graph->operand0[node];
            const uint8_t kind = left < graph->count ? check_action_kind(program->types, check_stream_type(stream, left)) : BUSTER_TYPE_KIND_VOID;
            next = kind == BUSTER_TYPE_KIND_POINTER || type_kind_is_array(kind) ? left : graph->operand1[node];
        }
        else if (op == CHECK_OP_COMMA)
        {
            next = graph->operand1[node];
        }
        else if (op == CHECK_OP_CONDITIONAL)
        {
            const uint32_t yes = graph->operand1[node];
            const uint32_t no = graph->operand2[node];
            const CheckValue yes_value = yes < graph->count ? check_stream_value(stream, yes) : (CheckValue){ 0 };
            const CheckValue no_value = no < graph->count ? check_stream_value(stream, no) : (CheckValue){ 0 };
            const bool yes_null = yes < graph->count && check_action_null(program->types, &yes_value, BUSTER_CHECK_EXTENSION_GNU_CONSTANT);
            const bool no_null = no < graph->count && check_action_null(program->types, &no_value, BUSTER_CHECK_EXTENSION_GNU_CONSTANT);
            next = yes_null ? no : yes;
            if (!yes_null && !no_null && yes != no)
            {
                result = depth < BUSTER_ARRAY_LENGTH(pending) ? result : BUSTER_CHECK_RETURN_UNKNOWN;
                walking = depth < BUSTER_ARRAY_LENGTH(pending);
                if (walking)
                {
                    pending[depth++] = no;
                }
            }
        }
        else if (op == CHECK_OP_VALUE)
        {
            declaration = token < count ? input->declarations[token] : BUSTER_CHECK_NONE;
            if (declaration < count && input->declaration_kinds[declaration] == C_BINDING_FUNCTION)
            {
                // _Noreturn/[[noreturn]] belong to a declaration; an indirect expression keeps only GNU type noreturn.
                result = check_input_fallthrough_noreturn_type(check_input_attributes_at(input, declaration)) ? result : 0;
                declaration = BUSTER_CHECK_NONE;
            }
        }
        else if (op == CHECK_OP_CAST)
        {
            declaration = check_input_token_type_spelling(input, parser, token, true);
            declaration = declaration != 0 ? declaration : BUSTER_CHECK_NONE;
        }
        else if (op == CHECK_OP_MEMBER || op == CHECK_OP_ARROW)
        {
            const uint32_t member = check_semantic_member_of(parser, input, stream, program->types, node);
            declaration = member != 0 ? check_semantic_member_token(parser, input, program->types, member) : BUSTER_CHECK_NONE;
        }
        else if (op == CHECK_OP_CALL)
        {
            uint32_t use = BUSTER_CHECK_NONE;
            const uint32_t object = check_target_feature_noreturn_object(program, node, &use);
            const uint32_t called = object != 0 && use < count ? input->declarations[use] : BUSTER_CHECK_NONE;
            declaration = called < count ? check_input_typedef_spelling(input, parser, called) : BUSTER_CHECK_NONE;
            declaration = declaration != 0 ? declaration : BUSTER_CHECK_NONE;
        }
        else
        {
            result = 0;
        }
        if (next == BUSTER_CHECK_NONE)
        {
            const bool function = op == CHECK_OP_VALUE && token < count && input->declarations[token] < count &&
                                  input->declaration_kinds[input->declarations[token]] == C_BINDING_FUNCTION;
            result = function || (declaration < count && check_input_fallthrough_noreturn_object(input, parser, declaration)) ? result : 0;
            walking = walking && depth != 0;
            node = depth != 0 ? pending[--depth] : BUSTER_CHECK_NONE;
        }
        else
        {
            node = next;
        }
    }
    return walking && result != 0 ? BUSTER_CHECK_RETURN_UNKNOWN : result;
}

static uint8_t check_program_return_call(const CheckProgram* program, uint32_t node, CheckProgramReturnContracts* contracts)
{
    const CheckInput* input = program->input;
    const CheckStream* stream = program->stream;
    uint8_t result = BUSTER_CHECK_RETURN_UNKNOWN;
    const uint32_t overload = check_program_builtin_index(stream->overloads, stream->overload_count, sizeof(CheckOverloadEffect), node);
    uint32_t use = BUSTER_CHECK_NONE;
    const uint32_t object = check_target_feature_noreturn_object(program, node, &use);
    const uint32_t declaration = overload != BUSTER_CHECK_NONE ? stream->overloads[overload].selected.declaration :
                                 input->declarations != NULL && use < input->token_count ? input->declarations[use] : BUSTER_CHECK_NONE;
    const bool direct = object != 0 && input->objects != NULL && input->declaration_kinds != NULL && declaration < input->token_count && input->objects[declaration] == object &&
                        input->declaration_kinds[declaration] == C_BINDING_FUNCTION && input->function_flags != NULL;
    const uint8_t annotation = direct && contracts != NULL ? check_program_return_annotation(program, contracts, object, declaration) : 0;
    const bool known = direct && (annotation & BUSTER_CHECK_RETURN_UNKNOWN) == 0;
    if (known && ((input->function_flags[declaration] & CHECK_INPUT_FUNCTION_NORETURN) != 0 || (annotation & BUSTER_CHECK_RETURN_STOPS) != 0))
    {
        result = BUSTER_CHECK_RETURN_STOPS;
    }
    else if (known && program->identifiers != NULL)
    {
        const uint32_t name = program->parser->tokens.names[declaration];
        result = contracts != NULL && contracts->names != NULL && name < program->identifiers->count ? contracts->names[name] :
                 check_program_return_library_search(program->identifiers, name);
        if (result != 0 && contracts != NULL && contracts->options != NULL && overload == BUSTER_CHECK_NONE)
        {
            const BuiltinIntrinsicInfo* info = builtin_intrinsic_lookup((const char*)intern_bytes(program->identifiers, name), intern_length(program->identifiers, name));
            if (!contracts->library_types && info != NULL && memchr(info->encoding, 'J', info->encoding_length) != NULL)
            {
                check_program_return_library_types(program, contracts);
            }
            result = check_target_feature_library_noreturn(program, contracts->options, contracts->type_names, object, use) ? BUSTER_CHECK_RETURN_STOPS : 0;
        }
        else if (overload != BUSTER_CHECK_NONE)
        {
            result = 0;
        }
    }
    else if (!direct && contracts != NULL && input->declarations != NULL && input->declaration_kinds != NULL && program->parser != NULL)
    {
        result = contracts->callable_types ? check_program_return_indirect(program, input->graph.operand0[node]) : 0;
    }
    return result;
}

static uint8_t check_program_return_fact(const uint8_t* facts, uint32_t count, uint32_t node)
{
    return node < count ? facts[node] : 0;
}

// One node's facts, from its own contract and its operands' and arguments' facts, which precede it: a guaranteed stop
// and a possible unknown control contract. Conditional operands retain only executable arms; an unknown condition
// guarantees a stop only if both alternatives stop. An operand or argument that doesn't precede the node counts for
// nothing.
static uint8_t check_program_return_node(CheckProgram* program, const uint8_t* facts, uint32_t node, CheckProgramReturnContracts* contracts)
{
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    const CheckOp op = graph->ops[node];
    uint8_t result = op == CHECK_OP_CALL ? check_program_return_call(program, node, contracts) : 0;
    if (op == CHECK_OP_BUILTIN_MISC)
    {
        const uint32_t index = check_program_builtin_index(stream->misc, stream->misc_count, sizeof(CheckBuiltinMiscEffect), node);
        result = index == BUSTER_CHECK_NONE || graph->members[node] == CBUILTIN_CALL_WITH_STATIC_CHAIN ? BUSTER_CHECK_RETURN_UNKNOWN :
                 stream->misc[index].terminates ? BUSTER_CHECK_RETURN_STOPS : 0;
    }
    else if (op == CHECK_OP_BUILTIN_X86 && check_target_feature_intrinsic_noreturn(program, node))
    {
        result = BUSTER_CHECK_RETURN_STOPS;
    }
    const uint32_t operands[] = { graph->operand0[node], graph->operand1[node], graph->operand2[node] };
    const bool selection = op == CHECK_OP_CONDITIONAL || op == CHECK_OP_LOGICAL_AND || op == CHECK_OP_LOGICAL_OR;
    if (selection)
    {
        const uint8_t condition = check_program_return_fact(facts, node, operands[0]);
        const uint8_t yes = check_program_return_fact(facts, node, operands[1]);
        const uint8_t no = check_program_return_fact(facts, node, operands[2]);
        bool truth = false;
        const bool known = check_program_runtime_truth(program, node, operands[0], &truth);
        result |= condition;
        if (known)
        {
            result |= op == CHECK_OP_CONDITIONAL ? (truth ? yes : no) : truth != (op == CHECK_OP_LOGICAL_OR) ? yes : 0;
        }
        else
        {
            result |= (yes | no) & BUSTER_CHECK_RETURN_UNKNOWN;
            result |= op == CHECK_OP_CONDITIONAL && (yes & no & BUSTER_CHECK_RETURN_STOPS) != 0 ? BUSTER_CHECK_RETURN_STOPS : 0;
        }
    }
    else
    {
        uint32_t mask = UINT32_MAX;
        if (op == CHECK_OP_VALUE)
        {
            mask = 0;
        }
        else if (op == CHECK_OP_SIZEOF || op == CHECK_OP_ALIGNOF)
        {
            mask = operands[0] < node && (graph->ops[operands[0]] == CHECK_OP_VLA_BOUND || graph->ops[operands[0]] == CHECK_OP_VLA_EXTENT) ? 1u : 0;
        }
        else if (op == CHECK_OP_VARARGS)
        {
            const uint32_t index = graph->argument_offsets[node];
            mask = index < stream->varargs_count && stream->varargs[index].node == node ? stream->varargs[index].evaluated_operands : 0;
        }
        for (uint32_t o = 0; o < BUSTER_ARRAY_LENGTH(operands); o += 1)
        {
            result |= (mask & (1u << o)) != 0 ? check_program_return_fact(facts, node, operands[o]) : 0;
        }
        if (check_graph_has_arguments(op))
        {
            const uint32_t arguments = check_program_argument_mask(program, node);
            for (uint32_t a = 0; a < graph->argument_counts[node]; a += 1)
            {
                const uint32_t argument = graph->argument_nodes[graph->argument_offsets[node] + a];
                // Clang's source CFG visits constant_p's argument and all fpclassify selectors, although their runtime
                // plans can skip them. Its object_size queries visit none. Keep this warning contract separate from
                // check_program_argument_mask, which describes executable effects and is unchanged.
                const bool query = op == CHECK_OP_BUILTIN_QUERY;
                const bool all = op == CHECK_OP_CALL || op == CHECK_OP_BUILTIN_X86 || op == CHECK_OP_BUILTIN_FLOAT ||
                                 (query && graph->members[node] == CBUILTIN_CONSTANT_P);
                const bool evaluated = all || (!query && a < 32 && (arguments & (1u << a)) != 0);
                const uint8_t fact = evaluated ? check_program_return_fact(facts, node, argument) : 0;
                result |= fact;
            }
        }
    }
    return result;
}

// check_program_return_facts' vector path: only a source, a node whose own contract may be a stop or unknown (a call, a
// miscellaneous or x86 builtin, a query), or a node one of whose operands' or arguments' facts isn't zero can have facts,
// so that each node's test reads its operands' facts, a byte each (an absent one the sink's, `facts[count]`, zero), and
// only those nodes take check_program_return_node. A node's operands precede it, so their facts are made; one that doesn't
// precede it has none yet, which check_program_return_node reads as none too. The sources are 16 operators at a time,
// whose bits each block's nodes then take. Every other node's facts are zero, as the scalar path makes them.
static void check_program_return_sparse(CheckProgram* program, uint8_t* facts, CheckProgramReturnContracts* contracts, uint64_t* work)
{
    const CheckGraph* graph = &program->input->graph;
    const uint32_t count = graph->count;
    const bool arguments = graph->argument_offsets != NULL && graph->argument_counts != NULL;
    memset(facts, 0, (uint64_t)count + 1);
    const __m512i call = _mm512_set1_epi32(CHECK_OP_CALL);
    const __m512i query = _mm512_set1_epi32(CHECK_OP_BUILTIN_QUERY);
    const __m512i misc = _mm512_set1_epi32(CHECK_OP_BUILTIN_MISC);
    const __m512i x86 = _mm512_set1_epi32(CHECK_OP_BUILTIN_X86);
    const __m512i limit = _mm512_set1_epi32((int)count);
    for (uint32_t first = 0; first < count; first += 16)
    {
        const __mmask16 live = count - first >= 16 ? (__mmask16)0xffff : (__mmask16)((1u << (count - first)) - 1);
        const __m512i ops = _mm512_maskz_loadu_epi32(live, graph->ops + first);
        const uint32_t sources = _mm512_mask_cmpeq_epi32_mask(live, ops, call) | _mm512_mask_cmpeq_epi32_mask(live, ops, query) |
                                 _mm512_mask_cmpeq_epi32_mask(live, ops, misc) | _mm512_mask_cmpeq_epi32_mask(live, ops, x86);
        // Each operand clamped to the sink, so that its fact is a plain load.
        _Alignas(64) uint32_t operands[3][16];
        _mm512_store_si512(operands[0], _mm512_min_epu32(_mm512_maskz_loadu_epi32(live, graph->operand0 + first), limit));
        _mm512_store_si512(operands[1], _mm512_min_epu32(_mm512_maskz_loadu_epi32(live, graph->operand1 + first), limit));
        _mm512_store_si512(operands[2], _mm512_min_epu32(_mm512_maskz_loadu_epi32(live, graph->operand2 + first), limit));
        const uint32_t lanes = count - first < 16 ? count - first : 16;
        for (uint32_t lane = 0; lane < lanes; lane += 1)
        {
            const uint32_t node = first + lane;
            uint32_t any = (sources >> lane) & 1;
            any |= facts[operands[0][lane]] | facts[operands[1][lane]] | facts[operands[2][lane]];
            if (arguments && check_graph_has_arguments(graph->ops[node]))
            {
                for (uint32_t a = 0; a < graph->argument_counts[node]; a += 1)
                {
                    const uint32_t argument = graph->argument_nodes[graph->argument_offsets[node] + a];
                    any |= facts[argument < count ? argument : count];
                }
            }
            if (any != 0)
            {
                facts[node] = check_program_return_node(program, facts, node, contracts);
                if (work != NULL)
                {
                    *work += 1;
                }
            }
        }
    }
}

// One postorder reduction gives each node a guaranteed stop and a possible unknown control contract
// (check_program_return_node). The scalar path visits every node in order; the vector path
// (check_program_return_sparse), only the nodes that can have facts. Their `count` + 1 entries end with a sink, zero.
static uint8_t* check_program_return_facts(CheckProgram* program, const PPOptions* options, Arena* scratch, uint64_t* work, bool scalar)
{
    (void)scalar;

    const CheckGraph* graph = &program->input->graph;
    const uint32_t count = graph->count;
    uint8_t* facts = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)count + 1) : NULL;
    CheckProgramReturnContracts contracts = { .options = options, .type_names = { BUSTER_CHECK_NONE, BUSTER_CHECK_NONE } };
    const bool ready = (count == 0 || facts != NULL) && check_program_return_unknowns(program, scratch, &contracts, scalar);
    if (!ready)
    {
        check_program_fail(program, CHECK_PROGRAM_RESOURCE, 0);
        facts = NULL;
    }
    // The names' answers are an optimisation only: without them every call searches the catalogue, as before.
    contracts.names = ready && count != 0 ? check_program_return_library_names(program->identifiers, scratch) : NULL;
    if (ready && !BUSTER_SCALAR_PATH(scalar) && count != 0)
    {
        check_program_return_sparse(program, facts, &contracts, work);
    }
    for (uint32_t node = 0; ready && BUSTER_SCALAR_PATH(scalar) && node < count; node += 1)
    {
        facts[node] = check_program_return_node(program, facts, node, &contracts);
        if (work != NULL)
        {
            *work += 1;
        }
    }
    if (ready && count != 0)
    {
        facts[count] = 0;
    }
    return facts;
}
