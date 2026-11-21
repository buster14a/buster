// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #2850's step 2, X's rows on the window, first their shapes (measure-node-machine --shadow --window-rows;
// docs/one-core-path.md, "X's rows on the window"): the kinds, the classes and the values of the entries X writes for
// a chunk's records, made from C's answers on the window (c_check_node_machine_window.c) with no walk up a tree and
// no dispatch a record. X (c_check_node_machine_expand.c's node_machine_expand) goes up an expression's postorder and writes each
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
// constant converted to a floating class, whose fold reads its value; a conditional whose first arm's conversion
// leaves its jump no room in that part (a conversion to bool); and a logical operator's value whose second operand's
// conversion is of a narrow class, whose test is one entry more than its own part has. (A logical operator's with a
// condition whose truth is known has templates that list their expression too: C on the window makes them of no
// record.) Listing is always safe.
//
// The entries' values are a third stream, a quadword an entry, made after the rows are placed
// (node_rows_window_values): zeros and each PTR_ADD's 1, then a list a source of the records whose row has another
// value (its record's value, which is its object's number, its payload or its member's offset; its destination's; a
// constant's bits by its class; a stride; the control's label or a conditional's end's), each written by a loop of
// its own from two columns; and the numbers of the few expressions with two calls, two operators with labels or two
// postfix updates through an address (node_rows_window_number).
//
// The reference is X on T's columns at reader time, whose tape the shadow keeps (NodeFrontTypes.tape_kinds,
// tape_classes and tape_values): every expression decided on the window and taken here is compared with it, entry by
// entry (node_rows_window_compare). No compile takes any of it.
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
// An operator evaluated as branches whose turn the branches' loop took (node_rows_window_branch): no entry, its own
// part's shape; the odd one's has a label of its own (an `&&` branched where true and an `||` where false,
// node_machine_record_past), which is a number in its expression (node_rows_window_number).
#define BUSTER_NODE_ROWS_BRANCHED 116u
// An operator's value's own part (node_machine_logical's BRANCH, node_machine_own's case), which the same loop
// writes at its turn (node_rows_window_held). A conditional's has no entry: its end's label is placed there. A
// logical operator's is its second operand's test (its conversion's class's, no narrow one), its jump to its end and
// its value at its alternate, 0 for `&&` and, the odd one, 1 for `||`. Each has two labels, its alternate and its
// end, numbers in its expression.
#define BUSTER_NODE_ROWS_HELD 118u
#define BUSTER_NODE_ROWS_LOGIC 119u
// A conditional's first arm's jump to its end (node_machine_hook's second case), its conversion's part's last entry,
// after the arm's own conversion: a value's jump alone, of the arm's own class or, where the reads read its
// target's, of that; void's alone; after a CONVERT; and void's after a DISCARD.
#define BUSTER_NODE_ROWS_ARM 121u
#define BUSTER_NODE_ROWS_SHAPES 126u
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

// A record's byte in the senses' column (NodeFactsReader.window_x_senses), which the branches' loop writes: a
// condition's operand's sense (1: its branch is taken where it holds); and whether its branch's label is an
// operator's own, the operator that owns it (an `&&` branched where true or an `||` where false, for its first
// operand), and not the control's. And the reader's bit of an expression's operand's last record
// (BUSTER_NODE_ROWS_SENSE_TOP), which no turn of the loop writes over: the record is no operand of an operator's.
#define BUSTER_NODE_ROWS_SENSE_TRUE 0x01u
#define BUSTER_NODE_ROWS_SENSE_OWNED 0x02u
// And a conditional's first arm's bit: its row's last entry is its jump to its conditional's end, whose value is that
// label's number (the list of the control's label's has the record, for the one in an expression with one such
// operator).
#define BUSTER_NODE_ROWS_SENSE_JUMPED 0x04u

_Static_assert((BUSTER_NODE_ROWS_SENSE_TOP & (BUSTER_NODE_ROWS_SENSE_TRUE | BUSTER_NODE_ROWS_SENSE_OWNED | BUSTER_NODE_ROWS_SENSE_JUMPED)) == 0 &&
               BUSTER_NODE_ROWS_SENSE_JUMPED == 4, "a record's sense's byte has a bit each, and the jump's is its third (node_rows_window_values)");

// A name's leaf's declaration's line's byte (NodeFactsReader.window_x_lines, which the tests' sweep copies beside the
// records' bytes: c_check_node_census.c's BUSTER_NODE_TESTS_LINE_*), as a summary's side facts read it, a function's
// alone: a plain one, a call of whose name has no fact for the return flow; and one declared noreturn. A function
// with neither is one whose name is a library's the flow leaves unknown. Another record's byte is zero.
#define BUSTER_NODE_ROWS_LINE_PLAIN 0x40u
#define BUSTER_NODE_ROWS_LINE_STOPS 0x80u

// The values' lists (NodeFactsReader.x_lists), the chunk's records' numbers by where their row's values come from
// (node_rows_window_values):
// - its record's value (NodeFactsReader.window_x_values: its object's number, its payload, or its member's offset in
//   bytes), its first entry's: a name's read, an address constant's (its object's address, or its offset alone), an
//   update's read by name, and a member's offset's CONST;
// - its first operand's record's value, its one entry's: a store by name;
// - its record's value by its entry's class, its first entry's: an integer constant's CONST, plain or folded;
// - its stride (NodeFactsReader.window_c_strides), its PTR_ADD's: a pointer's sum's, its subscript's and its
//   update's;
// - the control's label, its last entry's: a tested value's branch that no operator owns, which the branches' loop
//   lists as it gives the value its test (node_rows_window_tested);
// - the rest, by its shape (node_rows_window_rest);
// - and the records with a number of their expression's for a value that are no first such record of it: the
//   expressions the numbering is asked for (node_rows_window_number), by a record of each.
#define BUSTER_NODE_ROWS_LIST_FIRST 0u
#define BUSTER_NODE_ROWS_LIST_STORED 1u
#define BUSTER_NODE_ROWS_LIST_CONSTANT 2u
#define BUSTER_NODE_ROWS_LIST_STRIDE 3u
#define BUSTER_NODE_ROWS_LIST_CONTROLLED 4u
#define BUSTER_NODE_ROWS_LIST_REST 5u
#define BUSTER_NODE_ROWS_LIST_SECOND 6u

_Static_assert(BUSTER_NODE_ROWS_LIST_SECOND + 1 == BUSTER_NODE_ROWS_LISTS && NODE_FACTS_COUNT_ROWS_LIST + BUSTER_NODE_ROWS_LIST_SECOND == NODE_FACTS_COUNT_ROWS_LIST_LAST,
               "the reader's state and the unit's counts have a place a list");

// An own shape's byte in the values' table (NodeRowsTables.value_owns): the lists the sweep puts its record on, a
// bit each; and that a value of its row's is a number in its expression: an evaluated call's row, an operator's own
// label and a postfix update's destination through its address's temporaries (node_rows_window_number).
#define BUSTER_NODE_ROWS_VALUE_FIRST 0x01u
#define BUSTER_NODE_ROWS_VALUE_STORED 0x02u
#define BUSTER_NODE_ROWS_VALUE_CONSTANT 0x08u
#define BUSTER_NODE_ROWS_VALUE_STRIDE 0x20u
#define BUSTER_NODE_ROWS_VALUE_REST 0x40u
#define BUSTER_NODE_ROWS_VALUE_NUMBERED 0x80u
// And that it has labels of its own, numbers in its expression too, counted apart from the calls' rows and the
// temporaries: an operator evaluated as branches with one, and an operator's value, with two. An expression with one
// such operator has its labels whatever calls it holds: the first, and the one after.
#define BUSTER_NODE_ROWS_VALUE_LABELED 0x10u
// And its byte of slots (NodeRowsTables.value_slots): its PTR_ADD's entry among its own (a pointer's row's: after a
// NEG, after an update's step's CONST), whether its first entry is a pointer's update's step's 1, and, for a postfix
// update's destination through its address, its own entries before the first row's six.
#define BUSTER_NODE_ROWS_SLOT_STRIDE 0x03u
#define BUSTER_NODE_ROWS_SLOT_STEP 0x04u
#define BUSTER_NODE_ROWS_SLOT_KEPT_SHIFT 4u

// A record's bits the reads' loop makes of its line (NodeFactsReader.window_x_bits), each read of the records it is
// for: a member's offset is not zero; and an address's object is a local's.
#define BUSTER_NODE_ROWS_BIT_FAR 0x01u
#define BUSTER_NODE_ROWS_BIT_LOCAL 0x02u

// Where a loop writes a value it has no place for, with no branch on whether it has one: a quadword past the window's
// records in the records' values' column, and one past a chunk's most entries in the values' stream (each column's
// room past its chunk's is more: node_facts_reader_begin).
#define BUSTER_NODE_ROWS_VALUE_NOWHERE (BUSTER_NODE_FRONT_RECORDS + 128u)
#define BUSTER_NODE_ROWS_ENTRY_NOWHERE (8u * BUSTER_NODE_FRONT_RECORDS + 63u)

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
    // The values' two tables by a record's own shape (BUSTER_NODE_ROWS_VALUE_* and BUSTER_NODE_ROWS_SLOT_*), 128 bytes
    // each: the first read as two registers.
    _Alignas(64) uint8_t value_owns[128];
    _Alignas(64) uint8_t value_slots[128];
    // A conditional's first arm's conversion's part's shape with its jump (BUSTER_NODE_ROWS_ARM), by whether the
    // conditional is void's (8), whether the reads read the arm's target's class (4) and the arm's conversion (none,
    // a CONVERT, a DISCARD, another): none where the jump has no room there.
    uint8_t arms[16];
};

_Static_assert(sizeof(NodeRowsTables) <= 3072, "the reader's state has room for the rows' tables (node_facts_reader_begin)");
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
    // The templates whose rows are not made here: a logical operator's with a condition whose truth is known, which C
    // on the window makes of no record (it lists them). An update's template gives no shape: its record's is the
    // updates' loop's to write (node_rows_window_update). Nor does an operator's evaluated as branches, which writes
    // no entry of its own: its operands' tests are after their rows (node_rows_window_tested). Nor does an operator's
    // value's: its own part is the branches' loop's to write at its turn (node_rows_window_held).
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
    // A logical operator's value's own row (node_machine_own's BRANCH: node_machine_put_truth of its second operand's
    // conversion's class, which the loop leaves in the operands' column; its JUMP_VALUE, of its own class, an int's;
    // and its value at its alternate). Its two values that are no zero are the rest's (node_rows_window_rest).
    for (uint32_t either = 0; either < 2; either += 1)
    {
        const uint8_t entries[4][2] = {
            { BUSTER_TYPED_CONST, BUSTER_NODE_ROWS_CLASS_OPERAND },
            { BUSTER_TYPED_NE, BUSTER_TYPED_CLASS_I32 },
            { BUSTER_TYPED_JUMP_VALUE, BUSTER_NODE_ROWS_CLASS_OWN },
            { BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32 },
        };
        node_rows_row(tables, BUSTER_NODE_ROWS_LOGIC + either, 4, entries);
    }
    // A conditional's first arm's jump, after its conversion (node_machine_hook: a JUMP of void's class, or a
    // JUMP_VALUE of the conditional's, which is the arm's target's).
    {
        const uint8_t value[1][2] = { { BUSTER_TYPED_JUMP_VALUE, BUSTER_NODE_ROWS_CLASS_OWN } };
        const uint8_t none[1][2] = { { BUSTER_TYPED_JUMP, BUSTER_TYPED_CLASS_VOID } };
        const uint8_t target[1][2] = { { BUSTER_TYPED_JUMP_VALUE, BUSTER_NODE_ROWS_CLASS_TARGET } };
        const uint8_t converted[2][2] = { { BUSTER_TYPED_CONVERT, BUSTER_NODE_ROWS_CLASS_TARGET }, { BUSTER_TYPED_JUMP_VALUE, BUSTER_NODE_ROWS_CLASS_TARGET } };
        const uint8_t discarded[2][2] = { { BUSTER_TYPED_DISCARD, BUSTER_NODE_ROWS_CLASS_OWN }, { BUSTER_TYPED_JUMP, BUSTER_TYPED_CLASS_VOID } };
        node_rows_row(tables, BUSTER_NODE_ROWS_ARM, 1, value);
        node_rows_row(tables, BUSTER_NODE_ROWS_ARM + 1, 1, none);
        node_rows_row(tables, BUSTER_NODE_ROWS_ARM + 2, 1, target);
        node_rows_row(tables, BUSTER_NODE_ROWS_ARM + 3, 2, converted);
        node_rows_row(tables, BUSTER_NODE_ROWS_ARM + 4, 2, discarded);
        // The arm's part's shape with its jump, by whether the conditional is void's, whether the reads read the
        // arm's target's class, and the conversion the shapes' sweep gave the arm (none, a CONVERT, a DISCARD, or
        // another): none where the part has no room for the jump or the two don't go together, which lists the
        // expression (a conversion to bool's four entries; a folded constant's fold is its own part's, and its part
        // here is free).
        tables->arms[0] = BUSTER_NODE_ROWS_ARM;
        tables->arms[1] = BUSTER_NODE_ROWS_ARM + 3;
        tables->arms[4] = BUSTER_NODE_ROWS_ARM + 2;
        tables->arms[5] = BUSTER_NODE_ROWS_ARM + 3;
        tables->arms[8] = BUSTER_NODE_ROWS_ARM + 1;
        tables->arms[10] = BUSTER_NODE_ROWS_ARM + 4;
    }
    // The values' tables, by a record's own shape, from the rows' own entries' kinds: where each value that is no
    // zero comes from (node_rows_window_values has the rules).
    for (uint32_t shape = 1; shape < BUSTER_NODE_ROWS_SHAPES; shape += 1)
    {
        const uint8_t first = tables->kinds[0][shape];
        const bool member = shape == BUSTER_NODE_ROWS_MEMBER || shape == BUSTER_NODE_ROWS_MEMBER + 1;
        const bool pointer = shape >= BUSTER_NODE_ROWS_POINTER && shape < BUSTER_NODE_ROWS_BOOL;
        const bool pointer_update = shape >= BUSTER_NODE_ROWS_UPDATE_POINTER && shape < BUSTER_NODE_ROWS_FIRST;
        const bool kept = shape >= BUSTER_NODE_ROWS_KEPT && shape < BUSTER_NODE_ROWS_KEPT_REST;
        // A postfix update's destination's own entries, before the first row's six: its base's (node_rows_tables'
        // `bases`, in the kept rows' order: none; a pointer's sum's; a local's address; a global's; a member's two).
        static const uint8_t kept_bases[BUSTER_NODE_ROWS_KEPT_REST - BUSTER_NODE_ROWS_KEPT] = { 0, BUSTER_NODE_ROWS_POINTER, BUSTER_NODE_ROWS_ADDRESS + 2, BUSTER_NODE_ROWS_ADDRESS + 6,
                                                                                               BUSTER_NODE_ROWS_MEMBER };
        const uint32_t base = kept ? kept_bases[shape - BUSTER_NODE_ROWS_KEPT] : shape;
        const uint32_t before = kept ? tables->lengths[base] : 0;
        uint8_t lists = 0;
        uint8_t slots = (uint8_t)(before << BUSTER_NODE_ROWS_SLOT_KEPT_SHIFT);
        // Its record's value: a name's read, an address constant's and an update's read by name are their rows' first
        // entry, an object's or, of an address constant with none, its offset's CONST; and so is a member's offset's
        // CONST. (A conversion's part's shape has a byte here too, which no one reads: the table is read by a
        // record's own shape.)
        const bool object = first == BUSTER_TYPED_LOCAL || first == BUSTER_TYPED_GLOBAL || first == BUSTER_TYPED_LOCAL_ADDRESS || first == BUSTER_TYPED_GLOBAL_ADDRESS;
        const bool alone = shape == BUSTER_NODE_ROWS_ADDRESS || shape == BUSTER_NODE_ROWS_ADDRESS + 1;
        lists |= object || alone || member || base == BUSTER_NODE_ROWS_MEMBER ? BUSTER_NODE_ROWS_VALUE_FIRST : 0;
        lists |= shape == BUSTER_TYPED_STORE_LOCAL || shape == BUSTER_TYPED_STORE_GLOBAL ? BUSTER_NODE_ROWS_VALUE_STORED : 0;
        lists |= shape == BUSTER_TYPED_CONST || shape == BUSTER_NODE_ROWS_FOLDED ? BUSTER_NODE_ROWS_VALUE_CONSTANT : 0;
        lists |= pointer || pointer_update || (kept && base == BUSTER_NODE_ROWS_POINTER) ? BUSTER_NODE_ROWS_VALUE_STRIDE : 0;
        // The rest: an address constant's offset after its object's address, the rows of a local's and of a global's
        // with one (node_rows_tables' variants 2 and 4, two rows each; a shape before the address constants' has a
        // variant past them all, the difference of two unsigned numbers).
        const uint32_t variant = (shape - BUSTER_NODE_ROWS_ADDRESS) / 2;
        const bool logic = shape == BUSTER_NODE_ROWS_LOGIC || shape == BUSTER_NODE_ROWS_LOGIC + 1;
        lists |= variant == 2 || variant == 4 || logic ? BUSTER_NODE_ROWS_VALUE_REST : 0;
        lists |= shape == BUSTER_TYPED_CALL || kept ? BUSTER_NODE_ROWS_VALUE_NUMBERED : 0;
        lists |= shape == BUSTER_NODE_ROWS_BRANCHED + 1 || shape == BUSTER_NODE_ROWS_HELD || logic ? BUSTER_NODE_ROWS_VALUE_LABELED : 0;
        // Its PTR_ADD's place among its own entries, and a pointer's update's step.
        uint32_t entries = 0;
        for (uint32_t slot = 0; slot < BUSTER_NODE_ROWS_SLOTS; slot += 1)
        {
            if (tables->kinds[slot][shape] == BUSTER_TYPED_PTR_ADD && (lists & BUSTER_NODE_ROWS_VALUE_STRIDE) != 0)
            {
                slots |= (uint8_t)entries;
            }
            entries += tables->kinds[slot][shape] != 0 ? 1u : 0u;
        }
        slots |= pointer_update && first == BUSTER_TYPED_CONST ? BUSTER_NODE_ROWS_SLOT_STEP : 0;
        tables->value_owns[shape] = lists;
        tables->value_slots[shape] = slots;
    }
}

typedef struct NodeRowsCounts NodeRowsCounts;
struct NodeRowsCounts
{
    // The records the reads' two loops read for: a target's word, where the target is not the record's type, and a line
    // or a table of the unit's (a member's, a truth's, an address constant's). And the updates, which their own loop
    // reads for, and the operators evaluated as branches and the operators' values that take a turn of theirs.
    uint64_t words;
    uint64_t lines;
    uint64_t updates;
    uint64_t branched;
    // And the tested values the branches' loop listed for the control's label, the values' (BUSTER_NODE_ROWS_LIST_CONTROLLED).
    uint32_t controlled;
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
        // an object, and one that had would be read for, its bits read by no one. (No constant here has the discarded
        // context, which writes a constant no entry: C gives that context to a comma, a bit-field's assignment and an
        // aggregate's store under a discard, and C on the window lists a folded comma and a bit-field's store and
        // refuses an aggregate's. A comma it takes has it, a wrapper's template with no entry of its own.)
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
        // (The rule converts in a value's context alone, node_machine_conversion: a discarded comma's target is void
        // and it converts nothing. A record read for its address has no target that is not its type.)
        const __m512i contexts = _mm512_loadu_si512(state->window_c_contexts + x);
        converts &= alive & _mm512_testn_epi8_mask(contexts, contexts);
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
        // A member's offset in bytes, its row's CONST's value, in its record's value's place: the values' loop reads
        // it there (node_rows_window_values), and a member's rows read nothing else of it. Another record's is
        // written nowhere: a branch on which the record is was mispredicted (the 9700X read the loop 1.0 tick a record
        // swept higher on `sqlite3.c` with one).
        const uint32_t other = state->window_c_templates[x] != CHECK_LOWER_TEMPLATE_MEMBER ? 1u : 0u;
        state->window_x_values[x + ((BUSTER_NODE_ROWS_VALUE_NOWHERE - x) & (0u - other))] = record->member_offset / 8;
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
        // here has it. And no constant has the discarded context: the reads' comment.) What the reads made of a record's
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
// `given` is its sense and its owner's bit (BUSTER_NODE_ROWS_SENSE_TRUE and _OWNED). A value that takes its test and
// whose branch no operator owns goes on the values' list of the control's label, whose count is `*controlled`.
// A `!`'s one operand takes this twice, with the same sense: the second time its part has the test's shape, which is
// neither free nor a conversion of its own, and nothing changes. (An operator evaluated as branches has no target
// and converts nothing, C's loop's rule: its part is free, and stays so.)
static inline __attribute__((always_inline)) void node_rows_window_tested(struct NodeFactsReader* state, uint32_t o, uint32_t given, uint32_t* controlled)
{
    const uint32_t sense = given & BUSTER_NODE_ROWS_SENSE_TRUE;
    const uint32_t value = state->window_c_templates[o] != NODE_MACHINE_TEMPLATE_BRANCHES ? 1u : 0u;
    const uint32_t before = state->window_x_conversions[o];
    // (A tested value's class is no void's: the checker takes only a scalar for a logical operator's operand,
    // check_action_logic.)
    const uint32_t narrow = state->window_c_classes[o] <= BUSTER_TYPED_CLASS_U16 ? 1u : 0u;
    const uint32_t shape = BUSTER_NODE_ROWS_TESTED + 2u * narrow + sense;
    const uint32_t converted = before - 1u < BUSTER_NODE_ROWS_TESTED - 1u ? 1u : 0u;
    const uint32_t takes = 0u - (value & (before == 0 ? 1u : 0u));
    // Its sense and whose label its branch has, the control's or an operator's own (BUSTER_NODE_ROWS_SENSE_*).
    state->window_x_senses[o] = (uint8_t)given;
    // The test's three entries, and a narrow class's CONVERT.
    state->window_x_conversions[o] = (uint8_t)(before | (shape & takes));
    state->window_x_lengths[o] = (uint8_t)(state->window_x_lengths[o] + ((3u + narrow) & takes));
    state->x_lists[BUSTER_NODE_ROWS_LIST_CONTROLLED][*controlled] = (uint16_t)o;
    *controlled += takes & (~given >> 1) & 1u;
    // The one branch: a value with no room is rare (none on the three corpora), and a write of the block's mask a
    // call would make each call wait for the one before it.
    if (__builtin_expect(converted != 0, 0))
    {
        state->window_c_masks[BUSTER_NODE_CONTEXTS_MASKS * (uint64_t)(o / 64) + BUSTER_NODE_ROWS_MASK_LISTED] |= UINT64_C(1) << (o & 63);
    }
}

// Whether the `&&` or the `||` at the window's record `c` has a second operand, the record before it, whose truth is
// known: a constant's, but an object's lvalue's, whose constant is its address (check_action_truth). A condition's
// truth is then that operand's to say, whatever the first is (check_program_branch_truth: `x && 0`, `x || 1`), where a
// summary node's value says a condition's truth only where it is a constant's: the operator's turn lists its
// expression. (One whose first operand's truth is known is C's to list.)
static inline __attribute__((always_inline)) bool node_rows_window_decided(const struct NodeFactsReader* state, uint32_t c)
{
    const uint32_t standing = state->window_c_standings[c - 1];
    return (standing & 0x1cu) != 0 && (standing & 0x1fu) != (((uint32_t)CHECK_CONSTANT_ADDRESS << 2) | (uint32_t)CHECK_LVALUE);
}

// The turn of the window's record `c`, an `&&`, an `||` or a `!` evaluated as branches
// (node_machine_classify_branches): its operands' senses, from its own, which its consumer's turn left in the senses'
// column. (A statement's CONDITION's is its statement's, and its one operand's the same: the reader writes it at the
// operand's record, node_facts_reader_expression, and the CONDITION has no turn.)
// - An `&&`'s first operand branches away where it doesn't hold, and an `||`'s where it does, whatever the operator's
//   own sense (to the operator's target, or to its own label past its second operand: the label is the entry's
//   value, which is not made here); its second operand, the record before it, has the operator's.
// - A `!`'s operand, the record before it, has the other sense.
// - And whose label an operand's branch has (node_machine_classify_branches' `owner`): an `&&` branched where true and
//   an `||` where false have a label of their own, past their second operand, which their first operand's branch
//   goes to; every other operand has its operator's owner, the control for a condition's own. The operator's own
//   part's shape says that its turn is taken and whether it has that label (BUSTER_NODE_ROWS_BRANCHED), which the
//   values read: the shapes' sweep gave it none.
static inline __attribute__((always_inline)) void node_rows_window_branch(struct NodeFactsReader* state, const NodeFront* front, uint32_t c, uint32_t* controlled)
{
    const uint32_t op = front->ops[c];
    const uint32_t byte = state->window_x_senses[c];
    const uint32_t sense = byte & BUSTER_NODE_ROWS_SENSE_TRUE;
    const uint32_t owned = byte & BUSTER_NODE_ROWS_SENSE_OWNED;
    const uint32_t negated = op == CHECK_OP_NOT ? 1u : 0u;
    const uint32_t either = op == CHECK_OP_LOGICAL_OR ? 1u : 0u;
    const uint32_t last = sense ^ negated;
    const uint32_t past = (negated ^ 1u) & (either ^ sense);
    state->window_x_owns[c] = (uint8_t)(BUSTER_NODE_ROWS_BRANCHED + past);
    // A `!`'s first operand is its last: the record before it, with the same sense and the same owner.
    node_rows_window_tested(state, c - front->window_left[c], (last & (0u - negated)) | (either & (negated - 1u)) | owned | (past * BUSTER_NODE_ROWS_SENSE_OWNED), controlled);
    node_rows_window_tested(state, c - 1, last | owned, controlled);
    // An operator whose last operand's truth is known: listed (a summary's side facts). An `&&`'s or an `||`'s second:
    // a `!` over a constant is folded, and has no turn.
    if (__builtin_expect(node_rows_window_decided(state, c), 0))
    {
        state->window_c_masks[BUSTER_NODE_CONTEXTS_MASKS * (uint64_t)(c / 64) + BUSTER_NODE_ROWS_MASK_LISTED] |= UINT64_C(1) << (c & 63);
        state->x_decided_now += 1;
    }
}

// The turn of the window's record `c`, a conditional, an `&&` or an `||` that is a value (node_machine_logical's
// BRANCH, whose segments node_machine_hook writes and whose own row node_machine_own does):
// - Its condition, its first operand, branches to its alternate, the first of its two labels, where it holds for an
//   `||` and where it doesn't for the others: a tested value's segment, or the sense of an operator evaluated as
//   branches, with this operator's label (node_rows_window_tested).
// - A conditional's first arm has its jump to the conditional's end after its conversion, in that part's slots: the
//   part's shape with it, by the table (NodeRowsTables.arms); where the table has none the expression is listed.
//   The jump's value is the label's number, and the arm goes on the list of the rows whose last entry's value is a
//   fixed one, with its bit (BUSTER_NODE_ROWS_SENSE_JUMPED). The conditional's own part has no entry.
// - A logical operator's own part is its row's four entries, with its second operand's conversion's class in the
//   operands' column, which its test reads. A narrow class's test is one entry more than the part has: listed.
// One operator in some hundreds of records: its branches are its own.
static __attribute__((noinline)) void node_rows_window_held(struct NodeFactsReader* state, const NodeFront* front, uint32_t c, uint32_t* controlled)
{
    const NodeRowsTables* tables = state->x_tables;
    const uint32_t op = front->ops[c] & 0x7fu;
    const uint32_t either = op == CHECK_OP_LOGICAL_OR ? 1u : 0u;
    bool listed = false;
    node_rows_window_tested(state, c - front->window_left[c], either | BUSTER_NODE_ROWS_SENSE_OWNED, controlled);
    if (op == CHECK_OP_CONDITIONAL)
    {
        const uint32_t arm = c - front->window_right[c];
        const uint32_t before = state->window_x_conversions[arm];
        const uint32_t worded = (uint32_t)(state->window_x_reads[2 * (uint64_t)(arm / 64) + 1] >> (arm & 63)) & 1u;
        const uint32_t voided = state->window_c_classes[c] == BUSTER_TYPED_CLASS_VOID ? 1u : 0u;
        const uint32_t conversion = before == 0 ? 0u : before == BUSTER_NODE_ROWS_CONVERT ? 1u : before == BUSTER_TYPED_DISCARD ? 2u : 3u;
        const uint32_t shape = tables->arms[8u * voided + 4u * worded + conversion];
        listed = shape == 0;
        state->window_x_owns[c] = BUSTER_NODE_ROWS_HELD;
        if (!listed)
        {
            state->window_x_conversions[arm] = (uint8_t)shape;
            state->window_x_lengths[arm] = (uint8_t)(state->window_x_lengths[arm] + 1u);
            // (An arm's byte has no other bit: it is no tested value and no expression's last record.)
            state->window_x_senses[arm] = BUSTER_NODE_ROWS_SENSE_JUMPED;
            state->x_lists[BUSTER_NODE_ROWS_LIST_CONTROLLED][*controlled] = (uint16_t)arm;
            *controlled += 1;
        }
    }
    else
    {
        const uint8_t tested = node_machine_class(state->facts.machine, state->facts.records[c].converted[1]);
        const bool decided = node_rows_window_decided(state, c);
        listed = tested <= BUSTER_TYPED_CLASS_U16 || decided;
        state->x_decided_now += decided ? 1u : 0u;
        state->window_x_operands[c] = tested;
        state->window_x_owns[c] = (uint8_t)(BUSTER_NODE_ROWS_LOGIC + either);
        state->window_x_lengths[c] = (uint8_t)(state->window_x_lengths[c] + 4u);
    }
    if (listed)
    {
        state->window_c_masks[BUSTER_NODE_CONTEXTS_MASKS * (uint64_t)(c / 64) + BUSTER_NODE_ROWS_MASK_LISTED] |= UINT64_C(1) << (c & 63);
    }
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
            // And an operator's value takes one: its condition's segment, its arm's jump and its own row.
            const __mmask64 branched = (_mm512_mask_cmpeq_epi8_mask(alive, templates, _mm512_set1_epi8((char)NODE_MACHINE_TEMPLATE_BRANCHES)) &
                                        _mm512_cmpneq_epi8_mask(_mm512_loadu_si512(front->ops + x), _mm512_set1_epi8(CHECK_OP_CONDITION))) |
                                       _mm512_mask_cmpeq_epi8_mask(alive, templates, _mm512_set1_epi8((char)NODE_MACHINE_TEMPLATE_BRANCH));
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
    uint32_t controlled = 0;
    for (uint32_t i = branch_count; i-- != 0;)
    {
        const uint32_t c = branches[i];
        if (__builtin_expect(state->window_c_templates[c] == NODE_MACHINE_TEMPLATE_BRANCH, 0))
        {
            node_rows_window_held(state, front, c, &controlled);
        }
        else
        {
            node_rows_window_branch(state, front, c, &controlled);
        }
    }
    counts->updates += update_count;
    counts->branched += branch_count;
    counts->controlled += controlled;
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
        // read) and in its conversion's second (a conditional's arm's jump after its CONVERT); its first operand's
        // conversion's in its own part's first two (a truth's; an update's CONST and operation; a logical operator's
        // value's test, its second operand's) and its conversion's last (an update's first row's CONVERT). The other slots' are classes
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
        const __m512i conversion_seconds = classes[BUSTER_NODE_ROWS_SLOTS + 1];
        classes[BUSTER_NODE_ROWS_SLOTS + 1] = _mm512_mask_mov_epi8(conversion_seconds, _mm512_cmpeq_epi8_mask(conversion_seconds, target_source), target_classes);
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

// An integer constant's CONST's value of class `class` (I8 to U64) from the bits `bits`, as node_machine_own's and
// check_lower_constant_conversion's: masked to the class's width and extended from it by the class's signedness (a
// signed class's number is odd). Two shifts, the second by the sign: no branch.
static inline __attribute__((always_inline)) uint64_t node_rows_extend(uint64_t bits, uint32_t class)
{
    // 64 less the class's width, by the class: I8's and U8's 56, I16's and U16's 48, I32's and U32's 32, I64's and
    // U64's none; no other class has an integer constant's CONST here.
    static const uint8_t shifts[16] = { 0, 56, 56, 48, 48, 32, 32, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    const uint32_t shift = shifts[class & 15u];
    const uint64_t high = bits << shift;
    return (class & 1u) != 0 ? (uint64_t)((int64_t)high >> shift) : high >> shift;
}


// An integer CONST holds its sign-extended i32. A LOCAL carries its object until the writer knows the local number.
void check_node_window_int_entry(CheckInputWindowRow* entry, uint32_t value, uint32_t token, bool local)
{
    *entry = (CheckInputWindowRow){
        .value = local ? value : node_rows_extend(value, BUSTER_TYPED_CLASS_I32),
        .token = token,
        .kind = local ? BUSTER_TYPED_LOCAL : BUSTER_TYPED_CONST,
        .class = BUSTER_TYPED_CLASS_I32,
        .flags = local ? BUSTER_CHECK_WINDOW_LOCAL | BUSTER_CHECK_WINDOW_READ : 0,
    };
}

// A discarded plain int assignment: store the RHS (the preceding row), then discard the stored value.
void check_node_window_store(CheckInputWindowRow* rows, uint32_t object, uint32_t token)
{
    rows[0] = (CheckInputWindowRow){ .value = object, .token = token, .kind = BUSTER_TYPED_STORE_LOCAL, .class = BUSTER_TYPED_CLASS_I32,
                                     .flags = BUSTER_CHECK_WINDOW_LOCAL | BUSTER_CHECK_WINDOW_WRITE, .event_offset = -1 };
    rows[1] = (CheckInputWindowRow){ .token = token + 2, .kind = BUSTER_TYPED_DISCARD, .class = BUSTER_TYPED_CLASS_I32 };
}

// A floating constant's CONST's value of class `class` (F32 or F64: C takes no other's, node_machine_constant_takes)
// from its record's line: check_lower_float_bits' of its long double (node_machine_record_floating), whose significand
// and whose sign and exponent the line has as its payload and its exponent where its standing says so. A zero's line
// says no such thing (node_machine_put_value: its payload is its integer's, and its exponent's field what a payload
// says, node_facts_small), and its value is zero's.
static uint64_t node_rows_float_bits(const NodeFactsRecord* record, uint8_t class)
{
    CheckValue value = { 0 };
    if ((record->standing & BUSTER_NODE_MACHINE_FIELD) == BUSTER_NODE_MACHINE_FLOATING)
    {
        value.floating_storage[0] = record->payload;
        value.floating_storage[1] = record->exponent;
    }
    return check_lower_float_bits(NULL, class, value.floating);
}

// The values of the rest's list's record `x`, by its own shape, a record at a time and out of the lists' loops' way,
// from its record's line: a floating constant's bits; whether a constant folded to bool is not zero; and an address
// constant's offset after its object's address.
static __attribute__((noinline)) void node_rows_window_rest(struct NodeFactsReader* state, uint32_t x)
{
    const NodeFactsRecord* record = state->facts.records + x;
    const uint32_t own = state->window_x_owns[x];
    const uint32_t place = node_rows_window_place(state, x);
    uint64_t* values = state->x_values + place;
    if (own == BUSTER_TYPED_CONST)
    {
        values[0] = node_rows_float_bits(record, state->x_classes[place]);
    }
    else if (own == BUSTER_NODE_ROWS_FOLDED)
    {
        // (A constant's payload is its value's bits of its class's width, or those extended: none is zero in them and
        // not zero past them.)
        values[0] = record->payload != 0 ? 1u : 0u;
    }
    else if (own >= BUSTER_NODE_ROWS_LOGIC)
    {
        // A logical operator's value's row: its jump's label, its end's, 1 in an expression with one operator with
        // labels (the numbering writes another's); and its value at its alternate, 0 for `&&` and 1 for `||`.
        values[2] = 1;
        values[3] = own - BUSTER_NODE_ROWS_LOGIC;
    }
    else
    {
        values[1] = record->payload;
    }
}

// One of the values' lists: the block's records under `mask`, their places in it (`places`) compressed and widened
// onto `list` from its `count`, each with the block's first record's number `base`; the list's new count. A register's
// 32 numbers are stored whatever the mask has, and the other 32 where it has more than 32: a list has their room
// past a chunk's records.
static inline __attribute__((always_inline)) uint32_t node_rows_listed(uint16_t* list, uint32_t count, __mmask64 mask, __m512i places, __m512i base)
{
    const __m512i packed = _mm512_maskz_compress_epi8(mask, places);
    const uint32_t more = (uint32_t)_mm_popcnt_u64(mask);
    _mm512_storeu_si512(list + count, _mm512_add_epi16(base, _mm512_cvtepu8_epi16(_mm512_castsi512_si256(packed))));
    if (__builtin_expect(more > 32, 0))
    {
        _mm512_storeu_si512(list + count + 32, _mm512_add_epi16(base, _mm512_cvtepu8_epi16(_mm512_extracti64x4_epi64(packed, 1))));
    }
    return count + more;
}

// The first label of the operator with labels at the window's record `owner`, among its expression's `marks` marked
// records `marked` (node_machine_number_labels' rule): the labels of the operators before its subtree's first record
// and of those after it whose subtree holds it, one that starts no later than its own. An operator evaluated as
// branches has one label and an operator's value two; a call's record and a kept destination's have none.
static inline __attribute__((always_inline)) uint32_t node_rows_window_label(const struct NodeFactsReader* state, const NodeFront* front, const uint16_t* marked, uint32_t marks,
                                                                           uint32_t owner)
{
    const uint32_t lowest = owner + 1 - front->sizes[owner];
    uint32_t label = 0;
    for (uint32_t j = 0; j < marks; j += 1)
    {
        const uint32_t other = marked[j];
        const uint32_t own = state->window_x_owns[other];
        const uint32_t labels = own >= BUSTER_NODE_ROWS_HELD ? 2u : own == BUSTER_NODE_ROWS_BRANCHED + 1 ? 1u : 0u;
        const bool counted = other < lowest || (other > owner && other + 1 - front->sizes[other] <= lowest);
        label += counted ? labels : 0u;
    }
    return label;
}

// The numbers of the expression whose operand's last record is the window's `top`, one with two records or more
// whose value is a number in it (node_rows_window_values' marks): by the reference's own rules, over the zeros and
// the first pair the loops wrote, which are an expression's first of each. Whether it numbered them: an expression
// the rows take. `*steps` counts the records its labels' walk went over.
// - A call's row (node_machine_call_own): its preorder number among the expression's evaluated calls, those before
//   its subtree's first record and those that hold it.
// - A label (node_machine_number_labels): an operator's own is its preorder number among the expression's operators
//   with a label, and a tested value's branch has its owner's. An owner comes down from the condition as in the
//   branches' loop (node_rows_window_branch): an operator with a label owns its first operand, and every other
//   operand has its operator's owner. The walk goes down from the expression's last operator with a label to the
//   first record of the first such operator's operands: a value outside them has the control's label, or one's that
//   is the expression's first, a zero.
// - A temporary (node_machine_update_first): a postfix update through an address has two, numbered in the order the
//   updates' first rows are written, their destinations' records'.
// A record at a time: about one expression in eighty has two such records (the census's count), an expression has a
// few of each, and their order is the tree's. The reference numbers them a record at a time too.
static __attribute__((noinline, cold)) bool node_rows_window_number(struct NodeFactsReader* state, const NodeFront* front, uint32_t top, uint64_t* steps)
{
    const NodeRowsTables* tables = state->x_tables;
    const uint32_t size = front->sizes[top];
    const uint32_t first = top + 1 - size;
    const uint32_t e = state->window_x_expressions[top];
    uint64_t* values = state->x_values;
    uint16_t* owners = state->x_owners;
    uint16_t* marked = state->x_marked;
    // Its marked records, in the records' order: the marks' bits from its operand's first record through its last,
    // two or more (the sweep lists a record after an expression's first marked one, and a record outside the batch's
    // expressions has no shape: the measurement record's count of this routine's calls).
    uint32_t marks = 0;
    for (uint32_t word = first / 64; word <= top / 64; word += 1)
    {
        const uint64_t from = word == first / 64 ? ~UINT64_C(0) << (first & 63) : ~UINT64_C(0);
        const uint64_t through = word == top / 64 ? ~UINT64_C(0) >> (63 - (top & 63)) : ~UINT64_C(0);
        for (uint64_t bits = state->window_x_marks[word] & from & through; bits != 0; bits &= bits - 1)
        {
            marked[marks] = (uint16_t)(64 * word + (uint32_t)_tzcnt_u64(bits));
            marks += 1;
        }
    }
    const bool numbers = state->c_verdicts[e] == BUSTER_NODE_CONTEXTS_TAKEN && !node_contexts_window_any(state->window_c_masks, BUSTER_NODE_ROWS_MASK_LISTED, first, size);
    if (numbers)
    {
        uint32_t temporaries = 0;
        // The operators with a label: the walk's records are from the first one's operands' first to the last one.
        uint32_t walk_first = top;
        uint32_t walk_last = 0;
        for (uint32_t i = 0; i < marks; i += 1)
        {
            const uint32_t x = marked[i];
            const uint32_t own = state->window_x_owns[x];
            if (own == BUSTER_TYPED_CALL)
            {
                // The calls before its subtree's first record, and those after it whose subtree holds it: one that
                // starts no later than its own (a callee's call starts where its caller's does).
                const uint32_t lowest = x + 1 - front->sizes[x];
                uint32_t row = 0;
                for (uint32_t j = 0; j < marks; j += 1)
                {
                    const uint32_t other = marked[j];
                    const bool counted = other < lowest || (other > x && other + 1 - front->sizes[other] <= lowest);
                    row += state->window_x_owns[other] == BUSTER_TYPED_CALL && counted ? 1u : 0u;
                }
                values[node_rows_window_place(state, x)] = row;
            }
            else if (own >= BUSTER_NODE_ROWS_BRANCHED)
            {
                // An operator with labels: one evaluated as branches, or an operator's value.
                const uint32_t lowest = x + 1 - front->sizes[x];
                walk_first = lowest < walk_first ? lowest : walk_first;
                walk_last = x;
            }
            else
            {
                const uint32_t before = tables->value_slots[own] >> BUSTER_NODE_ROWS_SLOT_KEPT_SHIFT;
                uint64_t* kept = values + node_rows_window_place(state, x) + before;
                kept[0] = BUSTER_NODE_MACHINE_TEMPORARY | temporaries;
                kept[1] = BUSTER_NODE_MACHINE_TEMPORARY | temporaries;
                kept[3] = BUSTER_NODE_MACHINE_TEMPORARY | (temporaries + 1);
                kept[4] = BUSTER_NODE_MACHINE_TEMPORARY | temporaries;
                kept[5] = BUSTER_NODE_MACHINE_TEMPORARY | (temporaries + 1);
                temporaries += 2;
            }
        }
        // Down the walk's records, consumers before their operands: each operator evaluated as branches gives its
        // operands their owner (BUSTER_NODE_ROWS_BRANCHED: the shape its turn left); the last operator with a label
        // has whatever owner, which no record of the walk reads but through an operator with a label of its own.
        for (uint32_t x = walk_last + 1; x-- > walk_first;)
        {
            const uint32_t own = state->window_x_owns[x];
            if (own >= BUSTER_NODE_ROWS_BRANCHED)
            {
                // (An operator's value owns its condition as an operator with a label owns its first operand; its
                // other operands are values, whose owner no one reads.)
                owners[x - 1] = owners[x];
                owners[x - front->window_left[x]] = own == BUSTER_NODE_ROWS_BRANCHED ? owners[x] : (uint16_t)x;
            }
            // An operator's value: its jump to its end, its second label's, which is its first arm's row's last entry
            // (a conditional's) or its own row's third (a logical operator's).
            if (own >= BUSTER_NODE_ROWS_HELD)
            {
                const uint32_t arm = x - front->window_right[x];
                const uint32_t place = own == BUSTER_NODE_ROWS_HELD ? node_rows_window_place(state, arm) + state->window_x_lengths[arm] - 1u : node_rows_window_place(state, x) + 2u;
                values[place] = node_rows_window_label(state, front, marked, marks, x) + 1u;
            }
            // A tested value an operator owns: its branch's label is its owner's first. (An arm's part's shape is past
            // a tested value's too, and its byte has no owner's bit.)
            if (state->window_x_conversions[x] >= BUSTER_NODE_ROWS_TESTED && (state->window_x_senses[x] & BUSTER_NODE_ROWS_SENSE_OWNED) != 0)
            {
                values[node_rows_window_place(state, x) + state->window_x_lengths[x] - 1u] = node_rows_window_label(state, front, marked, marks, owners[x]);
            }
            *steps += 1;
        }
    }
    return numbers;
}

// The entries' values of a chunk's `count` records, whose rows the write has just placed (`written` entries): the
// chunk's third stream, a quadword an entry (X's tape's third column, node_machine_expand's). A census of the values
// (the measurement record's x-values-2850) found each one a function of what the window has: half are zero, and the
// others a field of a record's line, a type's stride or a fixed number, but for the numbers of the few expressions
// with two calls, two labels or two postfix updates through an address (node_rows_window_number).
// - The stream starts as zeros, and as the 1 of each PTR_ADD: a member's offset's sum's and an address constant's
//   (node_machine_own, node_machine_address), which no loop then writes; a pointer's stride is written over its own.
// - A sweep of masks, 64 records a step, over each record's own shape once the updates' and the branches' loops are
//   done, makes the lists (BUSTER_NODE_ROWS_LIST_*): a record's lists are its own shape's byte of the values' table
//   (NodeRowsTables.value_owns), but a constant's by its class and its fold (a floating one's and one folded to bool
//   are the rest's). And the records that are no first of their expression's with a number for a value (the table's
//   bit), from two masks a block: those records, and the expressions' operands' last records, which the reader
//   marked in the senses' column.
// - A loop a list writes its values, each a load or two and a store at its row's place, none waiting for another.
//   What a loop reads is a column: a record's value (NodeFactsReader.window_x_values: its object's number or its
//   payload, which the tests' sweep made with the records' bytes, and a member's offset, which the rows' reads left
//   there) and a pointer's stride (window_c_strides, which C's loop kept as it asked for it). A record's line is 64
//   bytes, and a loop that read one a record waited for a line of the cache a value (the 9700X's readings are in
//   docs/node-machine.md).
// - Two loops go over lists made before: the tested values the branches' loop listed for the control's label, and
//   the chunk's updates (the updates' loop's list), each for its store by name, its step and its destination's
//   temporaries.
// A value's rule is X's (node_machine_own, node_machine_address, node_machine_fold_conversion,
// node_machine_update_first and _last, node_machine_branch_hook):
// - an object's number for a LOCAL, a GLOBAL and their addresses (at reader time a global's entry holds its object,
//   which stands for its symbol until a splice patches it: node_machine_reader_strings, the shadow's comparison), and
//   a store's and an update's by name their destination's, their first operand's;
// - an integer constant's bits by its entry's class, after its own class's where its conversion folded; an address
//   constant's offset;
// - a member's offset in bytes; a pointer's stride, and its update's step's 1; an arithmetic update's step, one of
//   the class it is computed in;
// - the control's label for a tested value no operator owns (BUSTER_NODE_MACHINE_CONTROL_LABEL), and zero, an
//   expression's first label, for one an operator owns; zero, an expression's first call's row, for a CALL; a postfix
//   update through an address's two temporaries, an expression's first pair;
// - and zero for an access (a vector's bytes, and C takes no vector's access), a conversion, an operation and a test.
// `made` has the two lists' counts; the sweep's lists' counts are added to `listed`, the expressions numbered to
// `*numbered` and the records their labels' walks went over to `*steps`.
static __attribute__((noinline)) void node_rows_window_values(struct NodeFactsReader* state, const NodeFront* front, uint32_t count, uint32_t written, const NodeRowsCounts* made,
                                                              uint32_t* listed, uint32_t* numbered, uint64_t* steps)
{
    const NodeRowsTables* tables = state->x_tables;
    const uint64_t* record_values = state->window_x_values;
    uint64_t* values = state->x_values;
    uint16_t* firsts = state->x_lists[BUSTER_NODE_ROWS_LIST_FIRST];
    uint16_t* stored = state->x_lists[BUSTER_NODE_ROWS_LIST_STORED];
    uint16_t* constants = state->x_lists[BUSTER_NODE_ROWS_LIST_CONSTANT];
    uint16_t* strides = state->x_lists[BUSTER_NODE_ROWS_LIST_STRIDE];
    const uint16_t* controlled = state->x_lists[BUSTER_NODE_ROWS_LIST_CONTROLLED];
    uint16_t* rest = state->x_lists[BUSTER_NODE_ROWS_LIST_REST];
    uint16_t* seconds = state->x_lists[BUSTER_NODE_ROWS_LIST_SECOND];
    uint32_t first_count = 0;
    uint32_t stored_count = 0;
    uint32_t constant_count = 0;
    uint32_t stride_count = 0;
    uint32_t rest_count = 0;
    uint32_t second_count = 0;
    // The stream's zeros and its PTR_ADDs' ones, 64 entries a step from their kinds: through the last step's entries,
    // within the streams' room past a chunk's most (an entry past the chunk's has whatever kind was there, and no
    // one reads its value).
    const __m512i ones = _mm512_set1_epi64(1);
    for (uint32_t at = 0; at < written; at += 64)
    {
        const uint64_t sums = _mm512_cmpeq_epi8_mask(_mm512_loadu_si512(state->x_kinds + at), _mm512_set1_epi8(BUSTER_TYPED_PTR_ADD));
        for (uint32_t eight = 0; eight < 8; eight += 1)
        {
            _mm512_storeu_si512(values + at + 8 * eight, _mm512_maskz_mov_epi64((__mmask8)(sums >> (8 * eight)), ones));
        }
    }
    const __m512i places = _mm512_load_si512(tables->places);
    const uint32_t blocks = (count + 63) / 64;
    // Whether the expression that goes on from the block before has a marked record there, of each of the two kinds
    // of numbers (a call's row or a temporary; a label).
    uint64_t open = 0;
    uint64_t open_labels = 0;
    for (uint32_t block = 0; block < blocks; block += 1)
    {
        const uint32_t x = block * 64;
        const __m512i owns = _mm512_loadu_si512(state->window_x_owns + x);
        const __m512i classes = _mm512_loadu_si512(state->window_c_classes + x);
        const __m512i senses = _mm512_loadu_si512(state->window_x_senses + x);
        const __m512i to_facts = _mm512_loadu_si512(state->window_x_facts + x);
        const __m512i lists = node_window_table(tables->value_owns, owns);
        // A constant's list by its class and its fold: an integer's CONST is the constants', a floating one's and one
        // folded to bool the rest's. A folded one is an integer constant with a target of an integer's class
        // (node_rows_window_shapes): its own class is no floating one, and the reads read its target's facts this
        // chunk (another record's byte there is an earlier chunk's, which no term here reads).
        const __mmask64 constant = _mm512_test_epi8_mask(lists, _mm512_set1_epi8(BUSTER_NODE_ROWS_VALUE_CONSTANT));
        const __mmask64 folded = _mm512_cmpeq_epi8_mask(owns, _mm512_set1_epi8(BUSTER_NODE_ROWS_FOLDED));
        const __mmask64 floating = constant & _mm512_cmpge_epu8_mask(classes, _mm512_set1_epi8(BUSTER_TYPED_CLASS_F32));
        const __mmask64 to_bool = folded & _mm512_test_epi8_mask(to_facts, _mm512_set1_epi8(BUSTER_CHECK_LOWER_TYPE_BOOL));
        const __m512i base = _mm512_set1_epi16((int16_t)x);
        first_count = node_rows_listed(firsts, first_count, _mm512_test_epi8_mask(lists, _mm512_set1_epi8(BUSTER_NODE_ROWS_VALUE_FIRST)), places, base);
        stored_count = node_rows_listed(stored, stored_count, _mm512_test_epi8_mask(lists, _mm512_set1_epi8(BUSTER_NODE_ROWS_VALUE_STORED)), places, base);
        constant_count = node_rows_listed(constants, constant_count, constant & ~floating & ~to_bool, places, base);
        stride_count = node_rows_listed(strides, stride_count, _mm512_test_epi8_mask(lists, _mm512_set1_epi8(BUSTER_NODE_ROWS_VALUE_STRIDE)), places, base);
        rest_count = node_rows_listed(rest, rest_count, _mm512_test_epi8_mask(lists, _mm512_set1_epi8(BUSTER_NODE_ROWS_VALUE_REST)) | floating | to_bool, places, base);
        // The records whose value is a number in its expression, and the expressions' ends: an expression's records
        // are those after the end before it, through its own (and a root's record between two, which has no such
        // value). The first marked record of each is found by one subtraction: a borrow from an expression's first
        // record stops at the first marked bit from there, which is then the one bit that differs. (An expression
        // with no marked record lends its borrow to the next one's first, the bit that one's own borrow would stop
        // at, and that one's start's bit, which the borrow set, takes its own: a start is no marked record, since a
        // call, an operator with a label and an update's destination through an address each come after an operand
        // of theirs.) The others are the seconds, and so is the first of the expression that goes on from the block
        // before with a marked record there.
        // The two kinds are counted apart, each by its own subtraction: an expression with one call and one operator
        // with labels has an expression's first of each, which the loops write.
        const uint64_t marks = _mm512_test_epi8_mask(lists, _mm512_set1_epi8((char)BUSTER_NODE_ROWS_VALUE_NUMBERED));
        const uint64_t labels = _mm512_test_epi8_mask(lists, _mm512_set1_epi8((char)BUSTER_NODE_ROWS_VALUE_LABELED));
        const uint64_t ends = _mm512_test_epi8_mask(senses, _mm512_set1_epi8((char)BUSTER_NODE_ROWS_SENSE_TOP));
        const uint64_t starts = (ends << 1) | 1;
        const uint64_t first_marks = marks & ~(marks - starts);
        const uint64_t first_labels = labels & ~(labels - starts);
        const uint64_t lowest = first_marks & (0 - first_marks);
        const uint64_t lowest_label = first_labels & (0 - first_labels);
        const uint64_t lowest_end = ends & (0 - ends);
        const uint64_t going_on = lowest & (0 - open) & (0 - (uint64_t)(lowest - 1 <= lowest_end - 1 ? 1 : 0));
        const uint64_t labels_going_on = lowest_label & (0 - open_labels) & (0 - (uint64_t)(lowest_label - 1 <= lowest_end - 1 ? 1 : 0));
        uint64_t later = (marks & ~first_marks) | going_on | (labels & ~first_labels) | labels_going_on;
        const uint32_t past = 63 - (uint32_t)_lzcnt_u64(ends);
        const uint64_t after = ends == 0 ? marks : marks >> past >> 1;
        const uint64_t labels_after = ends == 0 ? labels : labels >> past >> 1;
        open = (after != 0 ? 1u : 0u) | (ends == 0 ? open : 0u);
        open_labels = (labels_after != 0 ? 1u : 0u) | (ends == 0 ? open_labels : 0u);
        state->window_x_marks[block] = marks | labels;
        state->window_x_ends[block] = ends;
        while (__builtin_expect(later != 0, 0))
        {
            seconds[second_count] = (uint16_t)(x + (uint32_t)_tzcnt_u64(later));
            second_count += 1;
            later &= later - 1;
        }
    }
    // The loops, each a list's: a row's place is the entries before its eight records' and before it among them
    // (node_rows_window_place), read here from the two columns. One copy of each loop's step: a list is some tens of
    // records a chunk, and a loop unrolled ends on two branches its count mispredicts, not one.
    const uint32_t* cursors = state->x_cursors;
    const uint8_t* offsets = state->window_x_offsets;
    const uint8_t* owns = state->window_x_owns;
    const uint8_t* entry_classes = state->x_classes;
    // Its record's value, its row's first entry's.
    #pragma clang loop unroll(disable)
    for (uint32_t i = 0; i < first_count; i += 1)
    {
        const uint32_t x = firsts[i];
        values[cursors[x / 8] + offsets[x]] = record_values[x];
    }
    // A store by name's one entry: its destination's record's value, its first operand's.
    const uint16_t* lefts = front->window_left;
    #pragma clang loop unroll(disable)
    for (uint32_t i = 0; i < stored_count; i += 1)
    {
        const uint32_t x = stored[i];
        values[cursors[x / 8] + offsets[x]] = record_values[x - lefts[x]];
    }
    // An integer constant's bits by its entry's class, which the rows' write left in the classes' stream: its own
    // class's, and then its target's for a folded one (the same class again for a plain one).
    const uint8_t* own_classes = state->window_c_classes;
    #pragma clang loop unroll(disable)
    for (uint32_t i = 0; i < constant_count; i += 1)
    {
        const uint32_t x = constants[i];
        const uint32_t place = cursors[x / 8] + offsets[x];
        values[place] = node_rows_extend(node_rows_extend(record_values[x], own_classes[x]), entry_classes[place]);
    }
    // A pointer's stride, its PTR_ADD's: the entry its shape says, after a NEG and after an update's step's CONST,
    // whose 1 is the row's first entry's (another row's first store is a zero's, or the stride's own place).
    const uint64_t* record_strides = state->window_c_strides;
    #pragma clang loop unroll(disable)
    for (uint32_t i = 0; i < stride_count; i += 1)
    {
        const uint32_t x = strides[i];
        const uint32_t place = cursors[x / 8] + offsets[x];
        const uint32_t slots = tables->value_slots[owns[x]];
        values[place] = (slots & BUSTER_NODE_ROWS_SLOT_STEP) != 0 ? 1u : 0u;
        values[place + (slots & BUSTER_NODE_ROWS_SLOT_STRIDE)] = record_strides[x];
    }
    // The control's label, a tested value's branch's, its row's last entry's; and a conditional's first arm's jump's,
    // its row's last entry's too: its conditional's end's label, 1 in an expression with one operator with labels
    // (the numbering writes another's), by the arm's bit, with no branch.
    const uint8_t* lengths = state->window_x_lengths;
    const uint8_t* jumped = state->window_x_senses;
    const uint32_t controlled_count = made->controlled;
    #pragma clang loop unroll(disable)
    for (uint32_t i = 0; i < controlled_count; i += 1)
    {
        const uint32_t x = controlled[i];
        const uint64_t jump = 0 - (uint64_t)((jumped[x] / BUSTER_NODE_ROWS_SENSE_JUMPED) & 1u);
        values[cursors[x / 8] + offsets[x] + lengths[x] - 1u] = BUSTER_NODE_MACHINE_CONTROL_LABEL ^ (jump & (BUSTER_NODE_MACHINE_CONTROL_LABEL ^ 1u));
    }
    // An update's own row and its destination's (node_machine_update_last and _first): its store by name's object,
    // its destination's record's value, its own part's last entry's (the updates' loop left a by-name store's kind
    // in the bits' column); an arithmetic update's step, one of the class it is computed in (its entry's: 1, or a
    // float's or a double's bits); and, of a postfix update through its address, its destination's two temporaries
    // after its own entries (the address's store and its read, the load, the old value's store, and the two read):
    // the expression's first pair, which the numbering writes again in an expression with two such updates.
    const uint32_t* updates = state->x_updates;
    const uint8_t* stores = state->window_x_bits;
    const uint32_t update_count = (uint32_t)made->updates;
    #pragma clang loop unroll(disable)
    for (uint32_t i = 0; i < update_count; i += 1)
    {
        const uint32_t x = updates[i];
        const uint32_t destination = x - lefts[x];
        const uint32_t own = owns[x];
        const uint32_t kept = owns[destination];
        const uint32_t place = cursors[x / 8] + offsets[x];
        const uint8_t class = entry_classes[place];
        const uint64_t one = class == BUSTER_TYPED_CLASS_F32 ? UINT32_C(0x3f800000) : class == BUSTER_TYPED_CLASS_F64 ? UINT64_C(0x3ff0000000000000) : 1u;
        // (Each of the two is written nowhere where the update has none: its kinds are no pattern a predictor learns.)
        const uint32_t unstepped = own - (BUSTER_NODE_ROWS_UPDATE + 2) > 1u ? 1u : 0u;
        const uint32_t through = stores[x] < BUSTER_TYPED_STORE_LOCAL ? 1u : 0u;
        const uint32_t last = place + tables->lengths[own] - 1u;
        values[place + ((BUSTER_NODE_ROWS_ENTRY_NOWHERE - place) & (0u - unstepped))] = one;
        values[last + ((BUSTER_NODE_ROWS_ENTRY_NOWHERE - last) & (0u - through))] = record_values[destination];
        if (__builtin_expect(kept >= BUSTER_NODE_ROWS_KEPT && kept < BUSTER_NODE_ROWS_KEPT_REST, 0))
        {
            uint64_t* six = values + cursors[destination / 8] + offsets[destination] + (tables->value_slots[kept] >> BUSTER_NODE_ROWS_SLOT_KEPT_SHIFT);
            six[0] = BUSTER_NODE_MACHINE_TEMPORARY;
            six[1] = BUSTER_NODE_MACHINE_TEMPORARY;
            six[3] = BUSTER_NODE_MACHINE_TEMPORARY | 1;
            six[4] = BUSTER_NODE_MACHINE_TEMPORARY;
            six[5] = BUSTER_NODE_MACHINE_TEMPORARY | 1;
        }
    }
    for (uint32_t i = 0; i < rest_count; i += 1)
    {
        node_rows_window_rest(state, rest[i]);
    }
    // The expressions with a second marked record: each one's numbers, once. Its operand's last record is the first
    // end at or after the record.
    uint32_t numbered_now = 0;
    uint32_t top_before = UINT32_MAX;
    for (uint32_t i = 0; i < second_count; i += 1)
    {
        const uint32_t x = seconds[i];
        uint32_t word = x / 64;
        uint64_t ends = state->window_x_ends[word] >> (x & 63) << (x & 63);
        while (ends == 0)
        {
            word += 1;
            ends = state->window_x_ends[word];
        }
        const uint32_t top = 64 * word + (uint32_t)_tzcnt_u64(ends);
        if (top != top_before)
        {
            numbered_now += node_rows_window_number(state, front, top, steps) ? 1u : 0u;
            top_before = top;
        }
    }
    listed[BUSTER_NODE_ROWS_LIST_FIRST] += first_count;
    listed[BUSTER_NODE_ROWS_LIST_STORED] += stored_count;
    listed[BUSTER_NODE_ROWS_LIST_CONSTANT] += constant_count;
    listed[BUSTER_NODE_ROWS_LIST_STRIDE] += stride_count;
    listed[BUSTER_NODE_ROWS_LIST_CONTROLLED] += made->controlled;
    listed[BUSTER_NODE_ROWS_LIST_REST] += rest_count;
    listed[BUSTER_NODE_ROWS_LIST_SECOND] += second_count;
    *numbered += numbered_now;
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

// What a summary's side facts' sweep made of a chunk (node_rows_window_summary): the records it listed for a
// reference; the calls it asked a record at a time; and the expressions whose fact the reduction made.
typedef struct NodeRowsSummaryCounts NodeRowsSummaryCounts;
struct NodeRowsSummaryCounts
{
    uint32_t references;
    uint32_t suspects;
    uint32_t reduced;
};

// The own return fact of the call at the window's record `call` (check_program_return_call, the flow's rule of a
// call's node): its callee is a function's name's leaf whose line's byte has one of the two bits, or the call is
// unknown: one through a value, or of a library's name the flow leaves so. A plain function's call has no fact, and a
// call of one declared noreturn stops. And a function the unit leaves unknown from one of its declarations on (an
// analyzer_noreturn, an ignored noreturn: check_program_return_unknowns' table) is unknown at a use after that
// declaration. (The flow reads a callee through `*` and `&` to a function's name: C lists an expression that calls
// `*f` or `&f` of one, and a name's leaf with no line lists its expression too. Such a call's fact is no one's.)
static __attribute__((noinline)) uint8_t node_rows_summary_call(const struct NodeFactsReader* state, const NodeFront* front, uint32_t call)
{
    const uint32_t callee = call - front->window_left[call];
    const uint8_t line = state->window_x_lines[callee];
    const bool function = (line & (BUSTER_NODE_ROWS_LINE_PLAIN | BUSTER_NODE_ROWS_LINE_STOPS)) != 0;
    const uint32_t* unknowns = state->types->return_unknowns;
    const bool known = function && (__builtin_expect(unknowns == NULL, 1) || unknowns[(uint32_t)state->window_x_values[callee]] >= front->tokens[callee]);
    return (uint8_t)(!known ? BUSTER_CHECK_RETURN_UNKNOWN : (line & BUSTER_NODE_ROWS_LINE_STOPS) != 0 ? BUSTER_CHECK_RETURN_STOPS : 0u);
}

// The return fact of the expression whose operand's records are the window's `first` to `last`, by the flow's
// reduction (check_program_return_node), a record at a time up the postorder: a call's own fact and its operands'
// joined; and an `&&`, an `||` or a `?:` keeps its condition's fact, an unknown of either arm, and a stop where both
// arms of a `?:` stop. (A taken expression has no operand that is not evaluated for a known condition, which C lists;
// and no `sizeof`, whose token the front leaves to the walk.) For the expressions with a call that stops and one of
// those three operators alone: any other's fact is its calls' own facts together, since an unknown goes up through
// every operator and a stop through every other one.
static __attribute__((noinline, cold)) uint8_t node_rows_summary_reduce(struct NodeFactsReader* state, const NodeFront* front, uint32_t first, uint32_t last)
{
    uint8_t* facts = state->window_x_returns;
    uint8_t* joined = state->window_x_joined;
    memset(joined + first, 0, (uint64_t)last - first + 1);
    for (uint32_t x = first; x <= last; x += 1)
    {
        const uint32_t op = front->ops[x] & 0x7fu;
        uint8_t fact = 0;
        if (op == CHECK_OP_LOGICAL_AND || op == CHECK_OP_LOGICAL_OR || op == CHECK_OP_CONDITIONAL)
        {
            const uint32_t third = op == CHECK_OP_CONDITIONAL ? front->window_entries[x] : 0;
            const uint8_t condition = facts[x - front->window_left[x]];
            const uint8_t yes = facts[x - front->window_right[x]];
            const uint8_t no = third != 0 ? facts[x - third] : 0;
            const uint8_t both = op == CHECK_OP_CONDITIONAL ? (uint8_t)(yes & no & BUSTER_CHECK_RETURN_STOPS) : 0;
            fact = (uint8_t)(condition | ((yes | no) & BUSTER_CHECK_RETURN_UNKNOWN) | both);
        }
        else
        {
            fact = (uint8_t)(joined[x] | (op == CHECK_OP_CALL ? node_rows_summary_call(state, front, x) : 0u));
        }
        facts[x] = fact;
        // (Every record but the operand's last has its consumer among these.)
        if (x != last)
        {
            const uint32_t consumer = x + front->window_ups[x];
            joined[consumer] = (uint8_t)(joined[consumer] | fact);
        }
    }
    return facts[last];
}

// Whether the window's records `first` to `last` have an `&&`, an `||` or a `?:` among them: 64 records a step.
static inline bool node_rows_summary_selects(const NodeFront* front, uint32_t first, uint32_t last)
{
    uint64_t any = 0;
    for (uint32_t x = first; x <= last; x += 64)
    {
        const uint32_t rest = last - x + 1;
        const __mmask64 lanes = _cvtu64_mask64(rest >= 64 ? UINT64_MAX : (UINT64_C(1) << rest) - 1);
        const __m512i codes = _mm512_and_si512(_mm512_maskz_loadu_epi8(lanes, front->ops + x), _mm512_set1_epi8(0x7f));
        any |= _cvtmask64_u64(_mm512_mask_cmple_epu8_mask(lanes, _mm512_sub_epi8(codes, _mm512_set1_epi8(CHECK_OP_LOGICAL_AND)), _mm512_set1_epi8(2)));
    }
    return any != 0;
}
_Static_assert(CHECK_OP_LOGICAL_OR == CHECK_OP_LOGICAL_AND + 1 && CHECK_OP_CONDITIONAL == CHECK_OP_LOGICAL_AND + 2, "the three operators that select an operand are one range of codes");
_Static_assert(BUSTER_NODE_FACTS_LITERAL_LINE == 6 && BUSTER_NODE_FACTS_OPERATOR < 16,
               "a record's kind is the place of its flag in the sweep's table, a literal's leaf's with a line the seventh");

// A summary's side facts, for the chunk's expressions: what the passes after the reader read below a full expression's
// root, which one summary node in its nodes' place has to answer from a side record (docs/node-machine.md, "A summary
// node's side facts on the window").
// - Its references (check_program_references: the static objects its leaves name, a set, which the selection's edges,
//   the symbols and the target groups read). A reference is a leaf whose object the walk takes, evaluated or not: a
//   name's with a symbol of its own, or a literal's with a line and an object (a string's). A sweep of masks, 64
//   records a step, lists the chunk's; a gather writes each one's object, its record's value, eight a step; and the
//   sweep writes the count of the references through each record (a block's are one multiplication and three steps
//   of sums in a register). An expression's references are then the list's between two of those counts, the one through the
//   record before its operand's first and the one through its last: two loads where its summary's record is written,
//   and no step of the sweep is an expression's own.
// - Its root's return fact (check_program_return_facts: a stop, an unknown). A call of a plain function's name has
//   none, and neither has an expression of such calls alone. The tests' loop gives each block a mask of its other
//   calls (c_check_node_census.c's node_tests_window_loop_by); the sweep lists them, and each is asked its own fact,
//   a record at a time (node_rows_summary_call). An expression's fact is its calls' facts together, but where one
//   stops under an operator that selects an operand: that expression has the flow's reduction over its records
//   (node_rows_summary_reduce).
// A condition's truth and a constant root's fold are a summary node's value's to answer: the turns list an `&&` or an
// `||` whose second operand's truth is known (node_rows_window_decided), and C lists a folded comma. A `switch`'s
// selector type is its operand's before the POSITIVE root applies the integer promotions (node_rows_window_keep).
static __attribute__((noinline)) void node_rows_window_summary(struct NodeFactsReader* state, const NodeFront* front, uint32_t count, NodeRowsSummaryCounts* made)
{
    const __m512i places = _mm512_load_si512(state->x_tables->places);
    const __m512i zero = _mm512_setzero_si512();
    // Each byte of a quadword the sum of the bytes up to it; and a quadword's first byte to its eight.
    const __m512i spread = _mm512_set1_epi64(INT64_C(0x0101010101010101));
    const __m512i quadwords = _mm512_broadcast_i32x4(_mm_set_epi8(8, 8, 8, 8, 8, 8, 8, 8, 0, 0, 0, 0, 0, 0, 0, 0));
    // The flag a reference has, by its record's kind: a symbol of its own (a name's leaf's alone has the bit), or for
    // a literal's leaf with a line an object (a string's; a leaf with no line lists its expression for C).
    const char symbol = BUSTER_NODE_CONTEXTS_SYMBOL;
    const char object = BUSTER_NODE_CONTEXTS_OBJECT;
    const __m512i asked = _mm512_broadcast_i32x4(_mm_setr_epi8(symbol, symbol, symbol, symbol, symbol, symbol, object, symbol, symbol, symbol, symbol, symbol, symbol, symbol,
                                                               symbol, symbol));
    const __m512i placed = _mm512_set1_epi8(BUSTER_NODE_CONTEXTS_PLACED);
    const __m512i step = _mm512_set1_epi16(64);
    const uint8_t* const kind_column = front->window_kinds;
    const uint8_t* const flag_column = state->window_c_flags;
    const uint64_t* const suspect_masks = state->window_x_suspects;
    uint16_t* const references = state->x_summary_references;
    uint16_t* const suspects = state->x_summary_suspects;
    uint16_t* const counted = state->window_x_referenced;
    __m512i base = zero;
    uint32_t reference_count = 0;
    uint32_t suspect_count = 0;
    for (uint32_t x = 0; x < count; x += 64)
    {
        // (A lane past the chunk's records has no flag: the tests' sweep writes a block's bytes whole.)
        const __m512i kinds = _mm512_loadu_si512(kind_column + x);
        const __m512i flags = _mm512_loadu_si512(flag_column + x);
        const __mmask64 referenced = _mm512_test_epi8_mask(flags, _mm512_shuffle_epi8(asked, kinds));
        // (A call the kernel didn't place is in an expression that is not the window's, and is no one's to ask.)
        const uint64_t suspected = suspect_masks[x / 64] & _cvtmask64_u64(_mm512_test_epi8_mask(flags, placed));
        // The block's references through each of its records: within a quadword by the multiplication, and the
        // quadwords before by three steps of sums of the eight totals.
        const __m512i ones = _mm512_maskz_set1_epi8(referenced, 1);
        const __m512i within = _mm512_mullo_epi64(ones, spread);
        const __m512i totals = _mm512_alignr_epi64(_mm512_sad_epu8(ones, zero), zero, 7);
        const __m512i pairs = _mm512_add_epi64(totals, _mm512_alignr_epi64(totals, zero, 7));
        const __m512i fours = _mm512_add_epi64(pairs, _mm512_alignr_epi64(pairs, zero, 6));
        const __m512i before = _mm512_add_epi64(fours, _mm512_alignr_epi64(fours, zero, 4));
        const __m512i through = _mm512_add_epi8(within, _mm512_shuffle_epi8(before, quadwords));
        // The chunk's through each: those before the block and the block's.
        const __m512i earlier = _mm512_set1_epi16((int16_t)reference_count);
        _mm512_store_si512(counted + x, _mm512_add_epi16(earlier, _mm512_cvtepu8_epi16(_mm512_castsi512_si256(through))));
        _mm512_store_si512(counted + x + 32, _mm512_add_epi16(earlier, _mm512_cvtepu8_epi16(_mm512_extracti64x4_epi64(through, 1))));
        reference_count = node_rows_listed(references, reference_count, referenced, places, base);
        if (__builtin_expect(suspected != 0, 0))
        {
            suspect_count = node_rows_listed(suspects, suspect_count, _cvtu64_mask64(suspected), places, base);
        }
        base = _mm512_add_epi16(base, step);
    }
    // Each listed record's object, its value's low half: a gather of eight values by eight of the list's places a
    // step, under a mask of the list's rest. (A loop of a load and a store a reference read the same on sqlite3.c and
    // more on Lua and musl, whose chunks' few references end it on a branch their count mispredicts; a gather of
    // sixteen doublewords read more on sqlite3.c: the 9700X's readings are in docs/node-machine.md.)
    const uint64_t* const values = state->window_x_values;
    uint32_t* const objects = state->x_summary_objects;
    #pragma clang loop unroll(disable)
    for (uint32_t i = 0; i < reference_count; i += 8)
    {
        const uint32_t rest = reference_count - i;
        const __mmask8 lanes = _cvtu32_mask8(rest >= 8 ? 0xffu : (1u << rest) - 1u);
        const __m512i at = _mm512_cvtepu16_epi64(_mm_maskz_loadu_epi16(lanes, references + i));
        const __m512i wide = _mm512_mask_i64gather_epi64(zero, lanes, at, values, 8);
        _mm256_mask_storeu_epi32(objects + i, lanes, _mm512_cvtepi64_epi32(wide));
    }
    const uint32_t expressions = state->expression_count;
    uint8_t* const facts = state->x_summary_facts;
    #pragma clang loop unroll(disable)
    for (uint32_t e = 0; e < expressions; e += 64)
    {
        _mm512_storeu_si512(facts + e, zero);
    }
    uint32_t reduced = 0;
    uint32_t reduced_expression = UINT32_MAX;
    for (uint32_t i = 0; i < suspect_count; i += 1)
    {
        const uint32_t call = suspects[i];
        const uint8_t fact = node_rows_summary_call(state, front, call);
        if (__builtin_expect(fact != 0, 0))
        {
            // Its expression: the one whose operand's last record is the first with the mark from the call on (the
            // kernel places a call only in an expression whose records are the window's in a row).
            uint32_t operand = call;
            while (operand < count && (state->window_x_senses[operand] & BUSTER_NODE_ROWS_SENSE_TOP) == 0)
            {
                operand += 1;
            }
            const uint32_t e = operand < count ? state->window_x_expressions[operand] : UINT32_MAX;
            if (e < expressions && e != reduced_expression)
            {
                // A stop under an `&&`, an `||` or a `?:` may be one arm's alone: the flow's reduction says. With
                // none of the three among the expression's records every fact goes up to its root.
                const uint32_t first = operand + 1 - front->sizes[operand];
                if ((fact & BUSTER_CHECK_RETURN_STOPS) != 0 && node_rows_summary_selects(front, first, operand))
                {
                    facts[e] = node_rows_summary_reduce(state, front, first, operand);
                    reduced_expression = e;
                    reduced += 1;
                }
                else
                {
                    facts[e] = (uint8_t)(facts[e] | fact);
                }
            }
        }
    }
    made->references = reference_count;
    made->suspects = suspect_count;
    made->reduced = reduced;
}

// The entries of the window's record `x`'s row.
static inline uint32_t node_rows_window_length(const struct NodeFactsReader* state, uint32_t x)
{
    return state->window_x_lengths[x];
}

// The reference's tape and the window's for the batch's expression `e`, taken here: whether they differ in their
// number of entries, in an entry's kind or in its class, or the reference refused a row; and `*valued` where they
// differ in an entry's value and in nothing else. X on T's columns gave the first, kept by the expression's root's
// node (the reader's NodeFrontTypes.tape_kinds, tape_classes and tape_values); `*unkept` where it kept none for want
// of room, which is compared with nothing. With the control (NodeFactsReader.x_fault), one of the reference's answers
// is read changed, an expression's first entry's kind, its last's class, its length and an entry's value in turn.
static bool node_rows_window_compare(struct NodeFactsReader* state, const NodeFront* front, uint32_t e, uint32_t* entries, bool* unkept, bool* valued)
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
    const uint64_t* values = types->tape_values + types->tape_firsts[root];
    bool differs = false;
    bool value_differs = false;
    *entries = inner + outer;
    *unkept = expected == UINT32_MAX;
    if (expected != UINT32_MAX)
    {
        // A tape with no entry has no kind, class or value to change: its length is, in those turns too. The value's
        // turn changes its lowest bit and its highest, a round of the four turns each, and the tape's first entry's
        // and its last's, two rounds each; and the class's turn changes the first entry's value too in a tape of two
        // entries or more, which then differs, and not in a value alone.
        const uint32_t turn = state->x_fault ? state->x_fault_turn % 4 : 4;
        const uint32_t round = state->x_fault_turn / 4;
        const uint64_t bit = UINT64_C(1) << (63u * (round % 2));
        const bool short_turn = expected == 0 && turn < 4;
        const uint32_t length = expected + (turn == 2 || short_turn ? 1u : 0u);
        const uint32_t valued_entry = turn == 3 && round / 2 % 2 != 0 ? length - 1 : 0;
        const bool valued_turn = turn == 3 || (turn == 1 && length > 1);
        differs = types->sweep_refusals[root] != NODE_MACHINE_TAKEN || inner + outer != length;
        for (uint32_t i = 0; !differs && i < length; i += 1)
        {
            const uint32_t at = i < inner ? start + i : node_rows_window_place(state, record) + (i - inner);
            const uint8_t kind = (uint8_t)(kinds[i] ^ (turn == 0 && i == 0 ? 1u : 0u));
            const uint8_t class = (uint8_t)(classes[i] ^ (turn == 1 && i == length - 1 ? 1u : 0u));
            const uint64_t value = values[i] ^ (valued_turn && i == valued_entry ? bit : 0u);
            differs = state->x_kinds[at] != kind || state->x_classes[at] != class;
            value_differs = value_differs || state->x_values[at] != value;
        }
        state->x_fault_turn += state->x_fault ? 1u : 0u;
    }
    *valued = !differs && value_differs;
    return differs;
}

// The batch's expression `e`'s side facts, kept by its root's node for the shadow's comparison with the program's
// own walks after the pipeline (c_check_node_census.c's node_shadow_summaries): its references' objects into the
// store (the list's between the counts through the record before its operand's first and through its last), and its
// return fact. An expression the store has no room for keeps none. With the control
// (NodeFactsReader.summary_fault) one of them is kept changed, in turn: a reference more, of no object's number, which
// the comparison's count sees; its first reference's object, that number in its place, which the comparison's
// objects see (or, where it has none, a reference more of a number past every object's, which its bound sees); its
// fact's stop; and its unknown. Returns its references' count.
static uint32_t node_rows_window_keep(struct NodeFactsReader* state, const NodeFront* front, uint32_t e)
{
    NodeFrontTypes* types = state->types;
    const uint32_t root = state->roots[e];
    const uint32_t operand = state->operands[e];
    // (The count through the record before a record is the entry before its own, the first record's among them.)
    const uint16_t* before = state->window_x_referenced - 1;
    const uint32_t first = before[operand + 1 - front->sizes[operand]];
    const uint32_t kept = (uint32_t)before[operand + 1] - first;
    const uint32_t turn = state->summary_fault ? state->summary_fault_turn % 4 : 4;
    const bool more = turn == 0 || (turn == 1 && kept == 0);
    // Only a switch has a POSITIVE wrapper at the expression root. Its operand's type is the selector's:
    // the front makes an implicit CONVERT only at a return, never inside a taken selector.
    const bool switched = front->ops[state->records[e]] == CHECK_OP_POSITIVE;
    const uint32_t words = kept + (more ? 1u : 0u) + (switched ? 1u : 0u);
    if (types->summary_firsts != NULL && root < types->node_capacity)
    {
        if (types->summary_count + words <= types->summary_capacity && kept < UINT16_MAX)
        {
            uint32_t* destination = types->summary_words + types->summary_count;
            memcpy(destination, state->x_summary_objects + first, (uint64_t)kept * sizeof(uint32_t));
            if (more)
            {
                destination[kept] = turn == 0 ? 0 : UINT32_MAX;
            }
            else if (turn == 1)
            {
                destination[0] = 0;
            }
            types->summary_firsts[root] = (uint32_t)types->summary_count;
            types->summary_counts[root] = (uint16_t)(kept + (more ? 1u : 0u));
            types->summary_facts[root] = (uint8_t)(state->x_summary_facts[e] ^ (turn == 2 ? BUSTER_CHECK_RETURN_STOPS : turn == 3 ? BUSTER_CHECK_RETURN_UNKNOWN : 0u));
            if (switched)
            {
                destination[types->summary_counts[root]] = state->window_x_types[operand] ^ (state->summary_fault ? 1u : 0u);
            }
            types->summary_count += words;
        }
        else
        {
            types->summary_unkept += 1;
        }
        state->summary_fault_turn += state->summary_fault ? 1u : 0u;
    }
    return kept;
}

// X's rows on the window for the chunk C has just answered: the shapes, the updates' rows and the branches' (where
// C's loop gave a record of the chunk the update's template, or evaluated a `!`, an `&&` or an `||` as branches: a
// chunk with neither has no sweep for them), the rows over its records and their entries' values, then each of the
// batch's expressions' part. Each expression taken here is then compared with the reference's tape, and each listed
// one counted by its first record's reason. The shadow runs them a second time over the same chunk, for their ticks
// with their code and their constants at hand: they give the same answers.
static void node_rows_window_chunk(struct NodeFactsReader* state, const NodeFront* front)
{
    NodeFrontTypes* types = state->types;
    uint64_t* counts = types->facts_counts;
    const uint32_t count = front->count;
    NodeRowsCounts read = { 0 };
    NodeWindowCounter* const counter = node_window_counting != NULL && node_window_counting->family == NODE_WINDOW_ROWS ? node_window_counting : NULL;
    uint64_t first_sample[8];
    uint64_t second_sample[8];
    const uint64_t t0 = __rdtsc();
    first_sample[0] = node_window_stamp(counter);
    node_rows_window_reads(state, count, &read);
    const uint64_t reads_end = __rdtsc();
    first_sample[1] = node_window_stamp(counter);
    node_rows_window_shapes(state, front, count);
    const uint64_t shapes_end = __rdtsc();
    first_sample[2] = node_window_stamp(counter);
    if ((state->c_updates_now | state->c_branches_now) != 0)
    {
        node_rows_window_updates(state, front, count, &read);
    }
    const uint64_t t1 = __rdtsc();
    first_sample[3] = node_window_stamp(counter);
    const uint32_t written = node_rows_window_write(state, front, count);
    const uint64_t t2 = __rdtsc();
    first_sample[4] = node_window_stamp(counter);
    uint32_t lists[BUSTER_NODE_ROWS_LISTS] = { 0 };
    uint32_t numbered = 0;
    uint64_t steps = 0;
    node_rows_window_values(state, front, count, written, &read, lists, &numbered, &steps);
    const uint64_t values_end = __rdtsc();
    first_sample[5] = node_window_stamp(counter);
    node_rows_window_part(state, front);
    const uint64_t t3 = __rdtsc();
    first_sample[6] = node_window_stamp(counter);
    NodeRowsSummaryCounts summary = { 0 };
    if (state->summaries)
    {
        node_rows_window_summary(state, front, count, &summary);
    }
    const uint64_t summary_end = __rdtsc();
    first_sample[7] = node_window_stamp(counter);
    const uint32_t decided = state->x_decided_now;
    state->x_decided_now = 0;
    // The first run's side facts, kept like its streams. And their comparison's control (NodeFactsReader.summary_fault):
    // one of them changed a chunk, in turn the references' count through the chunk's last record, its first
    // expression's fact (that count again where it has no expression), the count of its references, and its first
    // reference's object (the references' count where it has none).
    const uint32_t expressions = state->expression_count;
    const uint32_t summary_turn = state->summary_fault ? state->summary_again_turn % 4 : UINT32_MAX;
    state->summary_again_turn += state->summary_fault ? 1u : 0u;
    const uint32_t first_references = summary.references ^ (summary_turn == 2 || (summary_turn < 2 && count == 0) || (summary_turn == 3 && summary.references == 0) ? 1u : 0u);
    if (state->summaries)
    {
        memcpy(state->x_first_summary_referenced, state->window_x_referenced, (uint64_t)count * sizeof(uint16_t));
        memcpy(state->x_first_summary_facts, state->x_summary_facts, expressions);
        memcpy(state->x_first_summary_objects, state->x_summary_objects, (uint64_t)summary.references * sizeof(uint32_t));
        // (A chunk with no record has no count to change: its references' count is, in those turns too.)
        const uint32_t last = count != 0 ? count - 1 : 0;
        state->x_first_summary_referenced[last] = (uint16_t)(state->x_first_summary_referenced[last] ^ (summary_turn == 0 || (summary_turn == 1 && expressions == 0) ? 1u : 0u));
        state->x_first_summary_facts[0] = (uint8_t)(state->x_first_summary_facts[0] ^ (summary_turn == 1 && expressions != 0 ? 1u : 0u));
        state->x_first_summary_objects[0] ^= summary_turn == 3 && summary.references != 0 ? 1u : 0u;
    }
    // The first run's streams, kept: the second's are compared with them. A sweep that read what an earlier run left
    // in a column (a sense, a shape) would write the same the second time and another the first.
    memcpy(state->x_first_kinds, state->x_kinds, written);
    memcpy(state->x_first_classes, state->x_classes, written);
    memcpy(state->x_first_values, state->x_values, (uint64_t)written * sizeof(uint64_t));
    // That comparison's control (NodeFactsReader.x_fault): one of the first run's answers changed a chunk, in turn its
    // entries' count, its first entry's kind, its class and its value, the count of the expressions numbered, of their
    // walks' records and of each list's. A chunk that wrote no entry has no first one: its count is changed in
    // those turns too. The comparison then counts every chunk.
    const uint32_t raw_turn = state->x_fault ? state->x_again_turn % (6 + BUSTER_NODE_ROWS_LISTS) : UINT32_MAX;
    const uint32_t turn = written == 0 && raw_turn >= 1 && raw_turn <= 3 ? 0 : raw_turn;
    state->x_again_turn += state->x_fault ? 1u : 0u;
    const uint32_t first_written = written ^ (turn == 0 ? 1u : 0u);
    state->x_first_kinds[0] = (uint8_t)(state->x_first_kinds[0] ^ (turn == 1 ? 1u : 0u));
    state->x_first_classes[0] = (uint8_t)(state->x_first_classes[0] ^ (turn == 2 ? 1u : 0u));
    state->x_first_values[0] ^= turn == 3 ? 1u : 0u;
    const uint32_t first_numbered = numbered ^ (turn == 4 ? 1u : 0u);
    const uint64_t first_steps = steps ^ (turn == 5 ? 1u : 0u);
    uint32_t first_lists[BUSTER_NODE_ROWS_LISTS];
    for (uint32_t list = 0; list < BUSTER_NODE_ROWS_LISTS; list += 1)
    {
        first_lists[list] = lists[list] ^ (turn == 6 + list ? 1u : 0u);
    }
    const uint64_t later_start = __rdtsc();
    second_sample[0] = node_window_stamp(counter);
    NodeRowsCounts again = { 0 };
    node_rows_window_reads(state, count, &again);
    const uint64_t later_reads_end = __rdtsc();
    second_sample[1] = node_window_stamp(counter);
    node_rows_window_shapes(state, front, count);
    const uint64_t later_shapes_end = __rdtsc();
    second_sample[2] = node_window_stamp(counter);
    if ((state->c_updates_now | state->c_branches_now) != 0)
    {
        node_rows_window_updates(state, front, count, &again);
    }
    const uint64_t t4 = __rdtsc();
    second_sample[3] = node_window_stamp(counter);
    const uint32_t written_again = node_rows_window_write(state, front, count);
    const uint64_t t5 = __rdtsc();
    second_sample[4] = node_window_stamp(counter);
    uint32_t lists_again[BUSTER_NODE_ROWS_LISTS] = { 0 };
    uint32_t numbered_again = 0;
    uint64_t steps_again = 0;
    node_rows_window_values(state, front, count, written_again, &again, lists_again, &numbered_again, &steps_again);
    const uint64_t later_values_end = __rdtsc();
    second_sample[5] = node_window_stamp(counter);
    node_rows_window_part(state, front);
    const uint64_t t6 = __rdtsc();
    second_sample[6] = node_window_stamp(counter);
    NodeRowsSummaryCounts summary_again = { 0 };
    if (state->summaries)
    {
        node_rows_window_summary(state, front, count, &summary_again);
    }
    const uint64_t later_summary_end = __rdtsc();
    second_sample[7] = node_window_stamp(counter);
    if (counter != NULL)
    {
        node_window_collect(counter, 0, first_sample, (state->summaries ? 7u : 6u), count);
        node_window_collect(counter, 1, second_sample, (state->summaries ? 7u : 6u), count);
    }
    const uint32_t decided_again = state->x_decided_now;
    state->x_decided_now = 0;
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
                bool valued = false;
                const bool differs = node_rows_window_compare(state, front, e, &entries, &unkept, &valued) || valued;
                counts[NODE_FACTS_COUNT_ROWS_TAKEN] += 1;
                counts[NODE_FACTS_COUNT_ROWS_TAKEN_RECORDS] += state->counts[e];
                counts[NODE_FACTS_COUNT_ROWS_ENTRIES] += entries;
                counts[NODE_FACTS_COUNT_ROWS_UNKEPT] += unkept;
                counts[NODE_FACTS_COUNT_ROWS_DIFFERENCES] += differs;
                counts[NODE_FACTS_COUNT_ROWS_VALUE_DIFFERENCES] += valued;
                counts[NODE_FACTS_COUNT_ROWS_REFERENCE_TICKS] += state->x_ticks[e];
                if (!differs && state->summaries)
                {
                    const uint8_t fact = state->x_summary_facts[e];
                    counts[NODE_FACTS_COUNT_SUMMARY_EXPRESSIONS] += 1;
                    counts[NODE_FACTS_COUNT_SUMMARY_REFERENCES] += node_rows_window_keep(state, front, e);
                    counts[NODE_FACTS_COUNT_SUMMARY_UNKNOWN] += (fact & BUSTER_CHECK_RETURN_UNKNOWN) != 0;
                    counts[NODE_FACTS_COUNT_SUMMARY_STOPS] += (fact & BUSTER_CHECK_RETURN_STOPS) != 0;
                }
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
    counts[NODE_FACTS_COUNT_ROWS_AGAIN_DIFFERENCES] += first_written != written_again || memcmp(state->x_first_kinds, state->x_kinds, written) != 0 ||
                                                       memcmp(state->x_first_classes, state->x_classes, written) != 0 ||
                                                       memcmp(state->x_first_values, state->x_values, (uint64_t)written * sizeof(uint64_t)) != 0 ||
                                                       first_numbered != numbered_again || first_steps != steps_again ||
                                                       memcmp(first_lists, lists_again, sizeof(first_lists)) != 0 || decided != decided_again;
    // A chunk's second run's side facts against its first's: the two lists' counts, the references' counts through
    // each record, the expressions' facts, and the references' objects.
    counts[NODE_FACTS_COUNT_SUMMARY_AGAIN_DIFFERENCES] +=
        state->summaries && (first_references != summary_again.references || summary.suspects != summary_again.suspects || summary.reduced != summary_again.reduced ||
                             memcmp(state->x_first_summary_referenced, state->window_x_referenced, (uint64_t)count * sizeof(uint16_t)) != 0 ||
                             memcmp(state->x_first_summary_facts, state->x_summary_facts, expressions) != 0 ||
                             memcmp(state->x_first_summary_objects, state->x_summary_objects, (uint64_t)summary.references * sizeof(uint32_t)) != 0);
    counts[NODE_FACTS_COUNT_SUMMARY_LISTED] += summary.references;
    counts[NODE_FACTS_COUNT_SUMMARY_SUSPECTS] += summary.suspects;
    counts[NODE_FACTS_COUNT_SUMMARY_REDUCED] += summary.reduced;
    counts[NODE_FACTS_COUNT_SUMMARY_DECIDED] += decided;
    counts[NODE_FACTS_COUNT_SUMMARY_TICKS] += summary_end - t3;
    counts[NODE_FACTS_COUNT_SUMMARY_LATER_TICKS] += later_summary_end - t6;
    for (uint32_t list = 0; list < BUSTER_NODE_ROWS_LISTS; list += 1)
    {
        counts[NODE_FACTS_COUNT_ROWS_LIST + list] += lists[list];
    }
    counts[NODE_FACTS_COUNT_ROWS_NUMBERED] += numbered;
    counts[NODE_FACTS_COUNT_ROWS_NUMBER_STEPS] += steps;
    counts[NODE_FACTS_COUNT_ROWS_WORDS] += read.words;
    counts[NODE_FACTS_COUNT_ROWS_LINES] += read.lines;
    counts[NODE_FACTS_COUNT_ROWS_UPDATES] += read.updates;
    counts[NODE_FACTS_COUNT_ROWS_BRANCHED] += read.branched;
    counts[NODE_FACTS_COUNT_ROWS_READ_TICKS] += reads_end - t0;
    counts[NODE_FACTS_COUNT_ROWS_SHAPE_TICKS] += shapes_end - reads_end;
    counts[NODE_FACTS_COUNT_ROWS_UPDATE_TICKS] += t1 - shapes_end;
    counts[NODE_FACTS_COUNT_ROWS_WRITE_TICKS] += t2 - t1;
    counts[NODE_FACTS_COUNT_ROWS_VALUE_TICKS] += values_end - t2;
    counts[NODE_FACTS_COUNT_ROWS_PART_TICKS] += t3 - values_end;
    counts[NODE_FACTS_COUNT_ROWS_LATER_READ_TICKS] += later_reads_end - later_start;
    counts[NODE_FACTS_COUNT_ROWS_LATER_SHAPE_TICKS] += later_shapes_end - later_reads_end;
    counts[NODE_FACTS_COUNT_ROWS_LATER_UPDATE_TICKS] += t4 - later_shapes_end;
    counts[NODE_FACTS_COUNT_ROWS_LATER_WRITE_TICKS] += t5 - t4;
    counts[NODE_FACTS_COUNT_ROWS_LATER_VALUE_TICKS] += later_values_end - t5;
    counts[NODE_FACTS_COUNT_ROWS_LATER_PART_TICKS] += t6 - later_values_end;
}

#endif
