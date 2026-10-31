// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Source initializer history, separate from emitted writes. A typed parent/index path distinguishes overlapping
// union members and equal-sized nested records. Opaque aggregate/string bases survive child overlays; implicit
// list zeros create no child entries. Only roots containing possible designators need this sparse scratch tree.

typedef enum CheckInitHistoryKind
{
    CHECK_INIT_HISTORY_NONE,
    CHECK_INIT_HISTORY_LIST,
    CHECK_INIT_HISTORY_SCALAR,
    CHECK_INIT_HISTORY_OPAQUE,
} CheckInitHistoryKind;

struct CheckInitHistoryNode
{
    CheckInitHistoryNode* parent;
    CheckInitHistoryNode* active;
    uint64_t index;
    uint64_t child_effects;
    TypeId type;
    uint32_t parent_generation;
    uint32_t generation;
    uint32_t token;
    CheckInitHistoryKind kind;
    bool own_effects;
};

struct CheckInitHistoryPending
{
    CheckInitHistoryNode previous;
    uint32_t token;
};

static bool check_init_history_needed(const Parser* parser, uint32_t begin, uint32_t end)
{
    bool needed = false;
    for (uint32_t first = begin; !needed && first < end && parser->tokens.kinds[begin] == PARSE_KIND_L_BRACE; first += 64)
    {
        const uint32_t live = end - first < 64 ? end - first : 64;
        if (BUSTER_SCALAR_PATH(parser->options.scalar))
        {
            for (uint32_t i = 0; !needed && i < live; i += 1)
            {
                const uint8_t kind = parser->tokens.kinds[first + i];
                const uint8_t previous = first + i != 0 ? parser->tokens.kinds[first + i - 1] : 0;
                needed = (kind == PARSE_KIND_PERIOD || kind == PARSE_KIND_L_SQUARE) &&
                         (previous == PARSE_KIND_L_BRACE || previous == PARSE_KIND_COMMA);
            }
        }
        else
        {
            const __mmask64 lanes = live == 64 ? UINT64_MAX : (UINT64_C(1) << live) - 1;
            const __m512i kinds = _mm512_maskz_loadu_epi8(lanes, parser->tokens.kinds + first);
            uint64_t selected = (_mm512_cmpeq_epi8_mask(kinds, _mm512_set1_epi8(PARSE_KIND_PERIOD)) |
                                 _mm512_cmpeq_epi8_mask(kinds, _mm512_set1_epi8(PARSE_KIND_L_SQUARE))) & lanes;
            for (; !needed && selected != 0; selected &= selected - 1)
            {
                const uint32_t token = first + (uint32_t)__builtin_ctzll(selected);
                const uint8_t previous = token != 0 ? parser->tokens.kinds[token - 1] : 0;
                needed = previous == PARSE_KIND_L_BRACE || previous == PARSE_KIND_COMMA;
            }
        }
    }
    return needed;
}

static uint32_t check_init_history_slot(CheckInitHistoryNode* const* slots, uint32_t capacity,
                                        const CheckInitHistoryNode* parent, uint64_t index, uint32_t generation)
{
    uint64_t hash = ((uint64_t)(uintptr_t)parent >> 4) ^ (index * UINT64_C(0x9e3779b97f4a7c15)) ^ generation;
    hash ^= hash >> 32;
    uint32_t slot = (uint32_t)hash & (capacity - 1);
    while (slots[slot] != NULL && (slots[slot]->parent != parent || slots[slot]->index != index || slots[slot]->parent_generation != generation))
    {
        slot = (slot + 1) & (capacity - 1);
    }
    return slot;
}

static CheckInitHistoryNode* check_init_history_node(CheckInitWalker* walker, CheckInitHistoryNode* parent, uint64_t index, TypeId type)
{
    CheckInitHistoryNode* node = NULL;
    if (walker->history_enabled && !walker->semantic_suppressed && walker->result->status == CHECK_INIT_OK)
    {
        if (parent != NULL && walker->history_count == walker->history_capacity / 2)
        {
            const uint32_t capacity = walker->history_capacity == 0 ? 16 : walker->history_capacity <= UINT32_MAX / 2 ? walker->history_capacity * 2 : 0;
            CheckInitHistoryNode** slots = capacity != 0 ? BUSTER_ARENA_PUSH_ARRAY(walker->scratch, CheckInitHistoryNode*, capacity) : NULL;
            if (slots == NULL)
            {
                check_init_fail(walker, CHECK_INIT_RESOURCE, walker->warning_completion);
            }
            else
            {
                memset(slots, 0, capacity * sizeof(*slots));
                for (uint32_t i = 0; i < walker->history_capacity; i += 1)
                {
                    CheckInitHistoryNode* previous = walker->history_slots[i];
                    if (previous != NULL)
                    {
                        const uint32_t slot = check_init_history_slot(slots, capacity, previous->parent, previous->index, previous->parent_generation);
                        slots[slot] = previous;
                    }
                }
                walker->history_slots = slots;
                walker->history_capacity = capacity;
            }
        }
        const uint32_t slot = parent != NULL && walker->history_capacity != 0 ?
                              check_init_history_slot(walker->history_slots, walker->history_capacity, parent, index, parent->generation) : 0;
        if (walker->result->status == CHECK_INIT_OK)
        {
            node = parent != NULL ? walker->history_slots[slot] : NULL;
            if (node == NULL)
            {
                node = BUSTER_ARENA_PUSH_ARRAY(walker->scratch, CheckInitHistoryNode, 1);
                if (node == NULL)
                {
                    check_init_fail(walker, CHECK_INIT_RESOURCE, walker->warning_completion);
                }
                else
                {
                    *node = (CheckInitHistoryNode){
                        .parent = parent,
                        .index = index,
                        .type = type,
                        .parent_generation = parent != NULL ? parent->generation : 0,
                        .generation = 1,
                        .token = BUSTER_CHECK_NONE,
                    };
                    if (parent != NULL)
                    {
                        walker->history_slots[slot] = node;
                        walker->history_count += 1;
                    }
                }
            }
        }
    }
    return node;
}

static bool check_init_history_effects(const CheckInitHistoryNode* node)
{
    return node->own_effects || node->child_effects != 0;
}

static void check_init_history_effects_changed(CheckInitHistoryNode* node, bool previous)
{
    bool current = check_init_history_effects(node);
    while (node->parent != NULL && current != previous)
    {
        node = node->parent;
        const bool old_parent = check_init_history_effects(node);
        node->child_effects = current ? node->child_effects + 1 : node->child_effects - 1;
        previous = old_parent;
        current = check_init_history_effects(node);
    }
}

static void check_init_history_warning(CheckInitWalker* walker, const CheckInitHistoryNode* previous, uint32_t token, bool partial)
{
    if (previous->token != BUSTER_CHECK_NONE)
    {
        const CheckWarningEvent event = {
            .id = CHECK_WARNING_ID_INITIALIZER_OVERRIDES,
            .kind = CHECK_WARNING_EVENT_INITIALIZER_OVERRIDES,
            .token = token,
            .source = BUSTER_CHECK_WARNING_NO_SOURCE,
            .completion_kind = CHECK_WARNING_COMPLETE_AT_TOKEN,
            .completion = walker->warning_completion,
            .payload = { .token = BUSTER_CHECK_WARNING_NO_SOURCE, .data = partial },
        };
        const CheckWarningNote note = {
            .kind = CHECK_WARNING_NOTE_PRIOR_INITIALIZATION,
            .token = previous->token,
            .source = BUSTER_CHECK_WARNING_NO_SOURCE,
            .payload = { .token = BUSTER_CHECK_WARNING_NO_SOURCE, .data = !partial && check_init_history_effects(previous) },
        };
        check_init_semantic_warning(walker, &event, &note, 1);
    }
}

static CheckInitHistoryPending* check_init_history_save(CheckInitWalker* walker, const CheckInitHistoryNode* previous, uint32_t token)
{
    CheckInitHistoryPending* pending = NULL;
    if (previous != NULL && previous->token != BUSTER_CHECK_NONE)
    {
        pending = BUSTER_ARENA_PUSH_ARRAY(walker->scratch, CheckInitHistoryPending, 1);
        if (pending == NULL)
        {
            check_init_fail(walker, CHECK_INIT_RESOURCE, token);
        }
        else
        {
            *pending = (CheckInitHistoryPending){ .previous = *previous, .token = token };
        }
    }
    return pending;
}

static void check_init_history_replace(CheckInitWalker* walker, CheckInitHistoryNode* node, uint32_t token,
                                        CheckInitHistoryKind kind, bool effects, bool warn)
{
    if (node != NULL && walker->result->status == CHECK_INIT_OK)
    {
        if (node->generation == UINT32_MAX)
        {
            check_init_fail(walker, CHECK_INIT_RESOURCE, token);
        }
        else
        {
            if (warn)
            {
                check_init_history_warning(walker, node, token, false);
            }
            const bool previous = check_init_history_effects(node);
            node->generation += 1;
            node->token = token;
            node->kind = kind;
            node->own_effects = effects;
            node->child_effects = 0;
            node->active = NULL;
            check_init_history_effects_changed(node, previous);
        }
    }
}

static void check_init_history_enter(CheckInitWalker* walker, CheckInitHistoryNode* node, uint32_t token)
{
    if (node != NULL)
    {
        if (node->kind == CHECK_INIT_HISTORY_OPAQUE)
        {
            check_init_history_warning(walker, node, token, true);
        }
        else if (node->kind == CHECK_INIT_HISTORY_NONE)
        {
            check_init_history_replace(walker, node, token, CHECK_INIT_HISTORY_LIST, false, false);
        }
    }
}

static void check_init_history_value(CheckInitWalker* walker, const CheckInitSelection* selected, uint32_t token, uint32_t node, bool string, uint32_t flags)
{
    CheckInitHistoryNode* history = selected->history;
    if (history != NULL)
    {
        const CheckInitFrame* frame = walker->frames + walker->depth - 1;
        const bool fill = frame->braced && frame->history == history && !check_init_aggregate(walker->types, frame->type);
        const bool effects = (((node < walker->stream->count ? walker->stream->values[node].flags : 0) | flags) & BUSTER_CHECK_SIDE_EFFECT) != 0;
        if (fill)
        {
            const bool previous = check_init_history_effects(history);
            history->own_effects = effects;
            check_init_history_effects_changed(history, previous);
        }
        else if (frame->braced && frame->history == history && history->kind == CHECK_INIT_HISTORY_LIST)
        {
            // A braced string/vector whole value fills this new container. Its expression becomes the opaque
            // origin; the opening brace did not initialize the same subobject a second time.
            check_init_history_replace(walker, history, token, CHECK_INIT_HISTORY_OPAQUE, effects, false);
        }
        else
        {
            const CheckInitHistoryKind kind = string || check_init_aggregate(walker->types, selected->type) ? CHECK_INIT_HISTORY_OPAQUE : CHECK_INIT_HISTORY_SCALAR;
            check_init_history_replace(walker, history, token, kind, effects, true);
        }
    }
}

static void check_init_history_union(CheckInitWalker* walker, const CheckInitFrame* frame, uint32_t token)
{
    CheckInitHistoryNode* parent = frame->history;
    if (parent != NULL)
    {
        const TypeId type = type_strip_aligned(walker->types, frame->type);
        const uint32_t tag = walker->types->types.index[type_index(type)];
        const uint32_t member = walker->types->tags.member_begin[tag] + (uint32_t)frame->next;
        CheckInitHistoryNode* selected = check_init_history_node(walker, parent, frame->next, walker->types->members.type[member]);
        if (selected != NULL && parent->active != selected)
        {
            if (parent->active != NULL)
            {
                check_init_history_warning(walker, parent->active, frame->designator != BUSTER_CHECK_NONE ? frame->designator : token, false);
                check_init_history_replace(walker, parent->active, BUSTER_CHECK_NONE, CHECK_INIT_HISTORY_NONE, false, false);
            }
            parent->active = selected;
        }
    }
}
