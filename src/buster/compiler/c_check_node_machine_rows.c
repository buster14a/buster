// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #2850's step 2, X's rows on the window, first their shapes (measure-node-machine --shadow --window-rows;
// docs/one-core-path.md, "X's rows on the window"): the kinds and the classes of the entries X writes for a chunk's
// records, made from C's answers on the window (c_check_node_machine_window.c) with no walk up a tree and no dispatch
// a record. X (c_check_node_machine_expand.c's node_machine_expand) goes up an expression's postorder and writes each
// evaluated record's row to the tape: its own entries by its template, then its conversion's to its target. A census
// of its rows (the measurement record's x-rows-census-2850) found each part of a row a table's row by a key of the
// record's own bytes and its target's.
//
// Three sweeps over a chunk's records:
// - The reads (node_rows_window_reads): what the window's bytes don't hold, read for the records that need it, into
//   columns. A target's type's class and facts, where an evaluated record has a target that is not its type (the
//   types' words by the type; a record's type is a column the tests' sweep makes with the records' bytes, compared
//   with its target in registers; none is read for void); a member's offset, in its record's line; a truth's
//   operand's conversion's class (its record's line names the conversion's type); and of an address constant, whether
//   its object is a local's (the unit's objects' symbols). A sweep of masks compresses those records' numbers into two
//   lists, 64 records a step, and a loop a list reads a record at a time.
//   A sweep of its own, before the shapes': a step that read a register of bytes its own loops had just stored a byte
//   at a time waited for each of those stores, and a loop a block ended on a branch the block's count mispredicted.
// - The shapes (node_rows_window_shapes, 64 records a step): a byte for a record's own entries and one for its
//   conversion's, by X's rules (node_machine_own's case of the record's template; node_machine_convert's cases; and
//   the fold of an integer constant's conversion into its CONST, node_machine_fold_conversion): each of their terms
//   that a record on the window separates is a mask, and the comments there say of the others what makes them. A
//   shape below BUSTER_NODE_ROWS_TABLE is one entry of that kind and the record's class, or none; another is a row of
//   the table (NodeRowsTables), which has each entry's kind and its class's source. With them the two parts' lengths,
//   and the records that list their expression for X.
// - The rows (node_rows_window_write, 64 records a step): each part has four slots, an entry each; a slot's kinds and
//   classes of a block's 64 records are one lookup of the shapes each, the classes then made from their sources; the
//   eight slots' registers are transposed into eight records' rows a register; and each register's entries, the bytes
//   whose kind is one, are compressed into the chunk's two byte streams (its kinds' and its classes'). The compress
//   joins a record's two parts and the records' rows: no entry is written a record at a time, and a row's place is the
//   entries before it.
// An expression's tape is then its operand's records' rows, in a row in the streams, and its root's.
//
// What is listed for X, and left to the reference as C's listed expressions are: an expression with an update or an
// operator evaluated as branches (their rows, and what follows a row, are not made here); a conversion from or to a
// class past double's (a long double's, a 128-bit integer's, a half-precision one's, and an aggregate's and a complex
// type's, which have no class's number), which the templates refuse; and an integer constant converted to a floating
// class, whose fold reads its value. Listing is always safe.
//
// The entries' values are not made: the kinds and the classes alone. The reference is X on T's columns at reader
// time, whose tape the shadow keeps (NodeFrontTypes.tape_kinds and tape_classes): every expression decided on the
// window and taken here is compared with it, entry by entry (node_rows_window_compare). No compile takes any of it.
#ifndef BUSTER_C_CHECK_NODE_MACHINE_ROWS_INCLUDED
#define BUSTER_C_CHECK_NODE_MACHINE_ROWS_INCLUDED

// A shape: none (0); one entry of the kind its number is, of the record's class (below BUSTER_NODE_ROWS_TABLE: a
// typed kind's number is below it); or a row of the table.
// - An address constant's (node_machine_address's entries, and its CONVERT after them): BUSTER_NODE_ROWS_ADDRESS, and
//   two a variant (no object; a local's; a local's with an offset; a global's; a global's with an offset), the odd
//   one with the CONVERT.
// - A member's offset's two entries, and with its LOAD.
// - A truth's test: its operand's conversion's class's, or a narrow one's (two for narrow), and one for `!`.
// - A pointer's sum; its difference's; its subscript's with its LOAD.
// - A conversion to bool, a narrow class's the second; a conversion to the target's class; and an integer constant
//   whose conversion folded: its CONST of the target's class, which is U8 for bool.
#define BUSTER_NODE_ROWS_TABLE 64u
#define BUSTER_NODE_ROWS_ADDRESS 64u
#define BUSTER_NODE_ROWS_MEMBER 74u
#define BUSTER_NODE_ROWS_TRUTH 76u
#define BUSTER_NODE_ROWS_POINTER 80u
#define BUSTER_NODE_ROWS_BOOL 83u
#define BUSTER_NODE_ROWS_CONVERT 85u
#define BUSTER_NODE_ROWS_FOLDED 86u
#define BUSTER_NODE_ROWS_SHAPES 87u
// The own shape of a record whose template's rows are not made here (NodeRowsTables.templates): no entry, and its
// expression is listed.
#define BUSTER_NODE_ROWS_LATER 127u
// An entry's class's source, in a row of the table: the class itself, or the record's, its target's, or its first
// operand's conversion's.
#define BUSTER_NODE_ROWS_CLASS_OWN 0x80u
#define BUSTER_NODE_ROWS_CLASS_TARGET 0x81u
#define BUSTER_NODE_ROWS_CLASS_OPERAND 0x82u
// A part's slots, its most entries (an address constant's four; a conversion to bool's four), and a record's two
// parts': X's row's most (BUSTER_NODE_MACHINE_ROW_MOST), a quadword's bytes.
#define BUSTER_NODE_ROWS_SLOTS 4u
#define BUSTER_NODE_ROWS_MOST 8u

_Static_assert(BUSTER_NODE_ROWS_SHAPES <= BUSTER_NODE_ROWS_LATER && BUSTER_NODE_ROWS_LATER < 128 && BUSTER_TYPED_JUMP_VALUE < BUSTER_NODE_ROWS_TABLE &&
               2 * BUSTER_NODE_ROWS_SLOTS == BUSTER_NODE_ROWS_MOST &&
               BUSTER_NODE_ROWS_MOST == BUSTER_NODE_MACHINE_ROW_MOST && BUSTER_TYPED_CLASS_F64 == BUSTER_TYPED_CLASS_F32 + 1 && BUSTER_TYPED_CLASSES < BUSTER_NODE_ROWS_CLASS_OWN &&
               BUSTER_TYPED_CLASS_VOID == 0 && NODE_MACHINE_TEMPLATE_BRANCHES < 64 && BUSTER_TYPED_CONST != 0,
               "a shape is a typed kind's number or a row of the table, a record's two parts are a quadword, a class is no source's number, and void's is zero");

// The masks the shapes' sweep adds to a block's (NodeFactsReader.window_c_masks, past C's): the records that list
// their expression for X, and of them those that list it for a conversion the templates refuse and for an integer
// constant converted to a floating class.
#define BUSTER_NODE_ROWS_MASK_LISTED 5u
#define BUSTER_NODE_ROWS_MASK_REFUSED 6u
#define BUSTER_NODE_ROWS_MASK_FLOATED 7u

_Static_assert(BUSTER_NODE_ROWS_MASK_LISTED > BUSTER_NODE_CONTEXTS_MASK_WIDE && BUSTER_NODE_ROWS_MASK_FLOATED < BUSTER_NODE_CONTEXTS_MASKS, "the rows' masks are a block's last three");

// A record's bits the reads' loop makes of its line (NodeFactsReader.window_x_bits), each read of the records it is
// for: a member's offset is not zero; and an address's object is a local's.
#define BUSTER_NODE_ROWS_BIT_FAR 0x01u
#define BUSTER_NODE_ROWS_BIT_LOCAL 0x02u

// What the rows made of an expression C takes on the window (NodeFactsReader.x_verdicts): nothing (it is not C's);
// it is listed; or its tape is made.
#define BUSTER_NODE_ROWS_LISTED 1u
#define BUSTER_NODE_ROWS_TAKEN 2u

// Why an expression is listed for X, by its first record that lists it (the shadow's count).
#define BUSTER_NODE_ROWS_REASON_UPDATE 0u
#define BUSTER_NODE_ROWS_REASON_BRANCHES 1u
#define BUSTER_NODE_ROWS_REASON_CONVERSION 2u
#define BUSTER_NODE_ROWS_REASON_FLOATING 3u

_Static_assert(NODE_FACTS_COUNT_ROWS_LISTED_UPDATE + BUSTER_NODE_ROWS_REASON_BRANCHES == NODE_FACTS_COUNT_ROWS_LISTED_BRANCHES &&
               NODE_FACTS_COUNT_ROWS_LISTED_UPDATE + BUSTER_NODE_ROWS_REASON_CONVERSION == NODE_FACTS_COUNT_ROWS_LISTED_CONVERSION &&
               NODE_FACTS_COUNT_ROWS_LISTED_UPDATE + BUSTER_NODE_ROWS_REASON_FLOATING == NODE_FACTS_COUNT_ROWS_LISTED_FLOATING,
               "the listed expressions' counts are in their reasons' order");

typedef struct NodeRowsTables NodeRowsTables;
struct NodeRowsTables
{
    // A shape's entries by their slot: each one's kind (0: the shape has none there) and its class's source, by the
    // shape's number, 128 bytes a slot read as two registers.
    _Alignas(64) uint8_t kinds[BUSTER_NODE_ROWS_SLOTS][128];
    _Alignas(64) uint8_t sources[BUSTER_NODE_ROWS_SLOTS][128];
    // A shape's length by its number, 128 bytes read as two registers.
    _Alignas(64) uint8_t lengths[128];
    // A record's own shape by its template, where the template alone gives it: one entry of the record's class, or
    // BUSTER_NODE_ROWS_LATER for a template whose rows are not made here.
    _Alignas(64) uint8_t templates[64];
    // A binary operator's kind by its operator's code (check_lower_binary), 128 bytes read as two registers.
    _Alignas(64) uint8_t binaries[128];
    // The order the rows' write reads a block's records in, so that its transposition's registers hold eight records
    // in a row each: the unpacks interleave within a lane of 16 bytes.
    _Alignas(64) uint8_t order[64];
};

_Static_assert(sizeof(NodeRowsTables) <= 1408, "the reader's state has room for the rows' tables (node_facts_reader_begin)");

// The table's row `shape`: `count` entries, each a kind and its class's source.
static void node_rows_row(NodeRowsTables* tables, uint32_t shape, uint32_t count, const uint8_t (*entries)[2])
{
    for (uint32_t i = 0; i < count && i < BUSTER_NODE_ROWS_SLOTS; i += 1)
    {
        tables->kinds[i][shape] = entries[i][0];
        tables->sources[i][shape] = entries[i][1];
    }
    tables->lengths[shape] = (uint8_t)count;
}

static void node_rows_tables(NodeRowsTables* tables)
{
    memset(tables, 0, sizeof(*tables));
    for (uint32_t kind = 1; kind < BUSTER_NODE_ROWS_TABLE; kind += 1)
    {
        tables->kinds[0][kind] = (uint8_t)kind;
        tables->sources[0][kind] = BUSTER_NODE_ROWS_CLASS_OWN;
        tables->lengths[kind] = 1;
    }
    // The transposition leaves, in its register `i`'s lane `l`, the records it read at bytes 16 l + 2 i and the next:
    // those are records 8 i + 2 l and the next.
    for (uint32_t lane = 0; lane < 4; lane += 1)
    {
        for (uint32_t i = 0; i < 8; i += 1)
        {
            tables->order[16 * lane + 2 * i] = (uint8_t)(8 * i + 2 * lane);
            tables->order[16 * lane + 2 * i + 1] = (uint8_t)(8 * i + 2 * lane + 1);
        }
    }
    tables->templates[CHECK_LOWER_TEMPLATE_CONSTANT] = BUSTER_TYPED_CONST;
    tables->templates[CHECK_LOWER_TEMPLATE_LOCAL] = BUSTER_TYPED_LOCAL;
    tables->templates[CHECK_LOWER_TEMPLATE_GLOBAL] = BUSTER_TYPED_GLOBAL;
    tables->templates[CHECK_LOWER_TEMPLATE_STORE_LOCAL] = BUSTER_TYPED_STORE_LOCAL;
    tables->templates[CHECK_LOWER_TEMPLATE_STORE_GLOBAL] = BUSTER_TYPED_STORE_GLOBAL;
    tables->templates[CHECK_LOWER_TEMPLATE_STORE] = BUSTER_TYPED_STORE;
    tables->templates[NODE_MACHINE_TEMPLATE_CALL] = BUSTER_TYPED_CALL;
    // The templates whose rows are not made here: an update's, an operator's evaluated as branches, and a logical
    // operator's and a conditional's value's three, which C on the window makes of no record yet (it lists them).
    tables->templates[NODE_MACHINE_TEMPLATE_BRANCH] = BUSTER_NODE_ROWS_LATER;
    tables->templates[NODE_MACHINE_TEMPLATE_DECIDED] = BUSTER_NODE_ROWS_LATER;
    tables->templates[NODE_MACHINE_TEMPLATE_SECOND] = BUSTER_NODE_ROWS_LATER;
    tables->templates[NODE_MACHINE_TEMPLATE_UPDATE] = BUSTER_NODE_ROWS_LATER;
    tables->templates[NODE_MACHINE_TEMPLATE_BRANCHES] = BUSTER_NODE_ROWS_LATER;
    for (uint32_t op = 0; op < CHECK_OP_COUNT && op < 128; op += 1)
    {
        tables->binaries[op] = check_lower_binary((CheckOp)op);
    }
    // An address's rows (node_machine_address): a constant's alone; a local's or a global's, and its offset's two
    // entries; then, for a constant that is an rvalue of an integer's class, its CONVERT.
    for (uint32_t variant = 0; variant < 5; variant += 1)
    {
        for (uint32_t converted = 0; converted < 2; converted += 1)
        {
            uint8_t entries[4][2];
            uint32_t count = 0;
            entries[count][0] = variant == 0 ? BUSTER_TYPED_CONST : variant <= 2 ? BUSTER_TYPED_LOCAL_ADDRESS : BUSTER_TYPED_GLOBAL_ADDRESS;
            entries[count][1] = BUSTER_TYPED_CLASS_U64;
            count += 1;
            if (variant == 2 || variant == 4)
            {
                entries[count][0] = BUSTER_TYPED_CONST;
                entries[count][1] = BUSTER_TYPED_CLASS_I64;
                entries[count + 1][0] = BUSTER_TYPED_PTR_ADD;
                entries[count + 1][1] = BUSTER_TYPED_CLASS_U64;
                count += 2;
            }
            if (converted != 0)
            {
                entries[count][0] = BUSTER_TYPED_CONVERT;
                entries[count][1] = BUSTER_NODE_ROWS_CLASS_OWN;
                count += 1;
            }
            node_rows_row(tables, BUSTER_NODE_ROWS_ADDRESS + 2 * variant + converted, count, entries);
        }
    }
    {
        const uint8_t entries[3][2] = {
            { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64 },
            { BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64 },
            { BUSTER_TYPED_LOAD, BUSTER_NODE_ROWS_CLASS_OWN },
        };
        node_rows_row(tables, BUSTER_NODE_ROWS_MEMBER, 2, entries);
        node_rows_row(tables, BUSTER_NODE_ROWS_MEMBER + 1, 3, entries);
    }
    // A truth's test (node_machine_put_truth): a narrow class's value converted to I32 first, then compared with 0.
    for (uint32_t narrow = 0; narrow < 2; narrow += 1)
    {
        for (uint32_t negated = 0; negated < 2; negated += 1)
        {
            uint8_t entries[3][2];
            uint32_t count = 0;
            if (narrow != 0)
            {
                entries[count][0] = BUSTER_TYPED_CONVERT;
                entries[count][1] = BUSTER_TYPED_CLASS_I32;
                count += 1;
            }
            entries[count][0] = BUSTER_TYPED_CONST;
            entries[count][1] = narrow != 0 ? BUSTER_TYPED_CLASS_I32 : BUSTER_NODE_ROWS_CLASS_OPERAND;
            entries[count + 1][0] = negated != 0 ? BUSTER_TYPED_EQ : BUSTER_TYPED_NE;
            entries[count + 1][1] = BUSTER_TYPED_CLASS_I32;
            count += 2;
            node_rows_row(tables, BUSTER_NODE_ROWS_TRUTH + 2 * narrow + negated, count, entries);
        }
    }
    {
        const uint8_t sum[2][2] = {
            { BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64 },
            { BUSTER_TYPED_LOAD, BUSTER_NODE_ROWS_CLASS_OWN },
        };
        const uint8_t difference[2][2] = {
            { BUSTER_TYPED_NEG, BUSTER_TYPED_CLASS_I64 },
            { BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64 },
        };
        node_rows_row(tables, BUSTER_NODE_ROWS_POINTER, 1, sum);
        node_rows_row(tables, BUSTER_NODE_ROWS_POINTER + 1, 2, difference);
        node_rows_row(tables, BUSTER_NODE_ROWS_POINTER + 2, 2, sum);
    }
    {
        // A conversion to bool (node_machine_convert's): the value compared with 0, a narrow class's as an I32.
        const uint8_t wide[3][2] = {
            { BUSTER_TYPED_CONST, BUSTER_NODE_ROWS_CLASS_OWN },
            { BUSTER_TYPED_NE, BUSTER_TYPED_CLASS_I32 },
            { BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U8 },
        };
        const uint8_t narrow[4][2] = {
            { BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_I32 },
            { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32 },
            { BUSTER_TYPED_NE, BUSTER_TYPED_CLASS_I32 },
            { BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U8 },
        };
        const uint8_t convert[1][2] = {
            { BUSTER_TYPED_CONVERT, BUSTER_NODE_ROWS_CLASS_TARGET },
        };
        const uint8_t folded[1][2] = {
            { BUSTER_TYPED_CONST, BUSTER_NODE_ROWS_CLASS_TARGET },
        };
        node_rows_row(tables, BUSTER_NODE_ROWS_BOOL, 3, wide);
        node_rows_row(tables, BUSTER_NODE_ROWS_BOOL + 1, 4, narrow);
        node_rows_row(tables, BUSTER_NODE_ROWS_CONVERT, 1, convert);
        node_rows_row(tables, BUSTER_NODE_ROWS_FOLDED, 1, folded);
    }
}

typedef struct NodeRowsCounts NodeRowsCounts;
struct NodeRowsCounts
{
    // The records the reads' two loops read for: a target's word, where the target is not the record's type, and a line
    // or a table of the unit's (a member's, a truth's, an address constant's).
    uint64_t words;
    uint64_t lines;
};

// The reads of a chunk's `count` records: for the records that need them, a target's class and facts, a first
// operand's conversion's class and two bits of its line (BUSTER_NODE_ROWS_BIT_*), into the reader's columns; and a
// block's mask of the records that convert. A sweep of masks first, 64 records a step, which compresses the numbers of
// the records to read for into two lists; then a loop a list, a record at a time: an iteration is loads by a type's or
// an object's number and stores of bytes, each on its own, and a list's loop ends once a chunk. Another record's bytes
// in the columns are an earlier chunk's, and are read by no one: the shapes' sweep reads a column under the mask of
// the records it was written for.
static __attribute__((noinline)) void node_rows_window_reads(struct NodeFactsReader* state, uint32_t count, NodeRowsCounts* counts)
{
    const NodeMachine* machine = state->facts.machine;
    const NodeFactsRecord* records = state->facts.records;
    uint32_t* words = state->x_words;
    uint32_t* lines = state->x_lines;
    uint32_t word_count = 0;
    uint32_t line_count = 0;
    const __m512i lanes = _mm512_set_epi32(15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);
    const uint32_t blocks = (count + 63) / 64;
    for (uint32_t block = 0; block < blocks; block += 1)
    {
        const uint32_t x = block * 64;
        const __mmask64 alive = state->window_c_masks[BUSTER_NODE_CONTEXTS_MASKS * (uint64_t)block + BUSTER_NODE_CONTEXTS_MASK_ALIVE];
        const __m512i templates = _mm512_loadu_si512(state->window_c_templates + x);
        const __m512i flags = _mm512_loadu_si512(state->window_c_flags + x);
        const __mmask64 is_constant = _mm512_cmpeq_epi8_mask(templates, _mm512_set1_epi8(CHECK_LOWER_TEMPLATE_CONSTANT));
        const __mmask64 is_member = _mm512_cmpeq_epi8_mask(templates, _mm512_set1_epi8(CHECK_LOWER_TEMPLATE_MEMBER));
        const __mmask64 is_truth = _mm512_cmpeq_epi8_mask(templates, _mm512_set1_epi8(CHECK_LOWER_TEMPLATE_TRUTH));
        const __mmask64 has_object = _mm512_test_epi8_mask(flags, _mm512_set1_epi8(BUSTER_NODE_CONTEXTS_OBJECT));
        // The records whose line or whose object's symbol is read: a member's (its offset), a truth's (its operand's
        // conversion) and a constant's with an object, an address constant's: no other constant of the corpora's has
        // an object, and one that had would be read for, its bits read by no one. (No record here has the discarded
        // context, which writes a constant no entry: C gives it to a comma's value, a bit-field's assignment and an
        // aggregate's store under a discard, each of which C on the window lists or refuses, and its contexts' byte
        // here has an address's bit alone.)
        const uint64_t lined = alive & (is_member | is_truth | (is_constant & has_object));
        // A record whose target is one, and is not its type (its line's, which the tests' sweep made a column of):
        // its conversion's, whose target's word is read, but void's: its class is void's and it has no fact
        // (check_lower_type_class, check_lower_type_facts), the zeros a column's byte reads as under a mask. A
        // discarded value's conversion is to void. (The rule converts in a value's context alone, and that is no byte
        // here: a record read for its address has no target but a `.`'s base, whose target is T's conversion of it,
        // its own type: node_machine_member.)
        uint64_t converts = 0;
        uint64_t voided = 0;
        for (uint32_t quarter = 0; quarter < 4; quarter += 1)
        {
            const __m512i targets = _mm512_loadu_si512(state->window_c_targets + x + 16 * quarter);
            const __m512i types = _mm512_loadu_si512(state->window_x_types + x + 16 * quarter);
            converts |= (uint64_t)_mm512_mask_cmpneq_epi32_mask(_mm512_test_epi32_mask(targets, targets), targets, types) << (16 * quarter);
            voided |= (uint64_t)_mm512_cmpeq_epi32_mask(targets, _mm512_set1_epi32((int32_t)BUSTER_TYPE_VOID)) << (16 * quarter);
        }
        converts &= alive;
        const uint64_t read_words = converts & ~voided;
        state->window_x_reads[2 * (uint64_t)block] = converts;
        state->window_x_reads[2 * (uint64_t)block + 1] = read_words;
        // Their numbers, compressed onto the two lists: a register's room past a list's most numbers.
        for (uint32_t quarter = 0; quarter < 4; quarter += 1)
        {
            const __m512i numbers = _mm512_add_epi32(_mm512_set1_epi32((int32_t)(x + 16 * quarter)), lanes);
            const __mmask16 worded = (__mmask16)(read_words >> (16 * quarter));
            const __mmask16 read = (__mmask16)(lined >> (16 * quarter));
            _mm512_storeu_si512(words + word_count, _mm512_maskz_compress_epi32(worded, numbers));
            _mm512_storeu_si512(lines + line_count, _mm512_maskz_compress_epi32(read, numbers));
            word_count += (uint32_t)_mm_popcnt_u32(worded);
            line_count += (uint32_t)_mm_popcnt_u32(read);
        }
    }
    // A target's class and facts, its type's word's (node_machine_word).
    for (uint32_t i = 0; i < word_count; i += 1)
    {
        const uint32_t x = words[i];
        const uint32_t word = node_machine_word(machine, state->window_c_targets[x]);
        state->window_x_targets[x] = (uint8_t)word;
        state->window_x_facts[x] = (uint8_t)(word >> 8);
    }
    // A member's offset, a truth's operand's conversion's class and an address's object, each its record's line's, the
    // object's kind the unit's symbols' (node_machine_local). A member's offset is in bits, and none here is at a part
    // of a byte: a bit-field's record is a value the columns don't hold whole, which lists its expression in C's loop.
    // A record with no object reads the first object's symbol, and no one reads its bit.
    for (uint32_t i = 0; i < line_count; i += 1)
    {
        const uint32_t x = lines[i];
        const NodeFactsRecord* record = records + x;
        const uint32_t object = record->object;
        const bool local = machine->object_symbols[object < machine->object_count ? object : 0] == 0;
        state->window_x_bits[x] = (uint8_t)((record->member_offset != 0 ? BUSTER_NODE_ROWS_BIT_FAR : 0u) | (local ? BUSTER_NODE_ROWS_BIT_LOCAL : 0u));
        state->window_x_operands[x] = node_machine_class(machine, record->converted[0]);
    }
    counts->words += word_count;
    counts->lines += line_count;
}

// The shapes of a chunk's `count` records, 64 a step: each record's own shape and its conversion's and their lengths
// (a nibble each), into the reader's columns; and a block's three masks. From the window's bytes and the reads'
// columns and masks, with no load a record.
static __attribute__((noinline, aligned(64))) void node_rows_window_shapes(struct NodeFactsReader* state, const NodeFront* front, uint32_t count)
{
    const NodeRowsTables* tables = state->x_tables;
    const uint32_t blocks = (count + 63) / 64;
    for (uint32_t block = 0; block < blocks; block += 1)
    {
        const uint32_t x = block * 64;
        uint64_t* masks = state->window_c_masks + BUSTER_NODE_CONTEXTS_MASKS * (uint64_t)block;
        const __mmask64 alive = masks[BUSTER_NODE_CONTEXTS_MASK_ALIVE];
        const __mmask64 converts = state->window_x_reads[2 * (uint64_t)block];
        const __mmask64 worded = state->window_x_reads[2 * (uint64_t)block + 1];
        const __m512i templates = _mm512_loadu_si512(state->window_c_templates + x);
        const __m512i contexts = _mm512_loadu_si512(state->window_c_contexts + x);
        const __m512i classes = _mm512_loadu_si512(state->window_c_classes + x);
        const __m512i facts = _mm512_loadu_si512(state->window_c_facts + x);
        const __m512i standings = _mm512_loadu_si512(state->window_c_standings + x);
        const __m512i flags = _mm512_loadu_si512(state->window_c_flags + x);
        const __m512i codes = _mm512_and_si512(_mm512_loadu_si512(front->ops + x), _mm512_set1_epi8(0x7f));
        // Its template's case.
        const __mmask64 is_constant = _mm512_cmpeq_epi8_mask(templates, _mm512_set1_epi8(CHECK_LOWER_TEMPLATE_CONSTANT));
        const __mmask64 is_member = _mm512_cmpeq_epi8_mask(templates, _mm512_set1_epi8(CHECK_LOWER_TEMPLATE_MEMBER));
        const __mmask64 is_dereference = _mm512_cmpeq_epi8_mask(templates, _mm512_set1_epi8(CHECK_LOWER_TEMPLATE_DEREFERENCE));
        const __mmask64 is_truth = _mm512_cmpeq_epi8_mask(templates, _mm512_set1_epi8(CHECK_LOWER_TEMPLATE_TRUTH));
        const __mmask64 is_binary = _mm512_cmpeq_epi8_mask(templates, _mm512_set1_epi8(CHECK_LOWER_TEMPLATE_BINARY));
        const __mmask64 is_pointer = _mm512_cmpeq_epi8_mask(templates, _mm512_set1_epi8(CHECK_LOWER_TEMPLATE_POINTER));
        const __mmask64 is_unary = _mm512_cmpeq_epi8_mask(templates, _mm512_set1_epi8(CHECK_LOWER_TEMPLATE_UNARY));
        // What a record's own bytes say (node_machine_loads; a constant's kind and category, its standing's).
        const __mmask64 loads = _mm512_testn_epi8_mask(contexts, _mm512_set1_epi8(BUSTER_CHECK_LOWER_TEMPLATE_ADDRESS)) &
                                _mm512_testn_epi8_mask(facts, _mm512_set1_epi8(BUSTER_CHECK_LOWER_TYPE_ADDRESS));
        const __m512i constants = _mm512_and_si512(standings, _mm512_set1_epi8(0x1c));
        const __mmask64 address_constant = _mm512_cmpeq_epi8_mask(constants, _mm512_set1_epi8(CHECK_CONSTANT_ADDRESS << 2));
        const __mmask64 integer_constant = _mm512_cmpeq_epi8_mask(constants, _mm512_set1_epi8(CHECK_CONSTANT_INTEGER << 2));
        const __mmask64 rvalue = _mm512_cmpeq_epi8_mask(_mm512_and_si512(standings, _mm512_set1_epi8(0x03)), _mm512_set1_epi8(CHECK_RVALUE));
        const __mmask64 has_object = _mm512_test_epi8_mask(flags, _mm512_set1_epi8(BUSTER_NODE_CONTEXTS_OBJECT));
        const __m512i from_i8 = _mm512_sub_epi8(classes, _mm512_set1_epi8(BUSTER_TYPED_CLASS_I8));
        const __mmask64 below_u64 = _mm512_cmple_epu8_mask(from_i8, _mm512_set1_epi8(BUSTER_TYPED_CLASS_I64 - BUSTER_TYPED_CLASS_I8));
        const __mmask64 narrow = _mm512_cmple_epu8_mask(from_i8, _mm512_set1_epi8(BUSTER_TYPED_CLASS_U16 - BUSTER_TYPED_CLASS_I8));
        // An address's rows' records: an address constant. (A leaf read for its address that is no constant has the
        // address's template in C's rules, with the same entries; C on the window lists its expression, so no record
        // here has it. And none has the discarded context: the reads' comment.) What the reads made of a record's
        // line: a member's offset, and an address's object's kind. Its offset is its standing's to say: whatever
        // writes a record's value sets BUSTER_NODE_MACHINE_OFFSET for an offset that is not zero, and for no other
        // (node_machine_put_value and the members' and the pointers' rules, T's and the kernel's); the field's other
        // bit is a floating constant's, which no address has.
        const __mmask64 addressed = is_constant & address_constant;
        const __m512i bits = _mm512_loadu_si512(state->window_x_bits + x);
        const __mmask64 far = is_member & _mm512_test_epi8_mask(bits, _mm512_set1_epi8(BUSTER_NODE_ROWS_BIT_FAR));
        const __mmask64 local = _mm512_test_epi8_mask(bits, _mm512_set1_epi8(BUSTER_NODE_ROWS_BIT_LOCAL));
        const __mmask64 offset = _mm512_test_epi8_mask(standings, _mm512_set1_epi8((char)BUSTER_NODE_MACHINE_OFFSET));
        const __m512i operands = _mm512_loadu_si512(state->window_x_operands + x);
        const __mmask64 narrow_operand = _mm512_cmple_epu8_mask(_mm512_sub_epi8(operands, _mm512_set1_epi8(BUSTER_TYPED_CLASS_I8)),
                                                                _mm512_set1_epi8(BUSTER_TYPED_CLASS_U16 - BUSTER_TYPED_CLASS_I8));
        // Its own shape (node_machine_own): its template's one entry, then each case. A template whose rows are not
        // made here has the table's mark, and lists its expression.
        __m512i own = _mm512_permutexvar_epi8(templates, _mm512_load_si512(tables->templates));
        const __mmask64 later = _mm512_cmpeq_epi8_mask(own, _mm512_set1_epi8(BUSTER_NODE_ROWS_LATER));
        // An address: an object's is a global's or a local's (its bit, which the reads made for a record with an
        // object alone), and two more with an offset; no object's is a CONST; one more with a constant's CONVERT (an
        // rvalue of an integer's class below U64).
        __m512i address = _mm512_mask_mov_epi8(_mm512_set1_epi8(BUSTER_NODE_ROWS_ADDRESS + 6), local, _mm512_set1_epi8(BUSTER_NODE_ROWS_ADDRESS + 2));
        address = _mm512_mask_add_epi8(address, offset, address, _mm512_set1_epi8(2));
        address = _mm512_mask_mov_epi8(_mm512_set1_epi8(BUSTER_NODE_ROWS_ADDRESS), has_object, address);
        address = _mm512_mask_add_epi8(address, rvalue & below_u64, address, _mm512_set1_epi8(1));
        own = _mm512_mask_mov_epi8(own, addressed, address);
        // An access: its LOAD where it loads, after a member's offset's two entries where it has one.
        const __m512i load = _mm512_maskz_mov_epi8(loads, _mm512_set1_epi8(BUSTER_TYPED_LOAD));
        own = _mm512_mask_mov_epi8(own, is_member | is_dereference, load);
        own = _mm512_mask_mov_epi8(own, far, _mm512_mask_mov_epi8(_mm512_set1_epi8(BUSTER_NODE_ROWS_MEMBER), loads, _mm512_set1_epi8(BUSTER_NODE_ROWS_MEMBER + 1)));
        // A truth: its operand's conversion's class's test, a narrow one's, and `!`'s.
        __m512i truth = _mm512_mask_mov_epi8(_mm512_set1_epi8(BUSTER_NODE_ROWS_TRUTH), narrow_operand, _mm512_set1_epi8(BUSTER_NODE_ROWS_TRUTH + 2));
        truth = _mm512_mask_add_epi8(truth, _mm512_cmpeq_epi8_mask(codes, _mm512_set1_epi8(CHECK_OP_NOT)), truth, _mm512_set1_epi8(1));
        own = _mm512_mask_mov_epi8(own, is_truth, truth);
        own = _mm512_mask_mov_epi8(own, is_binary, node_window_table(tables->binaries, codes));
        // A pointer's sum; its difference; its subscript with its LOAD.
        __m512i pointer = _mm512_mask_mov_epi8(_mm512_set1_epi8(BUSTER_NODE_ROWS_POINTER), _mm512_cmpeq_epi8_mask(codes, _mm512_set1_epi8(CHECK_OP_SUBTRACT)),
                                               _mm512_set1_epi8(BUSTER_NODE_ROWS_POINTER + 1));
        pointer = _mm512_mask_mov_epi8(pointer, loads & _mm512_cmpeq_epi8_mask(codes, _mm512_set1_epi8(CHECK_OP_SUBSCRIPT)), _mm512_set1_epi8(BUSTER_NODE_ROWS_POINTER + 2));
        own = _mm512_mask_mov_epi8(own, is_pointer, pointer);
        own = _mm512_mask_mov_epi8(own, is_unary,
                                   _mm512_mask_mov_epi8(_mm512_set1_epi8(BUSTER_TYPED_NOT), _mm512_cmpeq_epi8_mask(codes, _mm512_set1_epi8(CHECK_OP_NEGATIVE)),
                                                        _mm512_set1_epi8(BUSTER_TYPED_NEG)));
        // Its conversion's (node_machine_convert): refused for a class past double's; a DISCARD to void; the test to
        // bool; else a CONVERT where the classes differ. The reads' columns have a target's class and facts for the
        // records whose target's word they read; void's are zeros. The rule's other terms are asked of no byte, each
        // for what makes it:
        // - An empty conversion (two types of one class, the target no bool): each line below writes it nothing. To
        //   another class's type alone is there a DISCARD or a CONVERT; the fold of an integer constant's is its own
        //   CONST again; and a void's conversion to a void is of one class.
        // - A complex's and a _BitInt's source: C gives a record of either no template (node_machine_classify's first
        //   test), which refuses its expression.
        // - A complex target, and an aggregate's on either side: neither has a class's number (check_lower_type_class),
        //   so its conversion is refused with the classes past double's, a long double's first. The rule reads an
        //   aggregate's target as an address's and refuses it for its source, an aggregate too.
        // - Two types of one class past double's: the rule has that conversion empty for a number's class (a long
        //   double's to a const one's), and here it is refused, which lists its expression. No record of such a class
        //   has a template but a cast's (C's rules), whose operand's conversion to it is refused already; and an
        //   aggregate's to an aggregate is refused by the rule as well.
        // - A _BitInt's target (an initialized object's alone): of its value's class the rule writes it no entry, and
        //   no fold; of another class it is a CONVERT, which the fold's mask keeps.
        const __m512i to_classes = _mm512_maskz_loadu_epi8(worded, state->window_x_targets + x);
        const __m512i to_facts = _mm512_maskz_loadu_epi8(worded, state->window_x_facts + x);
        const __mmask64 same_class = _mm512_cmpeq_epi8_mask(classes, to_classes);
        const __mmask64 to_bool = _mm512_test_epi8_mask(to_facts, _mm512_set1_epi8(BUSTER_CHECK_LOWER_TYPE_BOOL));
        const __mmask64 valued = _mm512_test_epi8_mask(to_classes, to_classes);
        // An integer constant's one CONST takes its conversion where it folds (check_lower_constant_conversion): to an
        // integer's class or bool's that is no _BitInt's; to float's or double's where its value converts exactly,
        // which is not read here. The constant's template with an integer constant's kind: a store of a constant has
        // the kind too, its value's (`a = b = 0`), and its own entry and its conversion's. Such a constant is of an
        // integer's class, and no aggregate or complex (node_machine_constant_takes), and C has no conversion of one
        // to an aggregate or an array.
        const __m512i to_from_i8 = _mm512_sub_epi8(to_classes, _mm512_set1_epi8(BUSTER_TYPED_CLASS_I8));
        const __mmask64 foldable = converts & is_constant & integer_constant & _mm512_testn_epi8_mask(to_facts, _mm512_set1_epi8((char)BUSTER_CHECK_LOWER_TYPE_BIT_INT));
        const __mmask64 folded = foldable & _mm512_cmple_epu8_mask(to_from_i8, _mm512_set1_epi8(BUSTER_TYPED_CLASS_U64 - BUSTER_TYPED_CLASS_I8));
        const __mmask64 floated = foldable & _mm512_cmple_epu8_mask(_mm512_sub_epi8(to_classes, _mm512_set1_epi8(BUSTER_TYPED_CLASS_F32)),
                                                                    _mm512_set1_epi8(BUSTER_TYPED_CLASS_F64 - BUSTER_TYPED_CLASS_F32));
        own = _mm512_mask_mov_epi8(own, folded, _mm512_set1_epi8(BUSTER_NODE_ROWS_FOLDED));
        // A refused one's shape is whichever of these its bytes give: it lists its expression, whose rows no one
        // reads.
        const __mmask64 converted = converts & ~folded;
        const __mmask64 moved = converted & ~same_class;
        const __mmask64 refused = converted & _mm512_cmpge_epu8_mask(_mm512_max_epu8(classes, to_classes), _mm512_set1_epi8(BUSTER_TYPED_CLASS_F80));
        // To another class's type, a DISCARD where the target is void and else a CONVERT; then, to bool from any
        // class, its test.
        __m512i conversion = _mm512_maskz_mov_epi8(moved, _mm512_mask_mov_epi8(_mm512_set1_epi8(BUSTER_TYPED_DISCARD), valued, _mm512_set1_epi8(BUSTER_NODE_ROWS_CONVERT)));
        conversion = _mm512_mask_mov_epi8(conversion, converted & to_bool,
                                          _mm512_mask_mov_epi8(_mm512_set1_epi8(BUSTER_NODE_ROWS_BOOL), narrow, _mm512_set1_epi8(BUSTER_NODE_ROWS_BOOL + 1)));
        own = _mm512_maskz_mov_epi8(alive, own);
        // The two parts' lengths, a nibble each.
        const __m512i own_lengths = node_window_table(tables->lengths, own);
        const __m512i conversion_lengths = node_window_table(tables->lengths, conversion);
        _mm512_storeu_si512(state->window_x_owns + x, own);
        _mm512_storeu_si512(state->window_x_conversions + x, conversion);
        _mm512_storeu_si512(state->window_x_lengths + x, _mm512_or_si512(own_lengths, _mm512_slli_epi16(conversion_lengths, 4)));
        // A record with such a template lists its expression whether it is evaluated or not. One that is not
        // evaluated converts nothing (the reads' mask), and in an expression C takes it is a store's or an update's
        // named destination, a callee called directly or a folded constant's operand (the rows of C's loop): none on
        // the corpora and none of the probes' is an update or an operator evaluated as branches (C lists a folded
        // `&&`, `||` and `?:` itself), and one that were would list its expression, which is safe.
        masks[BUSTER_NODE_ROWS_MASK_LISTED] = later | refused | floated;
        masks[BUSTER_NODE_ROWS_MASK_REFUSED] = refused;
        masks[BUSTER_NODE_ROWS_MASK_FLOATED] = floated;
    }
}

// A slot's registers of a block: the two parts' shapes' entry there, by `table` (NodeRowsTables.kinds or sources, a
// slot's 128 bytes).
static inline __attribute__((always_inline)) __m512i node_rows_slot(const uint8_t* table, __m512i shapes)
{
    return _mm512_permutex2var_epi8(_mm512_load_si512(table), shapes, _mm512_load_si512(table + 64));
}

// Eight registers of 64 records' bytes, a register a slot (`slots`: a record's own part's four, then its
// conversion's), into eight registers of eight records' rows, a quadword a record with a byte a slot: three rounds of
// unpacks, of bytes, of words and of doublewords. An unpack interleaves within a lane of 16 bytes, so register `i`
// has, in its lane `l`, what the slots have at bytes 16 l + 2 i and the next: the slots are read in the tables' order,
// which puts records 8 i to 8 i + 7 there.
static inline __attribute__((always_inline)) void node_rows_transpose(const __m512i* slots, __m512i* rows)
{
    __m512i words[8];
    __m512i doubles[8];
    for (uint32_t pair = 0; pair < 4; pair += 1)
    {
        words[2 * pair] = _mm512_unpacklo_epi8(slots[2 * pair], slots[2 * pair + 1]);
        words[2 * pair + 1] = _mm512_unpackhi_epi8(slots[2 * pair], slots[2 * pair + 1]);
    }
    for (uint32_t half = 0; half < 2; half += 1)
    {
        // A part's four slots' words: its first two slots' with its last two's, the low eight records then the high.
        doubles[4 * half] = _mm512_unpacklo_epi16(words[4 * half], words[4 * half + 2]);
        doubles[4 * half + 1] = _mm512_unpackhi_epi16(words[4 * half], words[4 * half + 2]);
        doubles[4 * half + 2] = _mm512_unpacklo_epi16(words[4 * half + 1], words[4 * half + 3]);
        doubles[4 * half + 3] = _mm512_unpackhi_epi16(words[4 * half + 1], words[4 * half + 3]);
    }
    for (uint32_t four = 0; four < 4; four += 1)
    {
        rows[2 * four] = _mm512_unpacklo_epi32(doubles[four], doubles[4 + four]);
        rows[2 * four + 1] = _mm512_unpackhi_epi32(doubles[four], doubles[4 + four]);
    }
}

// The rows of a chunk's `count` records, 64 a step, into the chunk's streams (NodeFactsReader.x_kinds and x_classes);
// the entries before each eight records' (x_cursors) and, in a record's eight, before its own (window_x_offsets): a
// row's place is the two together (node_rows_window_place). How many entries it wrote.
static __attribute__((noinline, aligned(64))) uint32_t node_rows_window_write(struct NodeFactsReader* state, uint32_t count)
{
    const NodeRowsTables* tables = state->x_tables;
    const __m512i order = _mm512_load_si512(tables->order);
    const __m512i own_source = _mm512_set1_epi8((char)BUSTER_NODE_ROWS_CLASS_OWN);
    const __m512i target_source = _mm512_set1_epi8((char)BUSTER_NODE_ROWS_CLASS_TARGET);
    const __m512i operand_source = _mm512_set1_epi8((char)BUSTER_NODE_ROWS_CLASS_OPERAND);
    uint32_t cursor = 0;
    const uint32_t blocks = (count + 63) / 64;
    for (uint32_t block = 0; block < blocks; block += 1)
    {
        const uint32_t x = block * 64;
        const __m512i lengths = _mm512_loadu_si512(state->window_x_lengths + x);
        // The block's records in the transposition's order: their two shapes and the three classes their entries' are.
        const __m512i owns = _mm512_permutexvar_epi8(order, _mm512_loadu_si512(state->window_x_owns + x));
        const __m512i conversions = _mm512_permutexvar_epi8(order, _mm512_loadu_si512(state->window_x_conversions + x));
        const __m512i own_classes = _mm512_permutexvar_epi8(order, _mm512_loadu_si512(state->window_c_classes + x));
        const __m512i target_classes = _mm512_permutexvar_epi8(order, _mm512_loadu_si512(state->window_x_targets + x));
        const __m512i operand_classes = _mm512_permutexvar_epi8(order, _mm512_loadu_si512(state->window_x_operands + x));
        // Each slot's kinds, and its classes from their sources: the record's own class in any slot of its own part
        // and its conversion's first; its target's in each part's first (a folded constant's and a CONVERT's, whose
        // targets' words the reads read); its operand's conversion's in its own part's first (a truth's). The other
        // slots' are classes themselves.
        __m512i kinds[BUSTER_NODE_ROWS_MOST];
        __m512i classes[BUSTER_NODE_ROWS_MOST];
        for (uint32_t slot = 0; slot < BUSTER_NODE_ROWS_SLOTS; slot += 1)
        {
            kinds[slot] = node_rows_slot(tables->kinds[slot], owns);
            kinds[BUSTER_NODE_ROWS_SLOTS + slot] = node_rows_slot(tables->kinds[slot], conversions);
            const __m512i sources = node_rows_slot(tables->sources[slot], owns);
            classes[slot] = _mm512_mask_mov_epi8(sources, _mm512_cmpeq_epi8_mask(sources, own_source), own_classes);
            classes[BUSTER_NODE_ROWS_SLOTS + slot] = node_rows_slot(tables->sources[slot], conversions);
        }
        const __m512i first_sources = classes[0];
        classes[0] = _mm512_mask_mov_epi8(classes[0], _mm512_cmpeq_epi8_mask(first_sources, target_source), target_classes);
        classes[0] = _mm512_mask_mov_epi8(classes[0], _mm512_cmpeq_epi8_mask(first_sources, operand_source), operand_classes);
        const __m512i conversion_sources = classes[BUSTER_NODE_ROWS_SLOTS];
        classes[BUSTER_NODE_ROWS_SLOTS] = _mm512_mask_mov_epi8(conversion_sources, _mm512_cmpeq_epi8_mask(conversion_sources, own_source), own_classes);
        classes[BUSTER_NODE_ROWS_SLOTS] = _mm512_mask_mov_epi8(classes[BUSTER_NODE_ROWS_SLOTS], _mm512_cmpeq_epi8_mask(conversion_sources, target_source), target_classes);
        __m512i kind_rows[8];
        __m512i class_rows[8];
        node_rows_transpose(kinds, kind_rows);
        node_rows_transpose(classes, class_rows);
        // Eight records' entries a register: the bytes whose kind is one, compressed into the streams, which have a
        // register's room past a chunk's most entries. The compress joins a record's two parts and the eight rows.
        for (uint32_t eight = 0; eight < 8; eight += 1)
        {
            const __mmask64 entries = _mm512_test_epi8_mask(kind_rows[eight], kind_rows[eight]);
            _mm512_storeu_si512(state->x_kinds + cursor, _mm512_maskz_compress_epi8(entries, kind_rows[eight]));
            _mm512_storeu_si512(state->x_classes + cursor, _mm512_maskz_compress_epi8(entries, class_rows[eight]));
            state->x_cursors[x / 8 + eight] = cursor;
            cursor += (uint32_t)_mm_popcnt_u64(entries);
        }
        // In each eight, the entries before each record: its eight's lengths' prefix sum, a quadword's product.
        const __m512i totals = _mm512_add_epi8(_mm512_and_si512(lengths, _mm512_set1_epi8(0x0f)), _mm512_and_si512(_mm512_srli_epi16(lengths, 4), _mm512_set1_epi8(0x0f)));
        _mm512_storeu_si512(state->window_x_offsets + x, _mm512_mullo_epi64(totals, _mm512_set1_epi64(0x0101010101010100)));
    }
    return cursor;
}

// The window's record `x`'s row's place in the chunk's streams: the entries before its eight records', and before it
// among them.
static inline uint32_t node_rows_window_place(const struct NodeFactsReader* state, uint32_t x)
{
    return state->x_cursors[x / 8] + state->window_x_offsets[x];
}

// Why the window's record `x` lists its expression for X (BUSTER_NODE_ROWS_REASON_*), where it does: by its block's
// masks, then by its template. The shadow's count.
static uint8_t node_rows_window_reason(const struct NodeFactsReader* state, uint32_t x)
{
    const uint64_t* masks = state->window_c_masks + BUSTER_NODE_CONTEXTS_MASKS * (uint64_t)(x / 64);
    const uint64_t bit = UINT64_C(1) << (x & 63);
    return (masks[BUSTER_NODE_ROWS_MASK_REFUSED] & bit) != 0                 ? BUSTER_NODE_ROWS_REASON_CONVERSION :
           (masks[BUSTER_NODE_ROWS_MASK_FLOATED] & bit) != 0                 ? BUSTER_NODE_ROWS_REASON_FLOATING :
           state->window_c_templates[x] == BUSTER_NODE_MACHINE_TEMPLATE_UPDATE ? BUSTER_NODE_ROWS_REASON_UPDATE :
                                                                               BUSTER_NODE_ROWS_REASON_BRANCHES;
}

// An expression's part, for each of the batch's expressions C takes on the window: it is listed for X where one of
// its operand's records lists it, and else its tape is its operand's records' rows and its root's. (With no branch an
// expression, another's places read as the window's first record's, the 9700X read it 0.19 ticks a record higher on
// `sqlite3.c`: 1.45 for 1.26.)
// A root's record of its own (a statement's discard, a control's CONDITION or a `switch`'s value, a `return`'s
// conversion) asks no test here. It has no target (node_contexts_window_part gives it its expression's, which only an
// initialization has, and that has no record of its own), so no conversion of its lists it; and of the templates whose
// rows are not made here C gives it one alone, a CONDITION's over `&&` or `||` (BUSTER_NODE_MACHINE_TEMPLATE_BRANCHES),
// whose operand C then takes with that template too, or doesn't take the expression: the operand's record lists it.
static __attribute__((noinline)) void node_rows_window_part(struct NodeFactsReader* state, const NodeFront* front)
{
    for (uint32_t e = 0; e < state->expression_count; e += 1)
    {
        uint8_t verdict = 0;
        if (state->c_verdicts[e] == BUSTER_NODE_CONTEXTS_TAKEN)
        {
            const uint32_t operand = state->operands[e];
            const uint32_t size = front->sizes[operand];
            const uint32_t first = operand + 1 - size;
            const bool listed = node_contexts_window_any(state->window_c_masks, BUSTER_NODE_ROWS_MASK_LISTED, first, size);
            verdict = (uint8_t)(listed ? BUSTER_NODE_ROWS_LISTED : BUSTER_NODE_ROWS_TAKEN);
        }
        state->x_verdicts[e] = verdict;
    }
}

// The entries of the window's record `x`'s row: its two parts' lengths.
static inline uint32_t node_rows_window_length(const struct NodeFactsReader* state, uint32_t x)
{
    const uint8_t lengths = state->window_x_lengths[x];
    return (uint32_t)(lengths & 0x0f) + (lengths >> 4);
}

// The reference's tape and the window's for the batch's expression `e`, taken here: whether they differ in their
// number of entries, in an entry's kind or in its class, or the reference refused a row. X on T's columns gave the
// first, kept by the expression's root's node (the reader's NodeFrontTypes.tape_kinds); `*unkept` where it kept none
// for want of room, which is compared with nothing. With the control (NodeFactsReader.x_fault), one of the
// reference's answers is read changed, an expression's first entry's kind, its last's class and its length in turn.
static bool node_rows_window_compare(struct NodeFactsReader* state, const NodeFront* front, uint32_t e, uint32_t* entries, bool* unkept)
{
    const NodeFrontTypes* types = state->types;
    const uint32_t record = state->records[e];
    const uint32_t operand = state->operands[e];
    const uint32_t size = front->sizes[operand];
    const uint32_t first = operand + 1 - size;
    const uint32_t last = first + size - 1;
    const bool rooted = state->counts[e] > size;
    const uint32_t start = node_rows_window_place(state, first);
    const uint32_t inner = node_rows_window_place(state, last) + node_rows_window_length(state, last) - start;
    const uint32_t outer = rooted ? node_rows_window_length(state, record) : 0;
    const uint32_t root = state->roots[e];
    const uint32_t expected = types->tape_counts[root];
    const uint8_t* kinds = types->tape_kinds + types->tape_firsts[root];
    const uint8_t* classes = types->tape_classes + types->tape_firsts[root];
    bool differs = false;
    *entries = inner + outer;
    *unkept = expected == UINT32_MAX;
    if (expected != UINT32_MAX)
    {
        const uint32_t turn = state->x_fault ? state->x_fault_turn % 3 : 3;
        const bool short_turn = expected == 0 && turn < 2;
        const uint32_t length = expected + (turn == 2 || short_turn ? 1u : 0u);
        differs = types->sweep_refusals[root] != NODE_MACHINE_TAKEN || inner + outer != length;
        for (uint32_t i = 0; !differs && i < length; i += 1)
        {
            const uint32_t at = i < inner ? start + i : node_rows_window_place(state, record) + (i - inner);
            const uint8_t kind = (uint8_t)(kinds[i] ^ (turn == 0 && i == 0 ? 1u : 0u));
            const uint8_t class = (uint8_t)(classes[i] ^ (turn == 1 && i == length - 1 ? 1u : 0u));
            differs = state->x_kinds[at] != kind || state->x_classes[at] != class;
        }
        state->x_fault_turn += state->x_fault ? 1u : 0u;
    }
    return differs;
}

// X's rows on the window for the chunk C has just answered: the shapes and the rows over its records, then each of
// the batch's expressions' part. Each expression taken here is then compared with the reference's tape, and each
// listed one counted by its first record's reason. The shadow runs the three a second time over the same chunk, for
// their ticks with their code and their constants at hand: they give the same answers.
static void node_rows_window_chunk(struct NodeFactsReader* state, const NodeFront* front)
{
    NodeFrontTypes* types = state->types;
    uint64_t* counts = types->facts_counts;
    const uint32_t count = front->count;
    NodeRowsCounts read = { 0 };
    const uint64_t t0 = __rdtsc();
    node_rows_window_reads(state, count, &read);
    const uint64_t reads_end = __rdtsc();
    node_rows_window_shapes(state, front, count);
    const uint64_t t1 = __rdtsc();
    const uint32_t written = node_rows_window_write(state, count);
    const uint64_t t2 = __rdtsc();
    node_rows_window_part(state, front);
    const uint64_t t3 = __rdtsc();
    NodeRowsCounts again = { 0 };
    node_rows_window_reads(state, count, &again);
    const uint64_t later_reads_end = __rdtsc();
    node_rows_window_shapes(state, front, count);
    const uint64_t t4 = __rdtsc();
    const uint32_t written_again = node_rows_window_write(state, count);
    const uint64_t t5 = __rdtsc();
    node_rows_window_part(state, front);
    const uint64_t t6 = __rdtsc();
    for (uint32_t e = 0; e < state->expression_count; e += 1)
    {
        const uint8_t verdict = state->x_verdicts[e];
        if (verdict != 0)
        {
            const uint32_t record = state->records[e];
            const uint32_t operand = state->operands[e];
            const uint32_t size = front->sizes[operand];
            const uint32_t first = operand + 1 - size;
            counts[NODE_FACTS_COUNT_ROWS_EXPRESSIONS] += 1;
            counts[NODE_FACTS_COUNT_ROWS_RECORDS] += state->counts[e];
            if (verdict == BUSTER_NODE_ROWS_LISTED)
            {
                // Its first record that lists it: one of its operand's (node_rows_window_part), before its root's.
                uint8_t reason = BUSTER_NODE_ROWS_REASON_BRANCHES;
                bool found = false;
                for (uint32_t r = 0; !found && r < state->counts[e]; r += 1)
                {
                    const uint32_t x = r < size ? first + r : record;
                    found = (state->window_c_masks[BUSTER_NODE_CONTEXTS_MASKS * (uint64_t)(x / 64) + BUSTER_NODE_ROWS_MASK_LISTED] >> (x & 63) & 1) != 0;
                    reason = found ? node_rows_window_reason(state, x) : reason;
                }
                counts[NODE_FACTS_COUNT_ROWS_LISTED_UPDATE + reason] += 1;
            }
            else
            {
                uint32_t entries = 0;
                bool unkept = false;
                const bool differs = node_rows_window_compare(state, front, e, &entries, &unkept);
                counts[NODE_FACTS_COUNT_ROWS_TAKEN] += 1;
                counts[NODE_FACTS_COUNT_ROWS_TAKEN_RECORDS] += state->counts[e];
                counts[NODE_FACTS_COUNT_ROWS_ENTRIES] += entries;
                counts[NODE_FACTS_COUNT_ROWS_UNKEPT] += unkept;
                counts[NODE_FACTS_COUNT_ROWS_DIFFERENCES] += differs;
                counts[NODE_FACTS_COUNT_ROWS_REFERENCE_TICKS] += state->x_ticks[e];
                if (differs && types->rows_shown < BUSTER_NODE_FRONT_LISTED)
                {
                    types->rows_tokens[types->rows_shown] = front->tokens[record];
                    types->rows_shown += 1;
                }
            }
        }
    }
    counts[NODE_FACTS_COUNT_ROWS_SWEPT] += count;
    counts[NODE_FACTS_COUNT_ROWS_WRITTEN] += written;
    counts[NODE_FACTS_COUNT_ROWS_AGAIN_DIFFERENCES] += written != written_again;
    counts[NODE_FACTS_COUNT_ROWS_WORDS] += read.words;
    counts[NODE_FACTS_COUNT_ROWS_LINES] += read.lines;
    counts[NODE_FACTS_COUNT_ROWS_READ_TICKS] += reads_end - t0;
    counts[NODE_FACTS_COUNT_ROWS_SHAPE_TICKS] += t1 - reads_end;
    counts[NODE_FACTS_COUNT_ROWS_WRITE_TICKS] += t2 - t1;
    counts[NODE_FACTS_COUNT_ROWS_PART_TICKS] += t3 - t2;
    counts[NODE_FACTS_COUNT_ROWS_LATER_READ_TICKS] += later_reads_end - t3;
    counts[NODE_FACTS_COUNT_ROWS_LATER_SHAPE_TICKS] += t4 - later_reads_end;
    counts[NODE_FACTS_COUNT_ROWS_LATER_WRITE_TICKS] += t5 - t4;
    counts[NODE_FACTS_COUNT_ROWS_LATER_PART_TICKS] += t6 - t5;
}

#endif
