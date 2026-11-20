// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The node machine's front (#2524, step 1b, docs/node-machine.md): the records the machine's T would type, made from the
// tokens the expressions' walk acts at, their classes and the reader's answers, without the walk's chains of values
// through the token columns and without its graph. In the shadow it runs beside the coded walk (PPOptions.node_front),
// which is its oracle: each record is compared with the node the walk made at the same token. No compile takes it.
//
// A record is a node's operator, its token, its operands as earlier records, each also as a byte, its distance back in
// the postorder (what #2850's layout asks of the front: an operand's facts are then a permute over the block of records
// before it, not a gather), and what the operator reads beside them: a leaf's value (the chunk's identifiers' and literals' entries, a string's object), a member's name, a call's arguments
// and its library mark, the type the token's query asked for. The operands come from a stack of the values not yet
// taken, in groups that share a consumer: a value's is its token's parent, and a token that passes its children's values
// on (a list, a group, a separator, a braced initializer) gives its group to its own parent, in one store however many
// values it holds. A token's values are then the top group's, where the token is its consumer, in source order, and a
// value whose consumer never acts is no entry at all. So a token costs its class's case, a load of its parent, and its
// record's stores, all in arrays a chunk long; nothing is written a token.
//
// Beside its operands' distances a record has the other structural bytes of #2850's layout: its operand count, and, once
// its consumer's record is made, that consumer's distance forward and its place among the consumer's operands. The lean
// front (PPOptions.node_front's BUSTER_NODE_FRONT_LEAN) writes only those, the token and a literal's or a string's value:
// an identifier's record is its token and declaration, whose facts are a load away (#2850's fact words), and a member's
// name, a requested type and a call's library mark are loads by the token. It reads none of the chunk's identifier
// entries.
//
// A token the front has no case for (the walk's own visits, a class's action's rarer branches, a literal whose decoding
// reports) is left: its values are taken and it gives an unknown one, which leaves its consumer in turn. A full
// expression with such a token has records only for the subtrees below it, and the shadow counts its nodes as not the
// front's (NodeFrontShadow.nodes).
// Included by c_check_input.c after the coded walk.

// The records the front holds: a chunk's, and those a full expression that began in an earlier chunk still needs.
#define BUSTER_NODE_FRONT_RECORDS 4096u
// The most values a token takes here: a call's callee and arguments, a terminator's values.
#define BUSTER_NODE_FRONT_VALUES 64u
// A stack entry whose value a left token gave, and an operand a record doesn't have.
#define BUSTER_NODE_FRONT_UNKNOWN UINT32_MAX
// A leaf's value with more than a record's columns hold (a floating constant, a wide integer), by its place among them.
#define BUSTER_NODE_FRONT_WIDES 1024u
// An operand's distance where it doesn't fit its byte: 255 records back or more. And the distance within which an operand's
// facts are in the block of 64 records before its record's (#2850).
#define BUSTER_NODE_FRONT_FAR 255u
#define BUSTER_NODE_FRONT_NEAR 64u

// The differing records a file's report lists.
#define BUSTER_NODE_FRONT_LISTED 16u

// PPOptions.node_front: 0 for no front, BUSTER_NODE_FRONT_AFTER or BUSTER_NODE_FRONT_BEFORE the walk's actions at a
// chunk, with BUSTER_NODE_FRONT_LEAN for the lean front and BUSTER_NODE_FRONT_WARM for its arrays touched beforehand.
#define BUSTER_NODE_FRONT_AFTER 1u
#define BUSTER_NODE_FRONT_BEFORE 2u
#define BUSTER_NODE_FRONT_ORDER 3u
#define BUSTER_NODE_FRONT_LEAN 4u
// The front's arrays touched before its first chunk (measure-node-machine --shadow --front-warm): its ticks without the
// first touch of fresh memory.
#define BUSTER_NODE_FRONT_WARM 8u
// What #2850's layout keeps in its byte columns: an operand count up to 63 and a place up to 3 (step 0's lanes.c); the
// front's bytes hold up to 255, and the shadow counts what is past the layout's.
#define BUSTER_NODE_FRONT_COUNT_MOST 63u
#define BUSTER_NODE_FRONT_PLACE_MOST 3u
// T run at reader time on the full expressions the front makes whole (measure-node-machine --shadow --front-types): the
// front after the walk, and its results in NodeFrontShadow.types.
#define BUSTER_NODE_FRONT_TYPES 16u
// The flat front beside the lean one, its twin (measure-node-machine --shadow --front-flat; c_check_node_machine_flat.c):
// after the walk acted at a chunk, as the lean front, or with BUSTER_NODE_FRONT_FLAT_FIRST before the walk's actions,
// where the machine would run. Its records are compared with the lean front's after both made the chunk's.
#define BUSTER_NODE_FRONT_FLAT 32u
#define BUSTER_NODE_FRONT_FLAT_FIRST 64u
// C and X run there after T, on the expressions it takes (measure-node-machine --shadow --front-sweeps, with
// BUSTER_NODE_FRONT_TYPES): their tapes kept in NodeFrontShadow.types.
#define BUSTER_NODE_FRONT_SWEEPS 128u
// PPOptions.node_reader: what else runs at reader time beside T, C and X, in the shadow. BUSTER_NODE_READER_TESTS: the
// census's set and silence tests after T, on each expression it takes (measure-node-machine --shadow --front-tests; "The
// tests where they sit"). BUSTER_NODE_READER_VERIFY: T's rows and rules checked against the action there (--verify).
#define BUSTER_NODE_READER_TESTS 1u
#define BUSTER_NODE_READER_VERIFY 2u
// BUSTER_NODE_READER_LAST: the tests after C and X, on the expressions those take, where they otherwise come between T
// and C (--front-tests-last).
#define BUSTER_NODE_READER_LAST 16u
// #2850's fact words' kernel at reader time too (measure-node-machine --shadow --front-facts;
// c_check_node_machine_facts.c's node_facts_reader_chunk): on each chunk's expressions T took there, T's records the oracle.
#define BUSTER_NODE_READER_FACTS 32u
// The lean front writes #2850's window too, a record's inputs for the fact words' kernel as it makes the record, and
// the kernel reads them where they are written (measure-node-machine --shadow --front-window, with
// BUSTER_NODE_READER_FACTS; c_check_node_machine_facts.c's "The window the front writes"). The front then keeps a
// waiting value's whole subtree across a chunk's end, where it otherwise keeps the value's place alone.
#define BUSTER_NODE_READER_WINDOW 64u
// The census's tests in the machine's own form, on that window (measure-node-machine --shadow --window-tests, with
// BUSTER_NODE_READER_WINDOW and BUSTER_NODE_READER_TESTS; c_check_node_census.c's node_tests_window_chunk; "The tests
// on the window"): once the kernel has made a chunk's records in place, the tests of each of its expressions from
// those records and the front's columns, 64 records a step, compared with the bits the census's code gave on T's
// records.
#define BUSTER_NODE_READER_WINDOW_TESTS 128u
// C's answers on that window too (measure-node-machine --shadow --window-contexts, with BUSTER_NODE_READER_WINDOW_TESTS,
// whose sweep makes the records' bytes; c_check_node_machine_window.c's node_contexts_window_chunk; "C on the window"):
// each record's context, template and target and whether it is evaluated, 64 records a step, compared with C's on T's
// columns at reader time.
#define BUSTER_NODE_READER_WINDOW_CONTEXTS 4u
// test-checker's control of that comparison (with BUSTER_NODE_READER_WINDOW_CONTEXTS): the reference's answers are kept
// with one of them changed for each expression C takes (its root's template, context, target or whether it is
// evaluated, or its verdict, in turn), so that the comparison is seen to count every one of them
// (c_check_node_machine_facts.c's node_facts_reader_fault).
#define BUSTER_NODE_READER_WINDOW_CONTEXTS_FAULT 8u
// test-checker's too (with BUSTER_NODE_READER_FACTS): the kernel's call rule's arguments' answers' slots are one
// (c_check_node_machine_facts.c's NodeFacts.argument_one: an answer is written to every slot), so that a key's probe
// meets another key's answer at its slot wherever the last answer was another's. The calls' loop then takes a slot's
// answer for a key only where the slot has that key, or the kernel's records are not T's.
#define BUSTER_NODE_READER_ONE_SLOT 256u
// X's rows on that window too, first their shapes (measure-node-machine --shadow --window-rows, with
// BUSTER_NODE_READER_WINDOW_CONTEXTS, whose answers they are made from; c_check_node_machine_rows.c's
// node_rows_window_chunk; "X's rows on the window"): the kinds, the classes and the values of the entries X writes
// for the expressions C takes there, compared with X's tape on T's columns at reader time.
#define BUSTER_NODE_READER_WINDOW_ROWS 512u
// test-checker's control of that comparison (with BUSTER_NODE_READER_WINDOW_ROWS): the reference's tape is read with
// one of its answers changed for each expression the rows take (its first entry's kind, its last's class, its length
// or an entry's value, in turn), so that the comparison is seen to count every one of them
// (node_rows_window_compare); and one of a chunk's first run's answers is changed, so that its second run's
// comparison is seen to count every chunk (node_rows_window_chunk).
#define BUSTER_NODE_READER_WINDOW_ROWS_FAULT 1024u
// Why C on the window lists an expression (c_check_node_machine_window.c's BUSTER_NODE_CONTEXTS_REASON_*): how many.
#define BUSTER_NODE_CONTEXTS_REASONS 8u
// What the reference did with an expression (NodeFactsReader.c_ran, which the reader gives the kernel's state at each
// root): C didn't run on it, took it, or refused it for a template.
#define BUSTER_NODE_CONTEXTS_RAN_NONE 0u
#define BUSTER_NODE_CONTEXTS_RAN_TAKEN 1u
#define BUSTER_NODE_CONTEXTS_RAN_REFUSED 2u
// A lean leaf's kind, in its record's `leaf_categories`.
#define BUSTER_NODE_FRONT_LEAF_IDENTIFIER 0u
#define BUSTER_NODE_FRONT_LEAF_LITERAL 1u
#define BUSTER_NODE_FRONT_LEAF_STRING 2u

// The counts of the fact words' kernel at reader time (NodeFrontTypes.facts_counts; c_check_node_machine_facts.c's
// node_facts_reader_flush), a unit's.
// - What it ran on: the expressions T took there, their records, and the windows they made.
// - Of the records: the leaves that name no declaration (literals); the records the kernel is to make besides a
//   literal's leaf (an identifier's leaf, a lookup, another operator its rules cover), those it made, and those of them
//   a short path or a class's loop made; and the literals' leaves it is to make from a line, and those it made.
// - The records that differ from T's over poison or over decoys, those over decoys alone, the windows whose lists
//   differ from their twin's, and the batches that had no room.
// - TSC ticks. The kernel's first run on each window (which fills the members' and the pointer conversions' slots it
//   meets first); a later run, all it reads where the runs before left it; T again on the same expressions, in the same
//   turn as that run (the kernel first in every other batch), with the records T ran again on; and the windows' making
//   from T's columns and the walk's nodes (the shadow's own: node_facts_append).
// - The words: the types' rows and their ticks, the declarations' lines and theirs, the lines made again for a
//   declaration, and the members' and the pointer conversions' slots filled.
// - The same counts of the records in the expressions the whole machine takes there (no test of the census's refuses
//   them, and C and X take them: with --front-tests), which are what the kernel's windows hold after the pipeline.
// - The windows' inputs made from the front's records and the reader's columns alone (node_facts_front_expression):
//   the expressions they were made for and their records, the records whose inputs differ from the window's (which
//   node_facts_append made from T's columns and the walk's nodes), the expressions the front's records don't give
//   whole, the ticks of making them as each expression came (a read of the counter before and after each), and the
//   ticks and the records of making a batch's again in one loop.
// - The windows the front wrote itself (BUSTER_NODE_READER_WINDOW), which the kernel reads where they are written: the
//   expressions whose records are there in a row and their records, the records whose inputs differ from either of the
//   other two makings', the expressions that are not there; and the kernel's run in place: the records it is to make
//   and those it made, the records that differ from T's (over poison, over decoys, or by its lists), and its later
//   run's ticks.
typedef enum NodeFactsCount
{
    NODE_FACTS_COUNT_EXPRESSIONS,
    NODE_FACTS_COUNT_RECORDS,
    NODE_FACTS_COUNT_WINDOWS,
    NODE_FACTS_COUNT_LITERALS,
    NODE_FACTS_COUNT_WANTED,
    NODE_FACTS_COUNT_MADE,
    NODE_FACTS_COUNT_SHORT,
    NODE_FACTS_COUNT_LINES,
    NODE_FACTS_COUNT_LINES_MADE,
    NODE_FACTS_COUNT_DIFFERENCES,
    NODE_FACTS_COUNT_DECOY_DIFFERENCES,
    NODE_FACTS_COUNT_LIST_DIFFERENCES,
    NODE_FACTS_COUNT_FAILURES,
    NODE_FACTS_COUNT_FIRST_TICKS,
    NODE_FACTS_COUNT_TICKS,
    NODE_FACTS_COUNT_AGAIN_TICKS,
    NODE_FACTS_COUNT_AGAIN_RECORDS,
    NODE_FACTS_COUNT_APPEND_TICKS,
    NODE_FACTS_COUNT_ROWS,
    NODE_FACTS_COUNT_ROW_TICKS,
    NODE_FACTS_COUNT_DECLARATION_LINES,
    NODE_FACTS_COUNT_LINE_TICKS,
    NODE_FACTS_COUNT_LINE_VERSIONS,
    NODE_FACTS_COUNT_MEMBER_SLOTS,
    NODE_FACTS_COUNT_PAIR_SLOTS,
    NODE_FACTS_COUNT_TAKEN_RECORDS,
    NODE_FACTS_COUNT_TAKEN_LITERALS,
    NODE_FACTS_COUNT_TAKEN_WANTED,
    NODE_FACTS_COUNT_TAKEN_MADE,
    NODE_FACTS_COUNT_FRONT_EXPRESSIONS,
    NODE_FACTS_COUNT_FRONT_RECORDS,
    NODE_FACTS_COUNT_FRONT_DIFFERENCES,
    NODE_FACTS_COUNT_FRONT_SKIPPED,
    NODE_FACTS_COUNT_FRONT_TICKS,
    NODE_FACTS_COUNT_FRONT_AGAIN_TICKS,
    NODE_FACTS_COUNT_FRONT_AGAIN_RECORDS,
    NODE_FACTS_COUNT_WINDOW_EXPRESSIONS,
    NODE_FACTS_COUNT_WINDOW_RECORDS,
    NODE_FACTS_COUNT_WINDOW_DIFFERENCES,
    NODE_FACTS_COUNT_WINDOW_SKIPPED,
    NODE_FACTS_COUNT_WINDOW_WANTED,
    NODE_FACTS_COUNT_WINDOW_MADE,
    NODE_FACTS_COUNT_WINDOW_KERNEL_DIFFERENCES,
    NODE_FACTS_COUNT_WINDOW_KERNEL_TICKS,
    // The kernel in place's calls: those it is to make and those it made; of the made, those whose arguments'
    // conversions it made too, and the plain ones among them; those its rule left to T for an argument alone; those
    // the rule made in the timed run, the short path having left them; and the answers its arguments' rule made (the
    // unit's so far, as the slots' counts).
    NODE_FACTS_COUNT_WINDOW_CALLS,
    NODE_FACTS_COUNT_WINDOW_CALLS_MADE,
    NODE_FACTS_COUNT_WINDOW_CALLS_CONVERTED,
    NODE_FACTS_COUNT_WINDOW_CALLS_PLAIN,
    NODE_FACTS_COUNT_WINDOW_CALLS_ARGUMENT,
    NODE_FACTS_COUNT_WINDOW_ANSWERS,
    NODE_FACTS_COUNT_ARGUMENT_ANSWERS,
    NODE_FACTS_COUNT_ARGUMENT_ANSWER_TICKS,
    // The tests on the window (BUSTER_NODE_READER_WINDOW_TESTS): the expressions and records they ran on, the
    // expressions whose bits are not the reference's, and their ticks.
    NODE_FACTS_COUNT_TESTS_EXPRESSIONS,
    NODE_FACTS_COUNT_TESTS_RECORDS,
    NODE_FACTS_COUNT_TESTS_DIFFERENCES,
    NODE_FACTS_COUNT_TESTS_TICKS,
    // Of the ticks: the records' bytes made, the loop over them, and the tests that read T's records still (the rest is
    // each expression's own part of the loop's results). Then the operands before the block before, the declarations'
    // lines whose byte was made, and the chunks' records the bytes and the loop ran over.
    NODE_FACTS_COUNT_TESTS_BYTES_TICKS,
    NODE_FACTS_COUNT_TESTS_LOOP_TICKS,
    NODE_FACTS_COUNT_TESTS_REST_TICKS,
    NODE_FACTS_COUNT_TESTS_FAR,
    NODE_FACTS_COUNT_TESTS_LINES,
    NODE_FACTS_COUNT_TESTS_SWEPT,
    // The expressions whose root's record is apart from its operand's, T's ticks alone on the tests' expressions, and
    // the chunks the tests ran on.
    NODE_FACTS_COUNT_TESTS_APART,
    NODE_FACTS_COUNT_TESTS_AGAIN_TICKS,
    NODE_FACTS_COUNT_TESTS_CHUNKS,
    // C on the window (BUSTER_NODE_READER_WINDOW_CONTEXTS): the expressions it ran on (the kernel placed them and the
    // reference's C ran on them) and their records; those it decided, their records, and of them those it refuses for
    // a template; the decided expressions and the records that differ from the reference; the records whose stride was
    // read, those of them asked of the type table (their type's row doesn't hold it), and the consumers and operands
    // past a block's reach; the chunks' records its sweeps ran over; and its
    // ticks: the loop, the targets, an expression's part, and the reference's C on the decided expressions; then the
    // three sweeps' ticks in a second run over the same chunk, which the shadow makes to say what of the first is the
    // chunk's cold start (its code and its constants come back from the second-level cache each chunk).
    NODE_FACTS_COUNT_CONTEXTS_EXPRESSIONS,
    NODE_FACTS_COUNT_CONTEXTS_RECORDS,
    NODE_FACTS_COUNT_CONTEXTS_DECIDED,
    NODE_FACTS_COUNT_CONTEXTS_DECIDED_RECORDS,
    NODE_FACTS_COUNT_CONTEXTS_REFUSED,
    NODE_FACTS_COUNT_CONTEXTS_DIFFERENCES,
    NODE_FACTS_COUNT_CONTEXTS_RECORD_DIFFERENCES,
    NODE_FACTS_COUNT_CONTEXTS_STRIDES,
    NODE_FACTS_COUNT_CONTEXTS_STRIDES_ASKED,
    NODE_FACTS_COUNT_CONTEXTS_CONVERTED,
    NODE_FACTS_COUNT_CONTEXTS_FAR,
    NODE_FACTS_COUNT_CONTEXTS_SWEPT,
    NODE_FACTS_COUNT_CONTEXTS_LOOP_TICKS,
    NODE_FACTS_COUNT_CONTEXTS_TARGET_TICKS,
    NODE_FACTS_COUNT_CONTEXTS_PART_TICKS,
    NODE_FACTS_COUNT_CONTEXTS_REFERENCE_TICKS,
    NODE_FACTS_COUNT_CONTEXTS_LATER_LOOP_TICKS,
    NODE_FACTS_COUNT_CONTEXTS_LATER_TARGET_TICKS,
    NODE_FACTS_COUNT_CONTEXTS_LATER_PART_TICKS,
    // The updates C asks its plan of, and those of them the loop's masks took: another lists its expression.
    NODE_FACTS_COUNT_CONTEXTS_UPDATES,
    NODE_FACTS_COUNT_CONTEXTS_UPDATES_TAKEN,
    // The records the loop evaluates as branches (a statement's CONDITION over `&&`, `||` and `!`), and those of them
    // that are the CONDITION.
    NODE_FACTS_COUNT_CONTEXTS_BRANCHED,
    NODE_FACTS_COUNT_CONTEXTS_BRANCHED_ROOTS,
    // The records the loop gives an operator's value's two labels (a conditional's, an `&&`'s and an `||`'s), those of
    // them that list their expression for the conversion their consumer puts into their arms, and the commas it takes.
    NODE_FACTS_COUNT_CONTEXTS_VALUED,
    NODE_FACTS_COUNT_CONTEXTS_SUNK,
    NODE_FACTS_COUNT_CONTEXTS_COMMAS,
    // X's rows on the window (BUSTER_NODE_READER_WINDOW_ROWS): the expressions it ran on (those C takes on the window)
    // and their records; those whose tape it made, their records and their entries; the listed ones by their first
    // record's reason (BUSTER_NODE_ROWS_REASON_*, in its order); the made tapes that differ from the reference's, and
    // those the reference kept no tape of; the chunks' records its sweeps ran over and the entries they wrote; the
    // chunks whose second run wrote other entries than its first; the records its loops read for (a target's word,
    // where the target is not the record's type; a line or a table of the unit's; an update's plan) and the updates
    // whose rows it made;
    // and its ticks: the reads, the shapes, the rows' write, an expression's part, and the reference's X on the
    // expressions taken; then the four's in a second run over the same chunk.
    NODE_FACTS_COUNT_ROWS_EXPRESSIONS,
    NODE_FACTS_COUNT_ROWS_RECORDS,
    NODE_FACTS_COUNT_ROWS_TAKEN,
    NODE_FACTS_COUNT_ROWS_TAKEN_RECORDS,
    NODE_FACTS_COUNT_ROWS_ENTRIES,
    NODE_FACTS_COUNT_ROWS_LISTED_UPDATE,
    NODE_FACTS_COUNT_ROWS_LISTED_BRANCHES,
    NODE_FACTS_COUNT_ROWS_LISTED_CONVERSION,
    NODE_FACTS_COUNT_ROWS_LISTED_FLOATING,
    NODE_FACTS_COUNT_ROWS_DIFFERENCES,
    NODE_FACTS_COUNT_ROWS_UNKEPT,
    NODE_FACTS_COUNT_ROWS_SWEPT,
    NODE_FACTS_COUNT_ROWS_WRITTEN,
    NODE_FACTS_COUNT_ROWS_AGAIN_DIFFERENCES,
    NODE_FACTS_COUNT_ROWS_WORDS,
    NODE_FACTS_COUNT_ROWS_LINES,
    NODE_FACTS_COUNT_ROWS_UPDATES,
    NODE_FACTS_COUNT_ROWS_BRANCHED,
    NODE_FACTS_COUNT_ROWS_READ_TICKS,
    NODE_FACTS_COUNT_ROWS_SHAPE_TICKS,
    NODE_FACTS_COUNT_ROWS_UPDATE_TICKS,
    NODE_FACTS_COUNT_ROWS_WRITE_TICKS,
    NODE_FACTS_COUNT_ROWS_PART_TICKS,
    NODE_FACTS_COUNT_ROWS_REFERENCE_TICKS,
    NODE_FACTS_COUNT_ROWS_LATER_READ_TICKS,
    NODE_FACTS_COUNT_ROWS_LATER_SHAPE_TICKS,
    NODE_FACTS_COUNT_ROWS_LATER_UPDATE_TICKS,
    NODE_FACTS_COUNT_ROWS_LATER_WRITE_TICKS,
    NODE_FACTS_COUNT_ROWS_LATER_PART_TICKS,
    // The rows' values: the records each of the values' lists has (BUSTER_NODE_ROWS_LISTS of them, in their order),
    // the expressions whose calls' rows, labels and temporaries are numbered, the tapes that differ from the
    // reference's by a value alone, and the values' sweep's ticks, a chunk's first run's and its second's.
    NODE_FACTS_COUNT_ROWS_LIST,
    NODE_FACTS_COUNT_ROWS_LIST_LAST = NODE_FACTS_COUNT_ROWS_LIST + 6,
    NODE_FACTS_COUNT_ROWS_NUMBERED,
    NODE_FACTS_COUNT_ROWS_NUMBER_STEPS,
    NODE_FACTS_COUNT_ROWS_VALUE_DIFFERENCES,
    NODE_FACTS_COUNT_ROWS_VALUE_TICKS,
    NODE_FACTS_COUNT_ROWS_LATER_VALUE_TICKS,
    NODE_FACTS_COUNTS,
} NodeFactsCount;

// Why the front's records don't give an expression's inputs (node_facts_front_expression): its records are not all the
// chunk's own (a chunk's end cut it); an operand is out of a byte's reach; a literal the reader doesn't decode again; a
// call of 255 operands or more. The front's own window doesn't give one for the first reason alone: its records are
// not there in a row (a chunk's end had no room to keep them whole).
#define BUSTER_NODE_FACTS_WHY_GIVEN 0u
#define BUSTER_NODE_FACTS_WHY_CUT 1u
#define BUSTER_NODE_FACTS_WHY_REACH 2u
#define BUSTER_NODE_FACTS_WHY_LITERAL 3u
#define BUSTER_NODE_FACTS_WHY_CALL 4u
#define BUSTER_NODE_FACTS_WHYS 5u

// What a listed difference of the kernel's at reader time is of: a record over poison, a record over decoys, a window's
// lists, a record's inputs as the front's records give them, a record's inputs as the front wrote them, or a record
// the kernel made of the front's window in place.
#define BUSTER_NODE_FACTS_DIFFERENCE_RECORD 0u
#define BUSTER_NODE_FACTS_DIFFERENCE_DECOY 1u
#define BUSTER_NODE_FACTS_DIFFERENCE_LISTS 2u
#define BUSTER_NODE_FACTS_DIFFERENCE_FRONT 3u
#define BUSTER_NODE_FACTS_DIFFERENCE_WINDOW 4u
#define BUSTER_NODE_FACTS_DIFFERENCE_PLACE 5u

// What T made, at reader time, of the full expressions the front made whole (BUSTER_NODE_FRONT_TYPES;
// c_check_node_machine_reader.c), in the output's arena: by each expression's root's graph node, for the shadow's
// comparison with its own T after the pipeline.
typedef struct NodeFrontTypes NodeFrontTypes;
struct NodeFrontTypes
{
    // By root node: a digest of T's records (node_machine_digest), the expression's first node and its records (0: T
    // wasn't run on it), and T's refusal (BUSTER_NODE_FRONT_TYPED_REFUSAL) with, after the pipeline, whether the
    // lowering popped the root (_POPPED) and whether the shadow compared it (_COMPARED).
    uint64_t* digests;
    uint32_t* firsts;
    uint32_t* counts;
    uint8_t* refusals;
    uint32_t node_capacity;
    // The expressions T ran on, their records, the records it typed (a refused expression's up to the one that refused),
    // those T refused, by refusal, and its ticks: a chunk's expressions a run, which the digests' second run is no part
    // of.
    uint64_t expressions;
    uint64_t records;
    uint64_t typed;
    uint64_t refused[16];
    uint64_t ticks;
    // The ticks of making the machine's state from the reader's (the types' words, the objects' declarations), and the
    // types whose words T's own derivations added.
    uint64_t setup_ticks;
    uint64_t late_types;
    // The roots whose expression T wasn't run on: an operand's record out of reach (the lean front's, further than a
    // byte), or a node the comparison didn't agree on.
    uint64_t skipped;
    // The front's roots that are a declaration's discard (the walk's cast to void of an initialization's node at the
    // declaration's `;`), which nothing evaluates: T isn't run on them.
    uint64_t discards;
    // What the machine made of the reader's columns in place of the program's (NodeMachine.reader_declarations and
    // reader_facts, `object_count` of each), and by call node its answers to "a library builtin's semantics of its
    // own?" (NodeMachine.reader_library): the shadow compares each with the program's after the pipeline.
    uint32_t* object_declarations;
    uint8_t* object_facts;
    uint32_t object_count;
    uint8_t* library;
    // What stands for each object's symbol at reader time (NodeMachine.object_symbols there): itself for an object with
    // a static declaration, 0 for a local. The shadow compares "has a symbol" with the program's.
    uint32_t* object_symbols;
    // C and X at reader time (BUSTER_NODE_FRONT_SWEEPS; null otherwise), by root node, for a root T took and they ran on
    // (BUSTER_NODE_FRONT_TYPED_SWEPT in `refusals`): their refusal; what the root's consumer gives it as the reader
    // derives it, its conversion's target and, an initialization's, its object (it has no context); its tape's first
    // entry in the store and its entries (UINT32_MAX for a tape the store had no room for); and a digest of its labels'
    // entries, its calls' records and its temporaries (node_machine_side_digest).
    uint8_t* sweep_refusals;
    TypeId* targets;
    uint32_t* objects;
    uint32_t* tape_firsts;
    uint32_t* tape_counts;
    uint64_t* side_digests;
    // The store: the kept tapes' entries one after another, `tape_count` of `tape_capacity`: X's kinds, classes and
    // values. A global's value is its object there, which stands for its symbol: what a splice would patch.
    uint8_t* tape_kinds;
    uint8_t* tape_classes;
    uint64_t* tape_values;
    uint64_t tape_count;
    uint64_t tape_capacity;
    // The expressions C and X ran on (those T took), by their refusal; their entries; and the tapes the store had no
    // room for. With them `ticks` is of T, C and X together.
    uint64_t swept;
    uint64_t swept_refused[16];
    uint64_t swept_entries;
    uint64_t swept_unkept;
    // The census's tests at reader time (BUSTER_NODE_READER_TESTS; null otherwise), by root node, for a root T took
    // (BUSTER_NODE_FRONT_TYPED_TESTED in `refusals`): the tests that refuse its expression, as bits of NodeCensusTest;
    // its root's kind as the reader derives it from the front's root (a NodeCensusRoot); and the declared type of the
    // object it initializes (0 for none). C and X then run on the expressions no test refuses. `policy` is the unit's
    // enabled warnings as reader time read them (a NodeCensusPolicy), which the shadow compares with the pipeline's.
    uint64_t* tests;
    uint8_t* kinds;
    TypeId* declared;
    const void* policy;
    // The expressions the tests ran on and those they refused; by test, the expressions it refuses (`tested_any`) and
    // those it refuses first in the tests' order (`tested_first`); and the nodes a test asked of T's records that the
    // expression's range doesn't hold, which must be none.
    uint64_t tested;
    uint64_t tested_refused;
    uint64_t tested_any[64];
    uint64_t tested_first[64];
    uint64_t tested_missed;
    // BUSTER_NODE_READER_VERIFY's: the nodes a row or a rule typed there whose value isn't the action's, and those whose
    // action gives a warning.
    uint64_t twin_differences;
    uint64_t twin_warnings;
    // BUSTER_NODE_READER_WINDOW_TESTS's: the expressions and records the tests' own form ran on; the expressions that
    // had a record sent to the census's code and those records, by what sent them (a NodeTestsTrigger); the expressions
    // walked for them; and the walks that reached another count of nodes than T's records.
    uint64_t own_expressions;
    uint64_t own_records;
    uint64_t own_visited;
    uint64_t own_visits;
    uint64_t own_walks;
    uint64_t own_triggers[32];
    // The records its loop listed: those whose token's parent the tree's tests read, and the operators with tests of
    // their own; and of the last, the calls, those with a constant operand, and those with a third operand.
    uint64_t own_trees;
    uint64_t own_operators;
    uint64_t own_calls;
    uint64_t own_constants;
    uint64_t own_thirds;
    uint64_t own_count_differences;
    // The operators the loop left out of its list for a constant operand whose tests have nothing to say
    // (BUSTER_NODE_TESTS_QUIET_*), and, with the reader's verify, those of them whose tests had something.
    uint64_t own_quiet;
    uint64_t own_quiet_checked;
    uint64_t own_quiet_wrong;
    // The calls the loop left out of its list for having no test to make (node_tests_window_loop's rule); and, with the
    // reader's verify, those of them checked, those whose test had something, and those with a loud argument by their
    // own list (node_tests_call_loud). And the asks (node_tests_window_asks): the casts in the loop's list, and the
    // callees whose declaration was asked whether it wants its result, once a declaration's line.
    uint64_t own_quiet_calls;
    uint64_t own_quiet_calls_checked;
    uint64_t own_quiet_calls_wrong;
    uint64_t own_quiet_calls_loud;
    uint64_t own_result_casts;
    uint64_t own_result_asks;
    // The initializers' roots asked of the census's code for a null pointer constant that is no literal (#3012): an
    // integer constant without the literal's flag whose object's type is a pointer's.
    uint64_t own_root_nulls;
    // The casts the loop left out of its list for having no test to make (a pointer's to void or to a pointer); and,
    // with the reader's verify, those of them checked and those whose test had something.
    uint64_t own_quiet_casts;
    uint64_t own_quiet_casts_checked;
    uint64_t own_quiet_casts_wrong;
    // With the reader's verify: the integer constants whose record says its value (node_facts_small), and those of them
    // whose payload is another.
    uint64_t own_values_checked;
    uint64_t own_values_wrong;
    // With the reader's verify: the records whose token's parent the tree's tests read (a comparison's, `!`, `=`) that
    // no operator listed for them has as an operand, and those of them whose test had something; and the words of an
    // expression's lists with a bit past its records.
    uint64_t own_tree_checked;
    uint64_t own_tree_wrong;
    uint64_t own_lists_past;
    // The expressions with nothing for the tests that read T's records, whose verdict is the window's alone; and, with
    // the reader's verify, those of them run through those tests all the same, and those that then had another verdict.
    uint64_t own_direct;
    uint64_t own_direct_checked;
    uint64_t own_direct_wrong;
    // The fact words' kernel at reader time (BUSTER_NODE_READER_FACTS; c_check_node_machine_facts.c's
    // node_facts_reader_flush): its counts (NodeFactsCount); the records it is to make and those it left to T, by
    // operator; and the first differences, each by its record's token, its operator and what differed
    // (BUSTER_NODE_FACTS_DIFFERENCE_*).
    uint64_t facts_counts[NODE_FACTS_COUNTS];
    uint64_t facts_wanted_ops[CHECK_OP_COUNT];
    uint64_t facts_left_ops[CHECK_OP_COUNT];
    uint32_t facts_difference_tokens[BUSTER_NODE_FRONT_LISTED];
    uint8_t facts_difference_ops[BUSTER_NODE_FRONT_LISTED];
    uint8_t facts_difference_kinds[BUSTER_NODE_FRONT_LISTED];
    uint32_t facts_listed;
    // The expressions the front's records don't give, by why (BUSTER_NODE_FACTS_WHY_*), and the first, each by its
    // root's token and why.
    uint64_t facts_skipped_whys[BUSTER_NODE_FACTS_WHYS];
    uint32_t facts_skipped_tokens[BUSTER_NODE_FRONT_LISTED];
    uint8_t facts_skipped_reasons[BUSTER_NODE_FRONT_LISTED];
    uint32_t facts_skipped_listed;
    // The same of the expressions the front's own window doesn't give (BUSTER_NODE_READER_WINDOW).
    uint64_t facts_window_whys[BUSTER_NODE_FACTS_WHYS];
    uint32_t facts_window_tokens[BUSTER_NODE_FRONT_LISTED];
    uint8_t facts_window_reasons[BUSTER_NODE_FRONT_LISTED];
    uint32_t facts_window_listed;
    uint32_t own_listed;
    // C on the window (BUSTER_NODE_READER_WINDOW_CONTEXTS): the listed expressions by their first record's reason
    // (BUSTER_NODE_CONTEXTS_REASON_*), and the first decided expressions that differ from the reference, each by its
    // root's token.
    uint64_t contexts_listed[BUSTER_NODE_CONTEXTS_REASONS];
    uint32_t contexts_tokens[BUSTER_NODE_FRONT_LISTED];
    uint32_t contexts_shown;
    // X's rows on the window (BUSTER_NODE_READER_WINDOW_ROWS): the first made tapes that differ from the reference's,
    // each by its expression's root's token.
    uint32_t rows_tokens[BUSTER_NODE_FRONT_LISTED];
    uint32_t rows_shown;
};

#define BUSTER_NODE_FRONT_TYPED_REFUSAL 0x0fu
#define BUSTER_NODE_FRONT_TYPED_SWEPT 0x10u
#define BUSTER_NODE_FRONT_TYPED_TESTED 0x20u
#define BUSTER_NODE_FRONT_TYPED_POPPED 0x40u
#define BUSTER_NODE_FRONT_TYPED_COMPARED 0x80u

// What the shadow reports of the front (CheckInput.node_front), in the output's arena.
typedef struct NodeFrontShadow NodeFrontShadow;
struct NodeFrontShadow
{
    // The TSC's ticks of the front's own pass over the chunks (the comparison is outside them), the acting tokens it
    // read and the records it made.
    uint64_t ticks;
    uint64_t tokens;
    uint64_t records;
    // The records compared with the walk's nodes, and those that differ: a difference is a failure.
    uint64_t checked;
    uint64_t differences;
    // The acting tokens the front read, by class (counted outside its ticks), the tokens left, by class, and the times a
    // full expression outgrew the front's arrays.
    uint64_t acting[BUSTER_CHECK_EXPRESSION_CODES];
    uint64_t left[BUSTER_CHECK_EXPRESSION_CODES];
    // Of the tokens left, the sizeof and _Alignof ones.
    uint64_t left_queries;
    uint64_t resets;
    // The values still waiting at the unit's end: none, where every value's consumer took it.
    uint64_t waiting;
    // The first records that differed: each one's token and field (BUSTER_NODE_FRONT_FIELD_*).
    uint32_t difference_tokens[BUSTER_NODE_FRONT_LISTED];
    uint32_t difference_fields[BUSTER_NODE_FRONT_LISTED];
    // A bit a graph node: the nodes whose records agreed.
    uint64_t* nodes;
    uint32_t node_capacity;
    // The operands of the records that are no full expression's root (a discard, a condition, a return's conversion, an
    // initialization, whose operand is the expression's value, a body away for a loop's or a switch's condition), by their
    // distance back: within BUSTER_NODE_FRONT_NEAR, further but in the byte, and BUSTER_NODE_FRONT_FAR or more. The
    // roots' operands, and those of them further than BUSTER_NODE_FRONT_NEAR. And the operands whose distance isn't the
    // difference of the walk's two nodes, which must be none where the front made every node between them.
    uint64_t operands;
    uint64_t operands_near;
    uint64_t operands_byte;
    uint64_t operands_far;
    uint64_t root_operands;
    uint64_t root_operands_past;
    // The calls' arguments, and those further back than BUSTER_NODE_FRONT_NEAR from their call.
    uint64_t arguments;
    uint64_t arguments_past;
    // The records with more operands than BUSTER_NODE_FRONT_COUNT_MOST, and the operands at a place past
    // BUSTER_NODE_FRONT_PLACE_MOST (a call's fourth argument and later): what #2850's byte columns cap.
    uint64_t counts_past;
    uint64_t places_past;
    // The walk's preparation of a chunk by its parts, in ticks, where the front ran: the acting tokens' compaction, the
    // identifiers' entries and the literals'. The lean front reads the first and the last.
    uint64_t compact_ticks;
    uint64_t identifier_ticks;
    uint64_t literal_ticks;
    // A bit a graph node: the nodes with an operand further back than BUSTER_NODE_FRONT_NEAR, no root's.
    uint64_t* past;
    // A byte a graph node: the context the front gives the node's record, compared once the walk has acted at every
    // token (a list gives its values theirs when the walk reaches it), or BUSTER_NODE_FRONT_CONTEXT_UNKNOWN for a record
    // whose value a left token took.
    uint8_t* contexts;
    // T's results at reader time, or null (BUSTER_NODE_FRONT_TYPES).
    NodeFrontTypes* types;
    // The flat front (BUSTER_NODE_FRONT_FLAT, c_check_node_machine_flat.c). Its passes' ticks after the compaction, its
    // own compaction's, and each stage's (BUSTER_NODE_FLAT_STAGE_*); the tokens it visited, the records it made and
    // the places it left empty, the children its tokens hopped over, the tokens with a child past the rings' reach, and
    // the tokens it left.
    uint64_t flat_ticks;
    uint64_t flat_compact_ticks;
    uint64_t flat_stage_ticks[8];
    uint64_t flat_tokens;
    uint64_t flat_records;
    uint64_t flat_empties;
    uint64_t flat_hops;
    uint64_t flat_beyond;
    uint64_t flat_left;
    // Its comparison with the lean front: the records both made, those that differ (a failure), the records only one of
    // them made, the flat records a chunk made past its ring's room, which nothing compared, and the distances the
    // flat front's byte doesn't hold where the lean front's does (the empty records between).
    uint64_t flat_compared;
    uint64_t flat_differences;
    uint64_t flat_only;
    uint64_t flat_lean_only;
    uint64_t flat_unread;
    uint64_t flat_far;
    // The first records that differed: each one's token and field (BUSTER_NODE_FLAT_FIELD_*).
    uint32_t flat_difference_tokens[BUSTER_NODE_FRONT_LISTED];
    uint32_t flat_difference_fields[BUSTER_NODE_FRONT_LISTED];
    // Where the front writes the window (BUSTER_NODE_READER_WINDOW): the same lean front without the window run on the
    // same chunks beside it (NodeFront.plain), whose ticks are what `ticks` rose from, with the records it made and the
    // chunks in which the two made other records or left other values waiting (a failure). And of the chunks' ends:
    // the waiting values whose subtrees were kept whole, the records kept for them, and the values kept as their place
    // alone, which are cut.
    uint64_t plain_ticks;
    uint64_t plain_records;
    uint64_t plain_differences;
    uint64_t window_kept_values;
    uint64_t window_kept_records;
    uint64_t window_cut_values;
    // The most the calls' arguments' list held at a chunk's end (NodeFront.window_arguments, which has room for
    // BUSTER_NODE_FRONT_RECORDS: more is a failure).
    uint64_t window_arguments_most;
    // The names' pass over each chunk's records, after the front's own (node_facts_window_names): its ticks, the
    // names' leaves it gave their lines, the declarations' lines it made, each at its declaration's first use, with
    // their ticks, which are part of the pass's; and the leaves whose kind or line its scalar path gave otherwise (a
    // failure).
    uint64_t window_name_ticks;
    uint64_t window_names;
    uint64_t window_lines;
    uint64_t window_line_ticks;
    uint64_t window_name_differences;
};

#define BUSTER_NODE_FRONT_CONTEXT_UNKNOWN 0xffu

#define BUSTER_NODE_FRONT_FIELD_NODE 1u
#define BUSTER_NODE_FRONT_FIELD_OPERATOR 2u
#define BUSTER_NODE_FRONT_FIELD_OPERAND 3u
#define BUSTER_NODE_FRONT_FIELD_LEAF 4u
#define BUSTER_NODE_FRONT_FIELD_MEMBER 5u
#define BUSTER_NODE_FRONT_FIELD_ARGUMENTS 6u
#define BUSTER_NODE_FRONT_FIELD_TYPE 7u
#define BUSTER_NODE_FRONT_FIELD_CONTEXT 8u
#define BUSTER_NODE_FRONT_FIELD_DISTANCE 9u
#define BUSTER_NODE_FRONT_FIELD_STRUCTURE 10u

// The front's arrays are one block, each a multiple of 4 KB long and BUSTER_NODE_FRONT_STAGGER bytes more: a record's
// fields, one in each array at the record's index, would otherwise all lie 4 KB apart, in one set of the first-level
// cache with more lines than it has ways, as the machine's columns would (BUSTER_NODE_MACHINE_STAGGER).
#define BUSTER_NODE_FRONT_STAGGER 128u
typedef struct NodeFront NodeFront;
struct NodeFront
{
    NodeFrontShadow* shadow;
    // The records made, the first the comparison hasn't read (the chunk's first until it has), the chunk's first (after
    // the comparison too, for T at reader time), the stack's entries, the calls' arguments and the wide leaves so far.
    uint32_t count;
    uint32_t pending;
    uint32_t made;
    uint32_t stack_count;
    uint32_t argument_count;
    uint32_t wide_count;
    // The entries the last token took off the stack (node_front_values): their place and how many.
    uint32_t taken_first;
    uint32_t taken_count;
    // The records made in the unit so far: a record's place in the unit's postorder (`ordinals`), which a chunk's
    // renumbering of the kept records doesn't change.
    uint32_t ordinal;
    // A record's columns. `forms` is the walk's CheckInputNodeForm, which says where its requested type and its context
    // come from; `types` the type its token's query asked for (a return's, the function's), `contexts` the graph's
    // context bits; `wides` a leaf's place among the wide values plus one, or 0.
    uint8_t ops[BUSTER_NODE_FRONT_RECORDS];
    uint8_t ops_stagger[BUSTER_NODE_FRONT_STAGGER];
    uint8_t forms[BUSTER_NODE_FRONT_RECORDS];
    uint8_t forms_stagger[BUSTER_NODE_FRONT_STAGGER];
    uint8_t contexts[BUSTER_NODE_FRONT_RECORDS];
    uint8_t contexts_stagger[BUSTER_NODE_FRONT_STAGGER];
    // Whether a left token took the record's value: the walk's visit may then give its node a context (a constant
    // expression's, an assertion's), which the front doesn't know.
    uint8_t taken_by_left[BUSTER_NODE_FRONT_RECORDS];
    uint8_t taken_by_left_stagger[BUSTER_NODE_FRONT_STAGGER];
    uint32_t tokens[BUSTER_NODE_FRONT_RECORDS];
    uint8_t tokens_stagger[BUSTER_NODE_FRONT_STAGGER];
    // Its operands: as records, which the comparison follows, and as the machine would read them, each one's distance back
    // in the postorder in a byte (0 for none, BUSTER_NODE_FRONT_FAR for that or more). `roots`: a full expression's root.
    uint32_t operands[3][BUSTER_NODE_FRONT_RECORDS + BUSTER_NODE_FRONT_STAGGER / sizeof(uint32_t)];
    uint8_t distances[3][BUSTER_NODE_FRONT_RECORDS + BUSTER_NODE_FRONT_STAGGER];
    uint8_t roots[BUSTER_NODE_FRONT_RECORDS];
    uint8_t roots_stagger[BUSTER_NODE_FRONT_STAGGER];
    // The other structural bytes of #2850's layout: a record's operand count (a call's callee and arguments; 255 for
    // that or more), and, written when its consumer's record is made, the consumer's distance forward (0 for none: a
    // root, a value a left token took or one nothing takes; BUSTER_NODE_FRONT_FAR for that or more) and its place among
    // the consumer's operands (a call's callee 0 and its arguments from 1; 255 for that or more).
    uint8_t counts[BUSTER_NODE_FRONT_RECORDS];
    uint8_t counts_stagger[BUSTER_NODE_FRONT_STAGGER];
    uint8_t ups[BUSTER_NODE_FRONT_RECORDS];
    uint8_t ups_stagger[BUSTER_NODE_FRONT_STAGGER];
    uint8_t places[BUSTER_NODE_FRONT_RECORDS];
    uint8_t places_stagger[BUSTER_NODE_FRONT_STAGGER];
    uint32_t ordinals[BUSTER_NODE_FRONT_RECORDS];
    uint8_t ordinals_stagger[BUSTER_NODE_FRONT_STAGGER];
    // The token of the name a record is, or is the address of (a call's library mark reads its callee's), or
    // BUSTER_CHECK_NONE.
    uint32_t uses[BUSTER_NODE_FRONT_RECORDS];
    uint8_t uses_stagger[BUSTER_NODE_FRONT_STAGGER];
    uint32_t members[BUSTER_NODE_FRONT_RECORDS];
    uint8_t members_stagger[BUSTER_NODE_FRONT_STAGGER];
    uint32_t argument_firsts[BUSTER_NODE_FRONT_RECORDS];
    uint8_t argument_firsts_stagger[BUSTER_NODE_FRONT_STAGGER];
    uint32_t argument_counts[BUSTER_NODE_FRONT_RECORDS];
    uint8_t argument_counts_stagger[BUSTER_NODE_FRONT_STAGGER];
    TypeId types[BUSTER_NODE_FRONT_RECORDS];
    uint8_t types_stagger[BUSTER_NODE_FRONT_STAGGER];
    // A leaf's value as a graph leaf's record holds it.
    TypeId leaf_types[BUSTER_NODE_FRONT_RECORDS];
    uint8_t leaf_types_stagger[BUSTER_NODE_FRONT_STAGGER];
    uint32_t leaf_flags[BUSTER_NODE_FRONT_RECORDS];
    uint8_t leaf_flags_stagger[BUSTER_NODE_FRONT_STAGGER];
    uint32_t leaf_objects[BUSTER_NODE_FRONT_RECORDS];
    uint8_t leaf_objects_stagger[BUSTER_NODE_FRONT_STAGGER];
    uint32_t leaf_heads[BUSTER_NODE_FRONT_RECORDS];
    uint8_t leaf_heads_stagger[BUSTER_NODE_FRONT_STAGGER];
    uint64_t leaf_integers[BUSTER_NODE_FRONT_RECORDS];
    uint8_t leaf_integers_stagger[BUSTER_NODE_FRONT_STAGGER];
    uint8_t leaf_categories[BUSTER_NODE_FRONT_RECORDS];
    uint8_t leaf_categories_stagger[BUSTER_NODE_FRONT_STAGGER];
    uint8_t leaf_constants[BUSTER_NODE_FRONT_RECORDS];
    uint8_t leaf_constants_stagger[BUSTER_NODE_FRONT_STAGGER];
    uint16_t wides[BUSTER_NODE_FRONT_RECORDS];
    uint8_t wides_stagger[BUSTER_NODE_FRONT_STAGGER];
    // The comparison's own: whether a compared consumer found the record at its place and distance (a consumer's
    // distance no consumer vouches for is a difference).
    uint8_t vouched[BUSTER_NODE_FRONT_RECORDS];
    uint8_t vouched_stagger[BUSTER_NODE_FRONT_STAGGER];
    // The walk's node of a record, once compared (BUSTER_CHECK_NONE for one that differed).
    uint32_t nodes[BUSTER_NODE_FRONT_RECORDS];
    uint8_t nodes_stagger[BUSTER_NODE_FRONT_STAGGER];
    // The values not yet taken, each one's record (or BUSTER_NODE_FRONT_UNKNOWN), in groups of one consumer: a group's
    // consumer's token and its first value's place. Two groups next to each other never share a consumer.
    uint32_t stack[BUSTER_NODE_FRONT_RECORDS];
    uint8_t stack_stagger[BUSTER_NODE_FRONT_STAGGER];
    uint32_t group_consumers[BUSTER_NODE_FRONT_RECORDS];
    uint8_t group_consumers_stagger[BUSTER_NODE_FRONT_STAGGER];
    uint32_t group_firsts[BUSTER_NODE_FRONT_RECORDS];
    uint8_t group_firsts_stagger[BUSTER_NODE_FRONT_STAGGER];
    uint32_t group_count;
    // Whether the stack is full and its entries forgotten already (node_front_give).
    bool full;
    // The lean front (BUSTER_NODE_FRONT_LEAN): its records have the structural bytes, the token and a leaf's payload
    // (`leaf_integers`: an identifier's declaration, a literal's integer, a string's object; `leaf_types`: a literal's
    // or a string's type; `leaf_categories`: BUSTER_NODE_FRONT_LEAF_*), and none of the other columns.
    bool lean;
    uint32_t arguments[BUSTER_NODE_FRONT_RECORDS];
    uint8_t arguments_stagger[BUSTER_NODE_FRONT_STAGGER];
    // The records of each record's subtree, itself among them: a root's operand's are its expression's, one range of the
    // walk's nodes where the expression is whole, which T at reader time reads (BUSTER_NODE_FRONT_TYPES).
    uint32_t sizes[BUSTER_NODE_FRONT_RECORDS];
    uint8_t sizes_stagger[BUSTER_NODE_FRONT_STAGGER];
    // #2850's window, where the lean front writes it (BUSTER_NODE_READER_WINDOW): what the fact words' kernel reads of a
    // record beside the words, written as the record is made (node_facts_window_leaf and its kin, in
    // c_check_node_machine_facts.c, whose NodeFacts' columns of the same names these are; its operator is `ops`). The
    // kernel reads them here, where they are written.
    // - `window_kinds`: its kind.
    // - `window_classes`: its class, made from its operands' classes, which are made before it. Or
    //   BUSTER_NODE_FACTS_CLASS_CUT: its subtree's records are not all in these arrays in a row before it (a chunk's end
    //   had no room to keep them), and neither are its consumers': the kernel makes none of them.
    // - `window_left` and `window_right`: its first two operands' distances back, whole. A root's operand is wherever
    //   its value waited.
    // - `window_ups`: its consumer's distance forward among these arrays' records, written when the consumer's record
    //   is made, beside `ups` (node_front_add), and by nothing before: it is read where `ups` says the record has a
    //   consumer. `ups` is the same distance in the unit's postorder, which is another where a chunk's end took records
    //   from between the two (a control's root, apart from its operand).
    // - `window_entries`: a leaf's line's number (a literal's or a string's own, the batch's line of its record's
    //   number, made as the leaf is; a name's declaration until the names' pass over the chunk gives it its
    //   declaration's line's), a member's name, a cast's or a return's type, a conditional's third operand's distance,
    //   a call's first argument's place in `window_arguments`.
    // - `window_counts`: a call's arguments (no other record's is read).
    // - `window_positions`: its source position.
    // - `window_arguments`: the calls' arguments' distances back, a call's in a row, `window_argument_count` of them.
    // `window_op_kinds` and `window_op_codes` are an operator's kind and code by its op, the kernel's
    // (node_facts_window_begin).
    bool window;
    // The kernel's words there, whose lines the leaves' writer and the names' pass make; and the lines' numbers by
    // declaration (NodeFacts.numbers), which a name's leaf's writer asks the cache for.
    struct NodeFacts* window_facts;
    const uint32_t* window_numbers;
    uint32_t window_argument_count;
    uint8_t window_op_kinds[256];
    uint8_t window_op_codes[256];
    uint8_t window_kinds[BUSTER_NODE_FRONT_RECORDS];
    uint8_t window_kinds_stagger[BUSTER_NODE_FRONT_STAGGER];
    uint8_t window_classes[BUSTER_NODE_FRONT_RECORDS];
    uint8_t window_classes_stagger[BUSTER_NODE_FRONT_STAGGER];
    uint16_t window_left[BUSTER_NODE_FRONT_RECORDS];
    uint8_t window_left_stagger[BUSTER_NODE_FRONT_STAGGER];
    uint16_t window_right[BUSTER_NODE_FRONT_RECORDS];
    uint8_t window_right_stagger[BUSTER_NODE_FRONT_STAGGER];
    uint16_t window_ups[BUSTER_NODE_FRONT_RECORDS];
    uint8_t window_ups_stagger[BUSTER_NODE_FRONT_STAGGER];
    uint16_t window_counts[BUSTER_NODE_FRONT_RECORDS];
    uint8_t window_counts_stagger[BUSTER_NODE_FRONT_STAGGER];
    uint16_t window_arguments[BUSTER_NODE_FRONT_RECORDS];
    uint8_t window_arguments_stagger[BUSTER_NODE_FRONT_STAGGER];
    uint32_t window_entries[BUSTER_NODE_FRONT_RECORDS];
    uint8_t window_entries_stagger[BUSTER_NODE_FRONT_STAGGER];
    uint32_t window_positions[BUSTER_NODE_FRONT_RECORDS];
    uint8_t window_positions_stagger[BUSTER_NODE_FRONT_STAGGER];
    // The same front without the window, run on the same chunks for its ticks (NodeFrontShadow.plain_ticks), or null;
    // and the chunks read, by whose parity one of the two runs first.
    struct NodeFront* plain;
    uint32_t chunks;
    // The machine's state at reader time (c_check_node_machine_reader.c), or null.
    struct NodeMachineReader* typer;
    // The flat front's state (c_check_node_machine_flat.c), or null.
    struct NodeFlat* flat;
    CheckValue wide_values[BUSTER_NODE_FRONT_WIDES];
};

// A record's class where its subtree's records are not all in the front's arrays in a row before it
// (NodeFront.window_classes): no made operator's class (BUSTER_NODE_FACTS_ROUNDS rounds and a code under 15), and not
// BUSTER_NODE_FACTS_NO_CLASS.
#define BUSTER_NODE_FACTS_CLASS_CUT 0xfeu

// The window's writers (c_check_node_machine_facts.c, which follows this file in the checker's unit, the only one with
// the kernel), each called right after the lean front made the record:
// - a leaf's, of `category` (BUSTER_NODE_FRONT_LEAF_*): a name's with its declaration, a literal's with the reader's
//   decoding of it where the front's columns don't hold its value, a string's;
// - an operator's with its operands' records (BUSTER_NODE_FRONT_UNKNOWN for none; `entry`: a member's name, a cast's or
//   a return's type);
// - a call's with its callee's and its arguments' (`values`, `count` of them).
// And what begins them for a unit (whether the front writes the window, the operators' kinds and codes, the front run
// beside it); the names' pass over a chunk's records, after the front's own, which gives each name's leaf its
// declaration's line; and a record kept at a chunk's end, at `to` from `from`, whose literal's line and whose call's
// arguments (`arguments`: the kept calls' so far) go with it. Each call of one is under this guard too: the other units have the front without the window.
#if BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER)
static inline __attribute__((always_inline)) void node_facts_window_leaf(NodeFront* front, CheckInputReader* reader, uint32_t record, uint32_t token, uint32_t category,
                                                                         uint32_t declaration, const CheckLiteral* decoded);
static inline __attribute__((always_inline)) void node_facts_window_operator(NodeFront* front, const CheckInputReader* reader, uint32_t record, CheckOp op, uint32_t token,
                                                                             uint32_t a, uint32_t b, uint32_t c, uint32_t entry);
static inline __attribute__((always_inline)) void node_facts_window_call(NodeFront* front, const CheckInputReader* reader, uint32_t record, uint32_t token,
                                                                         const uint32_t* values, uint32_t count);
static void node_facts_window_begin(NodeFront* front, CheckInputReader* reader);
static void node_facts_window_names(NodeFront* front);
static inline void node_facts_window_kept(NodeFront* front, uint32_t to, uint32_t from, uint32_t* arguments);
#endif

// T at reader time (c_check_node_machine_reader.c): the machine is the checker unit's alone, so the reader's other
// consumers (the complete driver's) have the front without it.
#if BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER)
static struct NodeMachineReader* node_machine_reader_begin(CheckInputReader* reader, NodeFrontShadow* shadow, bool sweeps);
static void node_machine_reader_chunk(struct NodeMachineReader* typer, const NodeFront* front, const CheckInputReader* reader);
static void node_machine_reader_finish(struct NodeMachineReader* typer);
#endif

// Whether the walk acts at `token` so as to read its children's values: a token it never acts at, or whose action is
// nothing, takes none, and a value given to it is no entry.
static inline bool node_front_consumes(const CheckInputReader* reader, uint32_t token)
{
    const uint8_t visit = token < reader->parser->tokens.count ? reader->visits[token] : 0;
    return (visit & BUSTER_CHECK_EXPRESSION_ACTS) != 0 && (visit & BUSTER_CHECK_EXPRESSION_CODE_MASK) >> BUSTER_CHECK_EXPRESSION_CODE_SHIFT != CHECK_INPUT_CODE_DROP;
}

// The front no longer knows the record's node's context: a left token took its value, or the stack lost it.
static inline void node_front_context_unknown(NodeFront* front, uint32_t record)
{
    NodeFrontShadow* shadow = front->shadow;
    const uint32_t node = front->nodes[record];
    front->taken_by_left[record] = 1;
    if (node < shadow->node_capacity)
    {
        shadow->contexts[node] = BUSTER_NODE_FRONT_CONTEXT_UNKNOWN;
    }
}

// Every waiting value made unknown: the stack has no room for one more (a static table's thousands of elements wait
// for their list's end), or the arrays none for the records they name. Their consumers are left in turn.
static __attribute__((noinline)) void node_front_forget(NodeFront* front)
{
    for (uint32_t i = 0; i < front->stack_count; i += 1)
    {
        const uint32_t record = front->stack[i];
        if (record != BUSTER_NODE_FRONT_UNKNOWN)
        {
            node_front_context_unknown(front, record);
        }
        front->stack[i] = BUSTER_NODE_FRONT_UNKNOWN;
    }
    front->shadow->resets += 1;
}

// Gives `token`'s value, `record` (or BUSTER_NODE_FRONT_UNKNOWN), to its parent: an entry of the top group where that is
// the parent's already, else of a new one. With no room for it, the value is lost and every waiting one unknown.
static inline void node_front_give(NodeFront* front, const CheckInputReader* reader, uint32_t token, uint32_t record)
{
    const uint32_t consumer = reader->parser->tree.parents[token];
    const bool consumed = node_front_consumes(reader, consumer);
    if (consumed && front->stack_count == BUSTER_NODE_FRONT_RECORDS)
    {
        // Once for a full stack: its entries stay unknown while it is full.
        if (!front->full)
        {
            node_front_forget(front);
            front->full = true;
        }
        if (record != BUSTER_NODE_FRONT_UNKNOWN)
        {
            node_front_context_unknown(front, record);
        }
    }
    else if (consumed)
    {
        if (front->group_count == 0 || front->group_consumers[front->group_count - 1] != consumer)
        {
            front->group_consumers[front->group_count] = consumer;
            front->group_firsts[front->group_count] = front->stack_count;
            front->group_count += 1;
        }
        front->stack[front->stack_count] = record;
        front->stack_count += 1;
        front->full = false;
    }
}

// A token that passes its children's values on (a list, a separator, a group's closing, a braced initializer's
// assignment): its group becomes its parent's, joined to the group below where that is the parent's too, or is dropped
// where the parent takes no values. A static braced initializer's list gives each value's record the value context, as
// the walk's list does when it reaches the token.
static inline void node_front_pass(NodeFront* front, CheckInputReader* reader, uint32_t token)
{
    if (front->group_count != 0 && front->group_consumers[front->group_count - 1] == token)
    {
        const uint32_t top = front->group_count - 1;
        const uint32_t first = front->group_firsts[top];
        if (BUSTER_CHECK_RARE(reader, static_initializers, token))
        {
            NodeFrontShadow* shadow = front->shadow;
            for (uint32_t i = first; i < front->stack_count; i += 1)
            {
                const uint32_t record = front->stack[i];
                if (record != BUSTER_NODE_FRONT_UNKNOWN)
                {
                    const uint32_t node = front->nodes[record];
                    front->contexts[record] |= BUSTER_CHECK_CONTEXT_VALUE;
                    if (node < shadow->node_capacity && shadow->contexts[node] != BUSTER_NODE_FRONT_CONTEXT_UNKNOWN)
                    {
                        shadow->contexts[node] |= BUSTER_CHECK_CONTEXT_VALUE;
                    }
                }
            }
        }
        const uint32_t consumer = reader->parser->tree.parents[token];
        if (!node_front_consumes(reader, consumer))
        {
            front->stack_count = first;
            front->group_count = top;
        }
        else if (top != 0 && front->group_consumers[top - 1] == consumer)
        {
            front->group_count = top;
        }
        else
        {
            front->group_consumers[top] = consumer;
        }
    }
}

// A token's values, the top group's where the token is its consumer (`grouped`), taken off the stack into `values` in
// source order: how many, or BUSTER_NODE_FRONT_UNKNOWN where one is unknown or they are more than
// BUSTER_NODE_FRONT_VALUES.
static inline uint32_t node_front_values(NodeFront* front, bool grouped, uint32_t* values)
{
    const uint32_t first = grouped ? front->group_firsts[front->group_count - 1] : front->stack_count;
    const uint32_t count = front->stack_count - first;
    bool known = count <= BUSTER_NODE_FRONT_VALUES;
    for (uint32_t i = 0; known && i < count; i += 1)
    {
        values[i] = front->stack[first + i];
        known = values[i] != BUSTER_NODE_FRONT_UNKNOWN;
    }
    front->taken_first = first;
    front->taken_count = count;
    front->stack_count = first;
    front->group_count -= grouped;
    return known ? count : BUSTER_NODE_FRONT_UNKNOWN;
}

// The values the last token took (node_front_values) are a left token's: their records' nodes may get a context from the
// walk's visit, which the front doesn't know. A record compared already says so at its node.
static inline void node_front_taken_by_left(NodeFront* front)
{
    for (uint32_t i = 0; i < front->taken_count; i += 1)
    {
        const uint32_t record = front->stack[front->taken_first + i];
        if (record != BUSTER_NODE_FRONT_UNKNOWN)
        {
            node_front_context_unknown(front, record);
        }
    }
    front->taken_count = 0;
}

// A record of `op` at `token` with operands a, b and c (records, or BUSTER_NODE_FRONT_UNKNOWN for none): its index, or
// BUSTER_NODE_FRONT_UNKNOWN where the front's arrays are full. Each operand's record gets its consumer's distance and its
// place. The lean front (`lean`, a constant where this is inlined) writes the structural bytes and the token alone;
// with the window (`window`, a constant too) each operand's record gets its consumer's distance among the arrays'
// records as well (`window_ups`).
static inline __attribute__((always_inline)) uint32_t node_front_add(NodeFront* front, CheckOp op, uint32_t token, uint32_t a, uint32_t b, uint32_t c, CheckInputNodeForm form,
                                                                     TypeId type, const bool lean, const bool window)
{
    const uint32_t record = front->count;
    uint32_t result = BUSTER_NODE_FRONT_UNKNOWN;
    if (record < BUSTER_NODE_FRONT_RECORDS)
    {
        front->ops[record] = (uint8_t)op;
        front->forms[record] = (uint8_t)form;
        front->contexts[record] = 0;
        front->taken_by_left[record] = 0;
        front->tokens[record] = token;
        const uint32_t ordinal = front->ordinal;
        const uint32_t operands[3] = { a, b, c };
        uint32_t count = 0;
        uint32_t size = 1;
        // From the last operand to the first: a conditional with its middle omitted takes its first value twice, and
        // that value's place is the first.
        for (uint32_t k = 3; k-- != 0;)
        {
            const uint32_t operand = operands[k];
            const bool present = operand != BUSTER_NODE_FRONT_UNKNOWN;
            const uint32_t distance = present ? ordinal - front->ordinals[operand] : 0;
            const uint8_t byte = (uint8_t)(distance < BUSTER_NODE_FRONT_FAR ? distance : BUSTER_NODE_FRONT_FAR);
            front->distances[k][record] = byte;
            if (present)
            {
                front->ups[operand] = byte;
                front->places[operand] = (uint8_t)k;
                if (window)
                {
                    front->window_ups[operand] = (uint16_t)(record - operand);
                }
                count += 1;
                // An operand taken twice is one subtree.
                size += k == 0 || operand != a ? front->sizes[operand] : 0;
            }
        }
        front->counts[record] = (uint8_t)count;
        front->sizes[record] = size;
        front->ups[record] = 0;
        front->places[record] = 0;
        front->ordinals[record] = ordinal;
        front->ordinal = ordinal + 1;
        front->roots[record] = form == CHECK_INPUT_FORM_DISCARD || form == CHECK_INPUT_FORM_INITIALIZER;
        front->nodes[record] = BUSTER_CHECK_NONE;
        if (!lean)
        {
            front->operands[0][record] = a;
            front->operands[1][record] = b;
            front->operands[2][record] = c;
            front->uses[record] = op == CHECK_OP_VALUE ? token : op == CHECK_OP_ADDRESS && a != BUSTER_NODE_FRONT_UNKNOWN && front->ops[a] == CHECK_OP_VALUE ? front->tokens[a] :
                                                                 BUSTER_CHECK_NONE;
            front->members[record] = 0;
            front->argument_firsts[record] = 0;
            front->argument_counts[record] = 0;
            front->types[record] = type;
            front->wides[record] = 0;
        }
        front->count = record + 1;
        result = record;
    }
    return result;
}

// A leaf's record of `value` at `token`, or BUSTER_NODE_FRONT_UNKNOWN where the arrays are full.
static inline uint32_t node_front_leaf(NodeFront* front, uint32_t token, const CheckValue* value)
{
    const bool wide = check_graph_leaf_rest(value);
    const bool room = !wide || front->wide_count < BUSTER_NODE_FRONT_WIDES;
    const uint32_t record = room ? node_front_add(front, CHECK_OP_VALUE, token, BUSTER_NODE_FRONT_UNKNOWN, BUSTER_NODE_FRONT_UNKNOWN, BUSTER_NODE_FRONT_UNKNOWN,
                                                  CHECK_INPUT_FORM_LEAF, 0, false, false) : BUSTER_NODE_FRONT_UNKNOWN;
    if (record != BUSTER_NODE_FRONT_UNKNOWN)
    {
        front->leaf_types[record] = value->type;
        front->leaf_flags[record] = value->flags;
        front->leaf_objects[record] = value->object;
        front->leaf_heads[record] = value->overload_head;
        front->leaf_integers[record] = (uint64_t)value->integer;
        front->leaf_categories[record] = (uint8_t)value->category;
        front->leaf_constants[record] = (uint8_t)value->constant_kind;
        if (wide)
        {
            front->wide_values[front->wide_count] = *value;
            front->wide_count += 1;
            front->wides[record] = (uint16_t)front->wide_count;
        }
    }
    return record;
}

// The literal `token` decoded as check_input_literal decodes it, where that is all it does: false for a literal it reports,
// warns of or makes a type for (an embedded resource, a bit-precise constant, a character constant with an escape or of
// several characters, a literal with a warning's event), which is the reader's own.
static bool node_front_literal(CheckInputReader* reader, uint32_t token, CheckLiteral* result)
{
    const Parser* parser = reader->parser;
    const uint64_t mark = reader->scratch->length;
    const uint8_t* bytes = 0;
    uint32_t length = 0;
    const uint32_t name = parser->tokens.names[token];
    uint32_t embed_length = 0;
    bool plain = false;
    *result = (CheckLiteral){ 0 };
    if (parse_embed_bytes(parser, token, &embed_length) != NULL)
    {
        plain = false;
    }
    else if (name == C_NAME_TRUE || name == C_NAME_FALSE)
    {
        result->type = BUSTER_TYPE_BOOL;
        result->integer = name == C_NAME_TRUE;
        result->valid = true;
        plain = true;
    }
    else if (name == C_NAME_NULLPTR || name == C_NAME_GNU_NULLPTR)
    {
        result->type = BUSTER_TYPE_NULLPTR;
        result->valid = true;
        plain = true;
    }
    else if (name == 0 && check_input_spelling(reader, token, &bytes, &length))
    {
        if (parser->tokens.kinds[token] == PARSE_KIND_CHARACTER)
        {
            bool escaped = false;
            for (uint32_t i = 0; i < length; i += 1)
            {
                escaped = escaped || bytes[i] == '\\';
            }
            *result = check_literal_character(bytes, length, reader->pp->options.language.standard >= 2023, reader->types->char_unsigned);
            plain = result->valid && !escaped && result->characters <= 1;
        }
        else
        {
            check_literal_number(bytes, length, reader->scratch, result);
            plain = result->valid;
        }
    }
    CheckWarningEvent event;
    plain = plain && result->valid && result->bit_width == 0 && !result->resource && !check_literal_warning_event(result, token, &event);
    arena_pop_to(reader->scratch, mark);
    return plain;
}

// A call's library mark, as the walk's call gives it (check_input_expression_call): the plain library builtin its callee's
// declaration is, where the callee is a name or the address of one.
static uint32_t node_front_library(NodeFront* front, CheckInputReader* reader, uint32_t callee)
{
    const uint32_t use = front->uses[callee];
    uint32_t library = use != BUSTER_CHECK_NONE ? check_input_library_call(reader, use) : 0;
    library = library != 0 && check_input_no_builtin(reader, reader->parser->tokens.names[use], use) ? 0 : library;
    return library;
}

// A kept record's columns moved down from `from` to `to`: the lean front's and the window's, every one a later record,
// the kernel or the shadow's comparisons read of a waiting value's subtree (node_front_keep, BUSTER_NODE_READER_WINDOW).
static inline void node_front_keep_record(NodeFront* front, uint32_t to, uint32_t from)
{
    front->ops[to] = front->ops[from];
    front->forms[to] = front->forms[from];
    front->tokens[to] = front->tokens[from];
    front->distances[0][to] = front->distances[0][from];
    front->distances[1][to] = front->distances[1][from];
    front->distances[2][to] = front->distances[2][from];
    front->roots[to] = front->roots[from];
    front->counts[to] = front->counts[from];
    front->ups[to] = front->ups[from];
    front->places[to] = front->places[from];
    front->ordinals[to] = front->ordinals[from];
    front->nodes[to] = front->nodes[from];
    front->sizes[to] = front->sizes[from];
    front->leaf_types[to] = front->leaf_types[from];
    front->leaf_integers[to] = front->leaf_integers[from];
    front->leaf_categories[to] = front->leaf_categories[from];
    front->window_kinds[to] = front->window_kinds[from];
    front->window_classes[to] = front->window_classes[from];
    front->window_left[to] = front->window_left[from];
    front->window_right[to] = front->window_right[from];
    front->window_ups[to] = front->window_ups[from];
    front->window_counts[to] = front->window_counts[from];
    front->window_entries[to] = front->window_entries[from];
    front->window_positions[to] = front->window_positions[from];
}

// Before a chunk: the records the waiting values still name kept, in their order, and every other one dropped. A
// condition's value waits for its statement's body (the tree puts a control's closing after it). A kept record was
// compared in its own chunk, so only what a later record reads of it stays: its place in the postorder (an operand's
// distance), its node (the comparison of its consumer), and its operator, token and use (a callee's library mark).
//
// Where the front writes the window (BUSTER_NODE_READER_WINDOW) a waiting value's whole subtree is kept, the records in
// a row that end at it, with every column the lean front and the window have of them: its expression's window is made
// of them when its root comes. The values wait in the postorder's order and one's subtree holds no other's, so each
// row moves down past nothing that is kept. A value keeps its place alone, as without the window, and is cut
// (BUSTER_NODE_FACTS_CLASS_CUT) where its records are not there in a row, or where the rows would take more of the
// arrays than a record a waiting value and a chunk's records leave: the front forgets its waiting values at the same
// count of them with the window as without. A row that is where it would be moved to stays as it is: the values that
// wait through many chunks (a table's elements, for their list's end) are the arrays' first, and are moved once. A kept
// row's calls' arguments are kept with it, the kept calls' the arguments' first. A cut value's are not, a call's among
// them: nothing reads them, and the list then holds an argument for a record the arrays hold and no more (each argument
// is a record's value taken once: one of a kept row, or one this chunk's call took), which is its room.
static void node_front_keep(NodeFront* front)
{
    uint32_t kept = 0;
    if (front->window)
    {
        NodeFrontShadow* shadow = front->shadow;
        uint32_t kept_values = 0;
        uint32_t kept_records = 0;
        uint32_t cut_values = 0;
        // The kept calls' arguments, which are the next chunk's first.
        uint32_t arguments = 0;
        uint32_t known = 0;
        shadow->window_arguments_most = front->window_argument_count > shadow->window_arguments_most ? front->window_argument_count : shadow->window_arguments_most;
        for (uint32_t i = 0; i < front->stack_count; i += 1)
        {
            known += front->stack[i] != BUSTER_NODE_FRONT_UNKNOWN;
        }
        const uint32_t most = BUSTER_NODE_FRONT_RECORDS - BUSTER_CHECK_EXPRESSION_CHUNK;
        // The records the rows may take beyond one a value.
        uint32_t spare = known <= most ? most - known : 0;
        // One past the last record a kept value took, among the records as they were.
        uint32_t passed = 0;
        for (uint32_t i = 0; i < front->stack_count; i += 1)
        {
            const uint32_t record = front->stack[i];
            if (record != BUSTER_NODE_FRONT_UNKNOWN)
            {
                const uint32_t size = front->sizes[record];
                // Its row has room, and is here: a value cut at an earlier end is not whole again, since of its records
                // its own alone is here, after the kept ones before it.
                const bool whole = size - 1 <= spare && passed + size <= record + 1;
                const uint32_t count = whole ? size : 1;
                const uint32_t first = record + 1 - count;
                for (uint32_t k = 0; k < count; k += 1)
                {
                    if (kept != first)
                    {
                        node_front_keep_record(front, kept + k, first + k);
                    }
#if BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER)
                    if (whole)
                    {
                        node_facts_window_kept(front, kept + k, first + k, &arguments);
                    }
#endif
                }
                const uint32_t last = kept + count - 1;
                if (!whole)
                {
                    front->window_classes[last] = BUSTER_NODE_FACTS_CLASS_CUT;
                }
                front->ups[last] = 0;
                front->places[last] = 0;
                front->stack[i] = last;
                kept_values += whole;
                kept_records += whole ? count : 0;
                cut_values += !whole;
                spare -= count - 1;
                kept += count;
                passed = record + 1;
            }
        }
        shadow->window_kept_values += kept_values;
        shadow->window_kept_records += kept_records;
        shadow->window_cut_values += cut_values;
        front->window_argument_count = arguments;
    }
    else
    {
        for (uint32_t i = 0; i < front->stack_count; i += 1)
        {
            const uint32_t record = front->stack[i];
            if (record != BUSTER_NODE_FRONT_UNKNOWN)
            {
                front->ops[kept] = front->ops[record];
                front->tokens[kept] = front->tokens[record];
                front->ordinals[kept] = front->ordinals[record];
                front->uses[kept] = front->uses[record];
                front->nodes[kept] = front->nodes[record];
                front->sizes[kept] = front->sizes[record];
                front->ups[kept] = 0;
                front->places[kept] = 0;
                front->stack[i] = kept;
                kept += 1;
            }
        }
    }
    front->count = kept;
    front->pending = kept;
    front->made = kept;
    front->argument_count = 0;
    front->wide_count = 0;
}

// The front's pass over a chunk's `acting` tokens and classes, in the walk's order, with the chunk's identifiers' and
// literals' entries (check_input_expression_identifiers, check_input_expression_literals): each token's record, or
// none, by its class. With `window` (the lean front's alone, and a constant where this is inlined, as `lean` is) each
// record's inputs for the fact words' kernel are written as it is made (BUSTER_NODE_READER_WINDOW).
static inline __attribute__((always_inline)) void node_front_chunk_by(NodeFront* front, CheckInputReader* reader, const uint32_t* tokens, const uint32_t* classes,
                                                                    uint32_t acting, const CheckInputIdentifiers* identifiers, const CheckInputLiterals* literals,
                                                                    const bool lean, const bool window)
{
    const Parser* parser = reader->parser;
    const CheckInput* input = reader->output;
    NodeFrontShadow* shadow = front->shadow;
    const uint32_t token_count = parser->tokens.count;
    const uint64_t start = __rdtsc();
    node_front_keep(front);
    if (front->count + BUSTER_CHECK_EXPRESSION_CHUNK > BUSTER_NODE_FRONT_RECORDS)
    {
        // More values wait than the arrays keep beside a chunk's records. (Where the front writes the window, none of
        // them kept a row then, so the calls' arguments' list is empty already.)
        node_front_forget(front);
        front->count = 0;
        front->pending = 0;
        front->made = 0;
    }
    uint32_t identifier = 0;
    uint32_t literal = 0;
    uint32_t values[BUSTER_NODE_FRONT_VALUES];
    for (uint32_t i = 0; i < acting; i += 1)
    {
        const uint32_t token = tokens[i];
        const uint32_t code = (classes[i] & BUSTER_CHECK_EXPRESSION_CODE_MASK) >> BUSTER_CHECK_EXPRESSION_CODE_SHIFT;
        // Whether the top group of the waiting values is the token's: a token that takes values takes that group, and
        // one that only passes values on has nothing to do without it. Most lists are declarations', with no value
        // below them: over half of Lua's and musl's acting tokens, a quarter of sqlite3.c's, end here.
        const bool mine = front->group_count != 0 && front->group_consumers[front->group_count - 1] == token;
        if (code == CHECK_INPUT_CODE_LIST || code == CHECK_INPUT_CODE_SEPARATOR || code == CHECK_INPUT_CODE_GROUP)
        {
            if (mine)
            {
                node_front_pass(front, reader, token);
            }
        }
        else
        {
            // Whether the front leaves the token, and whether the token has a value for its consumer: its record's, or an
            // unknown one where it is left or the arrays are full.
            uint32_t record = BUSTER_NODE_FRONT_UNKNOWN;
            bool value = false;
            bool left = false;
            front->taken_count = 0;
            switch (code)
            {
                case CHECK_INPUT_CODE_IDENTIFIER:
                {
                    if (lean)
                    {
                        // Its status from its token's own columns, as check_input_identifier_entry has it: its class's
                        // action takes it but in a K&R unit and for a constexpr object's use, and it has a value where it
                        // has a declared type. Its record is its token and its declaration.
                        const TypeId type = reader->declared_types[token];
                        const uint32_t declaration = type != 0 ? input->declarations[token] : 0;
                        left = input->kr_parameter_count != 0 || (type != 0 && (reader->declaration_flags[declaration] & BUSTER_CHECK_DECLARATION_CONSTEXPR) != 0);
                        value = left || type != 0;
                        if (!left && value)
                        {
                            record = node_front_add(front, CHECK_OP_VALUE, token, BUSTER_NODE_FRONT_UNKNOWN, BUSTER_NODE_FRONT_UNKNOWN, BUSTER_NODE_FRONT_UNKNOWN,
                                                    CHECK_INPUT_FORM_LEAF, 0, true, window);
                            if (record != BUSTER_NODE_FRONT_UNKNOWN)
                            {
                                front->leaf_categories[record] = BUSTER_NODE_FRONT_LEAF_IDENTIFIER;
                                front->leaf_integers[record] = declaration;
#if BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER)
                                if (window)
                                {
                                    node_facts_window_leaf(front, reader, record, token, BUSTER_NODE_FRONT_LEAF_IDENTIFIER, declaration, NULL);
                                }
#endif
                            }
                        }
                        break;
                    }
                    const uint32_t at = identifier;
                    const uint32_t status = identifiers->status[at];
                    identifier += 1;
                    left = (status & BUSTER_CHECK_IDENTIFIER_ACTED) == 0;
                    value = left || (status & BUSTER_CHECK_IDENTIFIER_NODE) != 0;
                    if (!left && value)
                    {
                        const CheckValue leaf =
                        {
                            .type = (TypeId)identifiers->types[at],
                            .category = (CheckCategory)(identifiers->types[at] >> 32),
                            .flags = (uint32_t)identifiers->flags[at],
                            .constant_kind = (CheckConstantKind)(identifiers->flags[at] >> 32),
                            .integer = (status & BUSTER_CHECK_IDENTIFIER_ENUMERATOR) != 0 ? reader->enum_values[identifiers->declarations[at]] : 0,
                            .object = (uint32_t)identifiers->objects[at],
                            .overload_head = (uint32_t)(identifiers->objects[at] >> 32),
                        };
                        record = node_front_leaf(front, token, &leaf);
                    }
                    break;
                }
                case CHECK_INPUT_CODE_LITERAL:
                {
                    const uint32_t at = literal;
                    literal += 1;
                    CheckLiteral decoded = { .type = BUSTER_TYPE_INT, .integer = literals->values[at], .valid = true };
                    left = check_input_function_name_token(parser, token) || (literals->values[at] == BUSTER_CHECK_NONE && !node_front_literal(reader, token, &decoded));
                    value = true;
                    if (!left && lean)
                    {
                        // Its type and its integer, as the reader decoded them: the rest of its value is the reader's entry.
                        record = node_front_add(front, CHECK_OP_VALUE, token, BUSTER_NODE_FRONT_UNKNOWN, BUSTER_NODE_FRONT_UNKNOWN, BUSTER_NODE_FRONT_UNKNOWN,
                                                CHECK_INPUT_FORM_LEAF, 0, true, window);
                        if (record != BUSTER_NODE_FRONT_UNKNOWN)
                        {
                            front->leaf_categories[record] = BUSTER_NODE_FRONT_LEAF_LITERAL;
                            front->leaf_types[record] = decoded.type;
                            front->leaf_integers[record] = decoded.integer;
#if BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER)
                            if (window)
                            {
                                node_facts_window_leaf(front, reader, record, token, BUSTER_NODE_FRONT_LEAF_LITERAL, 0, &decoded);
                            }
#endif
                        }
                    }
                    else if (!left)
                    {
                        const CheckValue leaf = check_input_literal_value(reader, token, decoded);
                        record = node_front_leaf(front, token, &leaf);
                    }
                    break;
                }
                case CHECK_INPUT_CODE_STRING:
                {
                    const CheckInitString* string = check_input_string(reader, token);
                    value = string != 0;
                    if (value && lean)
                    {
                        record = node_front_add(front, CHECK_OP_VALUE, token, BUSTER_NODE_FRONT_UNKNOWN, BUSTER_NODE_FRONT_UNKNOWN, BUSTER_NODE_FRONT_UNKNOWN,
                                                CHECK_INPUT_FORM_LEAF, 0, true, window);
                        if (record != BUSTER_NODE_FRONT_UNKNOWN)
                        {
                            front->leaf_categories[record] = BUSTER_NODE_FRONT_LEAF_STRING;
                            front->leaf_types[record] = string->type;
                            front->leaf_integers[record] = string->object;
#if BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER)
                            if (window)
                            {
                                node_facts_window_leaf(front, reader, record, token, BUSTER_NODE_FRONT_LEAF_STRING, 0, NULL);
                            }
#endif
                        }
                    }
                    else if (value)
                    {
                        const CheckValue leaf =
                        {
                            .type = string->type,
                            .category = CHECK_LVALUE,
                            .flags = BUSTER_CHECK_STATIC_OBJECT | (string->object != 0 ? BUSTER_CHECK_NONNULL_ADDRESS : 0),
                            .constant_kind = CHECK_CONSTANT_ADDRESS,
                            .object = string->object,
                        };
                        record = node_front_leaf(front, token, &leaf);
                    }
                    break;
                }
                case CHECK_INPUT_CODE_PREFIX:
                case CHECK_INPUT_CODE_POSTFIX:
                case CHECK_INPUT_CODE_BINARY:
                case CHECK_INPUT_CODE_CAST:
                case CHECK_INPUT_CODE_SUBSCRIPT:
                case CHECK_INPUT_CODE_TERNARY:
                case CHECK_INPUT_CODE_MEMBER:
                {
                    const uint32_t count = node_front_values(front, mine, values);
                    const uint8_t kind = parser->tokens.kinds[token];
                    CheckOp op = CHECK_OP_COUNT;
                    uint32_t wanted = 1;
                    uint32_t member = 0;
                    switch (code)
                    {
                        case CHECK_INPUT_CODE_PREFIX:
                            switch (kind)
                            {
                                case PARSE_KIND_PLUS: op = CHECK_OP_POSITIVE; break;
                                case PARSE_KIND_EXTENSION: op = CHECK_OP_SELECT; break;
                                case PARSE_KIND_MINUS: op = CHECK_OP_NEGATIVE; break;
                                case PARSE_KIND_EXCLAIM: op = CHECK_OP_NOT; break;
                                case PARSE_KIND_TILDE: op = CHECK_OP_BIT_NOT; break;
                                case PARSE_KIND_AMP: op = CHECK_OP_ADDRESS; break;
                                case PARSE_KIND_STAR: op = CHECK_OP_DEREFERENCE; break;
                                case PARSE_KIND_PLUSPLUS: op = CHECK_OP_PRE_INCREMENT; break;
                                case PARSE_KIND_MINUSMINUS: op = CHECK_OP_PRE_DECREMENT; break;
                                default: break;
                            }
                            break;
                        case CHECK_INPUT_CODE_POSTFIX:
                            op = kind == PARSE_KIND_PLUSPLUS ? CHECK_OP_POST_INCREMENT : kind == PARSE_KIND_MINUSMINUS ? CHECK_OP_POST_DECREMENT : CHECK_OP_COUNT;
                            break;
                        case CHECK_INPUT_CODE_BINARY:
                            op = check_input_binary(kind);
                            wanted = 2;
                            break;
                        case CHECK_INPUT_CODE_CAST:
                            op = reader->query_types[token] != 0 ? CHECK_OP_CAST : CHECK_OP_COUNT;
                            break;
                        case CHECK_INPUT_CODE_SUBSCRIPT:
                            op = CHECK_OP_SUBSCRIPT;
                            wanted = 2;
                            break;
                        case CHECK_INPUT_CODE_TERNARY:
                            op = CHECK_OP_CONDITIONAL;
                            wanted = count == 2 ? 2 : 3;
                            break;
                        default:
                            if (token + 1 < token_count && parser->tree.roles[token + 1] == PARSE_ROLE_MEMBER)
                            {
                                op = kind == PARSE_KIND_ARROW ? CHECK_OP_ARROW : CHECK_OP_MEMBER;
                                member = parser->tokens.names[token + 1];
                            }
                            break;
                    }
                    left = op == CHECK_OP_COUNT || count != wanted;
                    value = true;
                    if (!left)
                    {
                        // A conditional with its middle omitted (x ?: y) takes its first value twice.
                        const bool omitted = code == CHECK_INPUT_CODE_TERNARY && count == 2;
                        const uint32_t b = omitted ? values[0] : count >= 2 ? values[1] : BUSTER_NODE_FRONT_UNKNOWN;
                        const uint32_t c = omitted ? values[1] : count == 3 ? values[2] : BUSTER_NODE_FRONT_UNKNOWN;
                        record = node_front_add(front, op, token, values[0], b, c, CHECK_INPUT_FORM_OPERATOR, lean ? 0 : reader->query_types[token], lean, window);
                        if (record != BUSTER_NODE_FRONT_UNKNOWN && !lean)
                        {
                            front->members[record] = member;
                        }
#if BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER)
                        if (record != BUSTER_NODE_FRONT_UNKNOWN && window)
                        {
                            // A member's name, and a cast's type, its token's query's.
                            node_facts_window_operator(front, reader, record, op, token, values[0], b, c, code == CHECK_INPUT_CODE_CAST ? reader->query_types[token] : member);
                        }
#endif
                    }
                    break;
                }
                case CHECK_INPUT_CODE_CALL:
                {
                    const uint32_t count = node_front_values(front, mine, values);
                    left = count == BUSTER_NODE_FRONT_UNKNOWN || count == 0 || input->kr_parameter_count != 0 ||
                           (!lean && front->argument_count + count > BUSTER_NODE_FRONT_RECORDS);
                    value = true;
                    if (!left)
                    {
                        record = node_front_add(front, CHECK_OP_CALL, token, values[0], BUSTER_NODE_FRONT_UNKNOWN, BUSTER_NODE_FRONT_UNKNOWN, CHECK_INPUT_FORM_OPERATOR,
                                                lean ? 0 : reader->query_types[token], lean, window);
                        if (record != BUSTER_NODE_FRONT_UNKNOWN)
                        {
                            // Its operands are its callee and its arguments: each argument's record gets the call's distance
                            // and its place after the callee's.
                            const uint32_t ordinal = front->ordinals[record];
                            front->counts[record] = (uint8_t)(count < 255 ? count : 255);
                            for (uint32_t k = 1; k < count; k += 1)
                            {
                                const uint32_t argument = values[k];
                                const uint32_t distance = ordinal - front->ordinals[argument];
                                front->ups[argument] = (uint8_t)(distance < BUSTER_NODE_FRONT_FAR ? distance : BUSTER_NODE_FRONT_FAR);
                                front->places[argument] = (uint8_t)(k < 255 ? k : 255);
                                if (window)
                                {
                                    front->window_ups[argument] = (uint16_t)(record - argument);
                                }
                                front->sizes[record] += front->sizes[argument];
                            }
                            if (!lean)
                            {
                                front->members[record] = node_front_library(front, reader, values[0]);
                                front->argument_firsts[record] = front->argument_count;
                                front->argument_counts[record] = count - 1;
                                for (uint32_t k = 1; k < count; k += 1)
                                {
                                    front->arguments[front->argument_count] = values[k];
                                    front->argument_count += 1;
                                }
                            }
#if BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER)
                            if (window)
                            {
                                node_facts_window_call(front, reader, record, token, values, count);
                            }
#endif
                        }
                    }
                    break;
                }
                case CHECK_INPUT_CODE_TERMINATOR:
                {
                    // Each of its values discarded, a CAST to void, or tested in a condition's context, as the walk's
                    // terminator does; a statement expression's last statement gives the same nodes.
                    const uint32_t count = node_front_values(front, mine, values);
                    // A terminator with no value (a declaration's, an empty statement's) reads nothing more.
                    if (count != 0)
                    {
                        const bool condition = reader->conditions[token] != 0;
                        bool region = false;
                        if (input->region_count != 0)
                        {
                            const uint32_t parent = parser->tree.parents[token];
                            uint32_t selected = parent < token_count ? input->expression_regions[parent] : 0;
                            if (selected == 0 && parent < token_count && parser->tree.roles[parent] == PARSE_ROLE_LABEL)
                            {
                                selected = check_input_labeled_region(reader, parent);
                            }
                            region = selected != 0 && input->regions[selected - 1].final_statement == token;
                        }
                        left = count == BUSTER_NODE_FRONT_UNKNOWN || (condition && count > 1) || (region && count != 1);
                        for (uint32_t k = 0; !left && k < count; k += 1)
                        {
                            const CheckOp op = condition ? CHECK_OP_CONDITION : CHECK_OP_CAST;
                            const uint32_t root = node_front_add(front, op, token, values[k], BUSTER_NODE_FRONT_UNKNOWN, BUSTER_NODE_FRONT_UNKNOWN, CHECK_INPUT_FORM_DISCARD,
                                                                 BUSTER_TYPE_VOID, lean, window);
#if BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER)
                            if (root != BUSTER_NODE_FRONT_UNKNOWN && window)
                            {
                                // A statement's discard is a cast to void.
                                node_facts_window_operator(front, reader, root, op, token, values[k], BUSTER_NODE_FRONT_UNKNOWN, BUSTER_NODE_FRONT_UNKNOWN,
                                                           condition ? 0 : BUSTER_TYPE_VOID);
                            }
#else
                            (void)root;
#endif
                        }
                    }
                    break;
                }
                case CHECK_INPUT_CODE_CONDITION:
                {
                    const uint32_t count = node_front_values(front, mine, values);
                    left = count != 1;
                    if (!left)
                    {
                        const uint32_t keyword = reader->first_child[token];
                        const bool switched = keyword < token_count && parser->tokens.kinds[keyword] == PARSE_KIND_SWITCH;
                        const CheckOp op = switched ? CHECK_OP_POSITIVE : CHECK_OP_CONDITION;
                        record = node_front_add(front, op, token, values[0], BUSTER_NODE_FRONT_UNKNOWN, BUSTER_NODE_FRONT_UNKNOWN, CHECK_INPUT_FORM_OPERATOR,
                                                reader->query_types[token], lean, window);
                        if (record != BUSTER_NODE_FRONT_UNKNOWN)
                        {
                            front->roots[record] = 1;
#if BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER)
                            if (window)
                            {
                                node_facts_window_operator(front, reader, record, op, token, values[0], BUSTER_NODE_FRONT_UNKNOWN, BUSTER_NODE_FRONT_UNKNOWN, 0);
                            }
#endif
                        }
                    }
                    break;
                }
                case CHECK_INPUT_CODE_RETURN:
                {
                    const uint32_t count = node_front_values(front, mine, values);
                    const TypeId type = reader->return_types[token];
                    left = type == 0 || count == BUSTER_NODE_FRONT_UNKNOWN || count > 1;
                    if (!left && count == 1)
                    {
                        record = node_front_add(front, CHECK_OP_CONVERT, token, values[0], BUSTER_NODE_FRONT_UNKNOWN, BUSTER_NODE_FRONT_UNKNOWN, CHECK_INPUT_FORM_OPERATOR, type,
                                                lean, window);
                        if (record != BUSTER_NODE_FRONT_UNKNOWN)
                        {
                            front->roots[record] = 1;
#if BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER)
                            if (window)
                            {
                                // Its conversion's type is the function's.
                                node_facts_window_operator(front, reader, record, CHECK_OP_CONVERT, token, values[0], BUSTER_NODE_FRONT_UNKNOWN, BUSTER_NODE_FRONT_UNKNOWN, type);
                            }
#endif
                        }
                    }
                    break;
                }
                case CHECK_INPUT_CODE_INITIALIZE:
                {
                    // A braced initializer's assignment passes its values on.
                    if (reader->initializers[token] == 2)
                    {
                        node_front_pass(front, reader, token);
                    }
                    else
                    {
                        const uint32_t count = node_front_values(front, mine, values);
                        left = count != 1;
                        value = true;
                        if (!left)
                        {
                            record = node_front_add(front, CHECK_OP_INITIALIZE, token, values[0], BUSTER_NODE_FRONT_UNKNOWN, BUSTER_NODE_FRONT_UNKNOWN, CHECK_INPUT_FORM_INITIALIZER,
                                                    reader->query_types[token], lean, window);
#if BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER)
                            if (record != BUSTER_NODE_FRONT_UNKNOWN && window)
                            {
                                // No window holds an initialization's own record: its expression is its value's.
                                node_facts_window_operator(front, reader, record, CHECK_OP_INITIALIZE, token, values[0], BUSTER_NODE_FRONT_UNKNOWN, BUSTER_NODE_FRONT_UNKNOWN,
                                                           reader->query_types[token]);
                            }
#endif
                        }
                    }
                    break;
                }
                default:
                {
                    // The walk's own visit, whose values and nodes the front doesn't know.
                    (void)node_front_values(front, mine, values);
                    left = true;
                    value = true;
                    break;
                }
            }
            if (left)
            {
                shadow->left[code] += 1;
                shadow->left_queries += parser->tokens.kinds[token] == PARSE_KIND_SIZEOF;
                node_front_taken_by_left(front);
            }
            if (value)
            {
                node_front_give(front, reader, token, left ? BUSTER_NODE_FRONT_UNKNOWN : record);
            }
        }
    }
    shadow->tokens += acting;
    shadow->records += front->count - front->pending;
    shadow->ticks += __rdtsc() - start;
}

// The front's pass with every column, the lean one, and the lean one that writes the window: each its own copy of the
// loop, which is the choices' constants away from the others.
static __attribute__((noinline)) void node_front_chunk(NodeFront* front, CheckInputReader* reader, const uint32_t* tokens, const uint32_t* classes, uint32_t acting,
                                                       const CheckInputIdentifiers* identifiers, const CheckInputLiterals* literals)
{
    node_front_chunk_by(front, reader, tokens, classes, acting, identifiers, literals, false, false);
}

static __attribute__((noinline)) void node_front_chunk_lean(NodeFront* front, CheckInputReader* reader, const uint32_t* tokens, const uint32_t* classes, uint32_t acting,
                                                            const CheckInputIdentifiers* identifiers, const CheckInputLiterals* literals)
{
    node_front_chunk_by(front, reader, tokens, classes, acting, identifiers, literals, true, false);
}

#if BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER)
static __attribute__((noinline)) void node_front_chunk_window(NodeFront* front, CheckInputReader* reader, const uint32_t* tokens, const uint32_t* classes, uint32_t acting,
                                                              const CheckInputIdentifiers* identifiers, const CheckInputLiterals* literals)
{
    node_front_chunk_by(front, reader, tokens, classes, acting, identifiers, literals, true, true);
}

// A chunk by the front that writes the window and by the same lean front without it (NodeFront.plain), whose ticks are
// what the window's columns and its chunks' ends add to: one after the other on the same tokens, which of them first by
// the chunk's parity, so that neither is always the one that finds the reader's columns as the other left them. The
// two make the same records of the chunk's tokens and leave the same values waiting, or the chunk is counted.
static __attribute__((noinline)) void node_front_chunk_windowed(NodeFront* front, CheckInputReader* reader, const uint32_t* tokens, const uint32_t* classes, uint32_t acting,
                                                                const CheckInputIdentifiers* identifiers, const CheckInputLiterals* literals)
{
    NodeFront* plain = front->plain;
    NodeFrontShadow* shadow = front->shadow;
    const bool first = plain != NULL && (front->chunks & 1) == 0;
    if (first)
    {
        node_front_chunk_lean(plain, reader, tokens, classes, acting, identifiers, literals);
    }
    node_front_chunk_window(front, reader, tokens, classes, acting, identifiers, literals);
    // The names' pass, the window's too: each name's leaf of the chunk's gets its declaration's line.
    node_facts_window_names(front);
    if (plain != NULL && !first)
    {
        node_front_chunk_lean(plain, reader, tokens, classes, acting, identifiers, literals);
    }
    front->chunks += 1;
    if (plain != NULL)
    {
        const uint32_t made = front->count - front->made;
        const bool same = plain->count - plain->made == made && plain->stack_count == front->stack_count && memcmp(plain->ops + plain->made, front->ops + front->made, made) == 0 &&
                          memcmp(plain->tokens + plain->made, front->tokens + front->made, (uint64_t)made * sizeof(uint32_t)) == 0;
        shadow->plain_ticks = plain->shadow->ticks;
        shadow->plain_records = plain->shadow->records;
        shadow->plain_differences += !same;
    }
}
#endif

// The front's own records among the walk's nodes strictly between two of them, counted up to `limit`.
static uint32_t node_front_made_between(const NodeFrontShadow* shadow, uint32_t low, uint32_t high, uint32_t limit)
{
    uint32_t made = 0;
    for (uint32_t node = low + 1; node < high && node < shadow->node_capacity && made < limit; node += 1)
    {
        made += (uint32_t)(shadow->nodes[node >> 6] >> (node & 63) & 1);
    }
    return made;
}

// The record that is the unit's `ordinal`th, among the kept ones and the chunk's (their ordinals rise with their
// indexes), or BUSTER_NODE_FRONT_UNKNOWN.
static uint32_t node_front_record_at(const NodeFront* front, uint32_t ordinal)
{
    uint32_t low = 0;
    uint32_t high = front->count;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        if (front->ordinals[middle] < ordinal)
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }
    return low < front->count && front->ordinals[low] == ordinal ? low : BUSTER_NODE_FRONT_UNKNOWN;
}

// `record`'s operand at `place` by the structural bytes alone: the record back whose consumer's distance ends at
// `record` with that place, or BUSTER_NODE_FRONT_UNKNOWN where none is within a byte's reach.
static uint32_t node_front_operand_placed(const NodeFront* front, uint32_t record, uint32_t place)
{
    const uint32_t ordinal = front->ordinals[record];
    uint32_t found = BUSTER_NODE_FRONT_UNKNOWN;
    for (uint32_t x = record; x != 0 && found == BUSTER_NODE_FRONT_UNKNOWN && ordinal - front->ordinals[x - 1] < BUSTER_NODE_FRONT_FAR; x -= 1)
    {
        const uint32_t up = front->ups[x - 1];
        found = up != 0 && front->ordinals[x - 1] + up == ordinal && front->places[x - 1] == place ? x - 1 : found;
    }
    return found;
}

// The front's records of the chunk against the walk's nodes, after the walk acted at the chunk and its batch went to
// the graph: each record's node (its token's; a discard's by a search), operator, operands and structural bytes, and
// what else its front keeps (a leaf's value, a member, a call's arguments and library mark, the requested type; the
// lean front's leaf payload). A record that agrees gets its node, the node's bit and its context to compare at the
// walk's end; one that differs is counted and listed.
static void node_front_compare(NodeFront* front, const CheckInputReader* reader)
{
    const CheckInput* input = reader->output;
    const CheckGraph* graph = &input->graph;
    NodeFrontShadow* shadow = front->shadow;
    const bool lean = front->lean;
    memset(front->vouched + front->pending, 0, front->count - front->pending);
    for (uint32_t record = front->pending; record < front->count; record += 1)
    {
        const uint32_t token = front->tokens[record];
        const CheckInputNodeForm form = (CheckInputNodeForm)front->forms[record];
        const bool discard = form == CHECK_INPUT_FORM_DISCARD;
        // Its first three operands as records: the lean front's by their distances back, where each is within the
        // byte's reach, and every other's as the front kept them.
        uint32_t operands[3];
        for (uint32_t k = 0; k < 3; k += 1)
        {
            const uint32_t distance = front->distances[k][record];
            operands[k] = !lean ? front->operands[k][record] : distance != 0 && distance != BUSTER_NODE_FRONT_FAR ? node_front_record_at(front, front->ordinals[record] - distance) :
                                                                                                                  BUSTER_NODE_FRONT_UNKNOWN;
        }
        const uint32_t operand = operands[0] != BUSTER_NODE_FRONT_UNKNOWN ? front->nodes[operands[0]] : BUSTER_CHECK_NONE;
        uint32_t node = BUSTER_CHECK_NONE;
        if (discard)
        {
            // The walk numbers a discard when it acts at the terminator, as the front makes its record: after the node of
            // the record before it, or else after its value's node (a for's step is discarded after the loop's body). It
            // is the next node at the token with the value for its operand, or, where the lean front's value is past its
            // byte's reach, the next with the record's operator.
            const uint32_t before = record != 0 ? front->nodes[record - 1] : BUSTER_CHECK_NONE;
            const bool reached = operand != BUSTER_CHECK_NONE;
            node = before != BUSTER_CHECK_NONE ? before + 1 : reached ? operand + 1 : BUSTER_CHECK_NONE;
            while (node < graph->count && (reached || (lean && front->distances[0][record] == BUSTER_NODE_FRONT_FAR)) &&
                   (graph->tokens[node] != token || (reached ? graph->operand0[node] != operand : graph->ops[node] != (CheckOp)front->ops[record])))
            {
                node += 1;
            }
        }
        else
        {
            node = input->token_nodes[token];
        }
        uint32_t field = 0;
        if (node >= graph->count || graph->tokens[node] != token)
        {
            field = BUSTER_NODE_FRONT_FIELD_NODE;
        }
        else if (graph->ops[node] != (CheckOp)front->ops[record])
        {
            field = BUSTER_NODE_FRONT_FIELD_OPERATOR;
        }
        else if (form == CHECK_INPUT_FORM_LEAF && lean)
        {
            // The lean leaf: an identifier's node is its token's; a literal has the leaf's type and, an integer, its
            // value; a string its type and object.
            const CheckValue theirs = check_graph_leaf_value(graph, node);
            const uint8_t kind = front->leaf_categories[record];
            const bool same = kind == BUSTER_NODE_FRONT_LEAF_IDENTIFIER ? front->leaf_integers[record] == (theirs.type != 0 ? input->declarations[token] : 0) :
                              kind == BUSTER_NODE_FRONT_LEAF_LITERAL ?
                              front->leaf_types[record] == theirs.type && (theirs.constant_kind != CHECK_CONSTANT_INTEGER || front->leaf_integers[record] == (uint64_t)theirs.integer) :
                              front->leaf_types[record] == theirs.type && front->leaf_integers[record] == theirs.object;
            field = same && front->counts[record] == 0 ? 0 : BUSTER_NODE_FRONT_FIELD_LEAF;
        }
        else if (form == CHECK_INPUT_FORM_LEAF)
        {
            const CheckValue theirs = check_graph_leaf_value(graph, node);
            const uint16_t wide = front->wides[record];
            bool same = false;
            if (wide != 0)
            {
                const CheckValue* mine = front->wide_values + wide - 1;
                same = mine->type == theirs.type && mine->category == theirs.category && mine->flags == theirs.flags && mine->constant_kind == theirs.constant_kind &&
                       mine->integer == theirs.integer && mine->floating_storage[0] == theirs.floating_storage[0] &&
                       (mine->floating_storage[1] & 0xffff) == (theirs.floating_storage[1] & 0xffff) && mine->imaginary_storage[0] == theirs.imaginary_storage[0] &&
                       (mine->imaginary_storage[1] & 0xffff) == (theirs.imaginary_storage[1] & 0xffff) && mine->object == theirs.object &&
                       mine->overload_head == theirs.overload_head && mine->offset == theirs.offset && mine->bit_width == theirs.bit_width && mine->fault == theirs.fault &&
                       mine->fault_node == theirs.fault_node && mine->object_bit_offset == theirs.object_bit_offset;
            }
            else
            {
                same = !check_graph_leaf_rest(&theirs) && front->leaf_types[record] == theirs.type && front->leaf_categories[record] == (uint8_t)theirs.category &&
                       front->leaf_flags[record] == theirs.flags && front->leaf_constants[record] == (uint8_t)theirs.constant_kind &&
                       front->leaf_integers[record] == (uint64_t)theirs.integer && front->leaf_objects[record] == theirs.object &&
                       front->leaf_heads[record] == theirs.overload_head;
            }
            field = same && front->counts[record] == 0 ? 0 : BUSTER_NODE_FRONT_FIELD_LEAF;
        }
        else
        {
            // The walk's node's operands: its three, then a call's arguments. Each of the front's is that node's record,
            // by the distance where it is within the byte's reach, and its bytes say so from its side: the same
            // distance forward, and its place. An operand's distance is one more than the front's own records between
            // the walk's two nodes, which the walk numbers in the same postorder (it also numbers the nodes of the
            // tokens the front left).
            const uint32_t theirs[3] = { graph->operand0[node], graph->operand1[node], graph->operand2[node] };
            const uint32_t argument_count = graph->argument_counts[node];
            const uint32_t whole = (theirs[0] != BUSTER_CHECK_NONE) + (theirs[1] != BUSTER_CHECK_NONE) + (theirs[2] != BUSTER_CHECK_NONE) + argument_count;
            bool identity = true;
            bool distances = true;
            bool structure = front->counts[record] == (whole < 255 ? whole : 255);
            bool past = false;
            for (uint32_t k = 0; k < 3; k += 1)
            {
                const uint32_t distance = front->distances[k][record];
                const uint32_t x = operands[k];
                if (theirs[k] == BUSTER_CHECK_NONE)
                {
                    identity = identity && x == BUSTER_NODE_FRONT_UNKNOWN && distance == 0;
                }
                else
                {
                    // The place of an operand taken twice (a conditional's first, its middle omitted) is its first.
                    const uint32_t place = k == 1 && theirs[1] == theirs[0] ? 0 : k;
                    const uint32_t between = node_front_made_between(shadow, theirs[k], node, BUSTER_NODE_FRONT_FAR) + 1;
                    const bool far = distance == BUSTER_NODE_FRONT_FAR;
                    identity = identity && distance != 0 && (lean && far ? x == BUSTER_NODE_FRONT_UNKNOWN : x != BUSTER_NODE_FRONT_UNKNOWN && front->nodes[x] == theirs[k]);
                    distances = distances && (far ? between >= BUSTER_NODE_FRONT_FAR : between == distance);
                    structure = structure && (x == BUSTER_NODE_FRONT_UNKNOWN || (front->ups[x] == distance && front->places[x] == place));
                    if (x != BUSTER_NODE_FRONT_UNKNOWN)
                    {
                        front->vouched[x] = 1;
                    }
                    if (front->roots[record] == 0)
                    {
                        shadow->operands += 1;
                        shadow->operands_near += distance <= BUSTER_NODE_FRONT_NEAR;
                        shadow->operands_byte += distance > BUSTER_NODE_FRONT_NEAR && distance < BUSTER_NODE_FRONT_FAR;
                        shadow->operands_far += far;
                        past = past || distance > BUSTER_NODE_FRONT_NEAR;
                    }
                    else
                    {
                        shadow->root_operands += 1;
                        shadow->root_operands_past += distance > BUSTER_NODE_FRONT_NEAR;
                    }
                }
            }
            // A call's arguments, at the places after its callee's: the lean front's are found by their bytes, each
            // other's is in the front's list. One past a byte's reach is only that far.
            bool arguments = lean || front->argument_counts[record] == argument_count;
            for (uint32_t j = 0; arguments && j < argument_count; j += 1)
            {
                const uint32_t argument = graph->argument_nodes[graph->argument_offsets[node] + j];
                const uint32_t place = j + 1;
                const uint32_t x = lean ? node_front_operand_placed(front, record, place) : front->arguments[front->argument_firsts[record] + j];
                const uint32_t between = node_front_made_between(shadow, argument, node, BUSTER_NODE_FRONT_FAR) + 1;
                if (x == BUSTER_NODE_FRONT_UNKNOWN)
                {
                    arguments = between >= BUSTER_NODE_FRONT_FAR;
                }
                else
                {
                    const uint32_t distance = front->ordinals[record] - front->ordinals[x];
                    const uint32_t byte = distance < BUSTER_NODE_FRONT_FAR ? distance : BUSTER_NODE_FRONT_FAR;
                    arguments = front->nodes[x] == argument;
                    distances = distances && (byte == BUSTER_NODE_FRONT_FAR ? between >= BUSTER_NODE_FRONT_FAR : between == byte);
                    structure = structure && front->ups[x] == byte && front->places[x] == (place < 255 ? place : 255);
                    front->vouched[x] = 1;
                    shadow->arguments_past += distance > BUSTER_NODE_FRONT_NEAR;
                    past = past || distance > BUSTER_NODE_FRONT_NEAR;
                }
                shadow->arguments += 1;
                shadow->places_past += place > BUSTER_NODE_FRONT_PLACE_MOST;
            }
            shadow->counts_past += whole > BUSTER_NODE_FRONT_COUNT_MOST;
            const bool kept = lean || (graph->members[node] == front->members[record] && graph->types[node] == front->types[record]);
            field = !identity ? BUSTER_NODE_FRONT_FIELD_OPERAND : !lean && graph->members[node] != front->members[record] ? BUSTER_NODE_FRONT_FIELD_MEMBER :
                    !arguments ? BUSTER_NODE_FRONT_FIELD_ARGUMENTS : !kept ? BUSTER_NODE_FRONT_FIELD_TYPE : !distances ? BUSTER_NODE_FRONT_FIELD_DISTANCE :
                    !structure ? BUSTER_NODE_FRONT_FIELD_STRUCTURE : 0;
            if (field == 0 && past && node < shadow->node_capacity)
            {
                shadow->past[node >> 6] |= UINT64_C(1) << (node & 63);
            }
        }
        shadow->checked += 1;
        if (field == 0)
        {
            // Its context, as the flush gives a node its own and a list its values': compared at the walk's end
            // (node_front_finish).
            const bool statics = (form == CHECK_INPUT_FORM_OPERATOR || form == CHECK_INPUT_FORM_INITIALIZER) && reader->static_initializers[token];
            const uint8_t context = (uint8_t)((statics && form == CHECK_INPUT_FORM_OPERATOR ? BUSTER_CHECK_CONTEXT_STATIC : 0) |
                                              (statics && form == CHECK_INPUT_FORM_INITIALIZER ? BUSTER_CHECK_CONTEXT_VALUE : 0) | front->contexts[record]);
            front->nodes[record] = node;
            if (node < shadow->node_capacity)
            {
                shadow->nodes[node >> 6] |= UINT64_C(1) << (node & 63);
                shadow->contexts[node] = front->taken_by_left[record] != 0 ? BUSTER_NODE_FRONT_CONTEXT_UNKNOWN : context;
            }
        }
        else
        {
            if (shadow->differences < BUSTER_NODE_FRONT_LISTED)
            {
                shadow->difference_tokens[shadow->differences] = token;
                shadow->difference_fields[shadow->differences] = field;
            }
            shadow->differences += 1;
        }
    }
    // A record of the chunk with a consumer's distance has a compared consumer that found it there: a value still
    // waiting, one a left token took and one nothing takes have none. One past the byte's reach the lean front's
    // consumer can't name.
    for (uint32_t record = front->pending; record < front->count; record += 1)
    {
        const uint8_t up = front->ups[record];
        if (up != 0 && front->vouched[record] == 0 && !(lean && up == BUSTER_NODE_FRONT_FAR))
        {
            if (shadow->differences < BUSTER_NODE_FRONT_LISTED)
            {
                shadow->difference_tokens[shadow->differences] = front->tokens[record];
                shadow->difference_fields[shadow->differences] = BUSTER_NODE_FRONT_FIELD_STRUCTURE;
            }
            shadow->differences += 1;
        }
    }
    // A value still waiting has no consumer yet, whichever chunk made its record.
    for (uint32_t i = 0; i < front->stack_count; i += 1)
    {
        const uint32_t record = front->stack[i];
        if (record != BUSTER_NODE_FRONT_UNKNOWN && front->ups[record] != 0)
        {
            if (shadow->differences < BUSTER_NODE_FRONT_LISTED)
            {
                shadow->difference_tokens[shadow->differences] = front->tokens[record];
                shadow->difference_fields[shadow->differences] = BUSTER_NODE_FRONT_FIELD_STRUCTURE;
            }
            shadow->differences += 1;
        }
    }
    front->pending = front->count;
}

// After the walk's last chunk: each agreed node's context compared with the graph's, final now. A node whose context
// differs is a difference, and no longer the front's.
static void node_front_finish(NodeFront* front, const CheckInputReader* reader, uint32_t base)
{
    const CheckGraph* graph = &reader->output->graph;
    NodeFrontShadow* shadow = front->shadow;
    shadow->waiting += front->stack_count;
    for (uint32_t node = base; node < graph->count && node < shadow->node_capacity; node += 1)
    {
        const bool agreed = (shadow->nodes[node >> 6] >> (node & 63) & 1) != 0;
        if (agreed && shadow->contexts[node] != BUSTER_NODE_FRONT_CONTEXT_UNKNOWN && graph->contexts[node] != shadow->contexts[node])
        {
            if (shadow->differences < BUSTER_NODE_FRONT_LISTED)
            {
                shadow->difference_tokens[shadow->differences] = graph->tokens[node];
                shadow->difference_fields[shadow->differences] = BUSTER_NODE_FRONT_FIELD_CONTEXT;
            }
            shadow->differences += 1;
            shadow->nodes[node >> 6] &= ~(UINT64_C(1) << (node & 63));
        }
    }
#if BUSTER_IN_UNIT(BUSTER_UNIT_CHECKER)
    if (front->typer != NULL)
    {
        node_machine_reader_finish(front->typer);
    }
#endif
}
