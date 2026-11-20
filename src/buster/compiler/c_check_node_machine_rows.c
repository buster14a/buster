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
// Three sweeps over a chunk's records, and the updates' loop:
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
//   the table (NodeRowsTables), which has each entry's kind and its class's source. With them a row's entries'
//   number, and the records that list their expression for X.
// - Between the two, the updates' rows (node_rows_window_updates): an update's own row and its first row, which
//   comes after its destination's entries, are shapes of rows of the table too, written over the shapes' sweep's for
//   its record and its destination's by a loop over a list of the chunk's updates.
// - And the branches' rows (node_rows_window_branch): a value that an operator evaluated as branches tests has its
//   test and its branch after its row, a shape of the table in its conversion's part, which converts nothing; the
//   branch's kind is by its sense, which comes down from the condition's root. A loop down a list of the chunk's
//   `!`, `&&` and `||` evaluated as branches, from its end, gives each one's operands their senses and their tests'
//   shapes.
// - The rows (node_rows_window_write, 64 records a step): each part has four slots, an entry each; a slot's kinds and
//   classes of a block's 64 records are one lookup of the shapes each, the classes then made from their sources; the
//   eight slots' registers are transposed into eight records' rows a register; and each register's entries, the bytes
//   whose kind is one, are compressed into the chunk's two byte streams (its kinds' and its classes'). The compress
//   joins a record's two parts and the records' rows: no entry is written a record at a time, and a row's place is the
//   entries before it.
// An expression's tape is then its operand's records' rows, in a row in the streams, and its root's.
//
// What is listed for X, and left to the reference as C's listed expressions are: an expression with a value an
// operator evaluated as branches tests after a conversion of its own (its test has no room in its conversion's
// part), with a bool's update or with a postfix update whose destination's entries leave its first row no room; a
// conversion from or to a class past double's (a long double's, a 128-bit integer's, a half-precision one's, and an
// aggregate's and a complex type's, which have no class's number), which the templates refuse; and an integer
// constant converted to a floating class, whose fold reads its value. (A logical operator's and a conditional's
// value's templates list their expression too: C on the window makes them of no record.) Listing is always safe.
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
// An update's two rows (node_machine_update_first's and node_machine_update_last's), each in the slots its record has
// free; their slots are fixed, with none where an entry is not written, which the write's compress drops:
// - Its own row, the update's record's own part: `++`'s or `--`'s CONST of the class the operation is computed in, the
//   operation, the result's CONVERT back to the destination's class, and the store; two a variant, the odd one with
//   the CONVERT. A pointer's: its step's CONST, a NEG, the PTR_ADD and the store.
// - Its first row by name, in its destination's record, which is not evaluated and has no entry of its own in either
//   part: the read, a local's or a global's, and a postfix update's DUP, the own part's (BUSTER_NODE_ROWS_FIRST: two a
//   variant, the odd one with the DUP); and the old value's CONVERT to the computation's class, the conversion's
//   part's (BUSTER_NODE_ROWS_FIRST_CONVERT).
// - Its first row through an address, its destination's record's conversion's part (a record read for its address
//   converts nothing): a DUP of the address, the LOAD and the CONVERT.
// - A postfix update's first row through an address keeps the address and the old value in two temporaries, six
//   entries and the CONVERT: after its destination's own entries, in the slots they leave of its own part and then
//   its conversion's. Its destination's own part is then a row of its own entries and the first of the six
//   (BUSTER_NODE_ROWS_KEPT: none of its own and four; one, of each of three kinds, and three; a member's two, and
//   two), and its conversion's part the others (BUSTER_NODE_ROWS_KEPT_REST: three rows, two with the CONVERT), each
//   by the destination's own shape (NodeRowsTables.kept_owns and kept_conversions).
// A first row's CONVERT is its part's last slot's in each: the one slot of a conversion's part that reads the
// computation's class. The updates' loop writes the shapes (node_rows_window_update).
#define BUSTER_NODE_ROWS_UPDATE 87u
#define BUSTER_NODE_ROWS_UPDATE_POINTER 91u
#define BUSTER_NODE_ROWS_FIRST 95u
#define BUSTER_NODE_ROWS_FIRST_CONVERT 99u
#define BUSTER_NODE_ROWS_FIRST_ADDRESS 100u
#define BUSTER_NODE_ROWS_KEPT 102u
#define BUSTER_NODE_ROWS_KEPT_REST 107u
// A tested value's segment (node_machine_branch_hook's), its conversion's part's where it converts nothing: its test
// (node_machine_put_truth of its class, which is its target's there: a narrow one's CONVERT to I32, a CONST and NE)
// and its branch, where its truth is its sense's. Two a class's width, the odd one where true; the branches' loop
// writes the shape (node_rows_window_tested).
#define BUSTER_NODE_ROWS_TESTED 112u
#define BUSTER_NODE_ROWS_SHAPES 116u
// The own shape of a record whose rows are not made here (a template's, NodeRowsTables.templates; a destination's
// whose own entries leave a postfix update's first row no room): no entry, and its expression is listed.
#define BUSTER_NODE_ROWS_LATER 127u

_Static_assert(BUSTER_TYPED_BRANCH_TRUE == BUSTER_TYPED_BRANCH_FALSE + 1 && BUSTER_TYPED_CLASS_I8 == BUSTER_TYPED_CLASS_VOID + 1,
               "a tested value's branch's kind is its sense's sum (node_rows_tables), and the narrow classes are the first after void's (node_rows_window_tested)");
// An entry's class's source, in a row of the table: the class itself, or the record's, its target's, or its first
// operand's conversion's (an update's computation's, which is that conversion's; its destination's too, whose first
// row converts to it).
#define BUSTER_NODE_ROWS_CLASS_OWN 0x80u
#define BUSTER_NODE_ROWS_CLASS_TARGET 0x81u
#define BUSTER_NODE_ROWS_CLASS_OPERAND 0x82u
// An entry's kind's mark, in an update's own row: the kind is the record's, its operation's (a binary operator's by
// its operator, ADD and SUB for `++` and `--`: NodeRowsTables.operations) or its store's (by name a local's or a
// global's, through its address; a SET for a postfix update), which the updates' loop leaves in the bits' column.
#define BUSTER_NODE_ROWS_KIND_OPERATION 0x40u
#define BUSTER_NODE_ROWS_KIND_STORE 0x41u
// A part's slots, its most entries (an address constant's four; a conversion to bool's four), and a record's two
// parts': X's row's most (BUSTER_NODE_MACHINE_ROW_MOST), a quadword's bytes.
#define BUSTER_NODE_ROWS_SLOTS 4u
#define BUSTER_NODE_ROWS_MOST 8u

_Static_assert(BUSTER_NODE_ROWS_SHAPES <= BUSTER_NODE_ROWS_LATER && BUSTER_NODE_ROWS_LATER < 128 &&
               BUSTER_TYPED_JUMP_VALUE < BUSTER_NODE_ROWS_TABLE &&
               BUSTER_TYPED_KINDS <= BUSTER_NODE_ROWS_KIND_OPERATION && 2 * BUSTER_NODE_ROWS_SLOTS == BUSTER_NODE_ROWS_MOST &&
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
    // A postfix update's destination through its address: its own part's shape and its conversion's part's by the
    // shape of its own entries, without its first row's CONVERT and with it; BUSTER_NODE_ROWS_LATER where the first
    // row has no room after them. 128 bytes each, read as two registers.
    _Alignas(64) uint8_t kept_owns[2][128];
    _Alignas(64) uint8_t kept_conversions[2][128];
    // An update's operation's kind by its operator's code: a compound assignment's binary operator's, ADD for `++` and
    // SUB for `--`.
    _Alignas(64) uint8_t operations[128];
    // An update's own row's shape by its operator's code: an arithmetic one's, before its CONVERT's one more (two more
    // for `++` and `--`, with their CONST); and a pointer's (the same two, and one for the NEG of `--` and `-=`). 128
    // bytes each, read as two registers.
    _Alignas(64) uint8_t updates[128];
    _Alignas(64) uint8_t pointer_updates[128];
    // A block's records' places in it, 0 to 63: what the updates' sweep compresses into their list.
    _Alignas(64) uint8_t places[64];
};

_Static_assert(sizeof(NodeRowsTables) <= 2560, "the reader's state has room for the rows' tables (node_facts_reader_begin)");
_Static_assert(BUSTER_TYPED_SET == BUSTER_TYPED_STORE + 1 && BUSTER_TYPED_STORE_LOCAL == BUSTER_TYPED_STORE + 2 && BUSTER_TYPED_SET_LOCAL == BUSTER_TYPED_STORE + 3 &&
               BUSTER_TYPED_STORE_GLOBAL == BUSTER_TYPED_STORE + 4 && BUSTER_TYPED_SET_GLOBAL == BUSTER_TYPED_STORE + 5 &&
               CHECK_OP_POST_DECREMENT == CHECK_OP_PRE_INCREMENT + 3 && CHECK_OP_POST_DECREMENT == CHECK_OP_POST_INCREMENT + 1,
               "an update's store's kind is a sum of its bits (node_rows_window_update), and `++` and `--` are four operators in a row, the postfix two last");

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

// One entry of the table's row `shape`, in its slot `slot`: a row whose entries have fixed slots.
static void node_rows_place(NodeRowsTables* tables, uint32_t shape, uint32_t slot, uint8_t kind, uint8_t source)
{
    tables->kinds[slot][shape] = kind;
    tables->sources[slot][shape] = source;
    tables->lengths[shape] += 1;
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
    for (uint32_t place = 0; place < 64; place += 1)
    {
        tables->places[place] = (uint8_t)place;
    }
    tables->templates[CHECK_LOWER_TEMPLATE_CONSTANT] = BUSTER_TYPED_CONST;
    tables->templates[CHECK_LOWER_TEMPLATE_LOCAL] = BUSTER_TYPED_LOCAL;
    tables->templates[CHECK_LOWER_TEMPLATE_GLOBAL] = BUSTER_TYPED_GLOBAL;
    tables->templates[CHECK_LOWER_TEMPLATE_STORE_LOCAL] = BUSTER_TYPED_STORE_LOCAL;
    tables->templates[CHECK_LOWER_TEMPLATE_STORE_GLOBAL] = BUSTER_TYPED_STORE_GLOBAL;
    tables->templates[CHECK_LOWER_TEMPLATE_STORE] = BUSTER_TYPED_STORE;
    tables->templates[NODE_MACHINE_TEMPLATE_CALL] = BUSTER_TYPED_CALL;
    // The templates whose rows are not made here: a logical operator's and a conditional's value's three, which C on
    // the window makes of no record yet (it lists them). An update's template gives no shape: its record's is the
    // updates' loop's to write (node_rows_window_update). Nor does an operator's evaluated as branches, which writes
    // no entry of its own: its operands' tests are after their rows (node_rows_window_tested).
    tables->templates[NODE_MACHINE_TEMPLATE_BRANCH] = BUSTER_NODE_ROWS_LATER;
    tables->templates[NODE_MACHINE_TEMPLATE_DECIDED] = BUSTER_NODE_ROWS_LATER;
    tables->templates[NODE_MACHINE_TEMPLATE_SECOND] = BUSTER_NODE_ROWS_LATER;
    for (uint32_t op = 0; op < CHECK_OP_COUNT && op < 128; op += 1)
    {
        tables->binaries[op] = check_lower_binary((CheckOp)op);
        tables->operations[op] = op == CHECK_OP_PRE_INCREMENT || op == CHECK_OP_POST_INCREMENT ? BUSTER_TYPED_ADD :
                                 op == CHECK_OP_PRE_DECREMENT || op == CHECK_OP_POST_DECREMENT ? BUSTER_TYPED_SUB :
                                                                                                 check_lower_binary((CheckOp)op);
        const uint32_t stepped = op >= CHECK_OP_PRE_INCREMENT && op <= CHECK_OP_POST_DECREMENT ? 2u : 0u;
        const uint32_t negated = op == CHECK_OP_PRE_DECREMENT || op == CHECK_OP_POST_DECREMENT || op == CHECK_OP_SUBTRACT_ASSIGN ? 1u : 0u;
        tables->updates[op] = (uint8_t)(BUSTER_NODE_ROWS_UPDATE + stepped);
        tables->pointer_updates[op] = (uint8_t)(BUSTER_NODE_ROWS_UPDATE_POINTER + stepped + negated);
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
    // An update's own row (node_machine_update_last), an entry a slot: `++`'s or `--`'s CONST; the operation; the
    // result's CONVERT back; the store. And a pointer's: its step's CONST, a NEG for a subtraction, the PTR_ADD, the
    // store.
    for (uint32_t stepped = 0; stepped < 2; stepped += 1)
    {
        for (uint32_t second = 0; second < 2; second += 1)
        {
            const uint32_t shape = BUSTER_NODE_ROWS_UPDATE + 2 * stepped + second;
            const uint32_t pointer = BUSTER_NODE_ROWS_UPDATE_POINTER + 2 * stepped + second;
            if (stepped != 0)
            {
                node_rows_place(tables, shape, 0, BUSTER_TYPED_CONST, BUSTER_NODE_ROWS_CLASS_OPERAND);
                node_rows_place(tables, pointer, 0, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I64);
            }
            node_rows_place(tables, shape, 1, BUSTER_NODE_ROWS_KIND_OPERATION, BUSTER_NODE_ROWS_CLASS_OPERAND);
            if (second != 0)
            {
                node_rows_place(tables, shape, 2, BUSTER_TYPED_CONVERT, BUSTER_NODE_ROWS_CLASS_OWN);
                node_rows_place(tables, pointer, 1, BUSTER_TYPED_NEG, BUSTER_TYPED_CLASS_I64);
            }
            node_rows_place(tables, pointer, 2, BUSTER_TYPED_PTR_ADD, BUSTER_TYPED_CLASS_U64);
            node_rows_place(tables, shape, 3, BUSTER_NODE_ROWS_KIND_STORE, BUSTER_NODE_ROWS_CLASS_OWN);
            node_rows_place(tables, pointer, 3, BUSTER_NODE_ROWS_KIND_STORE, BUSTER_NODE_ROWS_CLASS_OWN);
        }
    }
    // Its first row (node_machine_update_first), an entry a slot. By name: the read and a postfix update's DUP, the
    // destination's own part's, and the old value's CONVERT, its conversion's part's. Through an address: the
    // address's DUP, the LOAD and the CONVERT, the conversion's part's. A CONVERT is its part's last slot's.
    for (uint32_t global = 0; global < 2; global += 1)
    {
        for (uint32_t postfix = 0; postfix < 2; postfix += 1)
        {
            const uint32_t shape = BUSTER_NODE_ROWS_FIRST + 2 * global + postfix;
            node_rows_place(tables, shape, 0, global != 0 ? BUSTER_TYPED_GLOBAL : BUSTER_TYPED_LOCAL, BUSTER_NODE_ROWS_CLASS_OWN);
            if (postfix != 0)
            {
                node_rows_place(tables, shape, 1, BUSTER_TYPED_DUP, BUSTER_NODE_ROWS_CLASS_OWN);
            }
        }
    }
    node_rows_place(tables, BUSTER_NODE_ROWS_FIRST_CONVERT, BUSTER_NODE_ROWS_SLOTS - 1, BUSTER_TYPED_CONVERT, BUSTER_NODE_ROWS_CLASS_OPERAND);
    for (uint32_t converted = 0; converted < 2; converted += 1)
    {
        const uint32_t shape = BUSTER_NODE_ROWS_FIRST_ADDRESS + converted;
        node_rows_place(tables, shape, 0, BUSTER_TYPED_DUP, BUSTER_TYPED_CLASS_U64);
        node_rows_place(tables, shape, 1, BUSTER_TYPED_LOAD, BUSTER_NODE_ROWS_CLASS_OWN);
        if (converted != 0)
        {
            node_rows_place(tables, shape, BUSTER_NODE_ROWS_SLOTS - 1, BUSTER_TYPED_CONVERT, BUSTER_NODE_ROWS_CLASS_OPERAND);
        }
    }
    // A postfix update's first row through an address (node_machine_update_first's six entries: the address kept in a
    // temporary and read back, the old value loaded, kept in another, and the two read for the store and the value),
    // after its destination's own entries: `own` of them (0 to 2) in the own part's first slots, the six from there on.
    // A destination's own entries are an address's: none (a dereference's, a member's at no offset); one (a
    // subscript's PTR_ADD, a local's or a global's address); or two (a member's offset's). One with three (an address
    // and its offset) leaves the six no room; a pointer's difference is no lvalue; and C on the window decides no
    // update through an address constant with no object.
    {
        const uint8_t six[6][2] = {
            { BUSTER_TYPED_SET_LOCAL, BUSTER_TYPED_CLASS_U64 },   { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64 },
            { BUSTER_TYPED_LOAD, BUSTER_NODE_ROWS_CLASS_OWN },    { BUSTER_TYPED_STORE_LOCAL, BUSTER_NODE_ROWS_CLASS_OWN },
            { BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64 },       { BUSTER_TYPED_LOCAL, BUSTER_NODE_ROWS_CLASS_OWN },
        };
        // A destination's own shapes, by the entries of their own: the kept row's number after each.
        const uint8_t bases[BUSTER_NODE_ROWS_KEPT_REST - BUSTER_NODE_ROWS_KEPT][2] = {
            { 0, 0 },
            { BUSTER_NODE_ROWS_POINTER, 1 },
            { BUSTER_NODE_ROWS_ADDRESS + 2, 1 },
            { BUSTER_NODE_ROWS_ADDRESS + 6, 1 },
            { BUSTER_NODE_ROWS_MEMBER, 2 },
        };
        memset(tables->kept_owns, BUSTER_NODE_ROWS_LATER, sizeof(tables->kept_owns));
        memset(tables->kept_conversions, BUSTER_NODE_ROWS_LATER, sizeof(tables->kept_conversions));
        // The conversion's part's three rows, by the destination's own entries: the six's last two, three or four; and
        // the first two with the CONVERT after them, in the part's last slot.
        for (uint32_t own = 0; own < 3; own += 1)
        {
            for (uint32_t converted = 0; converted < 2 - own / 2; converted += 1)
            {
                const uint32_t shape = BUSTER_NODE_ROWS_KEPT_REST + 2 * own + converted;
                for (uint32_t i = 4 - own; i < 6; i += 1)
                {
                    node_rows_place(tables, shape, i - (4 - own), six[i][0], six[i][1]);
                }
                if (converted != 0)
                {
                    node_rows_place(tables, shape, BUSTER_NODE_ROWS_SLOTS - 1, BUSTER_TYPED_CONVERT, BUSTER_NODE_ROWS_CLASS_OPERAND);
                }
            }
        }
        for (uint32_t b = 0; b < BUSTER_ARRAY_LENGTH(bases); b += 1)
        {
            const uint32_t base = bases[b][0];
            const uint32_t own = bases[b][1];
            const uint32_t shape = BUSTER_NODE_ROWS_KEPT + b;
            for (uint32_t slot = 0; slot < own; slot += 1)
            {
                node_rows_place(tables, shape, slot, tables->kinds[slot][base], tables->sources[slot][base]);
            }
            for (uint32_t slot = own; slot < BUSTER_NODE_ROWS_SLOTS; slot += 1)
            {
                node_rows_place(tables, shape, slot, six[slot - own][0], six[slot - own][1]);
            }
            for (uint32_t converted = 0; converted < 2 - own / 2; converted += 1)
            {
                tables->kept_owns[converted][base] = (uint8_t)shape;
                tables->kept_conversions[converted][base] = (uint8_t)(BUSTER_NODE_ROWS_KEPT_REST + 2 * own + converted);
            }
        }
    }
    // A tested value's test and its branch (node_machine_branch_hook: node_machine_put_truth of its target's class,
    // then the branch where its truth is its sense's). In a conversion's part that is free the class tested is the
    // record's own.
    for (uint32_t narrow = 0; narrow < 2; narrow += 1)
    {
        for (uint32_t sense = 0; sense < 2; sense += 1)
        {
            uint8_t entries[4][2];
            uint32_t count = 0;
            if (narrow != 0)
            {
                entries[count][0] = BUSTER_TYPED_CONVERT;
                entries[count][1] = BUSTER_TYPED_CLASS_I32;
                count += 1;
            }
            entries[count][0] = BUSTER_TYPED_CONST;
            entries[count][1] = narrow != 0 ? BUSTER_TYPED_CLASS_I32 : BUSTER_NODE_ROWS_CLASS_OWN;
            entries[count + 1][0] = BUSTER_TYPED_NE;
            entries[count + 1][1] = BUSTER_TYPED_CLASS_I32;
            entries[count + 2][0] = (uint8_t)(BUSTER_TYPED_BRANCH_FALSE + sense);
            entries[count + 2][1] = BUSTER_TYPED_CLASS_I32;
            count += 3;
            node_rows_row(tables, BUSTER_NODE_ROWS_TESTED + 2 * narrow + sense, count, entries);
        }
    }
}

typedef struct NodeRowsCounts NodeRowsCounts;
struct NodeRowsCounts
{
    // The records the reads' two loops read for: a target's word, where the target is not the record's type, and a line
    // or a table of the unit's (a member's, a truth's, an address constant's). And the updates, which their own loop
    // reads for, and the operators evaluated as branches that take a turn of theirs.
    uint64_t words;
    uint64_t lines;
    uint64_t updates;
    uint64_t branched;
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

// The shapes of a chunk's `count` records, 64 a step: each record's own shape and its conversion's and its row's
// entries' number, into the reader's columns; and a block's three masks. From the window's bytes and the reads'
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
        // Its row's entries: its two parts' lengths' sum.
        const __m512i own_lengths = node_window_table(tables->lengths, own);
        const __m512i conversion_lengths = node_window_table(tables->lengths, conversion);
        _mm512_storeu_si512(state->window_x_owns + x, own);
        _mm512_storeu_si512(state->window_x_conversions + x, conversion);
        _mm512_storeu_si512(state->window_x_lengths + x, _mm512_add_epi8(own_lengths, conversion_lengths));
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

// The two rows of the update at the window's record `x` (node_machine_update_plan's choices, which
// node_machine_update_first and node_machine_update_last write), as shapes, over what the shapes' sweep made of its
// record and of its destination's, with their lengths; the kind its store's mark stands for and the class its
// operation is computed in, which both rows' entries take, where the rows' write reads them; and, where its rows are
// not made, the bit that lists its expression.
// From the window's bytes, the shapes' columns and one read of its line: T's conversion of its first operand, the
// computation's type. What the plan asks more is no term here, for what C on the window takes an update by (its
// loop's masks):
// - The stored class and facts, its destination's type's, are the update's own record's and its destination's
//   record's alike: an update's type is its destination's value's.
// - The class the operation is computed in is its computation's: check_lower_computed's other case is a narrow
//   class's, a _BitInt's alone (C promotes the others), and C takes no update of a _BitInt. The computation's type
//   is no bool, a promoted one.
// - A pointer's computation is its own class's, and a compound assignment's operator has a binary kind.
// - A destination by name has an object, and one through its address converts nothing (it has no target).
// - A division or a remainder by a power of two is a shift or a mask in the plan; C on the window lists every
//   compound division and remainder by an integer constant, so no update here has it.
// Each choice is a bit and each shape a sum or a table's byte, with no branch: an update's bits are no pattern a
// predictor learns, and the first build's selections read 49 ticks an update on `sqlite3.c`.
static inline __attribute__((always_inline)) void node_rows_window_update(struct NodeFactsReader* state, const NodeFront* front, uint32_t x)
{
    const NodeRowsTables* tables = state->x_tables;
    const uint32_t op = front->ops[x] & 0x7fu;
    const uint32_t destination = x - front->window_left[x];
    const uint32_t postfix = op - CHECK_OP_POST_INCREMENT <= (uint32_t)(CHECK_OP_POST_DECREMENT - CHECK_OP_POST_INCREMENT) ? 1u : 0u;
    // A destination by name is its object's leaf: a global's by its symbol's flag, as a store by name's is in C's loop
    // (its own template there is an address constant's, which nothing evaluates).
    const uint32_t named = (front->ops[destination] & 0x7fu) == CHECK_OP_VALUE ? 1u : 0u;
    const uint32_t global = (state->window_c_flags[destination] & BUSTER_NODE_CONTEXTS_SYMBOL) != 0 ? 1u : 0u;
    const uint32_t stored = state->window_c_classes[x];
    const uint32_t facts = state->window_c_facts[x];
    const uint32_t pointer = (facts & BUSTER_CHECK_LOWER_TYPE_POINTER) != 0 ? 1u : 0u;
    const uint32_t calculated = node_machine_class(state->facts.machine, state->facts.records[x].converted[0]);
    // Where the computation's class is not the record's own, the old value's CONVERT to it and the result's back.
    const uint32_t computed = calculated != stored ? 1u : 0u;
    // Its own row, the update's record's own part: by its operator (`++`'s and `--`'s CONST), one more with the
    // CONVERT back; a pointer's by its type's fact, its NEG by the operator. A bool's update tests its result (the
    // conversion back is the test's entries), which these slots have no room for: its expression is listed. (Its two
    // records have their rows all the same, which no one reads.)
    const uint32_t tested = (facts & BUSTER_CHECK_LOWER_TYPE_BOOL) != 0 ? 1u : 0u;
    const uint32_t own = ((tables->updates[op] + computed) & (pointer - 1u)) | (tables->pointer_updates[op] & (0u - pointer));
    uint64_t* masks = state->window_c_masks + BUSTER_NODE_CONTEXTS_MASKS * (uint64_t)(x / 64);
    state->window_x_owns[x] = (uint8_t)own;
    // (Its record's own part had no entry: an update's template gives no shape. Its conversion's stay.)
    state->window_x_lengths[x] = (uint8_t)(state->window_x_lengths[x] + tables->lengths[own]);
    state->window_x_bits[x] = (uint8_t)(BUSTER_TYPED_STORE + postfix + named * (BUSTER_TYPED_STORE_LOCAL - BUSTER_TYPED_STORE + 2u * global));
    state->window_x_operands[x] = (uint8_t)calculated;
    masks[BUSTER_NODE_ROWS_MASK_LISTED] |= (uint64_t)tested << (x & 63);
    // Its first row, its destination's record's: an operand's, before its own, which no other update writes. A
    // destination by name is not evaluated and has no entry in either part: the read and a postfix update's DUP are
    // its own part's, the CONVERT its conversion's part's. One through its address has its own entries and no
    // conversion: the row is its conversion's part's, and a postfix update's is the two parts' rows by the shape of
    // the destination's own entries, or the mark of no room, which lists its expression.
    const uint32_t entries = state->window_x_owns[destination];
    const uint32_t by_name = 0u - named;
    const uint32_t kept = 0u - postfix;
    const uint32_t pointed_own = (tables->kept_owns[computed][entries] & kept) | (entries & ~kept);
    const uint32_t pointed_conversion = (tables->kept_conversions[computed][entries] & kept) | ((BUSTER_NODE_ROWS_FIRST_ADDRESS + computed) & ~kept);
    const uint32_t first_own = ((BUSTER_NODE_ROWS_FIRST + 2u * global + postfix) & by_name) | (pointed_own & ~by_name);
    const uint32_t first_conversion = ((BUSTER_NODE_ROWS_FIRST_CONVERT * computed) & by_name) | (pointed_conversion & ~by_name);
    uint64_t* destination_masks = state->window_c_masks + BUSTER_NODE_CONTEXTS_MASKS * (uint64_t)(destination / 64);
    state->window_x_owns[destination] = (uint8_t)first_own;
    state->window_x_conversions[destination] = (uint8_t)first_conversion;
    state->window_x_lengths[destination] = (uint8_t)(tables->lengths[first_own] + tables->lengths[first_conversion]);
    state->window_x_operands[destination] = (uint8_t)calculated;
    destination_masks[BUSTER_NODE_ROWS_MASK_LISTED] |= (uint64_t)(first_own == BUSTER_NODE_ROWS_LATER ? 1u : 0u) << (destination & 63);
}

// A condition's operand, the window's record `o`, with its sense `sense` (1: its branch is taken where it holds):
// what its consumer, an operator evaluated as branches, gives it (node_machine_branch_operand).
// - One that is such an operator in its turn (its template) keeps the sense for its own operands' turn.
// - A value is tested: its test and its branch are its conversion's part's row, where that part is free
//   (BUSTER_NODE_ROWS_TESTED: a narrow class's or not, by the sense), and its row has their entries more. A value
//   with a conversion of its own has no room for them: its row stays as it is, and its expression is listed.
// A `!`'s one operand takes this twice, with the same sense: the second time its part has the test's shape, which is
// neither free nor a conversion of its own, and nothing changes. (An operator evaluated as branches has no target
// and converts nothing, C's loop's rule: its part is free, and stays so.)
static inline __attribute__((always_inline)) void node_rows_window_tested(struct NodeFactsReader* state, uint32_t o, uint32_t sense)
{
    const uint32_t value = state->window_c_templates[o] != NODE_MACHINE_TEMPLATE_BRANCHES ? 1u : 0u;
    const uint32_t before = state->window_x_conversions[o];
    // (A tested value's class is no void's: the checker takes only a scalar for a logical operator's operand,
    // check_action_logic.)
    const uint32_t narrow = state->window_c_classes[o] <= BUSTER_TYPED_CLASS_U16 ? 1u : 0u;
    const uint32_t shape = BUSTER_NODE_ROWS_TESTED + 2u * narrow + sense;
    const uint32_t converted = before - 1u < BUSTER_NODE_ROWS_TESTED - 1u ? 1u : 0u;
    const uint32_t takes = 0u - (value & (before == 0 ? 1u : 0u));
    state->window_x_senses[o] = (uint8_t)sense;
    // The test's three entries, and a narrow class's CONVERT.
    state->window_x_conversions[o] = (uint8_t)(before | (shape & takes));
    state->window_x_lengths[o] = (uint8_t)(state->window_x_lengths[o] + ((3u + narrow) & takes));
    // The one branch: a value with no room is rare (none on the three corpora), and a write of the block's mask a
    // call would make each call wait for the one before it.
    if (__builtin_expect(converted != 0, 0))
    {
        state->window_c_masks[BUSTER_NODE_CONTEXTS_MASKS * (uint64_t)(o / 64) + BUSTER_NODE_ROWS_MASK_LISTED] |= UINT64_C(1) << (o & 63);
    }
}

// The turn of the window's record `c`, an `&&`, an `||` or a `!` evaluated as branches
// (node_machine_classify_branches): its operands' senses, from its own, which its consumer's turn left in the senses'
// column. (A statement's CONDITION's is its statement's, and its one operand's the same: the reader writes it at the
// operand's record, node_facts_reader_expression, and the CONDITION has no turn.)
// - An `&&`'s first operand branches away where it doesn't hold, and an `||`'s where it does, whatever the operator's
//   own sense (to the operator's target, or to its own label past its second operand: the label is the entry's
//   value, which is not made here); its second operand, the record before it, has the operator's.
// - A `!`'s operand, the record before it, has the other sense.
static inline __attribute__((always_inline)) void node_rows_window_branch(struct NodeFactsReader* state, const NodeFront* front, uint32_t c)
{
    const uint32_t op = front->ops[c];
    const uint32_t sense = state->window_x_senses[c];
    const uint32_t negated = op == CHECK_OP_NOT ? 1u : 0u;
    const uint32_t either = op == CHECK_OP_LOGICAL_OR ? 1u : 0u;
    const uint32_t last = sense ^ negated;
    // A `!`'s first operand is its last: the record before it, with the same sense.
    node_rows_window_tested(state, c - front->window_left[c], (last & (0u - negated)) | (either & (negated - 1u)));
    node_rows_window_tested(state, c - 1, last);
}

// The updates' rows of a chunk's `count` records, after the shapes' sweep and before the rows' write: a sweep of
// masks, 64 records a step, which compresses the updates' numbers into a list (an evaluated record with the update's
// template: a folded `&&`'s dead operand has the template too, and no row), then a loop over the list, an update at a
// time (node_rows_window_update). A sweep and a loop of their own: an update is one record in some ninety, and a
// term of its masks in the shapes' sweep is a term of every block's (the 9700X's readings are in
// docs/node-machine.md).
// And the branches' rows: the sweep compresses the chunk's operators evaluated as branches into a second list in the
// same pass, and a loop goes down that list from its end (node_rows_window_branch): a record's consumer is a later
// record, whose turn gave it its sense. An update that is a tested value has its own row from the updates' loop and
// its test from this one, in either order: the one writes its own part and the other its conversion's, and each adds
// its entries to the row's length. In this function, with no call of its own (the builds' readings on the 9700X are
// in docs/node-machine.md).
static __attribute__((noinline)) void node_rows_window_updates(struct NodeFactsReader* state, const NodeFront* front, uint32_t count, NodeRowsCounts* counts)
{
    uint32_t* updates = state->x_updates;
    uint32_t* branches = state->x_branches;
    uint32_t update_count = 0;
    uint32_t branch_count = 0;
    // The second list, in a chunk with an operator that takes a turn (C's loop's count): the same in every block of a
    // chunk.
    const bool branching = state->c_branches_now != 0;
    const __m512i places = _mm512_load_si512(state->x_tables->places);
    const uint32_t blocks = (count + 63) / 64;
    // One copy of the step. The compiler unrolls this loop by four, with a remainder's copies (2,145 bytes of
    // function for 1,223). A chunk has some eight blocks, and on Lua its first run read 0.43 ticks a record swept
    // more unrolled, and its second 0.02 more (the 9700X's readings are in docs/node-machine.md).
    #pragma clang loop unroll(disable)
    for (uint32_t block = 0; block < blocks; block += 1)
    {
        const uint32_t x = block * 64;
        const __mmask64 alive = state->window_c_masks[BUSTER_NODE_CONTEXTS_MASKS * (uint64_t)block + BUSTER_NODE_CONTEXTS_MASK_ALIVE];
        const __m512i templates = _mm512_loadu_si512(state->window_c_templates + x);
        const __mmask64 updated = _mm512_mask_cmpeq_epi8_mask(alive, templates, _mm512_set1_epi8((char)NODE_MACHINE_TEMPLATE_UPDATE));
        const __m512i base = _mm512_set1_epi32((int32_t)x);
        // Few a block: their places in it are compressed as bytes, once, and widened onto the list 16 a turn, which
        // has a register's room past a chunk's records; a block with more than 16 takes another turn.
        __m512i update_places = _mm512_maskz_compress_epi8(updated, places);
        const uint32_t update_end = update_count + (uint32_t)_mm_popcnt_u64(updated);
        do
        {
            _mm512_storeu_si512(updates + update_count, _mm512_add_epi32(base, _mm512_cvtepu8_epi32(_mm512_castsi512_si128(update_places))));
            update_places = _mm512_shuffle_i64x2(update_places, update_places, 0x39);
            update_count += 16;
        } while (update_count < update_end);
        update_count = update_end;
        if (branching)
        {
            // A statement's CONDITION takes no turn: its one operand's sense is its own, which the reader wrote there.
            const __mmask64 branched = _mm512_mask_cmpeq_epi8_mask(alive, templates, _mm512_set1_epi8((char)NODE_MACHINE_TEMPLATE_BRANCHES)) &
                                       _mm512_cmpneq_epi8_mask(_mm512_loadu_si512(front->ops + x), _mm512_set1_epi8(CHECK_OP_CONDITION));
            __m512i branch_places = _mm512_maskz_compress_epi8(branched, places);
            const uint32_t branch_end = branch_count + (uint32_t)_mm_popcnt_u64(branched);
            do
            {
                _mm512_storeu_si512(branches + branch_count, _mm512_add_epi32(base, _mm512_cvtepu8_epi32(_mm512_castsi512_si128(branch_places))));
                branch_places = _mm512_shuffle_i64x2(branch_places, branch_places, 0x39);
                branch_count += 16;
            } while (branch_count < branch_end);
            branch_count = branch_end;
        }
    }
    for (uint32_t i = 0; i < update_count; i += 1)
    {
        node_rows_window_update(state, front, updates[i]);
    }
    for (uint32_t i = branch_count; i-- != 0;)
    {
        node_rows_window_branch(state, front, branches[i]);
    }
    counts->updates += update_count;
    counts->branched += branch_count;
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
static __attribute__((noinline, aligned(64))) uint32_t node_rows_window_write(struct NodeFactsReader* state, const NodeFront* front, uint32_t count)
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
        // An update's two marks' kinds: its operation's by its operator's code (the lookup reads an index's low seven
        // bits, an operator's code without the byte's flag), and its store's, which the updates' loop left where a
        // member's and an address's bits are.
        const __m512i operations = node_window_table(tables->operations, _mm512_permutexvar_epi8(order, _mm512_loadu_si512(front->ops + x)));
        const __m512i stores = _mm512_permutexvar_epi8(order, _mm512_loadu_si512(state->window_x_bits + x));
        // Each slot's kinds, and its classes from their sources: the record's own class in any slot of either part;
        // its target's in each part's first (a folded constant's and a CONVERT's, whose targets' words the reads
        // read); its first operand's conversion's in its own part's first two (a truth's; an update's CONST and
        // operation) and its conversion's last (an update's first row's CONVERT). The other slots' are classes
        // themselves. A source's mark is no class's number, so each is looked for in what the ones before left. And an
        // update's own row's two marks, each in its one slot: the operation's kind and the store's.
        __m512i kinds[BUSTER_NODE_ROWS_MOST];
        __m512i classes[BUSTER_NODE_ROWS_MOST];
        for (uint32_t slot = 0; slot < BUSTER_NODE_ROWS_SLOTS; slot += 1)
        {
            kinds[slot] = node_rows_slot(tables->kinds[slot], owns);
            kinds[BUSTER_NODE_ROWS_SLOTS + slot] = node_rows_slot(tables->kinds[slot], conversions);
            const __m512i own_sources = node_rows_slot(tables->sources[slot], owns);
            const __m512i conversion_sources = node_rows_slot(tables->sources[slot], conversions);
            classes[slot] = _mm512_mask_mov_epi8(own_sources, _mm512_cmpeq_epi8_mask(own_sources, own_source), own_classes);
            classes[BUSTER_NODE_ROWS_SLOTS + slot] = _mm512_mask_mov_epi8(conversion_sources, _mm512_cmpeq_epi8_mask(conversion_sources, own_source), own_classes);
        }
        const __m512i first_sources = classes[0];
        const __m512i conversion_firsts = classes[BUSTER_NODE_ROWS_SLOTS];
        const __m512i conversion_lasts = classes[BUSTER_NODE_ROWS_MOST - 1];
        classes[0] = _mm512_mask_mov_epi8(classes[0], _mm512_cmpeq_epi8_mask(first_sources, target_source), target_classes);
        classes[0] = _mm512_mask_mov_epi8(classes[0], _mm512_cmpeq_epi8_mask(first_sources, operand_source), operand_classes);
        classes[1] = _mm512_mask_mov_epi8(classes[1], _mm512_cmpeq_epi8_mask(classes[1], operand_source), operand_classes);
        classes[BUSTER_NODE_ROWS_SLOTS] = _mm512_mask_mov_epi8(conversion_firsts, _mm512_cmpeq_epi8_mask(conversion_firsts, target_source), target_classes);
        classes[BUSTER_NODE_ROWS_MOST - 1] = _mm512_mask_mov_epi8(conversion_lasts, _mm512_cmpeq_epi8_mask(conversion_lasts, operand_source), operand_classes);
        kinds[1] = _mm512_mask_mov_epi8(kinds[1], _mm512_cmpeq_epi8_mask(kinds[1], _mm512_set1_epi8(BUSTER_NODE_ROWS_KIND_OPERATION)), operations);
        kinds[3] = _mm512_mask_mov_epi8(kinds[3], _mm512_cmpeq_epi8_mask(kinds[3], _mm512_set1_epi8(BUSTER_NODE_ROWS_KIND_STORE)), stores);
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
        _mm512_storeu_si512(state->window_x_offsets + x, _mm512_mullo_epi64(lengths, _mm512_set1_epi64(0x0101010101010100)));
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
// masks, then by what gave it the bit in the two loops. The shadow's count.
// - An update lists for its own row (a bool's), and its destination for the update's first row, where the
//   destination's own entries leave it no room: the destination's own shape is then the mark.
// - Any other record that lists by neither mask is a value an operator evaluated as branches tests, with no room for
//   its test.
// (A record with a template whose rows are not made here, a logical operator's or a conditional's value's, has the
// mark too, and would count with the updates: C on the window gives no record one.)
static uint8_t node_rows_window_reason(const struct NodeFactsReader* state, uint32_t x)
{
    const uint64_t* masks = state->window_c_masks + BUSTER_NODE_CONTEXTS_MASKS * (uint64_t)(x / 64);
    const uint64_t bit = UINT64_C(1) << (x & 63);
    const bool updated = (state->window_c_templates[x] & 63u) == NODE_MACHINE_TEMPLATE_UPDATE || state->window_x_owns[x] == BUSTER_NODE_ROWS_LATER;
    return (masks[BUSTER_NODE_ROWS_MASK_REFUSED] & bit) != 0 ? BUSTER_NODE_ROWS_REASON_CONVERSION :
           (masks[BUSTER_NODE_ROWS_MASK_FLOATED] & bit) != 0 ? BUSTER_NODE_ROWS_REASON_FLOATING :
           updated                                           ? BUSTER_NODE_ROWS_REASON_UPDATE :
                                                               BUSTER_NODE_ROWS_REASON_BRANCHES;
}

// An expression's part, for each of the batch's expressions C takes on the window: it is listed for X where one of
// its operand's records lists it, and else its tape is its operand's records' rows and its root's. (With no branch an
// expression, another's places read as the window's first record's, the 9700X read it 0.19 ticks a record higher on
// `sqlite3.c`: 1.45 for 1.26.)
// A root's record of its own (a statement's discard, a control's CONDITION or a `switch`'s value, a `return`'s
// conversion) asks no test here. It has no target (node_contexts_window_part gives it its expression's, which only an
// initialization has, and that has no record of its own), so no conversion of its lists it; C gives it none of the
// templates whose rows are not made here; and it is no operand: no update's destination and no tested value.
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

// The entries of the window's record `x`'s row.
static inline uint32_t node_rows_window_length(const struct NodeFactsReader* state, uint32_t x)
{
    return state->window_x_lengths[x];
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

// X's rows on the window for the chunk C has just answered: the shapes, the updates' rows and the branches' (where
// C's loop gave a record of the chunk the update's template, or evaluated a `!`, an `&&` or an `||` as branches: a
// chunk with neither has no sweep for them) and the rows over its records, then each of the batch's expressions' part. Each expression taken here is then
// compared with the reference's tape, and each listed one counted by its first record's reason. The shadow runs them
// a second time over the same chunk, for their ticks with their code and their constants at hand: they give the same
// answers.
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
    const uint64_t shapes_end = __rdtsc();
    if ((state->c_updates_now | state->c_branches_now) != 0)
    {
        node_rows_window_updates(state, front, count, &read);
    }
    const uint64_t t1 = __rdtsc();
    const uint32_t written = node_rows_window_write(state, front, count);
    const uint64_t t2 = __rdtsc();
    node_rows_window_part(state, front);
    const uint64_t t3 = __rdtsc();
    // The first run's streams, kept: the second's are compared with them. A sweep that read what an earlier run left
    // in a column (a sense, a shape) would write the same the second time and another the first.
    memcpy(state->x_first_kinds, state->x_kinds, written);
    memcpy(state->x_first_classes, state->x_classes, written);
    const uint64_t later_start = __rdtsc();
    NodeRowsCounts again = { 0 };
    node_rows_window_reads(state, count, &again);
    const uint64_t later_reads_end = __rdtsc();
    node_rows_window_shapes(state, front, count);
    const uint64_t later_shapes_end = __rdtsc();
    if ((state->c_updates_now | state->c_branches_now) != 0)
    {
        node_rows_window_updates(state, front, count, &again);
    }
    const uint64_t t4 = __rdtsc();
    const uint32_t written_again = node_rows_window_write(state, front, count);
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
    counts[NODE_FACTS_COUNT_ROWS_AGAIN_DIFFERENCES] += written != written_again || memcmp(state->x_first_kinds, state->x_kinds, written) != 0 ||
                                                       memcmp(state->x_first_classes, state->x_classes, written) != 0;
    counts[NODE_FACTS_COUNT_ROWS_WORDS] += read.words;
    counts[NODE_FACTS_COUNT_ROWS_LINES] += read.lines;
    counts[NODE_FACTS_COUNT_ROWS_UPDATES] += read.updates;
    counts[NODE_FACTS_COUNT_ROWS_BRANCHED] += read.branched;
    counts[NODE_FACTS_COUNT_ROWS_READ_TICKS] += reads_end - t0;
    counts[NODE_FACTS_COUNT_ROWS_SHAPE_TICKS] += shapes_end - reads_end;
    counts[NODE_FACTS_COUNT_ROWS_UPDATE_TICKS] += t1 - shapes_end;
    counts[NODE_FACTS_COUNT_ROWS_WRITE_TICKS] += t2 - t1;
    counts[NODE_FACTS_COUNT_ROWS_PART_TICKS] += t3 - t2;
    counts[NODE_FACTS_COUNT_ROWS_LATER_READ_TICKS] += later_reads_end - later_start;
    counts[NODE_FACTS_COUNT_ROWS_LATER_SHAPE_TICKS] += later_shapes_end - later_reads_end;
    counts[NODE_FACTS_COUNT_ROWS_LATER_UPDATE_TICKS] += t4 - later_shapes_end;
    counts[NODE_FACTS_COUNT_ROWS_LATER_WRITE_TICKS] += t5 - t4;
    counts[NODE_FACTS_COUNT_ROWS_LATER_PART_TICKS] += t6 - t5;
}

#endif
