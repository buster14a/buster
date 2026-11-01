// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The attributes a declaration adds after its entity's definition. Clang warns at each one that the attribute declaration
// must precede the definition, notes the definition, and drops the attribute (Sema::checkNewAttributesAfterDef): a
// definition keeps what the declarations up to it gave it (check_input_attribute_late_kind's classes).
//
// The declarations are scanned in source order, so an entity's inherited set is the declarations up to its definition's,
// frozen there: a later declaration that repeats one of them warns nowhere, as clang's does, since its definition carries
// it. A 'gnu_inline' whose own declaration isn't 'inline' is ignored wherever it stands, with clang's other message, and
// contributes nothing to a definition's set. The warning's place is each attribute's name token and its note the
// definition's declarator. Included after c_check_asm_input.c, whose declaration scan it uses.

// One bit per class: weak, constructor, destructor, used, retain, gnu_inline and a standard noreturn, which is not GNU's.
static uint8_t check_input_attribute_late_bit(const CheckInputAttribute* attribute)
{
    uint8_t bit = 0;
    switch (attribute->kind)
    {
        case CHECK_ATTRIBUTE_WEAK: bit = 1u << 0; break;
        case CHECK_ATTRIBUTE_CONSTRUCTOR: bit = 1u << 1; break;
        case CHECK_ATTRIBUTE_DESTRUCTOR: bit = 1u << 2; break;
        case CHECK_ATTRIBUTE_USED: bit = 1u << 3; break;
        case CHECK_ATTRIBUTE_RETAIN: bit = 1u << 4; break;
        case CHECK_ATTRIBUTE_GNU_INLINE: bit = 1u << 5; break;
        case CHECK_ATTRIBUTE_NORETURN: bit = 1u << 6; break;
        default: break;
    }
    return bit;
}

// What one declaration adds to its entity's inherited set. A 'gnu_inline' its declaration doesn't mark 'inline' is
// ignored wherever it stands, so it never becomes the set's: clang keeps warning a later one.
static uint8_t check_input_attribute_late_mask(const CheckInputAttributes* attributes, uint8_t function_flags)
{
    uint8_t mask = 0;
    for (uint32_t i = 0; attributes != NULL && i < attributes->count; i += 1)
    {
        const CheckInputAttribute* attribute = attributes->records + i;
        const bool missing_inline = attribute->kind == CHECK_ATTRIBUTE_GNU_INLINE && (function_flags & CHECK_INPUT_FUNCTION_INLINE) == 0;
        mask |= !missing_inline && check_input_attribute_late_kind(attribute) ? check_input_attribute_late_bit(attribute) : 0;
    }
    return mask;
}

// The unit's declarations in source order: each one after its entity's definition marks the attributes clang drops
// ignored, and `records`' next entry (where it isn't NULL) is that declaration's warning. The return is how many there
// are, so that a first walk counts them and a second publishes them; both see the same declarations, since the marks the
// first leaves don't change which attributes are of a late kind. A lane never runs this: a body that reads one of them
// leaves the unit to source order (check_input_attribute_read_item), whose serial reader owns the definitions' state.
static uint32_t check_input_attribute_late_walk(CheckInputReader* reader, CheckInputAttributeWarning* records)
{
    CheckInput* input = reader->output;
    const uint64_t mark = reader->scratch->length;
    const bool scalar = BUSTER_SCALAR_PATH(reader->parser->options.scalar);
    const uint32_t limit = reader->expression_limit < input->token_count ? reader->expression_limit : input->token_count;
    const uint64_t slots = (uint64_t)input->token_count + 1;
    // Per object: the late attributes the declarations up to its definition gave it, whether it is defined yet, and the
    // definition's declarator name token, its note's place.
    uint8_t* masks = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint8_t, slots);
    uint8_t* frozen = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint8_t, slots);
    uint32_t* definitions = BUSTER_ARENA_PUSH_ARRAY(reader->scratch, uint32_t, slots);
    input->resource |= masks == NULL || frozen == NULL || definitions == NULL;
    uint32_t count = 0;
    if (!input->resource)
    {
        memset(masks, 0, slots);
        memset(frozen, 0, slots);
        memset(definitions, 0xff, slots * sizeof(uint32_t));
        for (uint32_t first = 0; first < limit; first += 16)
        {
            for (uint32_t selected = check_input_asm_declared(input, first, limit, scalar); selected != 0; selected &= selected - 1)
            {
                const uint32_t token = first + (uint32_t)__builtin_ctz(selected);
                const uint32_t object = input->declarations[token] == token ? input->objects[token] : 0;
                if (object == 0 || object >= slots)
                {
                    continue;
                }
                CheckInputAttributes* attributes = !input->registers[token] ? input->attributes[token] : NULL;
                const uint8_t flags = input->function_flags != NULL ? input->function_flags[token] : 0;
                // A declaration of an entity whose definition came before it: clang warns for what the definition
                // doesn't carry and keeps the attribute out of the definition.
                const bool late = frozen[object] != 0 && !input->defined[token];
                for (uint32_t i = 0; late && attributes != NULL && i < attributes->count; i += 1)
                {
                    CheckInputAttribute* attribute = attributes->records + i;
                    if (!check_input_attribute_late_kind(attribute))
                    {
                        continue;
                    }
                    const bool missing_inline = attribute->kind == CHECK_ATTRIBUTE_GNU_INLINE && (flags & CHECK_INPUT_FUNCTION_INLINE) == 0;
                    const bool warned = missing_inline || (masks[object] & check_input_attribute_late_bit(attribute)) == 0;
                    attribute->ignored = true;
                    if (warned)
                    {
                        if (records != NULL && count < input->attribute_warning_count)
                        {
                            records[count] = (CheckInputAttributeWarning){
                                .token = attribute->name_token,
                                .definition = definitions[object],
                                .missing_inline = missing_inline,
                            };
                        }
                        count += 1;
                    }
                }
                if (frozen[object] == 0)
                {
                    masks[object] |= check_input_attribute_late_mask(attributes, flags);
                    if (input->defined[token])
                    {
                        frozen[object] = 1;
                        definitions[object] = token;
                    }
                }
            }
        }
    }
    arena_pop_to(reader->scratch, mark);
    return count;
}

// Publishes the declarations after a definition that add an ignored attribute: their attributes' `ignored` flags, which
// selection, emission and diagnostics read, and the warnings the driver renders with the definition as a note.
static void check_input_attribute_late_finish(CheckInputReader* reader)
{
    CheckInput* input = reader->output;
    if (reader->lane || input->attribute_warnings_prepared)
    {
        return;
    }
    input->attribute_warnings_prepared = true;
    const uint32_t count = check_input_attribute_late_walk(reader, NULL);
    CheckInputAttributeWarning* records = count != 0 && !input->resource ?
        BUSTER_ARENA_PUSH_ARRAY(reader->arena, CheckInputAttributeWarning, count) : NULL;
    input->resource |= count != 0 && records == NULL;
    // A count without records is a resource failure; the driver never indexes an absent array.
    input->attribute_warnings = records;
    input->attribute_warning_count = records != NULL ? count : 0;
    if (records != NULL)
    {
        check_input_attribute_late_walk(reader, records);
    }
}
