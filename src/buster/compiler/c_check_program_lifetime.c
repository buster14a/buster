// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The lifetimes of a unit's block-scope objects (#177), for Tier 1's frame, which shares a slot between objects that are
// never live at once (tier1_emit_cfg_lifetimes). C gives an automatic object its block's lifetime: from the block's entry
// to its exit, however either happens (C17 6.2.4p5), whatever its declaration's place in the block. So each block that
// declares locals (CheckProgram.lifetime_first, by its closing brace) gets a CHECK_CONTROL_LIFETIME start where its
// statements begin, and an end where they end and before each break, continue or goto that leaves it
// (check_control_lifetimes_left); a return, a computed goto and the function's own end need none, since nothing follows
// them in the block, and an end missing on a path only makes its objects live longer. The lowering turns each into its
// locals' events (Tier0Function.lifetime_entries), and ir_lower into IR_LIFETIME markers.
//
// A block that a jump can enter other than through its opening brace has no lifetimes: its objects live throughout the
// function, as clang leaves an object a goto bypasses without lifetime markers (VarBypassDetector). Those are the blocks
// between a goto's label (or an asm goto's) and the innermost block that holds the jump too, between a case or default
// label and its switch (a switch's own block among them, whose opening brace never runs), and every block around a label
// whose address is taken. So is a statement expression's block (its region's), whose value the region computes after
// its statements. No emitted body reaches the asm goto part yet: the lowering refuses asm goto by name
// (CheckProgramAsm.reason, c_check_program_asm.c), and the part stands ready for when it takes one.
//
// Included by c_check_program.c. Only a program asked for them (CheckProgramOptions.lifetimes, the C driver's --tier1)
// computes them, so a compile without --tier1 pays nothing for them. Their passes are walks up the parse tree from a
// unit's local declarations, its jumps, its cases and its labels, each bounded by its depth: scalar, as the control
// stream they feed is (docs/tier1-frame.md's "Slot sharing between objects").

// Whether `token` is a local declaration of an object: the lowering's own test (check_lower_local_declaration), whose
// locals the program's bodies allocate.
static bool check_program_lifetime_local(const CheckInput* input, uint32_t token)
{
    return input->declarations[token] == token && input->declaration_bodies[token] < input->token_count &&
           input->declaration_kinds[token] == C_BINDING_OBJECT && !input->static_objects[token];
}

// The innermost block around `token` (its closing brace), or BUSTER_CHECK_NONE where the function's body is the
// innermost, or nothing is.
static uint32_t check_program_lifetime_block(const Parser* parser, uint32_t token)
{
    const uint32_t count = parser->tokens.count;
    uint32_t ancestor = parser->tree.parents[token];
    uint32_t steps = 0;
    while (ancestor < count && steps < count && parser->tree.roles[ancestor] != PARSE_ROLE_CLOSE_BLOCK && parser->tree.roles[ancestor] != PARSE_ROLE_CLOSE_BODY)
    {
        ancestor = parser->tree.parents[ancestor];
        steps += 1;
    }
    return ancestor < count && parser->tree.roles[ancestor] == PARSE_ROLE_CLOSE_BLOCK ? ancestor : BUSTER_CHECK_NONE;
}

// Each block's local declarations, linked in token order from `first` (by the block's closing brace, BUSTER_CHECK_NONE
// for a block without any) through `next` (by declaration); both have a token's room each. Returns the most
// CHECK_CONTROL_LIFETIME entries the writer can make of them: a start and an end per such block, and an end per block
// such a jump leaves, each jump (a token of role RETURN) counted for every such block around it.
static uint64_t check_program_lifetime_blocks(const Parser* parser, const CheckInput* input, uint32_t* first, uint32_t* next)
{
    const uint32_t count = input->token_count;
    uint64_t room = 0;
    memset(first, 0xff, ((uint64_t)count + 1) * sizeof(uint32_t));
    for (uint32_t token = count; token-- > 0;)
    {
        const uint32_t block = check_program_lifetime_local(input, token) ? check_program_lifetime_block(parser, token) : BUSTER_CHECK_NONE;
        if (block != BUSTER_CHECK_NONE)
        {
            room += first[block] == BUSTER_CHECK_NONE ? 2 : 0;
            next[token] = first[block];
            first[block] = token;
        }
    }
    for (uint32_t token = 0; token < count; token += 1)
    {
        uint32_t ancestor = parser->tree.roles[token] == PARSE_ROLE_RETURN ? parser->tree.parents[token] : count;
        uint32_t steps = 0;
        while (ancestor < count && steps < count && parser->tree.roles[ancestor] != PARSE_ROLE_CLOSE_BODY)
        {
            room += parser->tree.roles[ancestor] == PARSE_ROLE_CLOSE_BLOCK && first[ancestor] != BUSTER_CHECK_NONE ? 1 : 0;
            ancestor = parser->tree.parents[ancestor];
            steps += 1;
        }
    }
    return room;
}

// Takes the lifetimes of the blocks between `token` (a label's or a case's) and the first ancestor stamped `epoch`, or
// with `epoch` 0 every block around it: a jump enters them.
static void check_program_lifetime_bypass(const Parser* parser, const uint32_t* stamps, uint32_t epoch, uint32_t token, uint32_t* first)
{
    const uint32_t count = parser->tokens.count;
    uint32_t ancestor = token < count ? parser->tree.parents[token] : count;
    uint32_t steps = 0;
    while (ancestor < count && steps < count && (epoch == 0 || stamps[ancestor] != epoch) && parser->tree.roles[ancestor] != PARSE_ROLE_CLOSE_BODY)
    {
        if (parser->tree.roles[ancestor] == PARSE_ROLE_CLOSE_BLOCK)
        {
            first[ancestor] = BUSTER_CHECK_NONE;
        }
        ancestor = parser->tree.parents[ancestor];
        steps += 1;
    }
}

// Stamps `token` and each of its ancestors with `epoch`.
static void check_program_lifetime_stamp(const Parser* parser, uint32_t* stamps, uint32_t epoch, uint32_t token)
{
    const uint32_t count = parser->tokens.count;
    uint32_t ancestor = token;
    uint32_t steps = 0;
    while (ancestor < count && steps <= count)
    {
        stamps[ancestor] = epoch;
        ancestor = parser->tree.parents[ancestor];
        steps += 1;
    }
}

// Leaves out of `first` the blocks a jump enters other than through their opening brace, and the statement expressions'
// blocks. `stamps` has a token's room, which this fills; `addressed` (null without a label whose address is taken) marks
// those labels by token.
static void check_program_lifetime_entered(const CheckProgram* program, const Parser* parser, const uint8_t* addressed, uint32_t* stamps, uint32_t* first)
{
    const CheckInput* input = program->input;
    const uint32_t count = input->token_count;
    memset(stamps, 0, ((uint64_t)count + 1) * sizeof(uint32_t));
    uint32_t epoch = 0;
    for (uint32_t token = 0; token < count; token += 1)
    {
        const bool jump = parser->tree.roles[token] == PARSE_ROLE_RETURN && parser->tokens.kinds[token] == PARSE_KIND_GOTO && !check_input_indirect_goto(parser, token);
        const uint32_t label = jump ? input->label_targets[token] : BUSTER_CHECK_NONE;
        if (label < count)
        {
            epoch += 1;
            check_program_lifetime_stamp(parser, stamps, epoch, token);
            check_program_lifetime_bypass(parser, stamps, epoch, label, first);
        }
        if (addressed != NULL && addressed[token] != 0)
        {
            check_program_lifetime_bypass(parser, stamps, 0, token, first);
        }
    }
    for (uint32_t c = 0; c < program->case_count; c += 1)
    {
        epoch += 1;
        check_program_lifetime_stamp(parser, stamps, epoch, program->cases[c].owner);
        check_program_lifetime_bypass(parser, stamps, epoch, program->cases[c].token, first);
    }
    // An asm goto's labels, which its statement may jump to (unreached while the lowering refuses asm goto).
    for (uint32_t a = 0; a < input->asm_count; a += 1)
    {
        const CheckInputAsm* assembly = input->asm_statements + a;
        if (assembly->label_count != 0 && assembly->statement < count)
        {
            epoch += 1;
            check_program_lifetime_stamp(parser, stamps, epoch, assembly->statement);
            for (uint32_t l = 0; l < assembly->label_count; l += 1)
            {
                check_program_lifetime_bypass(parser, stamps, epoch, assembly->labels[l].target, first);
            }
        }
    }
    for (uint32_t r = 0; r < input->region_count; r += 1)
    {
        if (input->regions[r].block < count)
        {
            first[input->regions[r].block] = BUSTER_CHECK_NONE;
        }
    }
}
