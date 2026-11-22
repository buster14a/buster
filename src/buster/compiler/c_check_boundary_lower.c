// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// SCAFFOLD (#2850's redesign, #2524's body boundary; not for merge): the lowering's side of c_check_boundary.c. The
// recording of each body's function at its part's end (check_lower_part_body, after the ABI wiring and before
// check_lower_finish, so that its runtime functions' symbols are still the part's provisional ones), and its replay in a
// supplying compile. Included by c_check_program_lower.c before check_lower_part_body.

// How many elements a column of a function has.
typedef enum CheckBoundaryExtent
{
    CHECK_BOUNDARY_EXTENT_ENTRIES,
    CHECK_BOUNDARY_EXTENT_LOCALS,
    CHECK_BOUNDARY_EXTENT_PARAMETERS,
    CHECK_BOUNDARY_EXTENT_CALLS,
    CHECK_BOUNDARY_EXTENT_ARGUMENTS,
    CHECK_BOUNDARY_EXTENT_ONE,
    CHECK_BOUNDARY_EXTENT_X87,
    CHECK_BOUNDARY_EXTENT_VECTOR_WORDS,
    CHECK_BOUNDARY_EXTENT_VECTORS,
    CHECK_BOUNDARY_EXTENT_OPERATIONS,
    CHECK_BOUNDARY_EXTENT_LABELS,
    CHECK_BOUNDARY_EXTENT_TABLE_LABELS,
    CHECK_BOUNDARY_EXTENT_TABLES,
    CHECK_BOUNDARY_EXTENT_LIFETIMES,
} CheckBoundaryExtent;

typedef struct CheckBoundaryColumn CheckBoundaryColumn;
struct CheckBoundaryColumn
{
    uint32_t offset;
    uint32_t bytes;
    uint32_t extent;
};

// Every column of Tier0Function the recording carries, in the file's order. symbol_access is the unit's, which the
// emission gives every function; asm_bodies and call_asm are a refused function's.
#define BUSTER_CHECK_BOUNDARY_COLUMN(field, Type, extent) { (uint32_t)offsetof(Tier0Function, field), (uint32_t)sizeof(Type), extent }
static const CheckBoundaryColumn check_boundary_columns[] = {
    BUSTER_CHECK_BOUNDARY_COLUMN(kinds, uint8_t, CHECK_BOUNDARY_EXTENT_ENTRIES),
    BUSTER_CHECK_BOUNDARY_COLUMN(classes, uint8_t, CHECK_BOUNDARY_EXTENT_ENTRIES),
    BUSTER_CHECK_BOUNDARY_COLUMN(values, uint64_t, CHECK_BOUNDARY_EXTENT_ENTRIES),
    BUSTER_CHECK_BOUNDARY_COLUMN(local_classes, uint8_t, CHECK_BOUNDARY_EXTENT_LOCALS),
    BUSTER_CHECK_BOUNDARY_COLUMN(local_flags, uint8_t, CHECK_BOUNDARY_EXTENT_LOCALS),
    BUSTER_CHECK_BOUNDARY_COLUMN(parameters, AbiLocation, CHECK_BOUNDARY_EXTENT_PARAMETERS),
    BUSTER_CHECK_BOUNDARY_COLUMN(call_targets, SymbolId, CHECK_BOUNDARY_EXTENT_CALLS),
    BUSTER_CHECK_BOUNDARY_COLUMN(call_first, uint32_t, CHECK_BOUNDARY_EXTENT_CALLS),
    BUSTER_CHECK_BOUNDARY_COLUMN(call_argument_counts, uint32_t, CHECK_BOUNDARY_EXTENT_CALLS),
    BUSTER_CHECK_BOUNDARY_COLUMN(call_variadic, uint8_t, CHECK_BOUNDARY_EXTENT_CALLS),
    BUSTER_CHECK_BOUNDARY_COLUMN(calls, AbiCall, CHECK_BOUNDARY_EXTENT_CALLS),
    BUSTER_CHECK_BOUNDARY_COLUMN(argument_locations, AbiLocation, CHECK_BOUNDARY_EXTENT_ARGUMENTS),
    BUSTER_CHECK_BOUNDARY_COLUMN(argument_classes, uint8_t, CHECK_BOUNDARY_EXTENT_ARGUMENTS),
    BUSTER_CHECK_BOUNDARY_COLUMN(local_sizes, uint32_t, CHECK_BOUNDARY_EXTENT_LOCALS),
    BUSTER_CHECK_BOUNDARY_COLUMN(local_alignments, uint8_t, CHECK_BOUNDARY_EXTENT_LOCALS),
    BUSTER_CHECK_BOUNDARY_COLUMN(incoming_variadic, AbiCall, CHECK_BOUNDARY_EXTENT_ONE),
    BUSTER_CHECK_BOUNDARY_COLUMN(argument_sizes, uint32_t, CHECK_BOUNDARY_EXTENT_ARGUMENTS),
    BUSTER_CHECK_BOUNDARY_COLUMN(call_result_objects, uint32_t, CHECK_BOUNDARY_EXTENT_CALLS),
    BUSTER_CHECK_BOUNDARY_COLUMN(x87_constants, uint64_t, CHECK_BOUNDARY_EXTENT_X87),
    BUSTER_CHECK_BOUNDARY_COLUMN(vector_constants, uint64_t, CHECK_BOUNDARY_EXTENT_VECTOR_WORDS),
    BUSTER_CHECK_BOUNDARY_COLUMN(vector_constant_bytes, uint8_t, CHECK_BOUNDARY_EXTENT_VECTORS),
    BUSTER_CHECK_BOUNDARY_COLUMN(operations, Tier0Operation, CHECK_BOUNDARY_EXTENT_OPERATIONS),
    BUSTER_CHECK_BOUNDARY_COLUMN(label_entries, uint32_t, CHECK_BOUNDARY_EXTENT_LABELS),
    BUSTER_CHECK_BOUNDARY_COLUMN(label_tables, uint32_t, CHECK_BOUNDARY_EXTENT_TABLE_LABELS),
    BUSTER_CHECK_BOUNDARY_COLUMN(table_jumps, uint32_t, CHECK_BOUNDARY_EXTENT_TABLES),
    BUSTER_CHECK_BOUNDARY_COLUMN(table_addresses, uint32_t, CHECK_BOUNDARY_EXTENT_TABLES),
    BUSTER_CHECK_BOUNDARY_COLUMN(call_tails, uint8_t, CHECK_BOUNDARY_EXTENT_CALLS),
    BUSTER_CHECK_BOUNDARY_COLUMN(lifetime_entries, uint32_t, CHECK_BOUNDARY_EXTENT_LIFETIMES),
    BUSTER_CHECK_BOUNDARY_COLUMN(lifetime_locals, uint32_t, CHECK_BOUNDARY_EXTENT_LIFETIMES),
};
#undef BUSTER_CHECK_BOUNDARY_COLUMN

static uint64_t check_boundary_extent(const Tier0Function* function, uint32_t extent)
{
    switch (extent)
    {
        case CHECK_BOUNDARY_EXTENT_ENTRIES: return function->count;
        case CHECK_BOUNDARY_EXTENT_LOCALS: return function->local_count;
        case CHECK_BOUNDARY_EXTENT_PARAMETERS: return function->parameter_count;
        case CHECK_BOUNDARY_EXTENT_CALLS: return function->call_count;
        case CHECK_BOUNDARY_EXTENT_ARGUMENTS: return function->argument_count;
        case CHECK_BOUNDARY_EXTENT_ONE: return 1;
        case CHECK_BOUNDARY_EXTENT_X87: return 2 * (uint64_t)function->x87_constant_count;
        case CHECK_BOUNDARY_EXTENT_VECTOR_WORDS: return 8 * (uint64_t)function->vector_constant_count;
        case CHECK_BOUNDARY_EXTENT_VECTORS: return function->vector_constant_count;
        case CHECK_BOUNDARY_EXTENT_OPERATIONS: return function->operation_count;
        case CHECK_BOUNDARY_EXTENT_LABELS: return function->label_count;
        case CHECK_BOUNDARY_EXTENT_TABLE_LABELS: return function->table_count != 0 ? function->label_count : 0;
        case CHECK_BOUNDARY_EXTENT_TABLES: return function->table_count;
        default: return function->lifetime_count;
    }
}

// The file's row of a body: its tokens, why it isn't supplied if it isn't, and where its columns are in the blob.
typedef struct CheckBoundaryRow CheckBoundaryRow;
struct CheckBoundaryRow
{
    uint32_t opening;
    uint32_t closing;
    uint32_t declaration;
    uint32_t refusal;
    uint32_t entries;
    uint32_t reserved;
    uint64_t offset;
};

// The recording's state over a unit's parts: each of the program's bodies' flags, found once at the first part, and the
// writer's runtime count at each body's start.
typedef struct CheckBoundaryUnit CheckBoundaryUnit;
struct CheckBoundaryUnit
{
    const CheckProgram* program;
    // Per body of the program: a floating builtin of it may set errno (bit 0); an asm statement is in it (bit 1), whose
    // number among the unit's names its helper whether or not its function has a row for it.
    uint8_t* errno_bodies;
    // Per string record of the reader: its body + 1, or 0 for one outside every body.
    uint32_t* string_bodies;
    // The writer's runtime functions' count before each body, and after the part's last one.
    uint32_t* runtime_marks;
    CheckBoundaryRow* rows;
    uint32_t row_count;
    // The census, per body of the program: the expression ops its nodes have (a bit an op, two words) and their count.
    uint64_t* op_masks;
    uint32_t* node_counts;
};

static CheckBoundaryUnit check_boundary_unit;
// A recording's bodies by name, for a person: a line a body beside the recording (its tokens, refusal, entries, name).
static FILE* check_boundary_names;

// The program's body that holds `token`, or BUSTER_CHECK_NONE: the bodies are in their openings' order.
static uint32_t check_boundary_body_of(const CheckProgram* program, uint32_t token)
{
    uint32_t low = 0;
    uint32_t high = program->body_count;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        if (program->bodies[middle].opening <= token)
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }
    const uint32_t body = low - 1;
    return low != 0 && token <= program->parser->tree.parents[program->bodies[body].opening] ? body : BUSTER_CHECK_NONE;
}

// A unit's start (check_boundary_begin): nothing of the unit before it.
static void check_boundary_unit_reset(void)
{
    free(check_boundary_unit.errno_bodies);
    free(check_boundary_unit.string_bodies);
    free(check_boundary_unit.runtime_marks);
    free(check_boundary_unit.rows);
    free(check_boundary_unit.op_masks);
    free(check_boundary_unit.node_counts);
    check_boundary_unit = (CheckBoundaryUnit){ 0 };
    if (check_boundary_names != NULL)
    {
        fclose(check_boundary_names);
        check_boundary_names = NULL;
    }
}

static void check_boundary_unit_prepare(const CheckProgram* program)
{
    if (check_boundary_unit.program != program)
    {
        check_boundary_unit_reset();
        check_boundary_unit.program = program;
        const CheckInput* input = program->input;
        const CheckStream* stream = program->stream;
        check_boundary_unit.errno_bodies = calloc((uint64_t)program->body_count + 1, 1);
        check_boundary_unit.string_bodies = calloc((uint64_t)input->string_count + 1, sizeof(uint32_t));
        check_boundary_unit.runtime_marks = calloc((uint64_t)program->body_count + 2, sizeof(uint32_t));
        check_boundary_unit.rows = calloc((uint64_t)program->body_count + 1, sizeof(CheckBoundaryRow));
        for (uint32_t k = 0; stream != NULL && check_boundary_unit.errno_bodies != NULL && k < stream->floating_count; k += 1)
        {
            const uint32_t body = stream->floating[k].may_set_errno ? check_boundary_body_of(program, input->graph.tokens[stream->floating[k].node]) : BUSTER_CHECK_NONE;
            if (body != BUSTER_CHECK_NONE)
            {
                check_boundary_unit.errno_bodies[body] |= 1;
            }
        }
        for (uint32_t s = 0; check_boundary_unit.errno_bodies != NULL && s < input->asm_count; s += 1)
        {
            const uint32_t body = check_boundary_body_of(program, input->asm_statements[s].token);
            if (body != BUSTER_CHECK_NONE)
            {
                check_boundary_unit.errno_bodies[body] |= 2;
            }
        }
        for (uint32_t s = 0; check_boundary_unit.string_bodies != NULL && s < input->string_count; s += 1)
        {
            const uint32_t body = check_boundary_body_of(program, input->strings[s].token);
            check_boundary_unit.string_bodies[s] = body != BUSTER_CHECK_NONE ? body + 1 : 0;
        }
        // BUSTER_BOUNDARY_NAME_OF=TOKEN[,TOKEN...]: those tokens' spellings beside the recording, for a person.
        const char* name_of = getenv("BUSTER_BOUNDARY_NAME_OF");
        if (name_of != NULL && program->identifiers != NULL)
        {
            char names[800];
            snprintf(names, sizeof(names), "%s.tokens", check_boundary.path);
            FILE* file = fopen(names, "w");
            while (file != NULL && *name_of != 0)
            {
                char* end = NULL;
                const uint32_t origin = (uint32_t)strtoul(name_of, &end, 10);
                for (uint32_t token = 0; token < input->token_count; token += 1)
                {
                    if (parse_origin(program->parser, token) == origin)
                    {
                        const uint32_t identifier = program->parser->tokens.names[token];
                        fprintf(file, "%u\t%s\n", origin, identifier != 0 && identifier < program->identifiers->count ? (const char*)intern_bytes(program->identifiers, identifier) : "?");
                    }
                }
                name_of = *end == ',' ? end + 1 : end;
                if (end == name_of && *end != 0)
                {
                    break;
                }
            }
            if (file != NULL)
            {
                fclose(file);
            }
        }
        check_boundary_unit.op_masks = calloc(2 * ((uint64_t)program->body_count + 1), sizeof(uint64_t));
        check_boundary_unit.node_counts = calloc((uint64_t)program->body_count + 1, sizeof(uint32_t));
        for (uint32_t node = 0; check_boundary_unit.op_masks != NULL && check_boundary_unit.node_counts != NULL && node < input->graph.count; node += 1)
        {
            const uint32_t body = check_boundary_body_of(program, input->graph.tokens[node]);
            const uint32_t op = (uint32_t)input->graph.ops[node];
            if (body != BUSTER_CHECK_NONE && op < 128)
            {
                check_boundary_unit.op_masks[2 * (uint64_t)body + (op >> 6)] |= UINT64_C(1) << (op & 63);
                check_boundary_unit.node_counts[body] += 1;
            }
        }
    }
}

// The recording names every token by its index in the unit's own parse (parse_origin): a compile of several units reads
// each through a view that leaves its attested declarations' tokens out, and a supplying compile, whose mask holds the
// view off, reads the parse itself. An object is its token + 1.
static uint32_t check_boundary_origin(const CheckProgram* program, uint32_t token)
{
    return parse_origin(program->parser, token);
}

static uint32_t check_boundary_object_origin(const CheckProgram* program, uint32_t object)
{
    return object != 0 ? parse_origin(program->parser, object - 1) + 1 : 0;
}

// What the function's name `symbol` is to the recording: a fix-up for an object's or a runtime function's (true), or the
// refusal of any other.
static bool check_boundary_classify(const CheckLowerShared* shared, SymbolId symbol, uint32_t table_first, uint32_t table_count, CheckBoundaryFixup* fixup,
                                    uint32_t* refusal)
{
    const CheckProgram* program = shared->program;
    const CheckProgramLowered* lowered = shared->lowered;
    const uint64_t helpers = (uint64_t)program->symbol_count + 1;
    const uint64_t pools = helpers + lowered->helpers.count;
    bool named = false;
    if (symbol != 0 && symbol <= program->symbol_count)
    {
        fixup->name = CHECK_BOUNDARY_NAME_OBJECT;
        fixup->payload = check_boundary_object_origin(program, program->symbol_objects[symbol - 1]);
        named = true;
    }
    else if (symbol >= helpers && symbol < pools)
    {
        *refusal = CHECK_BOUNDARY_HELPER;
    }
    else if (symbol >= pools && symbol < pools + lowered->initializers.count)
    {
        *refusal = CHECK_BOUNDARY_POOL;
    }
    else if (symbol >= shared->provisional_runtimes && symbol < shared->provisional_runtimes + CHECK_LOWER_RUNTIMES)
    {
        fixup->name = CHECK_BOUNDARY_NAME_RUNTIME;
        fixup->payload = symbol - shared->provisional_runtimes;
        named = true;
        if (fixup->payload == CHECK_LOWER_RUNTIME_ERRNO)
        {
            // errno's function is numbered first of the runtime's wherever a builtin of the unit may set it.
            *refusal = CHECK_BOUNDARY_ERRNO;
            named = false;
        }
    }
    else if (symbol >= shared->provisional_tables && (uint64_t)symbol - shared->provisional_tables - table_first < table_count)
    {
        fixup->name = CHECK_BOUNDARY_NAME_TABLE;
        fixup->payload = symbol - shared->provisional_tables - table_first;
        named = true;
    }
    else if (symbol >= shared->provisional_tables)
    {
        *refusal = CHECK_BOUNDARY_TABLE;
    }
    else if (symbol != 0)
    {
        *refusal = CHECK_BOUNDARY_SYMBOL;
    }
    return named;
}

// A part's bodies recorded, after its ABI wiring: each one's row, and for a supplied one its lists and its function in
// the blob. `runtimes` are the writer's, in the part's first calls' order.
static void check_boundary_record_part(const CheckLowerShared* shared, const CheckLowerPart* part, const uint16_t* runtimes, uint32_t runtime_count, bool sources)
{
    const CheckProgram* program = shared->program;
    const CheckProgramLowered* lowered = shared->lowered;
    const CheckInput* input = program->input;
    check_boundary_unit_prepare(program);
    CheckBoundaryUnit* unit = &check_boundary_unit;
    const bool ready = unit->errno_bodies != NULL && unit->string_bodies != NULL && unit->runtime_marks != NULL && unit->rows != NULL;
    for (uint32_t b = part->first; ready && b < part->end; b += 1)
    {
        const CheckProgramBody body = program->bodies[b];
        const Tier0Function* function = &lowered->functions[b];
        const uint32_t closing = program->parser->tree.parents[body.opening];
        const CheckProgramNaked* naked = lowered->asms.naked != 0 ? lowered->asms.naked + b : 0;
        uint32_t refusal = CHECK_BOUNDARY_SUPPLIED;
        refusal = sources ? CHECK_BOUNDARY_SOURCES : refusal;
        refusal = refusal == 0 && (function->asm_body_count != 0 || function->call_asm != NULL || (naked != NULL && naked->planned)) ? CHECK_BOUNDARY_ASM : refusal;
        // Its jump tables: the program's whose function it is, one after another. Its labels are theirs alone, or one is a
        // label whose address the code takes.
        uint32_t table_first = 0;
        while (table_first < lowered->table_count && lowered->tables[table_first].function != b)
        {
            table_first += 1;
        }
        uint32_t table_count = 0;
        uint32_t table_labels = 0;
        uint64_t table_values = 0;
        while (table_first + table_count < lowered->table_count && lowered->tables[table_first + table_count].function == b)
        {
            table_labels += lowered->tables[table_first + table_count].label_count;
            table_values += lowered->tables[table_first + table_count].count;
            table_count += 1;
        }
        refusal = refusal == 0 && (function->label_count != table_labels || function->table_count != table_count) ? CHECK_BOUNDARY_LABELS : refusal;
        refusal = refusal == 0 && function->call_tails != NULL ? CHECK_BOUNDARY_TAILS : refusal;
        refusal = refusal == 0 && (unit->errno_bodies[b] & 2) != 0 ? CHECK_BOUNDARY_ASM : refusal;
        refusal = refusal == 0 && (unit->errno_bodies[b] & 1) != 0 ? CHECK_BOUNDARY_ERRNO : refusal;
        // Its tokens that are objects with symbols: its string literals' alone may be.
        for (uint32_t token = body.opening; refusal == 0 && token <= closing; token += 1)
        {
            if (program->object_symbols[token + 1] != 0)
            {
                const uint32_t string = check_sparse_get(input->string_ids, token);
                const bool literal = string != BUSTER_CHECK_NONE && string < input->string_count && input->strings[string].object == token + 1 && !input->strings[string].predefined;
                refusal = literal ? refusal : CHECK_BOUNDARY_OBJECT;
            }
            const uint32_t name = program->parser->tokens.names[token];
            const bool storage = program->parser->tree.roles[token] == PARSE_ROLE_SPECIFIER && (name == C_NAME_STATIC || name == C_NAME_EXTERN);
            refusal = refusal == 0 && storage ? CHECK_BOUNDARY_DECLARATION : refusal;
        }
        // A function's name's array among its strings (__func__ and its kin), which the reader makes by the function's own
        // caches.
        for (uint32_t s = 0; refusal == 0 && s < input->string_count; s += 1)
        {
            refusal = unit->string_bodies[s] == b + 1 && input->strings[s].predefined ? CHECK_BOUNDARY_OBJECT : refusal;
        }
        // Its names: the entries' of the global kinds, the calls' targets and the hidden copy.
        const uint64_t mark = check_boundary.length;
        uint32_t fixup_count = 0;
        CheckBoundaryFixup* fixups = refusal == 0 ? malloc(((uint64_t)function->count + function->call_count + 2) * sizeof(CheckBoundaryFixup)) : NULL;
        for (uint32_t i = 0; fixups != NULL && refusal == 0 && i < function->count; i += 1)
        {
            CheckBoundaryFixup fixup = { .place = CHECK_BOUNDARY_PLACE_VALUE, .index = i };
            if (tier0_global_kind(function->kinds[i]) && check_boundary_classify(shared, (SymbolId)function->values[i], table_first, table_count, &fixup, &refusal))
            {
                fixups[fixup_count++] = fixup;
            }
        }
        for (uint32_t c = 0; fixups != NULL && refusal == 0 && c < function->call_count; c += 1)
        {
            CheckBoundaryFixup fixup = { .place = CHECK_BOUNDARY_PLACE_CALL, .index = c };
            if (check_boundary_classify(shared, function->call_targets[c], table_first, table_count, &fixup, &refusal))
            {
                fixups[fixup_count++] = fixup;
            }
        }
        if (fixups != NULL && refusal == 0)
        {
            CheckBoundaryFixup fixup = { .place = CHECK_BOUNDARY_PLACE_HIDDEN_COPY };
            if (check_boundary_classify(shared, function->hidden_copy, table_first, table_count, &fixup, &refusal))
            {
                fixups[fixup_count++] = fixup;
            }
        }
        if (refusal == 0 && fixups != NULL)
        {
            uint32_t* references = malloc(((uint64_t)body.reference_count + 1) * sizeof(uint32_t));
            for (uint32_t r = 0; references != NULL && r < body.reference_count; r += 1)
            {
                references[r] = check_boundary_object_origin(program, program->references[body.reference_first + r]);
            }
            check_boundary_write(references, references != NULL ? body.reference_count : 0, sizeof(uint32_t));
            free(references);
            uint32_t string_count = 0;
            for (uint32_t s = 0; s < input->string_count; s += 1)
            {
                string_count += unit->string_bodies[s] == b + 1;
            }
            CheckBoundaryString* strings = malloc(((uint64_t)string_count + 1) * sizeof(CheckBoundaryString));
            uint32_t written = 0;
            for (uint32_t s = 0; strings != NULL && s < input->string_count; s += 1)
            {
                if (unit->string_bodies[s] == b + 1)
                {
                    strings[written++] = (CheckBoundaryString){ .token = check_boundary_origin(program, input->strings[s].token), .index = s };
                }
            }
            check_boundary_write(strings, strings != NULL ? string_count : 0, sizeof(CheckBoundaryString));
            free(strings);
            // The runtime functions the part first called in this body, in that order.
            const uint32_t first = unit->runtime_marks[b];
            const uint32_t end = b + 1 < part->end ? unit->runtime_marks[b + 1] : runtime_count;
            uint32_t called[CHECK_LOWER_RUNTIMES];
            for (uint32_t r = first; r < end; r += 1)
            {
                called[r - first] = runtimes[r];
            }
            check_boundary_write(called, end - first, sizeof(uint32_t));
            check_boundary_write(fixups, fixup_count, sizeof(CheckBoundaryFixup));
            // Its uses of the declarations outside it, as the reader's unused analysis met them (none where it didn't run).
            const uint32_t use_count = check_boundary_body_uses(check_boundary_origin(program, body.opening), NULL, 0);
            CheckBoundaryUse* body_uses = malloc(((uint64_t)use_count + 1) * sizeof(CheckBoundaryUse));
            const uint32_t use_written = body_uses != NULL ? check_boundary_body_uses(check_boundary_origin(program, body.opening), body_uses, use_count) : 0;
            check_boundary_write(body_uses, body_uses != NULL ? use_written : 0, sizeof(CheckBoundaryUse));
            free(body_uses);
            CheckBoundaryTable* tables = malloc(((uint64_t)table_count + 1) * sizeof(CheckBoundaryTable));
            uint32_t* table_entries = malloc((table_values + 1) * sizeof(uint32_t));
            uint64_t value = 0;
            for (uint32_t k = 0; tables != NULL && table_entries != NULL && k < table_count; k += 1)
            {
                const CheckLowerTable* table = &lowered->tables[table_first + k];
                tables[k] = (CheckBoundaryTable){ .count = table->count, .label_count = table->label_count, .address = table->address, .fallback = table->fallback,
                                                  .lower = table->lower };
                for (uint32_t v = 0; v < table->count; v += 1)
                {
                    table_entries[value++] = table->entries[v] - table->first_label;
                }
            }
            check_boundary_write(tables, tables != NULL && table_entries != NULL ? table_count : 0, sizeof(CheckBoundaryTable));
            check_boundary_write(table_entries, tables != NULL && table_entries != NULL ? table_values : 0, sizeof(uint32_t));
            free(tables);
            free(table_entries);
            check_boundary_write(function, sizeof(Tier0Function), 1);
            for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(check_boundary_columns); c += 1)
            {
                const CheckBoundaryColumn* column = &check_boundary_columns[c];
                const void* data = NULL;
                memcpy(&data, (const uint8_t*)function + column->offset, sizeof(data));
                check_boundary_write(data, check_boundary_extent(function, column->extent), column->bytes);
            }
        }
        free(fixups);
        if (check_boundary_names == NULL)
        {
            char names[800];
            snprintf(names, sizeof(names), "%s.bodies", check_boundary.path);
            check_boundary_names = fopen(names, "w");
        }
        if (check_boundary_names != NULL)
        {
            const uint32_t identifier = program->parser->tokens.names[body.declaration];
            const char* spelling = program->identifiers != NULL && identifier != 0 && identifier < program->identifiers->count ?
                                   (const char*)intern_bytes(program->identifiers, identifier) : "?";
            uint32_t control_kinds = 0;
            for (uint32_t i = 0; i < body.count; i += 1)
            {
                control_kinds |= 1u << (program->controls[body.first + i].kind & 31);
            }
            fprintf(check_boundary_names, "%u\t%u\t%s\t%u\t%s\t%x\t%llx\t%llx\t%u\t%u\n", check_boundary_origin(program, body.opening),
                    check_boundary_origin(program, closing), check_boundary_refusal_names[refusal], function->count, spelling, control_kinds,
                    unit->op_masks != NULL ? (unsigned long long)unit->op_masks[2 * (uint64_t)b] : 0,
                    unit->op_masks != NULL ? (unsigned long long)unit->op_masks[2 * (uint64_t)b + 1] : 0, unit->node_counts != NULL ? unit->node_counts[b] : 0, body.count);
        }
        unit->rows[unit->row_count++] = (CheckBoundaryRow){
            .opening = check_boundary_origin(program, body.opening),
            .closing = check_boundary_origin(program, closing),
            .declaration = check_boundary_origin(program, body.declaration),
            .refusal = refusal,
            .entries = function->count,
            .offset = mark,
        };
    }
}

static int check_boundary_row_order(const void* left, const void* right)
{
    const uint32_t a = ((const CheckBoundaryRow*)left)->opening;
    const uint32_t b = ((const CheckBoundaryRow*)right)->opening;
    return a < b ? -1 : a > b;
}

// A body with a diagnostic in it is refused: `token` is the diagnostic's. So is the body after a diagnostic between two
// bodies, whose declarator it may be at (an unused parameter's warning is the body's to give, at the body's end).
// Before the recording's end.
static void check_boundary_record_diagnostic(uint32_t token)
{
    CheckBoundaryUnit* unit = &check_boundary_unit;
    // The first body of the parse that ends at the token or after it.
    uint32_t low = 0;
    uint32_t high = check_boundary.range_count;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        if (check_boundary.ranges[middle].closing < token)
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }
    const uint32_t opening = low < check_boundary.range_count ? check_boundary.ranges[low].opening : UINT32_MAX;
    for (uint32_t r = 0; check_boundary_recording() && opening != UINT32_MAX && r < unit->row_count; r += 1)
    {
        CheckBoundaryRow* row = &unit->rows[r];
        if (row->opening == opening && row->refusal == CHECK_BOUNDARY_SUPPLIED)
        {
            row->refusal = CHECK_BOUNDARY_DIAGNOSTIC;
        }
    }
}

// A body with a diagnostic whose place among the diagnostics is `position` is refused. A place between two bodies may be
// the one before's (a body's diagnostics may be published at its end) or the one after's (its declarator's): both.
static void check_boundary_record_position(uint32_t position)
{
    CheckBoundaryUnit* unit = &check_boundary_unit;
    uint32_t low = 0;
    uint32_t high = check_boundary.range_count;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        if (check_boundary.ranges[middle].closing < position)
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }
    const bool inside = low < check_boundary.range_count && position >= check_boundary.ranges[low].opening;
    const uint32_t after = low < check_boundary.range_count ? check_boundary.ranges[low].opening : UINT32_MAX;
    const uint32_t before = !inside && low != 0 ? check_boundary.ranges[low - 1].opening : UINT32_MAX;
    for (uint32_t r = 0; check_boundary_recording() && r < unit->row_count; r += 1)
    {
        CheckBoundaryRow* row = &unit->rows[r];
        if ((row->opening == after || row->opening == before) && row->refusal == CHECK_BOUNDARY_SUPPLIED)
        {
            row->refusal = CHECK_BOUNDARY_DIAGNOSTIC;
        }
    }
}

static void check_boundary_record_finish(void)
{
    CheckBoundaryUnit* unit = &check_boundary_unit;
    if (check_boundary_recording())
    {
        for (uint32_t k = 0; k < check_boundary.refused_token_count; k += 1)
        {
            check_boundary_record_diagnostic(check_boundary.refused_tokens[k]);
        }
        if (unit->rows != NULL)
        {
            qsort(unit->rows, unit->row_count, sizeof(CheckBoundaryRow), check_boundary_row_order);
        }
        FILE* file = fopen(check_boundary.path, "wb");
        if (file != NULL)
        {
            const uint32_t header[4] = { BUSTER_CHECK_BOUNDARY_MAGIC, BUSTER_CHECK_BOUNDARY_VERSION, check_boundary.token_count, unit->row_count };
            const uint64_t blob = check_boundary.length;
            fwrite(header, sizeof(header), 1, file);
            fwrite(&blob, sizeof(blob), 1, file);
            if (unit->row_count != 0)
            {
                fwrite(unit->rows, sizeof(CheckBoundaryRow), unit->row_count, file);
            }
            if (check_boundary.length != 0)
            {
                fwrite(check_boundary.bytes, 1, check_boundary.length, file);
            }
            fclose(file);
        }
        // The report reads the bodies' table.
        free(check_boundary.bodies);
        check_boundary.bodies = calloc((uint64_t)unit->row_count + 1, sizeof(CheckBoundaryBody));
        check_boundary.body_count = check_boundary.bodies != NULL ? unit->row_count : 0;
        for (uint32_t r = 0; r < check_boundary.body_count; r += 1)
        {
            check_boundary.bodies[r] = (CheckBoundaryBody){ .opening = unit->rows[r].opening, .closing = unit->rows[r].closing, .refusal = unit->rows[r].refusal,
                                                            .entries = unit->rows[r].entries };
        }
        check_boundary_report("recorded");
    }
    check_boundary_unit_reset();
}

// A supplying compile's recording parsed, once its bytes are read (check_boundary_begin): the bodies' table, and each
// supplied body's lists and function pointed into the bytes. A recording that doesn't parse drops the unit's supply.
static void check_boundary_load(void)
{
    const uint64_t start = __rdtsc();
    const uint64_t fixed = 4 * sizeof(uint32_t) + sizeof(uint64_t);
    uint32_t header[4] = { 0 };
    uint64_t blob = 0;
    bool valid = check_boundary.length >= fixed;
    if (valid)
    {
        memcpy(header, check_boundary.bytes, sizeof(header));
        memcpy(&blob, check_boundary.bytes + sizeof(header), sizeof(blob));
        valid = header[0] == BUSTER_CHECK_BOUNDARY_MAGIC && header[1] == BUSTER_CHECK_BOUNDARY_VERSION &&
                check_boundary.length == fixed + (uint64_t)header[3] * sizeof(CheckBoundaryRow) + blob;
    }
    const CheckBoundaryRow* rows = valid ? (const CheckBoundaryRow*)(const void*)(check_boundary.bytes + fixed) : NULL;
    const uint64_t base = valid ? fixed + (uint64_t)header[3] * sizeof(CheckBoundaryRow) : 0;
    check_boundary.bodies = valid ? calloc((uint64_t)header[3] + 1, sizeof(CheckBoundaryBody)) : NULL;
    valid = valid && check_boundary.bodies != NULL;
    check_boundary.token_count = valid ? header[2] : 0;
    check_boundary.body_count = valid ? header[3] : 0;
    for (uint32_t r = 0; valid && r < check_boundary.body_count; r += 1)
    {
        CheckBoundaryBody* body = &check_boundary.bodies[r];
        *body = (CheckBoundaryBody){ .opening = rows[r].opening, .closing = rows[r].closing, .declaration = rows[r].declaration, .refusal = rows[r].refusal,
                                     .entries = rows[r].entries };
        if (body->refusal == CHECK_BOUNDARY_SUPPLIED)
        {
            uint64_t cursor = base + rows[r].offset;
            uint64_t count = 0;
            body->references = check_boundary_read(&cursor, sizeof(uint32_t), &count);
            body->reference_count = (uint32_t)count;
            body->strings = check_boundary_read(&cursor, sizeof(CheckBoundaryString), &count);
            body->string_count = (uint32_t)count;
            body->runtimes = check_boundary_read(&cursor, sizeof(uint32_t), &count);
            body->runtime_count = (uint32_t)count;
            body->fixups = check_boundary_read(&cursor, sizeof(CheckBoundaryFixup), &count);
            body->fixup_count = (uint32_t)count;
            body->uses = check_boundary_read(&cursor, sizeof(CheckBoundaryUse), &count);
            body->use_count = (uint32_t)count;
            body->tables = check_boundary_read(&cursor, sizeof(CheckBoundaryTable), &count);
            body->table_count = (uint32_t)count;
            body->table_entries = check_boundary_read(&cursor, sizeof(uint32_t), &count);
            Tier0Function* function = check_boundary_read(&cursor, 1, &count);
            valid = function != NULL && count == sizeof(Tier0Function);
            for (uint32_t c = 0; valid && c < BUSTER_ARRAY_LENGTH(check_boundary_columns); c += 1)
            {
                const CheckBoundaryColumn* column = &check_boundary_columns[c];
                const void* data = check_boundary_read(&cursor, column->bytes, &count);
                valid = data == NULL || count == check_boundary_extent(function, column->extent);
                memcpy((uint8_t*)function + column->offset, &data, sizeof(data));
            }
            valid = valid && cursor <= check_boundary.length;
            body->function = function;
        }
    }
    if (!valid && check_boundary.mode == CHECK_BOUNDARY_SUPPLYING && check_boundary.dropped == NULL)
    {
        check_boundary.dropped = "a recording that doesn't parse";
    }
    check_boundary.load_ticks += __rdtsc() - start;
}

// A supplied body's function in the supplying compile: the recording's, its names made this unit's. False where a name
// has no symbol here (the unit is not the recording's).
static bool check_boundary_supply(CheckLowerWriter* writer, const CheckLowerShared* shared, const CheckBoundaryBody* body, uint32_t b, Tier0Function* function)
{
    const CheckProgram* program = shared->program;
    CheckProgramLowered* lowered = shared->lowered;
    CheckProgramLowered* status = writer->output;
    Tier0Function* recorded = body->function;
    bool valid = true;
    // Its jump tables, which the lowering's preparation numbered among the unit's (check_boundary_supply_tables): their
    // labels' places among the tables' labels, in the part's label symbols, as its dispatches would have written them.
    uint32_t table_first = BUSTER_CHECK_NONE;
    for (uint32_t t = 0; body->table_count != 0 && table_first == BUSTER_CHECK_NONE && t < lowered->table_count; t += 1)
    {
        table_first = lowered->tables[t].function == b ? t : BUSTER_CHECK_NONE;
    }
    valid = body->table_count == 0 || (table_first != BUSTER_CHECK_NONE && table_first + body->table_count <= lowered->table_count);
    for (uint32_t k = 0; valid && k < body->table_count; k += 1)
    {
        const CheckLowerTable* table = &lowered->tables[table_first + k];
        for (uint32_t j = 0; valid && j < table->label_count; j += 1)
        {
            valid = status->label_symbol_count < writer->label_symbol_capacity;
            if (valid)
            {
                status->label_symbols[status->label_symbol_count++] = BUSTER_CHECK_LOWER_CASE_SYMBOL | (table->first_label + j);
            }
        }
    }
    // The runtime functions it called first in its part, in that order (check_lower_runtime keeps the part's order).
    for (uint32_t r = 0; r < body->runtime_count; r += 1)
    {
        valid = valid && body->runtimes[r] < CHECK_LOWER_RUNTIMES;
        if (valid)
        {
            (void)check_lower_runtime(writer, (CheckLowerRuntime)body->runtimes[r]);
        }
    }
    for (uint32_t f = 0; valid && f < body->fixup_count; f += 1)
    {
        const CheckBoundaryFixup fixup = body->fixups[f];
        SymbolId symbol = 0;
        if (fixup.name == CHECK_BOUNDARY_NAME_OBJECT)
        {
            symbol = fixup.payload != 0 && fixup.payload < program->object_count ? program->object_symbols[fixup.payload] : 0;
        }
        else if (fixup.name == CHECK_BOUNDARY_NAME_TABLE)
        {
            symbol = table_first != BUSTER_CHECK_NONE && fixup.payload < body->table_count ? writer->provisional_tables + table_first + fixup.payload : 0;
        }
        else if (fixup.payload < CHECK_LOWER_RUNTIMES)
        {
            symbol = check_lower_runtime(writer, (CheckLowerRuntime)fixup.payload);
        }
        valid = symbol != 0;
        if (valid && fixup.place == CHECK_BOUNDARY_PLACE_VALUE && fixup.index < recorded->count)
        {
            ((uint64_t*)recorded->values)[fixup.index] = symbol;
        }
        else if (valid && fixup.place == CHECK_BOUNDARY_PLACE_CALL && fixup.index < recorded->call_count)
        {
            ((SymbolId*)recorded->call_targets)[fixup.index] = symbol;
        }
        else if (valid && fixup.place == CHECK_BOUNDARY_PLACE_HIDDEN_COPY)
        {
            recorded->hidden_copy = symbol;
        }
        else
        {
            valid = false;
        }
    }
    if (valid)
    {
        *function = *recorded;
        function->symbol_access = NULL;
        function->symbol_count = 0;
    }
    return valid;
}

// The jump tables the supplied bodies of the program bring, for the tables' room (check_lower_switch_tables).
static uint32_t check_boundary_table_room(const CheckProgram* program)
{
    uint32_t tables = 0;
    for (uint32_t b = 0; check_boundary_supplying() && b < program->body_count; b += 1)
    {
        const CheckBoundaryBody* taken = check_boundary_taken(program->bodies[b].opening);
        tables += taken != NULL ? taken->table_count : 0;
    }
    return tables;
}

// A supplied body's jump tables among the unit's, where its switches' would have been numbered (check_lower_switch_range):
// each one's entries from the unit's next label on.
static bool check_boundary_supply_tables(CheckProgramLowered* lowered, uint32_t capacity, const CheckBoundaryBody* body, uint32_t b, Arena* output)
{
    bool valid = true;
    uint64_t value = 0;
    for (uint32_t k = 0; valid && k < body->table_count; k += 1)
    {
        const CheckBoundaryTable recorded = body->tables[k];
        uint32_t* entries = lowered->table_count < capacity ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, (uint64_t)recorded.count + 1) : NULL;
        valid = entries != NULL;
        if (valid)
        {
            const uint32_t first = lowered->case_symbol_count;
            for (uint32_t v = 0; v < recorded.count; v += 1)
            {
                entries[v] = first + body->table_entries[value++];
            }
            lowered->tables[lowered->table_count] = (CheckLowerTable){
                .entries = entries,
                .count = recorded.count,
                .function = b,
                .control = BUSTER_CHECK_NONE,
                .labels = NULL,
                .label_count = recorded.label_count,
                .first_label = first,
                .address = recorded.address,
                .lower = recorded.lower,
                .fallback = recorded.fallback,
            };
            lowered->table_count += 1;
            lowered->case_symbol_count += recorded.label_count;
        }
    }
    return valid;
}
