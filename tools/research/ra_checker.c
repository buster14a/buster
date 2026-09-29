/* Research-only independent scalar allocation checker.
 * Ownership: ra_check_preflight checks frozen correspondence and edit coverage;
 * ra_check_transfer models full-width GPR and eight-byte stack writes;
 * ra_check_edge performs simultaneous parameter substitution;
 * ra_check runs a descending must-fact fixed point before checking uses;
 * ra_check_certificate checks entry and every edge without trusting the solver.
 * The source program, original CFG, entry ABI and call masks are trusted inputs.
 */
#include "ra_checker.h"
#include <string.h>

static RaCheckResult ra_check_result(void)
{
    RaCheckResult result = { RA_CHECK_VALID, "validated supported allocation contract", RA_CHECK_NONE, RA_CHECK_NONE, 0, 0, 0, 0 };
    return result;
}

static void ra_check_fail(RaCheckResult *result, RaCheckStatus status, const char *reason, uint32_t block, uint32_t row)
{
    if (result->status == RA_CHECK_VALID)
    {
        result->status = status;
        result->reason = reason;
        result->block = block;
        result->row = row;
    }
}

static uint64_t ra_check_mask(uint32_t count)
{
    uint64_t result = count == RA_CHECK_LIMIT ? UINT64_MAX : (UINT64_C(1) << count) - 1;
    return result;
}

static int ra_check_range(uint32_t first, uint32_t count, uint32_t limit)
{
    int result = first <= limit && count <= limit - first;
    return result;
}

static int ra_check_writes(uint32_t opcode)
{
    int result = opcode != RA_CHECK_OBSERVE64;
    return result;
}

static int ra_check_alias(const RaCheckLocation *a, const RaCheckLocation *b)
{
    int result = 0;
    if (a->kind == b->kind)
    {
        if (a->kind == RA_CHECK_GPR)
        {
            result = a->index == b->index;
        }
        else
        {
            result = (uint64_t)a->index < (uint64_t)b->index + 8 && (uint64_t)b->index < (uint64_t)a->index + 8;
        }
    }
    return result;
}

static void ra_check_kill_location(const RaCheckProgram *program, RaCheckState *state, uint32_t location)
{
    for (uint32_t i = 0; i < program->location_count; i += 1)
    {
        if (ra_check_alias(&program->locations[location], &program->locations[i]))
        {
            state->facts[i] = 0;
        }
    }
}

static void ra_check_kill_symbols(const RaCheckProgram *program, RaCheckState *state, uint64_t symbols)
{
    for (uint32_t i = 0; i < program->location_count; i += 1)
    {
        state->facts[i] &= ~symbols;
    }
}

static void ra_check_owned_edits(const RaCheckProgram *program, RaCheckWorkspace *workspace, RaCheckResult *result, uint32_t first, uint32_t count, uint32_t phase, uint32_t owner)
{
    if (!ra_check_range(first, count, program->edit_count))
    {
        ra_check_fail(result, RA_CHECK_INVALID, "edit range is outside edit stream", RA_CHECK_NONE, owner);
    }
    else
    {
        for (uint32_t i = first; i < first + count && result->status == RA_CHECK_VALID; i += 1)
        {
            const RaCheckEdit *edit = &program->edits[i];
            if (workspace->edit_seen[i] || edit->phase != phase || edit->owner != owner)
            {
                ra_check_fail(result, RA_CHECK_INVALID, "edit has duplicate, wrong phase or wrong owner", RA_CHECK_NONE, owner);
            }
            else
            {
                workspace->edit_seen[i] = 1;
            }
        }
    }
}

static void ra_check_preflight(const RaCheckProgram *program, RaCheckWorkspace *workspace, RaCheckResult *result)
{
    if (program == 0 || workspace == 0)
    {
        ra_check_fail(result, RA_CHECK_INVALID, "missing program or workspace", RA_CHECK_NONE, RA_CHECK_NONE);
    }
    else if (program->symbol_count > RA_CHECK_LIMIT || program->location_count > RA_CHECK_LIMIT || program->block_count > RA_CHECK_LIMIT || program->row_count > RA_CHECK_MAX_ROWS || program->edit_count > RA_CHECK_MAX_EDITS || program->edge_count > RA_CHECK_MAX_EDGES || program->binding_count > RA_CHECK_LIMIT)
    {
        ra_check_fail(result, RA_CHECK_UNCOVERED, "research checker capacity exceeded", RA_CHECK_NONE, RA_CHECK_NONE);
    }
    else if (program->version != RA_CHECK_VERSION || program->block_count == 0 || program->entry_block >= program->block_count || program->row_count != program->allocated_row_count || program->blocks == 0 || (program->location_count && program->locations == 0) || (program->row_count && (program->rows == 0 || program->allocated_rows == 0)) || (program->edit_count && program->edits == 0) || (program->edge_count && program->edges == 0) || (program->binding_count && program->bindings == 0))
    {
        ra_check_fail(result, RA_CHECK_INVALID, "malformed counts, version, pointers or row coverage", RA_CHECK_NONE, RA_CHECK_NONE);
    }
    if (result->status == RA_CHECK_VALID)
    {
        memset(workspace, 0, sizeof(*workspace));
        for (uint32_t i = 0; i < program->location_count && result->status == RA_CHECK_VALID; i += 1)
        {
            const RaCheckLocation *location = &program->locations[i];
            if (location->width_bits != 64 || location->kind > RA_CHECK_STACK)
            {
                ra_check_fail(result, RA_CHECK_UNCOVERED, "partial, vector, mask or unknown resource", RA_CHECK_NONE, RA_CHECK_NONE);
            }
            else if ((location->kind == RA_CHECK_GPR && location->index >= RA_CHECK_LIMIT) || (location->kind == RA_CHECK_STACK && ((uint64_t)location->index + 8 > program->frame_size)))
            {
                ra_check_fail(result, RA_CHECK_INVALID, "resource id or eight-byte frame extent out of bounds", RA_CHECK_NONE, RA_CHECK_NONE);
            }
            for (uint32_t j = 0; j < i && result->status == RA_CHECK_VALID; j += 1)
            {
                if (location->kind == program->locations[j].kind && location->index == program->locations[j].index)
                {
                    ra_check_fail(result, RA_CHECK_INVALID, "duplicate resource identity", RA_CHECK_NONE, RA_CHECK_NONE);
                }
            }
        }
        uint64_t binding_symbols = 0;
        for (uint32_t i = 0; i < program->binding_count && result->status == RA_CHECK_VALID; i += 1)
        {
            const RaCheckBinding *binding = &program->bindings[i];
            if (binding->symbol >= program->symbol_count || binding->location >= program->location_count)
            {
                ra_check_fail(result, RA_CHECK_INVALID, "entry binding out of bounds", program->entry_block, RA_CHECK_NONE);
            }
            else
            {
                uint64_t bit = UINT64_C(1) << binding->symbol;
                if ((binding_symbols & bit) || workspace->entries[program->entry_block].facts[binding->location])
                {
                    ra_check_fail(result, RA_CHECK_INVALID, "entry binding symbol or location duplicated", program->entry_block, RA_CHECK_NONE);
                }
                for (uint32_t j = 0; j < i && result->status == RA_CHECK_VALID; j += 1)
                {
                    if (ra_check_alias(&program->locations[binding->location], &program->locations[program->bindings[j].location]))
                    {
                        ra_check_fail(result, RA_CHECK_INVALID, "entry bindings overlap without equality proof", program->entry_block, RA_CHECK_NONE);
                    }
                }
                if (result->status == RA_CHECK_VALID)
                {
                    binding_symbols |= bit;
                    workspace->entries[program->entry_block].facts[binding->location] = bit;
                    workspace->entries[program->entry_block].available_symbols |= bit;
                }
            }
        }
        uint32_t next_row = 0;
        for (uint32_t b = 0; b < program->block_count && result->status == RA_CHECK_VALID; b += 1)
        {
            const RaCheckBlock *block = &program->blocks[b];
            if (block->first_row != next_row || !ra_check_range(block->first_row, block->row_count, program->row_count) || block->parameter_count > RA_CHECK_LIMIT || (block->parameter_count && block->parameters == 0) || (b == program->entry_block && block->parameter_count))
            {
                ra_check_fail(result, RA_CHECK_INVALID, "blocks do not cover original row stream or malformed parameters", b, next_row);
            }
            else
            {
                uint64_t parameters = 0;
                for (uint32_t p = 0; p < block->parameter_count && result->status == RA_CHECK_VALID; p += 1)
                {
                    uint8_t symbol = block->parameters[p];
                    if (symbol >= program->symbol_count || (parameters & (UINT64_C(1) << symbol)))
                    {
                        ra_check_fail(result, RA_CHECK_INVALID, "duplicate or out-of-bounds block parameter", b, next_row);
                    }
                    else
                    {
                        parameters |= UINT64_C(1) << symbol;
                    }
                }
                next_row += block->row_count;
            }
        }
        if (next_row != program->row_count)
        {
            ra_check_fail(result, RA_CHECK_INVALID, "unconsumed original rows", RA_CHECK_NONE, next_row);
        }
        uint32_t next_edit = 0;
        for (uint32_t r = 0; r < program->row_count && result->status == RA_CHECK_VALID; r += 1)
        {
            const RaCheckRow *row = &program->rows[r];
            const RaCheckAllocatedRow *allocated = &program->allocated_rows[r];
            uint32_t uses = 0;
            switch (row->opcode)
            {
                case RA_CHECK_CONST64: uses = 0; break;
                case RA_CHECK_COPY64:
                case RA_CHECK_NEG64:
                case RA_CHECK_OBSERVE64: uses = 1; break;
                case RA_CHECK_ADD64:
                case RA_CHECK_SUB64:
                case RA_CHECK_AND64:
                case RA_CHECK_OR64:
                case RA_CHECK_XOR64:
                case RA_CHECK_IMUL64:
                case RA_CHECK_EARLY64: uses = 2; break;
                case RA_CHECK_CALL64: uses = row->use_count; break;
                default: ra_check_fail(result, RA_CHECK_UNCOVERED, "opcode lacks independent transfer semantics", RA_CHECK_NONE, r); break;
            }
            if (row->width_bits != 64 || allocated->width_bits != 64)
            {
                ra_check_fail(result, RA_CHECK_UNCOVERED, "non-full-width scalar instruction", RA_CHECK_NONE, r);
            }
            else if (row->opcode != allocated->opcode || row->payload != allocated->payload || row->use_count != uses || uses > 3 || row->early_clobber > 1 || (row->early_clobber && allocated->definition_location >= program->location_count) || (row->opcode == RA_CHECK_EARLY64 && !row->early_clobber) || (row->opcode != RA_CHECK_CALL64 && row->call_clobber_gprs))
            {
                ra_check_fail(result, RA_CHECK_INVALID, "opcode, payload, operand role or effect correspondence failed", RA_CHECK_NONE, r);
            }
            else if ((row->definition != RA_CHECK_NONE && row->definition >= program->symbol_count) || (row->definition == RA_CHECK_NONE && row->opcode != RA_CHECK_COPY64 && row->opcode != RA_CHECK_OBSERVE64 && row->opcode != RA_CHECK_CALL64) || (row->opcode == RA_CHECK_OBSERVE64 && (row->definition != RA_CHECK_NONE || allocated->definition_location != RA_CHECK_NONE)) || (ra_check_writes(row->opcode) && !(row->opcode == RA_CHECK_CALL64 && row->definition == RA_CHECK_NONE) && (allocated->definition_location >= program->location_count || program->locations[allocated->definition_location].kind != RA_CHECK_GPR)) || (row->opcode == RA_CHECK_CALL64 && row->definition == RA_CHECK_NONE && allocated->definition_location != RA_CHECK_NONE))
            {
                ra_check_fail(result, RA_CHECK_INVALID, "definition or physical output location malformed", RA_CHECK_NONE, r);
            }
            for (uint32_t u = 0; u < 3 && result->status == RA_CHECK_VALID; u += 1)
            {
                if (u < uses)
                {
                    if (row->uses[u] >= program->symbol_count || allocated->use_locations[u] >= program->location_count || program->locations[allocated->use_locations[u]].kind != RA_CHECK_GPR || (row->fixed_uses[u] != RA_CHECK_NONE && (row->fixed_uses[u] >= RA_CHECK_LIMIT || program->locations[allocated->use_locations[u]].index != row->fixed_uses[u])))
                    {
                        ra_check_fail(result, RA_CHECK_INVALID, "source or fixed-use location malformed", RA_CHECK_NONE, r);
                    }
                }
                else if (row->uses[u] != RA_CHECK_NONE || allocated->use_locations[u] != RA_CHECK_NONE || row->fixed_uses[u] != RA_CHECK_NONE)
                {
                    ra_check_fail(result, RA_CHECK_INVALID, "inactive operand is not absent", RA_CHECK_NONE, r);
                }
            }
            if (result->status == RA_CHECK_VALID && row->fixed_definition != RA_CHECK_NONE)
            {
                if (allocated->definition_location >= program->location_count || row->fixed_definition >= RA_CHECK_LIMIT || program->locations[allocated->definition_location].index != row->fixed_definition)
                {
                    ra_check_fail(result, RA_CHECK_INVALID, "fixed-definition constraint violated", RA_CHECK_NONE, r);
                }
            }
            if (result->status == RA_CHECK_VALID && row->tied_use != RA_CHECK_NONE)
            {
                if (row->tied_use >= uses || allocated->definition_location != allocated->use_locations[row->tied_use])
                {
                    ra_check_fail(result, RA_CHECK_INVALID, "tied-operand constraint violated", RA_CHECK_NONE, r);
                }
            }
            if (result->status == RA_CHECK_VALID && row->early_clobber)
            {
                for (uint32_t u = 0; u < uses && result->status == RA_CHECK_VALID; u += 1)
                {
                    if (u != row->tied_use && allocated->definition_location == allocated->use_locations[u])
                    {
                        ra_check_fail(result, RA_CHECK_INVALID, "early output aliases an untied source", RA_CHECK_NONE, r);
                    }
                }
            }
            if (result->status == RA_CHECK_VALID)
            {
                if ((allocated->before_count && allocated->first_before != next_edit) || (allocated->after_count && allocated->first_after != next_edit + allocated->before_count))
                {
                    ra_check_fail(result, RA_CHECK_INVALID, "row edits are not in execution stream order", RA_CHECK_NONE, r);
                }
                ra_check_owned_edits(program, workspace, result, allocated->first_before, allocated->before_count, RA_CHECK_BEFORE, r);
                ra_check_owned_edits(program, workspace, result, allocated->first_after, allocated->after_count, RA_CHECK_AFTER, r);
                if (result->status == RA_CHECK_VALID)
                {
                    next_edit += allocated->before_count + allocated->after_count;
                }
            }
        }
        for (uint32_t e = 0; e < program->edge_count && result->status == RA_CHECK_VALID; e += 1)
        {
            const RaCheckEdge *edge = &program->edges[e];
            if (edge->source_block >= program->block_count || edge->destination_block >= program->block_count || edge->argument_count > RA_CHECK_LIMIT || (edge->argument_count && edge->arguments == 0))
            {
                ra_check_fail(result, RA_CHECK_INVALID, "malformed original CFG edge", RA_CHECK_NONE, RA_CHECK_NONE);
            }
            else if (edge->argument_count != program->blocks[edge->destination_block].parameter_count)
            {
                ra_check_fail(result, RA_CHECK_INVALID, "edge arguments do not cover destination parameters", edge->destination_block, RA_CHECK_NONE);
            }
            else
            {
                for (uint32_t p = 0; p < edge->argument_count && result->status == RA_CHECK_VALID; p += 1)
                {
                    if (edge->arguments[p] >= program->symbol_count)
                    {
                        ra_check_fail(result, RA_CHECK_INVALID, "edge argument symbol out of bounds", edge->source_block, RA_CHECK_NONE);
                    }
                }
                if (edge->edit_count && edge->first_edit != next_edit)
                {
                    ra_check_fail(result, RA_CHECK_INVALID, "edge edits are not in serialized edge order", edge->source_block, RA_CHECK_NONE);
                }
                ra_check_owned_edits(program, workspace, result, edge->first_edit, edge->edit_count, RA_CHECK_EDGE, e);
                if (result->status == RA_CHECK_VALID)
                {
                    next_edit += edge->edit_count;
                }
            }
        }
        for (uint32_t i = 0; i < program->edit_count && result->status == RA_CHECK_VALID; i += 1)
        {
            const RaCheckEdit *edit = &program->edits[i];
            if (!workspace->edit_seen[i])
            {
                ra_check_fail(result, RA_CHECK_INVALID, "unconsumed edit or unsupported edit phase", RA_CHECK_NONE, edit->owner);
            }
            else if (edit->width_bits != 64 || edit->kind > RA_CHECK_REMATERIALIZE64)
            {
                ra_check_fail(result, RA_CHECK_UNCOVERED, "edit lacks full-width independent semantics", RA_CHECK_NONE, edit->owner);
            }
            else if (edit->destination_location >= program->location_count || (edit->kind != RA_CHECK_REMATERIALIZE64 && edit->source_location >= program->location_count))
            {
                ra_check_fail(result, RA_CHECK_INVALID, "edit location out of bounds", RA_CHECK_NONE, edit->owner);
            }
            else
            {
                uint32_t destination_kind = program->locations[edit->destination_location].kind;
                uint32_t source_kind = edit->kind == RA_CHECK_REMATERIALIZE64 ? RA_CHECK_GPR : program->locations[edit->source_location].kind;
                if ((edit->kind == RA_CHECK_MOVE64 && (source_kind != RA_CHECK_GPR || destination_kind != RA_CHECK_GPR)) || (edit->kind == RA_CHECK_SPILL64 && (source_kind != RA_CHECK_GPR || destination_kind != RA_CHECK_STACK)) || (edit->kind == RA_CHECK_RELOAD64 && (source_kind != RA_CHECK_STACK || destination_kind != RA_CHECK_GPR)) || (edit->kind == RA_CHECK_REMATERIALIZE64 && (destination_kind != RA_CHECK_GPR || edit->source_location != RA_CHECK_NONE)))
                {
                    ra_check_fail(result, RA_CHECK_INVALID, "edit resource class mismatches operation", RA_CHECK_NONE, edit->owner);
                }
                if (result->status == RA_CHECK_VALID && edit->kind == RA_CHECK_REMATERIALIZE64)
                {
                    if (edit->recipe_row >= program->row_count || program->rows[edit->recipe_row].opcode != RA_CHECK_CONST64 || program->rows[edit->recipe_row].payload != edit->payload)
                    {
                        ra_check_fail(result, RA_CHECK_INVALID, "rematerialization is not bound to an original literal recipe", RA_CHECK_NONE, edit->owner);
                    }
                    else
                    {
                        uint8_t symbol = program->rows[edit->recipe_row].definition;
                        uint32_t definitions = 0;
                        for (uint32_t r = 0; r < program->row_count; r += 1)
                        {
                            definitions += program->rows[r].definition == symbol;
                        }
                        for (uint32_t b = 0; b < program->block_count; b += 1)
                        {
                            for (uint32_t p = 0; p < program->blocks[b].parameter_count; p += 1)
                            {
                                definitions += program->blocks[b].parameters[p] == symbol;
                            }
                        }
                        if (definitions != 1 || (binding_symbols & (UINT64_C(1) << symbol)))
                        {
                            ra_check_fail(result, RA_CHECK_UNCOVERED, "rematerialization recipe symbol is not immutable", RA_CHECK_NONE, edit->owner);
                        }
                    }
                }
            }
        }
    }
}

static void ra_check_edits(const RaCheckProgram *program, RaCheckState *state, RaCheckResult *result, uint32_t first, uint32_t count, int validate, uint32_t block, uint32_t row)
{
    for (uint32_t i = first; i < first + count && result->status == RA_CHECK_VALID; i += 1)
    {
        const RaCheckEdit *edit = &program->edits[i];
        uint64_t facts;
        result->transfer_edits += 1;
        if (edit->kind == RA_CHECK_REMATERIALIZE64)
        {
            facts = UINT64_C(1) << program->rows[edit->recipe_row].definition;
            if (validate && !(state->available_symbols & facts))
            {
                ra_check_fail(result, RA_CHECK_NOT_PROVEN, "literal rematerialization precedes its reaching definition", block, row);
            }
            facts &= state->available_symbols;
        }
        else
        {
            facts = state->facts[edit->source_location];
        }
        ra_check_kill_location(program, state, edit->destination_location);
        state->facts[edit->destination_location] = facts;
    }
}

static void ra_check_transfer(const RaCheckProgram *program, uint32_t block_index, RaCheckState *state, RaCheckResult *result, int validate)
{
    const RaCheckBlock *block = &program->blocks[block_index];
    for (uint32_t r = block->first_row; r < block->first_row + block->row_count && result->status == RA_CHECK_VALID; r += 1)
    {
        const RaCheckRow *row = &program->rows[r];
        const RaCheckAllocatedRow *allocated = &program->allocated_rows[r];
        result->transfer_rows += 1;
        ra_check_edits(program, state, result, allocated->first_before, allocated->before_count, validate, block_index, r);
        for (uint32_t u = 0; u < row->use_count && validate && result->status == RA_CHECK_VALID; u += 1)
        {
            uint64_t bit = UINT64_C(1) << row->uses[u];
            if (!(state->facts[allocated->use_locations[u]] & bit) || !(state->available_symbols & bit))
            {
                ra_check_fail(result, RA_CHECK_NOT_PROVEN, "source location does not prove the required reaching value", block_index, r);
            }
        }
        if (result->status == RA_CHECK_VALID)
        {
            uint64_t copied = row->opcode == RA_CHECK_COPY64 ? state->facts[allocated->use_locations[0]] : 0;
            uint64_t defined = row->definition == RA_CHECK_NONE ? 0 : UINT64_C(1) << row->definition;
            if (row->opcode == RA_CHECK_CALL64)
            {
                for (uint32_t l = 0; l < program->location_count; l += 1)
                {
                    if (program->locations[l].kind == RA_CHECK_GPR && (row->call_clobber_gprs & (UINT64_C(1) << program->locations[l].index)))
                    {
                        state->facts[l] = 0;
                    }
                }
            }
            if (ra_check_writes(row->opcode) && !(row->opcode == RA_CHECK_CALL64 && row->definition == RA_CHECK_NONE))
            {
                ra_check_kill_symbols(program, state, defined);
                ra_check_kill_location(program, state, allocated->definition_location);
                state->facts[allocated->definition_location] = (copied & ~defined) | defined;
                state->available_symbols |= defined;
            }
            ra_check_edits(program, state, result, allocated->first_after, allocated->after_count, validate, block_index, r);
        }
    }
}

static void ra_check_edge(const RaCheckProgram *program, uint32_t edge_index, RaCheckState *state, RaCheckResult *result, int validate)
{
    const RaCheckEdge *edge = &program->edges[edge_index];
    const RaCheckBlock *destination = &program->blocks[edge->destination_block];
    RaCheckState snapshot;
    uint64_t killed = 0;
    result->edge_transfers += 1;
    ra_check_edits(program, state, result, edge->first_edit, edge->edit_count, validate, edge->source_block, RA_CHECK_NONE);
    snapshot = *state;
    for (uint32_t p = 0; p < destination->parameter_count; p += 1)
    {
        killed |= UINT64_C(1) << destination->parameters[p];
    }
    ra_check_kill_symbols(program, state, killed);
    state->available_symbols &= ~killed;
    for (uint32_t p = 0; p < destination->parameter_count; p += 1)
    {
        uint64_t source = UINT64_C(1) << edge->arguments[p];
        uint64_t target = UINT64_C(1) << destination->parameters[p];
        if (snapshot.available_symbols & source)
        {
            state->available_symbols |= target;
            for (uint32_t l = 0; l < program->location_count; l += 1)
            {
                if (snapshot.facts[l] & source)
                {
                    state->facts[l] |= target;
                }
            }
        }
    }
}

static void ra_check_reachability(const RaCheckProgram *program, RaCheckWorkspace *workspace)
{
    int changed = 1;
    workspace->reachable[program->entry_block] = 1;
    while (changed)
    {
        changed = 0;
        for (uint32_t e = 0; e < program->edge_count; e += 1)
        {
            const RaCheckEdge *edge = &program->edges[e];
            if (workspace->reachable[edge->source_block] && !workspace->reachable[edge->destination_block])
            {
                workspace->reachable[edge->destination_block] = 1;
                changed = 1;
            }
        }
    }
}

static int ra_check_subset(const RaCheckProgram *program, const RaCheckState *a, const RaCheckState *b)
{
    int result = !(a->available_symbols & ~b->available_symbols);
    for (uint32_t l = 0; l < program->location_count; l += 1)
    {
        result &= !(a->facts[l] & ~b->facts[l]);
    }
    return result;
}

static int ra_check_meet(const RaCheckProgram *program, RaCheckState *destination, const RaCheckState *source)
{
    int result = 0;
    uint64_t available = destination->available_symbols & source->available_symbols;
    result |= available != destination->available_symbols;
    destination->available_symbols = available;
    for (uint32_t l = 0; l < program->location_count; l += 1)
    {
        uint64_t facts = destination->facts[l] & source->facts[l];
        result |= facts != destination->facts[l];
        destination->facts[l] = facts;
    }
    return result;
}

RaCheckResult ra_check(const RaCheckProgram *program, RaCheckWorkspace *workspace)
{
    RaCheckResult result = ra_check_result();
    ra_check_preflight(program, workspace, &result);
    if (result.status == RA_CHECK_VALID)
    {
        uint64_t top = ra_check_mask(program->symbol_count);
        uint64_t sweep_limit = UINT64_C(1) + (uint64_t)program->block_count * ((uint64_t)program->location_count + 1) * program->symbol_count;
        int changed = 1;
        ra_check_reachability(program, workspace);
        for (uint32_t b = 0; b < program->block_count; b += 1)
        {
            if (workspace->reachable[b] && b != program->entry_block)
            {
                workspace->entries[b].available_symbols = top;
                for (uint32_t l = 0; l < program->location_count; l += 1)
                {
                    workspace->entries[b].facts[l] = top;
                }
            }
        }
        while (changed && result.status == RA_CHECK_VALID)
        {
            changed = 0;
            result.sweeps += 1;
            if (result.sweeps > sweep_limit)
            {
                ra_check_fail(&result, RA_CHECK_UNCOVERED, "descending lattice convergence bound exceeded", RA_CHECK_NONE, RA_CHECK_NONE);
            }
            for (uint32_t b = 0; b < program->block_count && result.status == RA_CHECK_VALID; b += 1)
            {
                if (workspace->reachable[b])
                {
                    workspace->exits[b] = workspace->entries[b];
                    ra_check_transfer(program, b, &workspace->exits[b], &result, 0);
                }
            }
            for (uint32_t e = 0; e < program->edge_count && result.status == RA_CHECK_VALID; e += 1)
            {
                const RaCheckEdge *edge = &program->edges[e];
                if (workspace->reachable[edge->source_block])
                {
                    RaCheckState state = workspace->exits[edge->source_block];
                    ra_check_edge(program, e, &state, &result, 0);
                    changed |= ra_check_meet(program, &workspace->entries[edge->destination_block], &state);
                }
            }
        }
        for (uint32_t b = 0; b < program->block_count && result.status == RA_CHECK_VALID; b += 1)
        {
            if (workspace->reachable[b])
            {
                workspace->exits[b] = workspace->entries[b];
                ra_check_transfer(program, b, &workspace->exits[b], &result, 1);
            }
        }
        for (uint32_t e = 0; e < program->edge_count && result.status == RA_CHECK_VALID; e += 1)
        {
            if (workspace->reachable[program->edges[e].source_block])
            {
                RaCheckState state = workspace->exits[program->edges[e].source_block];
                ra_check_edge(program, e, &state, &result, 1);
            }
        }
    }
    return result;
}

RaCheckResult ra_check_certificate(const RaCheckProgram *program, RaCheckWorkspace *workspace, const RaCheckCertificate *certificate)
{
    RaCheckResult result = ra_check_result();
    if (certificate == 0 || certificate->present != 1 || certificate->version != RA_CHECK_VERSION || certificate->block_entries == 0)
    {
        ra_check_fail(&result, RA_CHECK_CERTIFICATE_REJECTED, "missing or malformed certificate", RA_CHECK_NONE, RA_CHECK_NONE);
    }
    else
    {
        ra_check_preflight(program, workspace, &result);
        if (result.status == RA_CHECK_VALID && certificate->state_count != program->block_count)
        {
            ra_check_fail(&result, RA_CHECK_CERTIFICATE_REJECTED, "certificate must cover exactly every original block", RA_CHECK_NONE, RA_CHECK_NONE);
        }
        if (result.status == RA_CHECK_VALID)
        {
            uint64_t allowed = ra_check_mask(program->symbol_count);
            ra_check_reachability(program, workspace);
            if (!ra_check_subset(program, &certificate->block_entries[program->entry_block], &workspace->entries[program->entry_block]))
            {
                ra_check_fail(&result, RA_CHECK_CERTIFICATE_REJECTED, "entry certificate asserts facts outside ABI contract", program->entry_block, RA_CHECK_NONE);
            }
            for (uint32_t b = 0; b < program->block_count && result.status == RA_CHECK_VALID; b += 1)
            {
                const RaCheckState *state = &certificate->block_entries[b];
                if ((state->available_symbols & ~allowed) || (!workspace->reachable[b] && state->available_symbols))
                {
                    ra_check_fail(&result, RA_CHECK_CERTIFICATE_REJECTED, "certificate has invalid or unreachable symbol facts", b, RA_CHECK_NONE);
                }
                for (uint32_t l = 0; l < RA_CHECK_LIMIT && result.status == RA_CHECK_VALID; l += 1)
                {
                    if ((state->facts[l] & ~state->available_symbols) || (l >= program->location_count && state->facts[l]) || (!workspace->reachable[b] && state->facts[l]))
                    {
                        ra_check_fail(&result, RA_CHECK_CERTIFICATE_REJECTED, "certificate has unbound, excess or unreachable location facts", b, RA_CHECK_NONE);
                    }
                }
                if (workspace->reachable[b] && result.status == RA_CHECK_VALID)
                {
                    workspace->entries[b] = *state;
                    workspace->exits[b] = *state;
                    ra_check_transfer(program, b, &workspace->exits[b], &result, 1);
                }
            }
            for (uint32_t e = 0; e < program->edge_count && result.status == RA_CHECK_VALID; e += 1)
            {
                const RaCheckEdge *edge = &program->edges[e];
                if (workspace->reachable[edge->source_block])
                {
                    RaCheckState state = workspace->exits[edge->source_block];
                    ra_check_edge(program, e, &state, &result, 1);
                    if (!ra_check_subset(program, &workspace->entries[edge->destination_block], &state))
                    {
                        ra_check_fail(&result, RA_CHECK_CERTIFICATE_REJECTED, "edge does not establish claimed destination invariant", edge->destination_block, RA_CHECK_NONE);
                    }
                }
            }
        }
        if (result.status == RA_CHECK_NOT_PROVEN)
        {
            result.status = RA_CHECK_CERTIFICATE_REJECTED;
            result.reason = "certificate insufficient to prove a supported reaching-value use";
        }
    }
    return result;
}
