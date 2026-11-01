// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Small-case oracle. Include after tier1_certificate.c. This deliberately uses
// dense sets of (source value, byte lane), intersection to a CFG fixpoint, full
// edge copies, and global kills on definitions. Certificates never seed the
// analysis. After inference proves them, a separate dense local check also
// verifies that the certificate is sufficient (uncertified bytes are unknown).
// XOR instructions execute as GF(2) expressions over incoming bytes;
// no exchange summary from the production checker is used.

#define BUSTER_CERT_ORACLE_VALUES 64u
#define BUSTER_CERT_ORACLE_UNITS 512u
#define BUSTER_CERT_ORACLE_BLOCKS 64u

typedef struct Tier1CertOracle Tier1CertOracle;
struct Tier1CertOracle
{
    const Tier1CertProgram* program;
    Tier1CertReport* report;
    uint64_t* saved;
    uint64_t* copy;
    uint64_t* expressions;
    uint64_t* expression_copy;
    // Incarnation bookkeeping for frame recipes: definitions erase a value
    // globally, so a recipe's recorded base generation is only usable while
    // the dependency has not been defined again since the establishment.
    uint32_t* generations;
    uint32_t* base_generations;
    uint32_t* establishments;
    size_t cells;
    uint32_t expression_words;
    bool checking;
};

static bool tier1_cert_oracle_shapes(const Tier1CertProgram* program, const Tier1CertFact* facts, uint32_t count)
{
    bool valid = true;
    for (uint32_t i = 0; valid && i < count; i += 1)
    {
        for (uint32_t j = 0; valid && j < i; j += 1)
        {
            valid = facts[i].value != facts[j].value;
            if (valid && facts[i].location != BUSTER_CERT_NONE && facts[j].location != BUSTER_CERT_NONE)
            {
                const Tier1CertLocation a = program->locations[facts[i].location];
                const Tier1CertLocation b = program->locations[facts[j].location];
                valid = a.unit + a.width <= b.unit || b.unit + b.width <= a.unit || (a.unit == b.unit && a.width == b.width);
            }
        }
    }
    return valid;
}

static bool tier1_cert_oracle_has(const Tier1CertOracle* oracle, const uint64_t* state, uint32_t value, uint32_t location)
{
    const Tier1CertLocation l = oracle->program->locations[location];
    const uint32_t values = oracle->program->value_count;
    bool valid = l.width == oracle->program->values[value].width;
    for (uint32_t lane = 0; valid && lane < l.width; lane += 1)
    {
        valid = (state[(size_t)(l.unit + lane) * values + value] & (UINT64_C(1) << lane)) != 0;
    }
    return valid;
}

static bool tier1_cert_oracle_known(const Tier1CertOracle* oracle, const uint64_t* row)
{
    const uint32_t values = oracle->program->value_count;
    uint64_t any = 0;
    for (uint32_t v = 0; v < values; v += 1)
    {
        any |= row[v];
    }
    return any != 0;
}

static void tier1_cert_oracle_kill(Tier1CertOracle* oracle, uint64_t* state, uint32_t location)
{
    const Tier1CertLocation l = oracle->program->locations[location];
    const uint32_t values = oracle->program->value_count;
    memset(state + (size_t)l.unit * values, 0, (size_t)l.extent * values * sizeof(uint64_t));
    oracle->report->unit_writes += l.extent;
}

static void tier1_cert_oracle_abi(const Tier1CertProgram* program, uint64_t* state)
{
    const uint32_t values = program->value_count;
    for (uint32_t f = 0; f < program->entry_count; f += 1)
    {
        const Tier1CertFact fact = program->entry[f];
        if (fact.location != BUSTER_CERT_NONE)
        {
            const Tier1CertLocation l = program->locations[fact.location];
            for (uint32_t lane = 0; lane < l.width; lane += 1)
            {
                state[(size_t)(l.unit + lane) * values + fact.value] |= UINT64_C(1) << lane;
            }
        }
    }
}

static bool tier1_cert_oracle_facts(Tier1CertOracle* oracle, const uint64_t* state, const Tier1CertFact* facts, uint32_t count)
{
    bool valid = tier1_cert_oracle_shapes(oracle->program, facts, count);
    for (uint32_t f = 0; valid && f < count; f += 1)
    {
        if (facts[f].location != BUSTER_CERT_NONE)
        {
            valid = tier1_cert_oracle_has(oracle, state, facts[f].value, facts[f].location);
        }
        oracle->report->facts += 1;
    }
    return valid;
}

static bool tier1_cert_oracle_original(Tier1CertOracle* oracle, uint64_t* state, const Tier1CertSource* source, Tier1CertInstruction in)
{
    const Tier1CertProgram* program = oracle->program;
    const uint32_t values = program->value_count;
    const uint32_t units = program->unit_count;
    bool valid = tier1_cert_correspondence(program, source, in);
    for (uint32_t i = 0; valid && i < source->before_count; i += 1)
    {
        tier1_cert_oracle_kill(oracle, state, source->before[i]);
    }
    for (uint32_t i = 0; valid && oracle->checking && i < source->use_count; i += 1)
    {
        valid = tier1_cert_oracle_has(oracle, state, source->operands[i].value, in.locations[i]);
    }
    if (valid)
    {
        // Save reads before any definition, implicit clobber, or tied write.
        memcpy(oracle->saved, state, oracle->cells * sizeof(uint64_t));
        const uint32_t count = source->use_count + source->def_count;
        for (uint32_t d = source->use_count; d < count; d += 1)
        {
            const uint32_t value = source->operands[d].value;
            for (uint32_t u = 0; u < units; u += 1)
            {
                state[(size_t)u * values + value] = 0;
            }
            if (oracle->checking)
            {
                oracle->generations[value] += 1;
                const Tier1CertValue defined = program->values[value];
                if (defined.recipe == TIER1_CERT_FRAME_RECIPE)
                {
                    oracle->establishments[value] = oracle->report->block + 1;
                    oracle->base_generations[value] = oracle->generations[defined.dependency];
                }
            }
        }
        if (source->copy)
        {
            const uint32_t old = source->operands[0].value;
            const uint32_t value = source->operands[1].value;
            // A semantic copy creates an equality at every surviving copy,
            // not just at its destination. The saved state makes self-copy
            // and read-before-write handling independent of kill ordering.
            for (uint32_t u = 0; u < units; u += 1)
            {
                state[(size_t)u * values + value] = oracle->saved[(size_t)u * values + old];
            }
            const Tier1CertLocation read = program->locations[in.locations[0]];
            memcpy(oracle->copy, state + (size_t)read.unit * values, (size_t)read.width * values * sizeof(uint64_t));
        }
        for (uint32_t i = 0; i < source->after_count; i += 1)
        {
            tier1_cert_oracle_kill(oracle, state, source->after[i]);
        }
        for (uint32_t d = source->use_count; d < count; d += 1)
        {
            const uint32_t value = source->operands[d].value;
            const Tier1CertLocation l = program->locations[in.locations[d]];
            tier1_cert_oracle_kill(oracle, state, in.locations[d]);
            if (source->copy)
            {
                memcpy(state + (size_t)l.unit * values, oracle->copy, (size_t)l.width * values * sizeof(uint64_t));
            }
            for (uint32_t lane = 0; lane < l.width; lane += 1)
            {
                state[(size_t)(l.unit + lane) * values + value] |= UINT64_C(1) << lane;
            }
        }
        oracle->report->instructions += 1;
        oracle->report->operands += count;
    }
    return valid;
}

static bool tier1_cert_oracle_move(Tier1CertOracle* oracle, uint64_t* state, Tier1CertInstruction in)
{
    const uint32_t values = oracle->program->value_count;
    const Tier1CertLocation a = oracle->program->locations[in.a];
    const Tier1CertLocation b = oracle->program->locations[in.b];
    bool valid = true;
    memcpy(oracle->saved, state, oracle->cells * sizeof(uint64_t));
    for (uint32_t lane = 0; valid && oracle->checking && lane < a.width; lane += 1)
    {
        valid = tier1_cert_oracle_known(oracle, oracle->saved + (size_t)(b.unit + lane) * values);
        if (valid && in.op == TIER1_CERT_XCHG)
        {
            valid = tier1_cert_oracle_known(oracle, oracle->saved + (size_t)(a.unit + lane) * values);
        }
    }
    if (valid)
    {
        tier1_cert_oracle_kill(oracle, state, in.a);
        if (in.op == TIER1_CERT_XCHG)
        {
            tier1_cert_oracle_kill(oracle, state, in.b);
        }
        memcpy(state + (size_t)a.unit * values, oracle->saved + (size_t)b.unit * values, (size_t)a.width * values * sizeof(uint64_t));
        if (in.op == TIER1_CERT_XCHG)
        {
            memcpy(state + (size_t)b.unit * values, oracle->saved + (size_t)a.unit * values, (size_t)b.width * values * sizeof(uint64_t));
        }
    }
    return valid;
}

static bool tier1_cert_oracle_xor(Tier1CertOracle* oracle, uint64_t* state, const Tier1CertInstruction* code)
{
    const Tier1CertProgram* program = oracle->program;
    const uint32_t units = program->unit_count;
    const uint32_t values = program->value_count;
    const uint32_t words = oracle->expression_words;
    uint64_t* const expressions = oracle->expressions;
    memcpy(oracle->saved, state, oracle->cells * sizeof(uint64_t));
    memset(expressions, 0, (size_t)units * words * sizeof(uint64_t));
    for (uint32_t u = 0; u < units; u += 1)
    {
        expressions[(size_t)u * words + u / 64] = UINT64_C(1) << (u % 64);
    }
    const Tier1CertLocation first = program->locations[code[0].a];
    const Tier1CertLocation left = program->locations[code[0].b];
    const Tier1CertLocation right = program->locations[code[0].c];
    const uint32_t other = tier1_cert_same(first, left) ? code[0].c : tier1_cert_same(first, right) ? code[0].b : BUSTER_CERT_NONE;
    bool valid = other != BUSTER_CERT_NONE;
    for (uint32_t i = 0; valid && i < 3; i += 1)
    {
        const Tier1CertInstruction in = code[i];
        valid = in.op == TIER1_CERT_XOR && tier1_cert_edit(program, in);
        if (valid)
        {
            // The supported recipe family is one pair of same-width/write-
            // extent locations. Within it, prove the instruction order and
            // transfer algebraically rather than sharing the fast recognizer.
            const Tier1CertLocation second = program->locations[other];
            const Tier1CertLocation a = program->locations[in.a];
            const Tier1CertLocation b = program->locations[in.b];
            const Tier1CertLocation c = program->locations[in.c];
            valid = (tier1_cert_same(a, first) || tier1_cert_same(a, second)) && (tier1_cert_same(b, first) || tier1_cert_same(b, second)) && (tier1_cert_same(c, first) || tier1_cert_same(c, second));
            for (uint32_t lane = 0; lane < a.width; lane += 1)
            {
                for (uint32_t w = 0; w < words; w += 1)
                {
                    const uint64_t left = expressions[(size_t)(b.unit + lane) * words + w];
                    const uint64_t right = expressions[(size_t)(c.unit + lane) * words + w];
                    oracle->expression_copy[(size_t)lane * words + w] = left ^ right;
                    uint64_t read = left | right;
                    while (oracle->checking && valid && read != 0)
                    {
                        const uint32_t unit = w * 64 + (uint32_t)__builtin_ctzll(read);
                        valid = tier1_cert_oracle_known(oracle, oracle->saved + (size_t)unit * values);
                        read &= read - 1;
                    }
                }
            }
            memset(expressions + (size_t)a.unit * words, 0, (size_t)a.extent * words * sizeof(uint64_t));
            memcpy(expressions + (size_t)a.unit * words, oracle->expression_copy, (size_t)a.width * words * sizeof(uint64_t));
            if (a.class == TIER1_CERT_GPR && program->flags_location != BUSTER_CERT_NONE)
            {
                const Tier1CertLocation flags = program->locations[program->flags_location];
                memset(expressions + (size_t)flags.unit * words, 0, words * sizeof(uint64_t));
            }
        }
    }
    // Independently prove the concrete three-instruction recipe's transfer.
    // This checks its algebra, not the fast checker's middle-instruction shape.
    if (valid)
    {
        valid = other != BUSTER_CERT_NONE;
        if (valid)
        {
            const Tier1CertLocation a = program->locations[code[0].a];
            const Tier1CertLocation b = program->locations[other];
            valid = tier1_cert_disjoint(a, b);
            for (uint32_t lane = 0; valid && lane < a.width; lane += 1)
            {
                for (uint32_t w = 0; valid && w < words; w += 1)
                {
                    const uint32_t left = b.unit + lane;
                    const uint32_t right = a.unit + lane;
                    const uint64_t expected_a = w == left / 64 ? UINT64_C(1) << (left % 64) : 0;
                    const uint64_t expected_b = w == right / 64 ? UINT64_C(1) << (right % 64) : 0;
                    valid = expressions[(size_t)(a.unit + lane) * words + w] == expected_a && expressions[(size_t)(b.unit + lane) * words + w] == expected_b;
                }
            }
        }
    }
    for (uint32_t u = 0; valid && u < units; u += 1)
    {
        uint32_t origin = BUSTER_CERT_NONE;
        for (uint32_t w = 0; w < words; w += 1)
        {
            uint64_t expression = expressions[(size_t)u * words + w];
            while (expression != 0)
            {
                const uint32_t bit = (uint32_t)__builtin_ctzll(expression);
                valid = valid && origin == BUSTER_CERT_NONE;
                origin = w * 64 + bit;
                expression &= expression - 1;
            }
        }
        memset(state + (size_t)u * values, 0, values * sizeof(uint64_t));
        if (valid && origin != BUSTER_CERT_NONE)
        {
            memcpy(state + (size_t)u * values, oracle->saved + (size_t)origin * values, values * sizeof(uint64_t));
        }
    }
    return valid;
}

static bool tier1_cert_oracle_remat(Tier1CertOracle* oracle, uint64_t* state, Tier1CertInstruction in)
{
    const Tier1CertProgram* program = oracle->program;
    const Tier1CertValue value = program->values[in.value];
    const Tier1CertLocation l = program->locations[in.a];
    const uint32_t values = program->value_count;
    const uint32_t units = program->unit_count;
    bool valid = true;
    if (oracle->checking && in.op == TIER1_CERT_FRAME_ADDRESS)
    {
        const uint32_t tag = oracle->report->block + 1;
        if (oracle->establishments[in.value] == tag)
        {
            // The recorded base generation must still be current: a definition
            // of the base after the establishment retires the old recipe.
            valid = oracle->generations[value.dependency] == oracle->base_generations[in.value];
        }
        else
        {
            oracle->establishments[in.value] = tag;
            oracle->base_generations[in.value] = oracle->generations[value.dependency];
        }
        valid = valid && tier1_cert_oracle_has(oracle, state, value.dependency, in.b);
    }
    if (valid)
    {
        memset(oracle->copy, 0, (size_t)l.width * values * sizeof(uint64_t));
        for (uint32_t lane = 0; lane < l.width; lane += 1)
        {
            uint64_t* const row = oracle->copy + (size_t)lane * values;
            row[in.value] = UINT64_C(1) << lane;
            for (uint32_t u = 0; u < units; u += 1)
            {
                if ((state[(size_t)u * values + in.value] & (UINT64_C(1) << lane)) != 0)
                {
                    for (uint32_t v = 0; v < values; v += 1)
                    {
                        row[v] |= state[(size_t)u * values + v];
                    }
                }
            }
        }
        tier1_cert_oracle_kill(oracle, state, in.a);
        memcpy(state + (size_t)l.unit * values, oracle->copy, (size_t)l.width * values * sizeof(uint64_t));
    }
    return valid;
}

static bool tier1_cert_oracle_stream(Tier1CertOracle* oracle, uint64_t* state, const Tier1CertSource* source, uint32_t source_count, const Tier1CertInstruction* code, uint32_t count)
{
    bool valid = true;
    uint32_t original = 0;
    for (uint32_t i = 0; valid && i < count; i += 1)
    {
        oracle->report->instruction = i;
        const Tier1CertInstruction in = code[i];
        if (in.op == TIER1_CERT_ORIGINAL)
        {
            valid = original < source_count && tier1_cert_oracle_original(oracle, state, source + original, in);
            original += 1;
        }
        else
        {
            valid = tier1_cert_edit(oracle->program, in);
            if (valid && in.op == TIER1_CERT_XOR)
            {
                valid = count - i >= 3 && tier1_cert_oracle_xor(oracle, state, code + i);
                i += 2;
            }
            else if (valid && (in.op == TIER1_CERT_MOVE || in.op == TIER1_CERT_XCHG))
            {
                valid = tier1_cert_oracle_move(oracle, state, in);
            }
            else if (valid)
            {
                valid = tier1_cert_oracle_remat(oracle, state, in);
            }
        }
    }
    return valid && original == source_count;
}

static bool tier1_cert_oracle_rename(Tier1CertOracle* oracle, uint64_t* state, Tier1CertEdge edge)
{
    const uint32_t values = oracle->program->value_count;
    const uint32_t units = oracle->program->unit_count;
    memcpy(oracle->saved, state, oracle->cells * sizeof(uint64_t));
    bool valid = true;
    for (uint32_t r = 0; valid && r < edge.rename_count; r += 1)
    {
        const Tier1CertRename rename = edge.renames[r];
        for (uint32_t previous = 0; valid && previous < r; previous += 1)
        {
            valid = rename.destination != edge.renames[previous].destination;
        }
        for (uint32_t u = 0; valid && u < units; u += 1)
        {
            state[(size_t)u * values + rename.destination] = oracle->saved[(size_t)u * values + rename.source];
        }
    }
    return valid;
}

static Tier1CertError tier1_certificate_oracle(const Tier1CertProgram* program, void* scratch, size_t capacity, Tier1CertReport* report)
{
    Tier1CertReport result = {
        .error = TIER1_CERT_INVALID,
        .edge = BUSTER_CERT_NONE,
        .instruction = BUSTER_CERT_NONE,
    };
    bool valid = report != 0 && scratch != 0 && tier1_cert_valid(program);
    if (valid)
    {
        valid = program->value_count <= BUSTER_CERT_ORACLE_VALUES && program->unit_count <= BUSTER_CERT_ORACLE_UNITS && program->block_count <= BUSTER_CERT_ORACLE_BLOCKS;
        result.error = TIER1_CERT_CAPACITY;
    }
    if (valid)
    {
        const uint32_t values = program->value_count;
        const uint32_t units = program->unit_count;
        const uint32_t blocks = program->block_count;
        result.storage_units = units;
        result.values = values;
        result.blocks = blocks;
        const size_t cells = (size_t)units * values;
        const uint32_t words = (units + 63) / 64;
        Tier1CertMemory memory = {
            .data = scratch,
            .capacity = capacity,
        };
        Tier1CertOracle oracle = {
            .program = program,
            .report = &result,
            .cells = cells,
            .expression_words = words,
        };
        uint64_t* entries = tier1_cert_take(&memory, (size_t)blocks * cells, sizeof(uint64_t));
        uint64_t* work = tier1_cert_take(&memory, cells, sizeof(uint64_t));
        uint64_t* edge_state = tier1_cert_take(&memory, cells, sizeof(uint64_t));
        oracle.saved = tier1_cert_take(&memory, cells, sizeof(uint64_t));
        oracle.copy = tier1_cert_take(&memory, (size_t)BUSTER_CERT_MAX_WIDTH * values, sizeof(uint64_t));
        oracle.expressions = tier1_cert_take(&memory, (size_t)units * words, sizeof(uint64_t));
        oracle.expression_copy = tier1_cert_take(&memory, (size_t)BUSTER_CERT_MAX_WIDTH * words, sizeof(uint64_t));
        oracle.generations = tier1_cert_take(&memory, values, sizeof(uint32_t));
        oracle.base_generations = tier1_cert_take(&memory, values, sizeof(uint32_t));
        oracle.establishments = tier1_cert_take(&memory, values, sizeof(uint32_t));
        uint32_t* queue = tier1_cert_take(&memory, blocks, sizeof(uint32_t));
        uint8_t* seen = tier1_cert_take(&memory, blocks, sizeof(uint8_t));
        result.scratch_bytes = memory.used;
        valid = !memory.failed;
        if (valid)
        {
            memset(oracle.generations, 0, (size_t)values * sizeof(uint32_t));
            memset(oracle.base_generations, 0, (size_t)values * sizeof(uint32_t));
            memset(oracle.establishments, 0, (size_t)values * sizeof(uint32_t));
            valid = tier1_cert_reachable(program, queue, seen) && tier1_cert_oracle_shapes(program, program->entry, program->entry_count);
            result.error = TIER1_CERT_INVALID;
        }
        if (valid)
        {
            memset(entries, 0, cells * sizeof(uint64_t));
            tier1_cert_oracle_abi(program, entries);
            for (uint32_t b = 1; b < blocks; b += 1)
            {
                for (uint32_t u = 0; u < units; u += 1)
                {
                    for (uint32_t v = 0; v < values; v += 1)
                    {
                        const uint32_t width = program->values[v].width;
                        entries[(size_t)b * cells + (size_t)u * values + v] = width == 64 ? UINT64_MAX : (UINT64_C(1) << width) - 1;
                    }
                }
            }
        }
        bool changed = valid;
        // Every non-stable round clears at least one bit of a finite lattice.
        // There is no heuristic iteration cutoff and no certificate seed.
        while (valid && changed)
        {
            changed = false;
            result.oracle_rounds += 1;
            for (uint32_t b = 0; valid && b < blocks; b += 1)
            {
                const Tier1CertBlock block = program->blocks[b];
                result.block = b;
                memcpy(work, entries + (size_t)b * cells, cells * sizeof(uint64_t));
                valid = tier1_cert_oracle_stream(&oracle, work, block.source, block.source_count, block.code, block.code_count);
                for (uint32_t e = 0; valid && e < block.edge_count; e += 1)
                {
                    const Tier1CertEdge edge = block.edges[e];
                    result.edge = e;
                    memcpy(edge_state, work, cells * sizeof(uint64_t));
                    valid = tier1_cert_oracle_stream(&oracle, edge_state, 0, 0, edge.moves, edge.move_count);
                    valid = valid && tier1_cert_oracle_rename(&oracle, edge_state, edge);
                    uint64_t* const destination = entries + (size_t)edge.destination * cells;
                    for (size_t c = 0; valid && c < cells; c += 1)
                    {
                        const uint64_t meet = destination[c] & edge_state[c];
                        changed |= meet != destination[c];
                        destination[c] = meet;
                    }
                }
            }
        }
        oracle.checking = true;
        result.error = TIER1_CERT_VALUE;
        for (uint32_t b = 0; valid && b < blocks; b += 1)
        {
            const Tier1CertBlock block = program->blocks[b];
            result.block = b;
            result.edge = BUSTER_CERT_NONE;
            memcpy(work, entries + (size_t)b * cells, cells * sizeof(uint64_t));
            valid = tier1_cert_oracle_facts(&oracle, work, block.certificate, block.certificate_count);
            // The fixpoint above did not use the certificate. Now check that
            // its proved assertions suffice on their own, rather than letting
            // extra inferred facts hide an incomplete supplied proof.
            if (valid)
            {
                memset(work, 0, cells * sizeof(uint64_t));
                for (uint32_t f = 0; f < block.certificate_count; f += 1)
                {
                    const Tier1CertFact fact = block.certificate[f];
                    const Tier1CertValue value = program->values[fact.value];
                    if (value.recipe == TIER1_CERT_FRAME_RECIPE)
                    {
                        oracle.establishments[fact.value] = b + 1;
                        oracle.base_generations[fact.value] = oracle.generations[value.dependency];
                    }
                    if (fact.location != BUSTER_CERT_NONE)
                    {
                        const Tier1CertLocation l = program->locations[fact.location];
                        for (uint32_t lane = 0; lane < l.width; lane += 1)
                        {
                            work[(size_t)(l.unit + lane) * values + fact.value] |= UINT64_C(1) << lane;
                        }
                    }
                }
            }
            valid = valid && tier1_cert_oracle_stream(&oracle, work, block.source, block.source_count, block.code, block.code_count);
            if (valid && block.edge_count == 0)
            {
                valid = tier1_cert_oracle_facts(&oracle, work, program->exit, program->exit_count);
            }
            for (uint32_t e = 0; valid && e < block.edge_count; e += 1)
            {
                const Tier1CertEdge edge = block.edges[e];
                const Tier1CertBlock destination = program->blocks[edge.destination];
                result.edge = e;
                memcpy(edge_state, work, cells * sizeof(uint64_t));
                valid = tier1_cert_oracle_stream(&oracle, edge_state, 0, 0, edge.moves, edge.move_count);
                valid = valid && tier1_cert_oracle_rename(&oracle, edge_state, edge);
                valid = valid && tier1_cert_oracle_facts(&oracle, edge_state, destination.certificate, destination.certificate_count);
            }
        }
    }
    if (valid)
    {
        result.error = TIER1_CERT_OK;
    }
    if (report != 0)
    {
        *report = result;
    }
    return result.error;
}
