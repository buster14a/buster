// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #2850's step 1, in #2524's shadow (measure-node-machine --shadow --facts; docs/one-core-path.md): a leaf's and a
// lookup's record from one load each. T (c_check_node_machine_type.c) types a leaf from the graph's leaf record, which
// the reader made from seven columns at the token and at its declaration, and a member, a dereference, a subscript and
// a call by a dozen queries of the type table, a column each. Here the same records are read from three kinds of words:
//
// - A declaration's line (32 bytes, by the line's number): its leaf's record's first half, as the record has it: its
//   type and that type's word, its flags and its object, its payload (an enumerator's value) and its standing. Made from
//   the declaration's own columns as the reader had them at a use (CheckInput.declared_types at the use's token, where
//   the reader copied the declaration's; declaration_kinds, declaration_bits, objects and enum_values at the
//   declaration; its weak attribute and the unit's weak pragmas before the use), never from the use's leaf. A
//   declaration whose line would differ from its last (a type completed since, a weak pragma since) gets a new line, as
//   a compile would write its line again: counted.
// - A type's row (NodeFactsRow, 64 bytes, by type index): its own word, where it is complete, its value type and its
//   promotion; its element (a pointer's pointee, an array's element) with the element's word and completeness; an array's
//   or a function's pointer type; a function's or a function pointer's return type, with its word, its completeness
//   and its prototype's flags and parameter count; and, as flags, what each lookup's rule asks of the type.
// - A member's slot (NodeFactsMember, 32 bytes, by a hash of the record's type and the name): the member's type as the
//   access qualifies it, that type's word, its offset and its flags. A memo of type_lookup_member's answers.
//
// The kernel's own record of a node is one line of the cache (NodeFactsRecord), T's record's fields together, where T
// has a column a field: a store a column a record costs Zen 5 about a cycle each, nine a record, and a line's stores
// don't. A leaf is then one load of its declaration's line and one store.
//
// The experiment's unit of work is a window: the unit's machine-taken expressions, their records one after another with
// their operands as distances back (node_facts_append, from T's columns as the shadow compares each expression), cut
// with their body and where the next expression would take it past the window's records. The records of the operators
// that are no lookup are given, as T made them, and so is a literal's leaf, the reader's entry. The kernel
// (node_facts_window) lists a window's identifiers' leaves and its lookups, sixteen records a step; makes the leaves; and
// makes the lookups (`->`, `.`, `*`, a subscript, a call's value and a discard) a class at a time, one loop an operator
// and a level of operands, each from its operands' records, its operand's type's row and, for a member, its slot. Each
// lookup has a short path for the shapes nearly all have, straight code over the row's flags, and behind it its rule:
// T's own rule's steps with every query of the type table replaced by a field of a row or a slot. A lookup whose guards
// the words don't cover (a wrapped type, a wide value, an element whose size the row doesn't hold under a constant index
// of an address constant) is left: it takes T's record, and is counted. T's columns are the oracle: node_facts_same
// compares every field T writes of a record, over records the kernel finds poisoned.
//
// #2850's step 2's first piece (--facts-operators; docs/one-core-path.md, "Step 2's first piece"): T's other records
// from the same words, so that none is given. A literal's leaf is a line of its own (the reader's record of it), copied
// as a declaration's. The operators that are no lookup are made by T's rows' and rules' own steps on the kernel's
// records: the arithmetic, comparison, shift and bitwise operators by T's baked table, with the constants' fold,
// pointer arithmetic and a pointer's equality with a null pointer constant behind it; `=`, a conversion and an
// initialization; a control's condition and `!`; `&x`; a cast; `&&` and `||`; the increments and the compound
// assignments; a comma; sizeof and _Alignof; and `?:`. What they ask of the type table beyond a type's row is a side
// row a type (the pointer types to it, its size and alignment) and a slot a pair of pointer types (whether one
// converts to the other). The classes are then a round and a code (BUSTER_NODE_FACTS_ROUNDS).
//
// #2850's step 2's third piece (--front-facts; docs/one-core-path.md, "The kernel where the machine sits"): the
// operators' kernel at reader time, in the reader's own hook (c_check_node_machine_reader.c's
// node_machine_reader_chunk), on each chunk's expressions T took there, before the unit's program exists. T's records
// there are the oracle. The words are made as the unit is read: a type's row when the type is (node_facts_settle), a
// declaration's line at its first use, a literal's a batch's own. The end of the file has it (node_facts_reader_flush).
//
// The piece after it (--front-window; docs/one-core-path.md, "The front writes the window"): the lean front writes each
// record's inputs for the kernel as it makes the record (node_facts_window_leaf, node_facts_window_operator and
// node_facts_window_call here, called from c_check_node_machine_front.c's pass), a pass over a chunk's records gives
// each name's leaf its declaration's line (node_facts_window_names), and the kernel reads the front's columns where
// they are written: a chunk's records are its window (node_facts_window_run). The two other makings of the same inputs
// are its twins.
//
// No compile takes any of it.
#ifndef BUSTER_C_CHECK_NODE_MACHINE_FACTS_INCLUDED
#define BUSTER_C_CHECK_NODE_MACHINE_FACTS_INCLUDED

// A window's record's kind: another operator's or a lookup the kernel doesn't model (T's record, given), a literal's
// leaf (the reader's, given), an identifier's leaf, and a lookup.
#define BUSTER_NODE_FACTS_GIVEN 0u
#define BUSTER_NODE_FACTS_LITERAL 1u
#define BUSTER_NODE_FACTS_IDENTIFIER 2u
#define BUSTER_NODE_FACTS_LOOKUP 3u
// A discard where the kernel makes none (the lean kernels): its operand's context, and its record T's, given.
#define BUSTER_NODE_FACTS_DISCARD 4u
// With the operators' kernel (--facts-operators; docs/one-core-path.md, "Step 2's first piece"): a literal's leaf made
// from a line of its own, and an operator that is no lookup, made by its rule. A kind's low two bits then say what the
// kernel does with a record: 2 a leaf from its line, 3 an operator's record made from its operands'.
#define BUSTER_NODE_FACTS_LITERAL_LINE 6u
#define BUSTER_NODE_FACTS_OPERATOR 7u

// The kernel's variants (docs/one-core-path.md, "Step 1's follow-up").
// - Whole: a leaf's and a lookup's record whole, T's every field. Step 1's kernel.
// - Lean: what later rules decide on. A leaf is its declaration's line alone; a lookup writes its record's first half
//   and a member's offset, and no conversion's entry (C's, from the operator, its operands' records and their types'
//   rows); a discard is not made (a context of its operand's).
// - Loads: what a lookup's load gives and nothing else: its type, that type's word and a member's offset. Its standing,
//   flags, object and payload are given, as lanes over its operands' bytes would give them, and so is whether its
//   operand is fit for the short path (`fits`, the lean kernel's own answer). No compile could run it alone: it says
//   what the load costs.
#define BUSTER_NODE_FACTS_WHOLE 0u
#define BUSTER_NODE_FACTS_LEAN 1u
#define BUSTER_NODE_FACTS_LOADS 2u
#define BUSTER_NODE_FACTS_MODES 3u

// How a record's `taken` says the kernel made it: left to T, by a short path (or a leaf's line), by its rule.
#define BUSTER_NODE_FACTS_LEFT 0u
#define BUSTER_NODE_FACTS_SHORT 1u
#define BUSTER_NODE_FACTS_RULED 2u

// What node_facts_given does with the records the kernel is to make: T's records (which the kernel writes over); each
// poisoned whole; of a lookup only what the loads' kernel writes poisoned (its type, its word and its member's offset;
// a leaf whole); or each a decoy, T's record of the node with one inherited flag the other way. Poison shows what the
// kernel doesn't write, but reads as no value a path takes (its type has no row): an operator made before its operand
// is only left to its rule there, and comes out right. A decoy reads as the operand's own value, as T's record does
// in a timed pass, and whatever is made from it carries its flag: that operator differs.
#define BUSTER_NODE_FACTS_POISON 0x25
#define BUSTER_NODE_FACTS_POISON_NONE 0u
#define BUSTER_NODE_FACTS_POISON_RECORDS 1u
#define BUSTER_NODE_FACTS_POISON_LOADS 2u
#define BUSTER_NODE_FACTS_POISON_DECOY 3u

// What makes a declaration's leaf the rule's, in its line's `unfit`: a register object, whose leaf's flags follow its
// use, and an enumerator whose value the line doesn't hold.
#define BUSTER_NODE_FACTS_REGISTER 0x01u
#define BUSTER_NODE_FACTS_WIDE 0x02u
// And, with the last, an enumerator whose value is past 64 bits: T's own record of its leaf is then no columns'
// (BUSTER_NODE_MACHINE_WIDE), which whatever makes the windows without T reads here (node_facts_front_expression).
#define BUSTER_NODE_FACTS_HIGH 0x04u

// What a record's `exponent` says where its payload is no floating value's: the payload and one, 0xffff for a payload
// past 0xfffd; 0 says none. The census's tests on the window read an integer constant's value there, with its standing,
// where they would gather its payload (c_check_node_census.c's node_tests_window_bytes): of a constant whose type is an
// integer's of 64 bits at most, whose payload is its value. T's own record says none (node_facts_expected), and so does
// a record made as no constant's (node_facts_record). And a call's says 1 where its arguments have nothing for those
// tests' call's test (node_facts_call_fast), which neither T's own record of a call nor the rule's (node_facts_call)
// says: a call's record says 0 or 1.
#define BUSTER_NODE_FACTS_QUIET_CALL 1u
// Two of a type's word's six bits for the census's tests (BUSTER_NODE_MACHINE_WORD_TESTS_SHIFT): an aggregate, an
// enumeration.
#define BUSTER_NODE_FACTS_TESTS_AGGREGATE 0x02u
#define BUSTER_NODE_FACTS_TESTS_ENUM 0x04u
static inline __attribute__((always_inline)) uint32_t node_facts_small(uint64_t payload)
{
    return (uint32_t)(payload < 0xffffu ? payload : 0xfffeu) + 1u;
}

// A record's exponent: a floating value's (`exponent`, its operand's), or what its payload says.
static inline __attribute__((always_inline)) uint32_t node_facts_exponent(uint32_t standing, uint64_t payload, uint32_t exponent)
{
    return (standing & BUSTER_NODE_MACHINE_FIELD) == BUSTER_NODE_MACHINE_FLOATING ? exponent : node_facts_small(payload);
}

// A row's `function` field: whether the type is a function or a pointer to one, whether it is the function itself (a
// designator's type), whether its return type is void or complete somewhere in the unit, and whether each of its
// parameters takes an integer constant of zero or 1 to 126 unchanged (no enumeration's type, no floating one, no
// integer's but a plain one of 8 to 64 bits: what the census's tests' conversion of a constant argument asks,
// node_facts_call_fast's); then its BUSTER_TYPE_FLAG_* in bits 8 to 15 and its parameter count from bit 16.
#define BUSTER_NODE_FACTS_FUNCTION 0x01u
#define BUSTER_NODE_FACTS_DESIGNATOR 0x02u
#define BUSTER_NODE_FACTS_RETURNS 0x04u
#define BUSTER_NODE_FACTS_TAKES 0x08u

// A row's `flags`: whether check_action_is_integer takes the type; a builtin integer type's sign and, in bits 8 to 15,
// its width (node_machine_operand_width's, 0 for another type); and whether `stride` is its element's size (an element
// of a size known when the unit is compiled, under 4 GB). Then what the lookups' short paths ask of a type in one test:
// an integer, floating or pointer type with no wrapper (node_machine_plain_kind's, whose lvalue converts to its value
// type); a pointer, an array, each with no wrapper; a struct or union with no wrapper that is complete somewhere in the
// unit; a pointer to one; and a pointer or array whose element is an object type complete somewhere.
#define BUSTER_NODE_FACTS_INTEGER 0x01u
#define BUSTER_NODE_FACTS_SIGNED 0x02u
#define BUSTER_NODE_FACTS_STRIDE 0x04u
#define BUSTER_NODE_FACTS_SCALAR 0x10000u
#define BUSTER_NODE_FACTS_POINTER 0x20000u
#define BUSTER_NODE_FACTS_ARRAY 0x40000u
#define BUSTER_NODE_FACTS_RECORD 0x80000u
#define BUSTER_NODE_FACTS_ARROW 0x100000u
#define BUSTER_NODE_FACTS_OBJECTS 0x200000u
// And what the operators' rules ask: whether check_action_is_scalar takes the type (a condition's operand), and whether
// its side row holds its size and alignment (a type sizeof and _Alignof take: no function, void or variably modified
// type, complete somewhere in the unit).
#define BUSTER_NODE_FACTS_TRUTHY 0x400000u
#define BUSTER_NODE_FACTS_SIZED 0x800000u
// And for a store: a builtin arithmetic type of T's rows (node_machine_row_kind's, from bool to unsigned __int128 and
// float and double), and one of them that is an integer type.
#define BUSTER_NODE_FACTS_ARITHMETIC 0x1000000u
#define BUSTER_NODE_FACTS_INTEGRAL 0x2000000u

// A member slot's flags: found (and no unknown member), a bit-field, of an incomplete array type, and in use (every
// slot that holds an answer, a member found or not).
#define BUSTER_NODE_FACTS_MEMBER_FOUND 0x01u
#define BUSTER_NODE_FACTS_MEMBER_BIT_FIELD 0x02u
#define BUSTER_NODE_FACTS_MEMBER_FLEXIBLE 0x04u
#define BUSTER_NODE_FACTS_MEMBER_USED 0x08u

// The members' slots: a power of two of them, at least four a member of the unit's type table, between these.
#define BUSTER_NODE_FACTS_MEMBER_LEAST_BITS 10u
#define BUSTER_NODE_FACTS_MEMBER_MOST_BITS 20u

// The lookups' operators, for the counts: `->`, `.`, `*`, a subscript, a call and a discard.
#define BUSTER_NODE_FACTS_LOOKUPS 6u
static const char* const node_facts_lookup_names[BUSTER_NODE_FACTS_LOOKUPS] = { "->", ".", "*", "[]", "call", "discard" };

// The most records a window has: the machine's most for an expression, so that a window holds at least one. And where a
// window is cut unless the command says otherwise: where the next expression would take it past this many records,
// whose lines are half the first-level cache.
#define BUSTER_NODE_FACTS_WINDOW BUSTER_NODE_MACHINE_MOST
#define BUSTER_NODE_FACTS_CUT 384u

// A made operator's code: a lookup's (`->`, a subscript, `.`, a call, `*`, a discard) or the operators' kernel's: `&x`;
// a cast to a type that is not void; a row's operator (an arithmetic, comparison, shift or bitwise operator: T's
// table's, then by rule pointer arithmetic, the constants' fold and a pointer's equality with a null pointer constant);
// `&&` and `||`; an update (the increments and the compound assignments); `=`; a conversion or an initialization; a
// truth (a control's condition and `!`); and the rest, each by its rule a record at a time: a comma, sizeof and
// _Alignof, `?:` and a unary plus. Their order is the order a round makes its classes in (below): the one that gives
// sqlite3.c's expressions the fewest classes, an operator's operands being mostly of the codes before its own.
#define BUSTER_NODE_FACTS_CODE_ARROW 0u
#define BUSTER_NODE_FACTS_CODE_SUBSCRIPT 1u
#define BUSTER_NODE_FACTS_CODE_DOT 2u
#define BUSTER_NODE_FACTS_CODE_ADDRESS 3u
#define BUSTER_NODE_FACTS_CODE_CAST 4u
#define BUSTER_NODE_FACTS_CODE_OTHER 5u
#define BUSTER_NODE_FACTS_CODE_ROW 6u
#define BUSTER_NODE_FACTS_CODE_CALL 7u
#define BUSTER_NODE_FACTS_CODE_LOGIC 8u
#define BUSTER_NODE_FACTS_CODE_UPDATE 9u
#define BUSTER_NODE_FACTS_CODE_DEREFERENCE 10u
#define BUSTER_NODE_FACTS_CODE_STORE 11u
#define BUSTER_NODE_FACTS_CODE_CONVERT 12u
#define BUSTER_NODE_FACTS_CODE_DISCARD 13u
#define BUSTER_NODE_FACTS_CODE_TRUTH 14u
#define BUSTER_NODE_FACTS_CODES 15u

// The made operators' classes. An operator's class is its round and its code (round << 4 | code). The kernel makes a
// window's operators a round at a time and, in a round, a code at a time in the codes' order, each class's operators in
// the window's order: so an operator reads only records made before it where its round is the highest of its operands'
// rounds, and one more than an operand's whose code is after its own. Operands of its own code are in its class before
// it (a chain of `->`, a call's call), and those of the codes before it are the round's earlier classes; a round's
// operators are independent but for those. BUSTER_NODE_FACTS_ROUNDS rounds have their classes, and what would be of a
// later one is made after them, up the postorder, each by its rule. A class comes with the record, as the operator and
// the operands' distances do: whatever emits the postorder has each record's operands at hand (node_facts_append here).
#define BUSTER_NODE_FACTS_ROUNDS 4u
#define BUSTER_NODE_FACTS_DEEP BUSTER_NODE_FACTS_ROUNDS
#define BUSTER_NODE_FACTS_NO_CLASS 0xffu
// The classes' lists: one a class of the rounds, and one for the deeper operators.
#define BUSTER_NODE_FACTS_LISTS (16u * BUSTER_NODE_FACTS_ROUNDS + 1u)

// The kernel's record of a node: T's record's fields (NodeMachineColumns' value_types, words, value_flags, objects,
// payloads, standings, exponents, converted, conversion_flags and member_offsets) in one line of the cache. T's columns
// take a store a field, each to its own line, and Zen 5 lands about one such store a cycle: nine cycles a record
// (stores.c in the measurement record). One line's stores land two a cycle, and a leaf's line is one vector's store; an
// operand's type, word, flags and standing are one line's loads. `taken`: whether a rule of the kernel's made the record
// (the experiment's count); `unfit`: zero in a record, and in a declaration's line whether the leaf is the rule's.
typedef struct NodeFactsRecord NodeFactsRecord;
struct NodeFactsRecord
{
    TypeId type;
    uint32_t word;
    uint32_t flags;
    uint32_t object;
    uint64_t payload;
    uint8_t standing;
    uint8_t taken;
    uint16_t exponent;
    uint32_t unfit;
    TypeId converted[2];
    uint32_t conversion_flags[2];
    uint64_t member_offset;
    uint64_t spare;
};

_Static_assert(sizeof(NodeFactsRecord) == 64 && offsetof(NodeFactsRecord, payload) == 16 && offsetof(NodeFactsRecord, standing) == 24 &&
               offsetof(NodeFactsRecord, converted) == 32 && offsetof(NodeFactsRecord, member_offset) == 48, "a record is one line of the cache, in quadwords");

typedef struct NodeFactsRow NodeFactsRow;
struct NodeFactsRow
{
    uint32_t word;
    uint32_t complete_at;
    TypeId value;
    TypeId promoted;
    TypeId element;
    uint32_t element_word;
    uint32_t element_complete_at;
    TypeId decayed[2];
    TypeId returned;
    uint32_t returned_word;
    uint32_t returned_complete_at;
    uint32_t function;
    uint32_t flags;
    uint32_t stride;
    // Where a call of the type has its value's type: 0 for void, else returned_complete_at.
    uint32_t returned_ready_at;
};

_Static_assert(sizeof(NodeFactsRow) == 64, "a type's row is one line of the cache");

typedef struct NodeFactsMember NodeFactsMember;
struct NodeFactsMember
{
    uint64_t key;
    TypeId type;
    uint32_t word;
    uint64_t offset;
    uint32_t flags;
    // What an access of the member adds to its value's flags: BUSTER_CHECK_CONST_READ_RESTRICTED for a flexible array.
    uint32_t result_flags;
};

_Static_assert(sizeof(NodeFactsMember) == 32, "a member's slot is half a line of the cache");

// A type's side row, the operators' kernel's (by type index): the pointer types to it, unqualified and const
// (type_pointer's answers, from the machine's memo as T fills it, node_facts_addresses), with their words; and, with
// BUSTER_NODE_FACTS_SIZED in its row's flags, its size and alignment.
typedef struct NodeFactsSide NodeFactsSide;
struct NodeFactsSide
{
    TypeId pointers[2];
    uint32_t pointer_words[2];
    uint64_t size;
    uint64_t alignment;
};

_Static_assert(sizeof(NodeFactsSide) == 32, "a type's side row is half a line of the cache");

// What T's rows ask of an operand's standing (node_machine_row_takes), as bits by the standing: an lvalue with no
// constant or its address's, which a row may take as its value; an rvalue with no constant; an rvalue with an integer
// or a floating constant, which a row may take where its other operand has none; and an rvalue with any constant. A
// wide value's standing has none: the rows here take what the record holds whole.
#define BUSTER_NODE_FACTS_STANDS_LVALUE 0x01u
#define BUSTER_NODE_FACTS_STANDS_RVALUE 0x02u
#define BUSTER_NODE_FACTS_STANDS_LITERAL 0x04u
#define BUSTER_NODE_FACTS_STANDS_CONSTANT 0x08u

// A pointer conversion's slot (by a hash of the target's and the source's pointer types): whether
// node_machine_pointer_assignable takes it, and whether the two point at the same type but for its qualifiers
// (check_action_convert_plain's test for BUSTER_CHECK_CONST_READ_RESTRICTED). A memo of the type table's answers, as
// the members' slots.
#define BUSTER_NODE_FACTS_PAIR_USED 0x01u
#define BUSTER_NODE_FACTS_PAIR_ASSIGNABLE 0x02u
#define BUSTER_NODE_FACTS_PAIR_SAME 0x04u
#define BUSTER_NODE_FACTS_PAIR_BITS 13u

typedef struct NodeFactsPair NodeFactsPair;
struct NodeFactsPair
{
    uint64_t key;
    uint32_t flags;
    uint32_t spare;
};

_Static_assert(sizeof(NodeFactsPair) == 16, "a pointer conversion's slot is a quarter of a line of the cache");

// An operand as its operator reads it (node_facts_operand): NodeMachineOperand's fields, and its type's kind after the
// conversion.
typedef struct NodeFactsOperand NodeFactsOperand;
struct NodeFactsOperand
{
    TypeId type;
    uint32_t flags;
    uint64_t integer;
    uint8_t category;
    uint8_t constant_kind;
    uint8_t kind;
};

typedef struct NodeFacts NodeFacts;
struct NodeFacts
{
    NodeMachine* machine;
    // The unit's reader's columns and its parser: the program's after the pipeline, the reader's own at reader time,
    // where there is no program yet (node_facts_reader_begin).
    const CheckInput* input;
    const Parser* parser;
    // At reader time the reader's own column of the enumerators' values, by declaration: the unit's list of them
    // (CheckInput.enum_values) is made when the reader ends. Null after the pipeline.
    const unsigned __int128* enum_values;
    // The words.
    NodeFactsRow* rows;
    uint64_t* lines;
    uint32_t* numbers;
    NodeFactsMember* members;
    NodeFactsMember no_member;
    uint32_t row_count;
    // The rows' room: the unit's types after the pipeline; at reader time the machine's room for the types made as it
    // reads, whose rows node_facts_settle makes.
    uint32_t row_capacity;
    uint32_t line_count;
    uint32_t line_capacity;
    // At reader time the literals' lines are a batch's own: the lines' first `literal_capacity` numbers, taken again a
    // batch (node_facts_reader_flush), the declarations' lines after them. After the pipeline (0) a literal's line
    // follows the lines made before it.
    uint32_t literal_capacity;
    uint32_t literal_count;
    uint32_t member_count;
    uint32_t member_bits;
    uint32_t void_word;
    // The operators' kernel's words: the types' side rows, the pointer conversions' slots, and the words of int and
    // unsigned long (a truth's and a sizeof's types).
    NodeFactsSide* sides;
    NodeFactsPair* pairs;
    NodeFactsPair no_pair;
    uint32_t pair_count;
    uint32_t int_word;
    uint32_t ulong_word;
    // What a row asks of an operand's standing, by the standing (BUSTER_NODE_FACTS_STANDS_*).
    uint8_t stands[256];
    // The windows' records: each record's operator, kind, a made operator's class (above), whether a lookup's operand
    // is fit for its short path (the loads' kernel's given, the lean kernel's answer), operands as distances back (0 for
    // none), input (a leaf's line's number, a member's name, a cast's, a conversion's or a sizeof's type, a call's first
    // argument in `arguments`, a conditional's third operand's distance back), a call's argument count, its node's
    // source position and its node; a call's arguments as distances back; and each window's first record,
    // `window_count` + 1 of them.
    uint8_t* ops;
    uint8_t* kinds;
    uint8_t* classes;
    uint8_t* fits;
    uint16_t* left;
    uint16_t* right;
    uint32_t* inputs;
    uint16_t* counts;
    uint32_t* positions;
    uint32_t* nodes;
    uint16_t* arguments;
    uint32_t* windows;
    uint32_t record_count;
    uint32_t record_capacity;
    uint32_t argument_count;
    uint32_t window_count;
    uint32_t window_body;
    uint32_t widest;
    // T's record of each (the oracle's, and what is given), by its place in the unit's windows; and the kernel's records
    // of the window at hand (NodeFactsRecord), by their places in the window.
    TypeId* expected_types;
    uint32_t* expected_flags;
    uint32_t* expected_objects;
    uint64_t* expected_payloads;
    uint8_t* expected_standings;
    uint16_t* expected_exponents;
    uint32_t* expected_words;
    TypeId* expected_converted;
    uint32_t* expected_conversion_flags;
    uint64_t* expected_member_offsets;
    NodeFactsRecord* records;
    // The kernel's lists for the window at hand: its leaves and its made operators by their places; each operator's
    // class beside its place; each class's operators (BUSTER_NODE_FACTS_LISTS lists of a window's most, and `sizes`, how
    // many each has); and those of the class at hand that no short path took. The first three have a vector's entries past a
    // window's most, which the listing stores whole.
    uint32_t* leaf_list;
    uint32_t* lookup_list;
    uint8_t* keys;
    uint32_t* batches;
    uint32_t sizes[BUSTER_NODE_FACTS_LISTS];
    uint32_t* rare;
    // The counts: the records by kind, the identifiers' leaves by what they name, the lookups by operator (each one's and
    // those the kernel left), the given records by operator, and the members' slots filled.
    uint64_t record_total;
    uint64_t literals;
    uint64_t identifiers;
    uint64_t enumerators;
    uint64_t functions;
    uint64_t static_objects;
    uint64_t automatic_objects;
    uint64_t left_identifiers;
    uint64_t lookups[BUSTER_NODE_FACTS_LOOKUPS];
    uint64_t left_lookups[BUSTER_NODE_FACTS_LOOKUPS];
    uint64_t given[CHECK_OP_COUNT];
    // The operators' kernel's: the records it is to make that are no lookup, by operator, and the literals' lines.
    uint64_t operator_records[CHECK_OP_COUNT];
    uint64_t literal_lines;
    uint64_t member_fills;
    uint64_t line_versions;
    uint64_t differences;
    // The kernel's TSC ticks in its two loops, summed over the windows it ran; and the words' making: the rows' and the
    // declarations' lines'.
    uint64_t leaf_ticks;
    uint64_t lookup_ticks;
    uint64_t row_ticks;
    uint64_t line_ticks;
    // The kernel's lists' lengths for the window it last ran.
    uint32_t leaf_count;
    uint32_t lookup_count;
    // Where a window is cut, in records, and whether it holds expressions of several bodies (else a body's alone).
    uint32_t cut;
    bool packed;
    // Whether the windows are a lean kernel's: their discards are no lookups.
    bool lean;
    // Whether they are the operators' kernel's: their literals' leaves have lines and their other operators are made.
    bool operators;
    bool room;
};

// A lookup's place in the counts, or BUSTER_NODE_FACTS_LOOKUPS for an operator that is none.
static inline uint32_t node_facts_lookup(CheckOp op)
{
    return op == CHECK_OP_ARROW ? 0 : op == CHECK_OP_MEMBER ? 1 : op == CHECK_OP_DEREFERENCE ? 2 : op == CHECK_OP_SUBSCRIPT ? 3 : op == CHECK_OP_CALL ? 4 :
           op == CHECK_OP_CAST ? 5 : BUSTER_NODE_FACTS_LOOKUPS;
}

// A lookup's code, by its place in the counts.
static const uint8_t node_facts_lookup_codes[BUSTER_NODE_FACTS_LOOKUPS] = {
    BUSTER_NODE_FACTS_CODE_ARROW, BUSTER_NODE_FACTS_CODE_DOT, BUSTER_NODE_FACTS_CODE_DEREFERENCE, BUSTER_NODE_FACTS_CODE_SUBSCRIPT, BUSTER_NODE_FACTS_CODE_CALL,
    BUSTER_NODE_FACTS_CODE_DISCARD,
};

// The operators' kernel's code for an operator that is no lookup (a cast to void is the discard's), or
// BUSTER_NODE_FACTS_CODES for one it doesn't make.
static uint32_t node_facts_operator(CheckOp op)
{
    uint32_t code = node_machine_row_operators[op] != NODE_MACHINE_ROW_NONE ? BUSTER_NODE_FACTS_CODE_ROW : BUSTER_NODE_FACTS_CODES;
    switch (op)
    {
        case CHECK_OP_ASSIGN: code = BUSTER_NODE_FACTS_CODE_STORE; break;
        case CHECK_OP_CONVERT: case CHECK_OP_INITIALIZE: code = BUSTER_NODE_FACTS_CODE_CONVERT; break;
        case CHECK_OP_CONDITION: case CHECK_OP_NOT: code = BUSTER_NODE_FACTS_CODE_TRUTH; break;
        case CHECK_OP_ADDRESS: code = BUSTER_NODE_FACTS_CODE_ADDRESS; break;
        case CHECK_OP_CAST: code = BUSTER_NODE_FACTS_CODE_CAST; break;
        case CHECK_OP_LOGICAL_AND: case CHECK_OP_LOGICAL_OR: code = BUSTER_NODE_FACTS_CODE_LOGIC; break;
        case CHECK_OP_PRE_INCREMENT: case CHECK_OP_PRE_DECREMENT: case CHECK_OP_POST_INCREMENT: case CHECK_OP_POST_DECREMENT: case CHECK_OP_ADD_ASSIGN:
        case CHECK_OP_SUBTRACT_ASSIGN: case CHECK_OP_MULTIPLY_ASSIGN: case CHECK_OP_DIVIDE_ASSIGN: case CHECK_OP_REMAINDER_ASSIGN: case CHECK_OP_SHIFT_LEFT_ASSIGN:
        case CHECK_OP_SHIFT_RIGHT_ASSIGN: case CHECK_OP_BIT_AND_ASSIGN: case CHECK_OP_BIT_OR_ASSIGN: case CHECK_OP_BIT_XOR_ASSIGN:
        {
            code = BUSTER_NODE_FACTS_CODE_UPDATE;
        } break;
        case CHECK_OP_COMMA: case CHECK_OP_SIZEOF: case CHECK_OP_ALIGNOF: case CHECK_OP_CONDITIONAL: case CHECK_OP_POSITIVE: code = BUSTER_NODE_FACTS_CODE_OTHER; break;
        default: break;
    }
    return code;
}

// A made operator's round after one operand, whose class is `operand` (BUSTER_NODE_FACTS_NO_CLASS for a record the
// kernel makes no operator of): at least `round`, the operand's round, and one more where the operand's code is after
// `code`, the operator's own.
static inline uint32_t node_facts_round(uint32_t round, uint32_t code, uint32_t operand)
{
    const uint32_t needed = operand == BUSTER_NODE_FACTS_NO_CLASS ? 0 : (operand >> 4) + ((operand & 0x0fu) > code ? 1u : 0u);
    return needed > round ? needed : round;
}

// What node_facts_round asks of one operand, by arithmetic alone: the operand's round, and one more where its code is
// after `code`; none for a record no operator made. A class's code is under 15 and `code` at most 15, so the sum of the
// operand's code and 15 less `code` carries into its round exactly where the operand's code is the greater; and
// BUSTER_NODE_FACTS_NO_CLASS, all ones, comes out sixteen, which the mask takes away.
static inline __attribute__((always_inline)) uint32_t node_facts_round_of(uint32_t code, uint32_t operand)
{
    return ((operand + 15u - code) >> 4) & 15u;
}

// A made operator's class: its round (the deeper together) and its code.
static inline uint8_t node_facts_class(uint32_t round, uint32_t code)
{
    return (uint8_t)((round < BUSTER_NODE_FACTS_ROUNDS ? round : BUSTER_NODE_FACTS_DEEP) << 4 | code);
}

// The later of list `latest` and the list a record of class `operand` is on, for the lists' twin: the lists are made in
// their numbers' order, a round's classes by their codes and the deeper records' list last.
static inline uint32_t node_facts_latest(uint32_t latest, uint32_t operand)
{
    const uint32_t list = operand == BUSTER_NODE_FACTS_NO_CLASS ? 0 : operand < 16u * BUSTER_NODE_FACTS_ROUNDS ? operand : 16u * BUSTER_NODE_FACTS_ROUNDS;
    return list > latest ? list : latest;
}

// Type `type`'s row's fields, from the type table and the machine's words: what every lookup of the unit asks of it. And
// its side row's size and alignment.
static void node_facts_row(const NodeMachine* machine, uint32_t index, NodeFactsRow* row, NodeFactsSide* side)
{
    const TypeTable* types = machine->types;
    const TypeId type = (TypeId)index << BUSTER_TYPE_INDEX_SHIFT;
    const uint32_t word = machine->type_words[index];
    const bool valid = (word & BUSTER_NODE_MACHINE_WORD_VALID) != 0;
    const bool plain = (word & BUSTER_NODE_MACHINE_WORD_PLAIN) != 0;
    const uint8_t kind = (uint8_t)(word >> 16);
    *row = (NodeFactsRow){ .word = word, .complete_at = machine->type_complete_at[index], .returned_ready_at = UINT32_MAX };
    if (valid)
    {
        row->value = type_conversion_operand(types, type);
        row->promoted = type_promote(types, type);
        const uint32_t width = index < BUSTER_NODE_MACHINE_KINDS ? node_machine_integer_widths[index] : 0;
        const bool record = plain && (kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION) && row->complete_at != UINT32_MAX;
        row->flags = (check_action_is_integer(types, type) ? BUSTER_NODE_FACTS_INTEGER : 0) | width << 8 |
                     (width != 0 && node_machine_integer_sign(machine, index) ? BUSTER_NODE_FACTS_SIGNED : 0) |
                     (plain && node_machine_plain_kind(kind) ? BUSTER_NODE_FACTS_SCALAR : 0) | (plain && kind == BUSTER_TYPE_KIND_POINTER ? BUSTER_NODE_FACTS_POINTER : 0) |
                     (plain && type_kind_is_array(kind) ? BUSTER_NODE_FACTS_ARRAY : 0) | (record ? BUSTER_NODE_FACTS_RECORD : 0) |
                     (check_action_is_scalar(types, type) ? BUSTER_NODE_FACTS_TRUTHY : 0);
        const bool arithmetic = index < BUSTER_NODE_MACHINE_KINDS && node_machine_row_kind(index);
        row->flags |= arithmetic ? BUSTER_NODE_FACTS_ARITHMETIC : 0;
        row->flags |= arithmetic && index != type_index(BUSTER_TYPE_FLOAT) && index != type_index(BUSTER_TYPE_DOUBLE) ? BUSTER_NODE_FACTS_INTEGRAL : 0;
        // node_machine_query's type: its size and alignment where it is complete somewhere, with no wrapper.
        const bool sized = plain && kind != BUSTER_TYPE_KIND_FUNCTION && kind != BUSTER_TYPE_KIND_VOID && row->complete_at != UINT32_MAX &&
                           !check_action_runtime_size(types, type);
        side->size = sized ? type_size(types, type) : 0;
        side->alignment = sized ? type_alignment(types, type) : 0;
        row->flags |= sized ? BUSTER_NODE_FACTS_SIZED : 0;
    }
    TypeId function = valid && plain && kind == BUSTER_TYPE_KIND_FUNCTION ? type : 0;
    if (valid && plain && (kind == BUSTER_TYPE_KIND_POINTER || type_kind_is_array(kind)))
    {
        const TypeId element = type_element(types, type);
        const uint32_t element_index = type_index(element);
        const uint32_t element_word = node_machine_word(machine, element);
        row->element = element;
        row->element_word = element_word;
        row->element_complete_at = element_index < machine->type_count ? machine->type_complete_at[element_index] : UINT32_MAX;
        // check_action_pointer_stride's, where a constant index's address may be folded with it.
        const uint8_t element_kind = (uint8_t)(element_word >> 16);
        const bool byte = element_kind == BUSTER_TYPE_KIND_VOID || element_kind == BUSTER_TYPE_KIND_FUNCTION;
        const bool sized = (element_word & BUSTER_NODE_MACHINE_WORD_VALID) != 0 && (byte || (type_is_complete(types, element) && !check_action_runtime_size(types, element)));
        const uint64_t size = sized ? (byte ? 1 : type_size(types, element)) : UINT64_MAX;
        row->stride = size <= UINT32_MAX ? (uint32_t)size : 0;
        row->flags |= size <= UINT32_MAX ? BUSTER_NODE_FACTS_STRIDE : 0;
        const bool complete = row->element_complete_at != UINT32_MAX;
        const bool pointed_record = (element_word & BUSTER_NODE_MACHINE_WORD_PLAIN) != 0 && (element_kind == BUSTER_TYPE_KIND_STRUCT || element_kind == BUSTER_TYPE_KIND_UNION);
        row->flags |= kind == BUSTER_TYPE_KIND_POINTER && pointed_record && complete ? BUSTER_NODE_FACTS_ARROW : 0;
        row->flags |= !byte && complete ? BUSTER_NODE_FACTS_OBJECTS : 0;
        const bool pointed = kind == BUSTER_TYPE_KIND_POINTER && (element_word & (BUSTER_NODE_MACHINE_WORD_VALID | BUSTER_NODE_MACHINE_WORD_PLAIN)) ==
                                                                  (BUSTER_NODE_MACHINE_WORD_VALID | BUSTER_NODE_MACHINE_WORD_PLAIN) &&
                             (uint8_t)(element_word >> 16) == BUSTER_TYPE_KIND_FUNCTION;
        function = pointed ? type_unqualified(element) : 0;
    }
    if (function != 0)
    {
        const uint32_t function_index = type_index(function);
        const TypeId returned = type_return(types, function);
        const uint32_t returned_index = type_index(returned);
        uint32_t parameters = 0;
        const TypeId* parameter_types = type_parameters(types, function, &parameters);
        bool takes = true;
        for (uint32_t p = 0; p < parameters; p += 1)
        {
            const uint32_t word = node_machine_word(machine, parameter_types[p]);
            const uint8_t parameter_kind = (uint8_t)(word >> 16);
            const bool plain_integer = (word & 0x00ffffffu) - ((uint32_t)BUSTER_TYPE_KIND_INTEGER << 16 | BUSTER_TYPED_CLASS_I8) <= BUSTER_TYPED_CLASS_U64 - BUSTER_TYPED_CLASS_I8;
            takes = takes && parameter_kind != BUSTER_TYPE_KIND_FLOATING && (word >> BUSTER_NODE_MACHINE_WORD_TESTS_SHIFT & BUSTER_NODE_FACTS_TESTS_ENUM) == 0 &&
                    (parameter_kind != BUSTER_TYPE_KIND_INTEGER || plain_integer);
        }
        row->returned = returned;
        row->returned_word = node_machine_word(machine, returned);
        row->returned_complete_at = returned_index < machine->type_count ? machine->type_complete_at[returned_index] : UINT32_MAX;
        row->returned_ready_at = (uint8_t)(row->returned_word >> 16) == BUSTER_TYPE_KIND_VOID ? 0 : row->returned_complete_at;
        row->function = BUSTER_NODE_FACTS_FUNCTION | (kind == BUSTER_TYPE_KIND_FUNCTION ? BUSTER_NODE_FACTS_DESIGNATOR : 0) |
                        (row->returned_ready_at != UINT32_MAX ? BUSTER_NODE_FACTS_RETURNS : 0) | (takes ? BUSTER_NODE_FACTS_TAKES : 0) |
                        (uint32_t)types->types.flags[function_index] << 8 | (parameters < 0xffffu ? parameters : 0xffffu) << 16;
    }
}

// The experiment's words and windows for the machine's unit, from `arena`, for at most `capacity` records, a window cut
// at `cut` records (0 for BUSTER_NODE_FACTS_CUT) and holding several bodies' expressions with `packed`, its discards no
// lookups with `lean`, its literals' leaves and other operators made with `operators`: whether there was room. With
// `literal_lines`, the literals' lines are that many of their own, the lines' first numbers (reader time's, a batch's).
static bool node_facts_begin(NodeFacts* facts, NodeMachine* machine, uint32_t capacity, uint32_t cut, bool packed, bool lean, bool operators, uint32_t literal_lines,
                             Arena* arena)
{
    const CheckInput* input = machine->program != NULL ? machine->program->input : machine->input;
    const Parser* parser = machine->program != NULL ? machine->program->parser : machine->parser;
    const uint64_t records = (uint64_t)capacity + 64;
    const uint32_t row_capacity = machine->type_capacity > machine->type_count ? machine->type_capacity : machine->type_count;
    // The members' slots: four a member of the unit's type table, so that the accesses' record types and names, each one
    // slot, leave it under half full but in a unit that reaches most members through several types.
    uint32_t member_bits = BUSTER_NODE_FACTS_MEMBER_LEAST_BITS;
    while (member_bits < BUSTER_NODE_FACTS_MEMBER_MOST_BITS && (UINT64_C(1) << member_bits) < 4 * (uint64_t)machine->types->members.count)
    {
        member_bits += 1;
    }
    *facts = (NodeFacts){
        .machine = machine,
        .input = input,
        .parser = parser,
        .row_count = machine->type_count,
        .row_capacity = row_capacity,
        // A declaration's lines, and with the operators' kernel a line a literal's leaf.
        .line_count = literal_lines,
        .line_capacity = input->token_count / 2 + 64 + (operators ? capacity : 0) + literal_lines,
        .literal_capacity = literal_lines,
        .member_bits = member_bits,
        .record_capacity = capacity,
        .window_body = UINT32_MAX,
        .cut = cut != 0 && cut <= BUSTER_NODE_FACTS_WINDOW ? cut : BUSTER_NODE_FACTS_CUT,
        .packed = packed,
        .lean = lean,
        .operators = operators,
    };
    facts->rows = arena_push_array(arena, (uint64_t)row_capacity + 1, sizeof(NodeFactsRow), 64);
    facts->sides = arena_push_array(arena, (uint64_t)row_capacity + 1, sizeof(NodeFactsSide), 64);
    facts->pairs = arena_push_array(arena, UINT64_C(1) << BUSTER_NODE_FACTS_PAIR_BITS, sizeof(NodeFactsPair), 64);
    facts->lines = arena_push_array(arena, 4 * (uint64_t)facts->line_capacity, sizeof(uint64_t), 64);
    facts->numbers = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)input->token_count + 1);
    facts->members = arena_push_array(arena, UINT64_C(1) << member_bits, sizeof(NodeFactsMember), 64);
    facts->ops = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, records);
    facts->kinds = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, records);
    facts->classes = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, records);
    facts->fits = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, records);
    facts->left = BUSTER_ARENA_PUSH_ARRAY(arena, uint16_t, records);
    facts->right = BUSTER_ARENA_PUSH_ARRAY(arena, uint16_t, records);
    facts->inputs = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, records);
    facts->counts = BUSTER_ARENA_PUSH_ARRAY(arena, uint16_t, records);
    facts->positions = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, records);
    facts->nodes = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, records);
    facts->arguments = BUSTER_ARENA_PUSH_ARRAY(arena, uint16_t, records);
    facts->windows = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, records);
    facts->expected_types = BUSTER_ARENA_PUSH_ARRAY(arena, TypeId, records);
    facts->expected_flags = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, records);
    facts->expected_objects = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, records);
    facts->expected_payloads = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, records);
    facts->expected_standings = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, records);
    facts->expected_exponents = BUSTER_ARENA_PUSH_ARRAY(arena, uint16_t, records);
    facts->expected_words = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, records);
    facts->expected_converted = BUSTER_ARENA_PUSH_ARRAY(arena, TypeId, 2 * records);
    facts->expected_conversion_flags = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, 2 * records);
    facts->expected_member_offsets = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, records);
    facts->records = arena_push_array(arena, BUSTER_NODE_FACTS_WINDOW, sizeof(NodeFactsRecord), 64);
    facts->leaf_list = arena_push_array(arena, BUSTER_NODE_FACTS_WINDOW + 16, sizeof(uint32_t), 64);
    facts->lookup_list = arena_push_array(arena, BUSTER_NODE_FACTS_WINDOW + 16, sizeof(uint32_t), 64);
    facts->keys = arena_push_array(arena, BUSTER_NODE_FACTS_WINDOW + 64, sizeof(uint8_t), 64);
    facts->batches = arena_push_array(arena, (uint64_t)BUSTER_NODE_FACTS_LISTS * BUSTER_NODE_FACTS_WINDOW, sizeof(uint32_t), 64);
    facts->rare = arena_push_array(arena, BUSTER_NODE_FACTS_WINDOW + 16, sizeof(uint32_t), 64);
    facts->room = facts->rows != NULL && facts->lines != NULL && facts->numbers != NULL && facts->members != NULL && facts->ops != NULL && facts->kinds != NULL &&
                  facts->left != NULL && facts->right != NULL && facts->inputs != NULL && facts->counts != NULL && facts->positions != NULL && facts->nodes != NULL &&
                  facts->arguments != NULL && facts->windows != NULL && facts->expected_types != NULL && facts->expected_flags != NULL &&
                  facts->expected_objects != NULL && facts->expected_payloads != NULL && facts->expected_standings != NULL && facts->expected_exponents != NULL &&
                  facts->expected_words != NULL && facts->expected_converted != NULL && facts->expected_conversion_flags != NULL &&
                  facts->expected_member_offsets != NULL && facts->records != NULL && facts->classes != NULL && facts->fits != NULL && facts->leaf_list != NULL &&
                  facts->lookup_list != NULL && facts->keys != NULL && facts->batches != NULL && facts->rare != NULL && facts->sides != NULL && facts->pairs != NULL;
    if (facts->room)
    {
        memset(facts->numbers, 0, ((uint64_t)input->token_count + 1) * sizeof(uint32_t));
        memset(facts->members, 0, (UINT64_C(1) << member_bits) * sizeof(NodeFactsMember));
        memset(facts->rows + facts->row_count, 0, sizeof(NodeFactsRow));
        memset(facts->sides, 0, ((uint64_t)row_capacity + 1) * sizeof(NodeFactsSide));
        memset(facts->pairs, 0, (UINT64_C(1) << BUSTER_NODE_FACTS_PAIR_BITS) * sizeof(NodeFactsPair));
        const uint64_t t0 = __rdtsc();
        for (uint32_t index = 0; index < facts->row_count; index += 1)
        {
            node_facts_row(machine, index, facts->rows + index, facts->sides + index);
        }
        facts->row_ticks = __rdtsc() - t0;
        facts->void_word = node_machine_word(machine, BUSTER_TYPE_VOID);
        for (uint32_t standing = 0; standing < 256; standing += 1)
        {
            const uint32_t category = standing & 0x03u;
            const uint32_t kind = (standing >> 2) & 0x07u;
            const bool whole = (standing & BUSTER_NODE_MACHINE_WIDE) == 0;
            const bool literal = kind == CHECK_CONSTANT_INTEGER || kind == CHECK_CONSTANT_FLOATING;
            facts->stands[standing] = (uint8_t)(
                (whole && category == CHECK_LVALUE && (kind == CHECK_CONSTANT_NONE || kind == CHECK_CONSTANT_ADDRESS) ? BUSTER_NODE_FACTS_STANDS_LVALUE : 0) |
                (whole && category == CHECK_RVALUE && kind == CHECK_CONSTANT_NONE ? BUSTER_NODE_FACTS_STANDS_RVALUE : 0) |
                (whole && category == CHECK_RVALUE && literal ? BUSTER_NODE_FACTS_STANDS_LITERAL : 0) |
                (category == CHECK_RVALUE && kind != CHECK_CONSTANT_NONE ? BUSTER_NODE_FACTS_STANDS_CONSTANT : 0));
        }
        facts->int_word = node_machine_word(machine, BUSTER_TYPE_INT);
        facts->ulong_word = node_machine_word(machine, BUSTER_TYPE_ULONG);
        facts->windows[0] = 0;
    }
    return facts->room;
}

// The rows of the types made since the rows were last made, up to the rows' room: at reader time the walk makes types as
// it reads and T's derivations do (node_machine_settle takes their words into the machine's table first). The row past
// the last, which a type past the rows reads, is empty again.
static void node_facts_settle(NodeFacts* facts)
{
    const NodeMachine* machine = facts->machine;
    const uint32_t count = machine->type_count < facts->row_capacity ? machine->type_count : facts->row_capacity;
    if (count > facts->row_count)
    {
        const uint64_t t0 = __rdtsc();
        for (uint32_t index = facts->row_count; index < count; index += 1)
        {
            memset(facts->sides + index, 0, sizeof(NodeFactsSide));
            node_facts_row(machine, index, facts->rows + index, facts->sides + index);
        }
        facts->row_count = count;
        memset(facts->rows + count, 0, sizeof(NodeFactsRow));
        memset(facts->sides + count, 0, sizeof(NodeFactsSide));
        facts->row_ticks += __rdtsc() - t0;
    }
}

// A miss of the members' slots at slot `at`: the type table's own lookup of member `name` of record type `record`, once a
// record type and name, kept there; with the table half full, no answer, and the access is the rule's.
static __attribute__((noinline, cold)) const NodeFactsMember* node_facts_member_fill(NodeFacts* facts, uint32_t at, TypeId record, uint32_t name)
{
    const NodeMachine* machine = facts->machine;
    const TypeTable* types = machine->types;
    const bool room = facts->member_count < (1u << facts->member_bits) / 2;
    uint64_t offset = 0;
    uint32_t qualifiers = 0;
    const uint32_t member = room ? type_lookup_member(types, record, name, &offset, &qualifiers) : 0;
    const bool found = member != 0 && member != BUSTER_TYPE_MEMBER_UNKNOWN;
    const TypeId type = found ? type_qualify(types, types->members.type[member], qualifiers) : 0;
    const bool flexible = found && type_kind(types, type_strip_aligned(types, type)) == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY;
    NodeFactsMember* slot = room ? facts->members + at : &facts->no_member;
    *slot = (NodeFactsMember){
        .key = (uint64_t)record << 32 | name,
        .type = type,
        .word = found ? node_machine_word(machine, type) : 0,
        .offset = offset,
        .flags = BUSTER_NODE_FACTS_MEMBER_USED | (found ? BUSTER_NODE_FACTS_MEMBER_FOUND : 0) |
                 (found && (types->members.flags[member] & BUSTER_TYPE_MEMBER_BIT_FIELD) != 0 ? BUSTER_NODE_FACTS_MEMBER_BIT_FIELD : 0) |
                 (flexible ? BUSTER_NODE_FACTS_MEMBER_FLEXIBLE : 0),
        .result_flags = flexible ? BUSTER_CHECK_CONST_READ_RESTRICTED : 0,
    };
    facts->member_count += room;
    facts->member_fills += room;
    return slot;
}

// The line of declaration `declaration` as the reader had it at the use `use`, its number (from 1), or 0 where the lines
// are full: the declaration's last line where that is still its line, and else a new one.
static uint32_t node_facts_declaration(NodeFacts* facts, uint32_t declaration, uint32_t use)
{
    const NodeMachine* machine = facts->machine;
    const CheckInput* input = facts->input;
    const Parser* parser = facts->parser;
    const uint64_t t0 = __rdtsc();
    const TypeId type = input->declared_types[use];
    const bool enumerator = input->declaration_kinds[declaration] == C_BINDING_ENUMERATOR;
    const uint32_t object = input->objects[declaration];
    // An enumerator's value: its row of the unit's enumerators, which are in source order; at reader time the reader's
    // own column's.
    const bool listed = facts->enum_values == NULL;
    uint32_t low = 0;
    uint32_t high = enumerator && listed ? input->enum_value_count : 0;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        if (input->enum_values[middle].token < declaration)
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }
    const bool valued = enumerator && (!listed || (low < input->enum_value_count && input->enum_values[low].token == declaration));
    const unsigned __int128 value = !valued ? 0 : listed ? input->enum_values[low].value : facts->enum_values[declaration];
    const bool wide = enumerator && (!valued || (uint64_t)(value >> 64) != 0 || object != 0);
    // An address that may be null: the declaration's weak attribute, or a weak pragma of its name before the use
    // (check_input_address_is_weak's, whose weak names the reader doesn't keep).
    const IdentId name = declaration < parser->tokens.count && parser->tokens.names != NULL ? parser->tokens.names[declaration] : 0;
    const bool weak = check_input_attribute_of(check_input_attributes_at(input, declaration), CHECK_ATTRIBUTE_WEAK) != NULL || check_input_pragma_weak(input, name, use);
    const uint8_t bits = input->declaration_bits[declaration];
    const CheckCategory category = enumerator ? CHECK_RVALUE : type_kind(machine->types, type) == BUSTER_TYPE_KIND_FUNCTION ? CHECK_FUNCTION : CHECK_LVALUE;
    // The line: the leaf's record's first half, a NodeFactsRecord's first four quadwords.
    const NodeFactsRecord made = {
        .type = type,
        .word = node_machine_word(machine, type),
        .flags = (enumerator ? BUSTER_CHECK_ICE : 0) |
                 ((bits & BUSTER_CHECK_BITS_STATIC) != 0 && (bits & BUSTER_CHECK_BITS_THREAD_LOCAL) == 0 ? BUSTER_CHECK_STATIC_OBJECT : 0) |
                 (object != 0 && !enumerator && !weak ? BUSTER_CHECK_NONNULL_ADDRESS : 0),
        .object = enumerator ? 0 : object,
        .payload = enumerator ? (uint64_t)value : 0,
        .standing = BUSTER_NODE_MACHINE_STANDING(category, enumerator ? CHECK_CONSTANT_INTEGER : CHECK_CONSTANT_ADDRESS),
        .taken = 1,
        .exponent = (uint16_t)(enumerator ? node_facts_small((uint64_t)value) : 0),
        .unfit = ((bits & BUSTER_CHECK_BITS_REGISTER) != 0 ? BUSTER_NODE_FACTS_REGISTER : 0) | (wide ? BUSTER_NODE_FACTS_WIDE : 0) |
                 (valued && (uint64_t)(value >> 64) != 0 ? BUSTER_NODE_FACTS_HIGH : 0),
    };
    uint64_t line[4];
    memcpy(line, &made, sizeof(line));
    uint32_t number = facts->numbers[declaration];
    const bool same = number != 0 && memcmp(facts->lines + 4 * (uint64_t)number, line, sizeof(line)) == 0;
    if (!same)
    {
        facts->line_versions += number != 0;
        number = facts->line_count + 1 < facts->line_capacity ? facts->line_count + 1 : 0;
        if (number != 0)
        {
            facts->line_count = number;
            memcpy(facts->lines + 4 * (uint64_t)number, line, sizeof(line));
            facts->numbers[declaration] = number;
        }
        facts->line_ticks += __rdtsc() - t0;
    }
    return number;
}

// An array's or a function's row's pointer types, from the machine's own memo (node_machine_pointer), which T fills as it
// converts one: type_pointer's answers, which a row holds for its type unqualified and const.
static void node_facts_decays(NodeFacts* facts, TypeId type)
{
    const NodeMachine* machine = facts->machine;
    const uint32_t index = type_index(type);
    NodeFactsRow* row = index < facts->row_count ? facts->rows + index : NULL;
    const uint8_t kind = row != NULL ? (uint8_t)(row->word >> 16) : BUSTER_TYPE_KIND_NONE;
    if (row != NULL && (row->word & BUSTER_NODE_MACHINE_WORD_PLAIN) != 0 && (type_kind_is_array(kind) || kind == BUSTER_TYPE_KIND_FUNCTION))
    {
        for (uint32_t qualifiers = 0; qualifiers <= BUSTER_TYPE_CONST; qualifiers += 1)
        {
            // An array's is its element's pointer, the element taking the array's qualifiers; a function's is its own.
            const TypeId pointee = kind == BUSTER_TYPE_KIND_FUNCTION ? type_unqualified(type) : row->element | qualifiers;
            const uint32_t pointee_index = type_index(pointee);
            const uint32_t pointee_qualifiers = type_qualifiers(pointee);
            if (pointee_index < machine->type_count && pointee_qualifiers <= BUSTER_TYPE_CONST)
            {
                const TypeId pointer = (pointee_qualifiers == 0 ? machine->pointers : machine->const_pointers)[pointee_index];
                row->decayed[qualifiers] = pointer != 0 ? pointer : row->decayed[qualifiers];
            }
        }
    }
}

// The pointer types to type `type`, type_pointer's answers as the machine's memo holds them (node_machine_pointer, which
// T fills as it takes an address), into the type's side row, with their words.
static void node_facts_addresses(NodeFacts* facts, TypeId type)
{
    const NodeMachine* machine = facts->machine;
    const uint32_t index = type_index(type);
    if (index < facts->row_count)
    {
        NodeFactsSide* side = facts->sides + index;
        const TypeId pointers[2] = { machine->pointers[index], machine->const_pointers[index] };
        for (uint32_t k = 0; k < 2; k += 1)
        {
            if (pointers[k] != 0)
            {
                side->pointers[k] = pointers[k];
                side->pointer_words[k] = node_machine_word(machine, pointers[k]);
            }
        }
    }
}

// A literal's line, for the machine's record `r`, a leaf that names no declaration: the reader's record of it (T's
// record of the leaf is the reader's), which a compile would write as it reads the literal, a line each. Its number, or
// 0 where the lines are full.
static uint32_t node_facts_literal(NodeFacts* facts, uint32_t r)
{
    const NodeMachineColumns* columns = facts->machine->columns;
    const uint8_t standing = columns->standings[r];
    const NodeFactsRecord made = {
        .type = columns->value_types[r],
        .word = columns->words[r],
        .flags = columns->value_flags[r],
        .object = columns->objects[r],
        .payload = columns->payloads[r],
        .standing = standing,
        .taken = 1,
        .exponent = (uint16_t)node_facts_exponent(standing, columns->payloads[r], columns->exponents[r]),
    };
    const bool own = facts->literal_capacity != 0;
    const uint32_t number = own ? (facts->literal_count < facts->literal_capacity ? facts->literal_count + 1 : 0) :
                                  (facts->line_count + 1 < facts->line_capacity ? facts->line_count + 1 : 0);
    if (number != 0)
    {
        facts->literal_count = own ? number : facts->literal_count;
        facts->line_count = own ? facts->line_count : number;
        memcpy(facts->lines + 4 * (uint64_t)number, &made, 4 * sizeof(uint64_t));
    }
    return number;
}

// The machine's current expression, which T has typed, appended to the window at hand: its records' inputs and T's
// record of each. A new window starts where the expression would take the window past its cut and, unless the windows
// are packed, with a new body (`body`).
static void node_facts_append(NodeFacts* facts, uint32_t body)
{
    const NodeMachine* machine = facts->machine;
    const NodeMachineColumns* columns = machine->columns;
    const CheckInput* input = facts->input;
    const Parser* parser = facts->parser;
    const CheckGraph* graph = machine->graph;
    const uint32_t count = machine->count;
    const uint32_t base = facts->record_count;
    if (facts->room && count <= BUSTER_NODE_FACTS_WINDOW && (uint64_t)base + count <= facts->record_capacity &&
        (uint64_t)facts->argument_count + machine->argument_total <= facts->record_capacity)
    {
        const bool first = facts->window_body == UINT32_MAX;
        const bool full = base - facts->windows[facts->window_count] + count > facts->cut;
        if (first || full || (!facts->packed && body != facts->window_body))
        {
            facts->window_count += !first;
            facts->windows[facts->window_count] = base;
        }
        facts->window_body = body;
        for (uint32_t r = 0; r < count; r += 1)
        {
            const uint32_t w = base + r;
            const uint32_t node = node_machine_node(machine, r);
            const CheckOp op = graph->ops[node];
            const uint32_t a = columns->left[r];
            const uint32_t b = columns->right[r];
            const uint32_t c = op == CHECK_OP_VALUE ? UINT32_MAX : node_machine_operand(machine, r, 2);
            const uint8_t standing = columns->standings[r];
            uint8_t kind = BUSTER_NODE_FACTS_GIVEN;
            uint32_t code = BUSTER_NODE_FACTS_CODES;
            uint32_t entry = 0;
            uint16_t arguments = 0;
            if (op == CHECK_OP_VALUE)
            {
                const uint32_t token = graph->tokens[node];
                const bool named = token < input->token_count && token < parser->tokens.count && parser->tokens.kinds[token] == PARSE_KIND_IDENTIFIER &&
                                   input->declared_types[token] != 0 && input->declarations[token] < input->token_count;
                const bool whole = (standing & BUSTER_NODE_MACHINE_WIDE) == 0;
                const uint32_t number = named && whole ? node_facts_declaration(facts, input->declarations[token], token) : 0;
                // Another leaf with the operators' kernel: its own line, where the columns hold its value whole.
                const uint32_t line = number == 0 && facts->operators && whole ? node_facts_literal(facts, r) : 0;
                kind = number != 0 ? BUSTER_NODE_FACTS_IDENTIFIER : line != 0 ? BUSTER_NODE_FACTS_LITERAL_LINE : BUSTER_NODE_FACTS_LITERAL;
                entry = number != 0 ? number : line;
                facts->literals += number == 0;
                facts->literal_lines += line != 0;
                facts->identifiers += number != 0;
                if (number != 0)
                {
                    const uint32_t flags = columns->value_flags[r];
                    const CheckCategory category = (CheckCategory)(standing & 0x03u);
                    facts->enumerators += (flags & BUSTER_CHECK_ICE) != 0;
                    facts->functions += (flags & BUSTER_CHECK_ICE) == 0 && category == CHECK_FUNCTION;
                    facts->static_objects += (flags & BUSTER_CHECK_ICE) == 0 && category != CHECK_FUNCTION && (flags & BUSTER_CHECK_STATIC_OBJECT) != 0;
                    facts->automatic_objects += (flags & BUSTER_CHECK_ICE) == 0 && category != CHECK_FUNCTION && (flags & BUSTER_CHECK_STATIC_OBJECT) == 0;
                }
            }
            else
            {
                const uint32_t lookup = node_facts_lookup(op);
                const TypeId written = graph->types != NULL ? graph->types[node] : 0;
                const bool discard = op == CHECK_OP_CAST && graph->types != NULL && written == BUSTER_TYPE_VOID;
                bool modelled = lookup < BUSTER_NODE_FACTS_LOOKUPS && (op != CHECK_OP_CAST || discard) && a < r && (b == UINT32_MAX || b < r);
                entry = op == CHECK_OP_ARROW || op == CHECK_OP_MEMBER ? graph->members[node] :
                        op == CHECK_OP_CAST || op == CHECK_OP_CONVERT || op == CHECK_OP_INITIALIZE || op == CHECK_OP_SIZEOF || op == CHECK_OP_ALIGNOF ? written :
                        op == CHECK_OP_CONDITIONAL && c < r ? r - c : 0;
                if (modelled && op == CHECK_OP_CALL)
                {
                    const uint32_t first = columns->argument_first[r];
                    const uint32_t argument_count = columns->argument_counts[r];
                    bool before = argument_count <= UINT16_MAX;
                    for (uint32_t i = 0; before && i < argument_count; i += 1)
                    {
                        before = machine->arguments[first + i] < r;
                    }
                    entry = facts->argument_count;
                    arguments = before ? (uint16_t)argument_count : 0;
                    modelled = before;
                    for (uint32_t i = 0; before && i < argument_count; i += 1)
                    {
                        facts->arguments[facts->argument_count + i] = (uint16_t)(r - machine->arguments[first + i]);
                    }
                    facts->argument_count += before ? argument_count : 0;
                }
                kind = !modelled ? BUSTER_NODE_FACTS_GIVEN : discard && facts->lean ? BUSTER_NODE_FACTS_DISCARD : BUSTER_NODE_FACTS_LOOKUP;
                code = modelled ? node_facts_lookup_codes[lookup] : BUSTER_NODE_FACTS_CODES;
                // Another operator with the operators' kernel, where T's rows and rules would see it: its operands
                // records before it (sizeof and _Alignof of a type name have none), and a third only a conditional's.
                const bool other = lookup == BUSTER_NODE_FACTS_LOOKUPS || (op == CHECK_OP_CAST && !discard);
                const uint32_t operation = facts->operators && other ? node_facts_operator(op) : BUSTER_NODE_FACTS_CODES;
                const bool query = op == CHECK_OP_SIZEOF || op == CHECK_OP_ALIGNOF;
                const bool shaped = operation < BUSTER_NODE_FACTS_CODES && (a < r || (query && a == UINT32_MAX)) && (b == UINT32_MAX || b < r) &&
                                    (op == CHECK_OP_CONDITIONAL ? b < r && c < r : c == UINT32_MAX);
                kind = shaped ? BUSTER_NODE_FACTS_OPERATOR : kind;
                code = shaped ? operation : code;
                facts->lookups[modelled ? lookup : 0] += modelled;
                facts->operator_records[op] += shaped;
                facts->given[op] += kind == BUSTER_NODE_FACTS_GIVEN;
                // The pointer types T interned for its operands' arrays and functions, into their rows, and for an
                // address its operand's, into its side row.
                if (a < r)
                {
                    node_facts_decays(facts, columns->value_types[a]);
                }
                if (b < r)
                {
                    node_facts_decays(facts, columns->value_types[b]);
                }
                if (c < r)
                {
                    node_facts_decays(facts, columns->value_types[c]);
                }
                if (op == CHECK_OP_ADDRESS && a < r)
                {
                    node_facts_addresses(facts, columns->value_types[a]);
                }
            }
            // A made operator's class: its round, after each of its operands (a call's arguments among them), which are
            // before it, and its code.
            const bool made = kind == BUSTER_NODE_FACTS_LOOKUP || kind == BUSTER_NODE_FACTS_OPERATOR;
            uint32_t round = 0;
            if (made)
            {
                round = a < r ? node_facts_round(round, code, facts->classes[w - (r - a)]) : round;
                round = b < r ? node_facts_round(round, code, facts->classes[w - (r - b)]) : round;
                round = c < r ? node_facts_round(round, code, facts->classes[w - (r - c)]) : round;
                for (uint32_t i = 0; i < arguments; i += 1)
                {
                    round = node_facts_round(round, code, facts->classes[w - facts->arguments[entry + i]]);
                }
            }
            facts->classes[w] = made ? node_facts_class(round, code) : BUSTER_NODE_FACTS_NO_CLASS;
            facts->ops[w] = (uint8_t)op;
            facts->kinds[w] = kind;
            facts->fits[w] = 0;
            facts->left[w] = a < r ? (uint16_t)(r - a) : 0;
            facts->right[w] = b < r ? (uint16_t)(r - b) : 0;
            facts->inputs[w] = entry;
            facts->counts[w] = arguments;
            facts->positions[w] = graph->source_positions != NULL ? graph->source_positions[node] : BUSTER_CHECK_NONE;
            facts->nodes[w] = node;
            facts->expected_types[w] = columns->value_types[r];
            facts->expected_flags[w] = columns->value_flags[r];
            facts->expected_objects[w] = columns->objects[r];
            facts->expected_payloads[w] = columns->payloads[r];
            facts->expected_standings[w] = standing;
            facts->expected_exponents[w] = columns->exponents[r];
            facts->expected_words[w] = columns->words[r];
            facts->expected_converted[2 * (uint64_t)w] = columns->converted[2 * r];
            facts->expected_converted[2 * (uint64_t)w + 1] = columns->converted[2 * r + 1];
            facts->expected_conversion_flags[2 * (uint64_t)w] = columns->conversion_flags[2 * r];
            facts->expected_conversion_flags[2 * (uint64_t)w + 1] = columns->conversion_flags[2 * r + 1];
            facts->expected_member_offsets[w] = columns->member_offsets[r];
        }
        facts->record_total += count;
        facts->record_count = base + count;
        facts->windows[facts->window_count + 1] = facts->record_count;
        const uint32_t width = facts->record_count - facts->windows[facts->window_count];
        facts->widest = width > facts->widest ? width : facts->widest;
    }
    else
    {
        facts->room = false;
    }
}

// T's record of the unit's record `at`, as a line: with a floating value's exponent, and else an exponent that says
// none (node_facts_small): T's columns don't hold what a payload says.
static inline NodeFactsRecord node_facts_expected(const NodeFacts* facts, uint64_t at)
{
    const uint8_t standing = facts->expected_standings[at];
    return (NodeFactsRecord){
        .type = facts->expected_types[at],
        .word = facts->expected_words[at],
        .flags = facts->expected_flags[at],
        .object = facts->expected_objects[at],
        .payload = facts->expected_payloads[at],
        .standing = standing,
        .taken = 1,
        .exponent = (standing & BUSTER_NODE_MACHINE_FIELD) == BUSTER_NODE_MACHINE_FLOATING ? facts->expected_exponents[at] : 0,
        .converted = { facts->expected_converted[2 * at], facts->expected_converted[2 * at + 1] },
        .conversion_flags = { facts->expected_conversion_flags[2 * at], facts->expected_conversion_flags[2 * at + 1] },
        .member_offset = facts->expected_member_offsets[at],
    };
}

// Window `window`'s records as the kernel finds them: T's, the given ones'. What the kernel is to make (a leaf with a
// line and a made operator: the kinds with bit 1) is as `poison` says: T's records, which the kernel writes over;
// poisoned whole, with bytes no record has, so that what it doesn't write shows in the comparison; a leaf poisoned whole
// and of an operator only what the loads' kernel writes; or decoys.
static void node_facts_given(NodeFacts* facts, uint32_t window, uint32_t poison)
{
    NodeFactsRecord* records = facts->records;
    const uint32_t first = facts->windows[window];
    const uint32_t count = facts->windows[window + 1] - first;
    for (uint32_t i = 0; i < count; i += 1)
    {
        const uint8_t kind = facts->kinds[first + i];
        const bool made = (kind & 0x02u) != 0;
        if (made && (poison == BUSTER_NODE_FACTS_POISON_RECORDS || (poison == BUSTER_NODE_FACTS_POISON_LOADS && (kind & 0x03u) == 0x02u)))
        {
            memset(records + i, BUSTER_NODE_FACTS_POISON, sizeof(NodeFactsRecord));
        }
        else
        {
            records[i] = node_facts_expected(facts, (uint64_t)first + i);
            if (made && poison == BUSTER_NODE_FACTS_POISON_LOADS)
            {
                memset(&records[i].type, BUSTER_NODE_FACTS_POISON, sizeof(records[i].type));
                memset(&records[i].word, BUSTER_NODE_FACTS_POISON, sizeof(records[i].word));
                memset(&records[i].member_offset, BUSTER_NODE_FACTS_POISON, sizeof(records[i].member_offset));
            }
            else if (made && poison == BUSTER_NODE_FACTS_POISON_DECOY)
            {
                records[i].flags ^= BUSTER_CHECK_SIDE_EFFECT;
            }
        }
    }
}

// What the kernel reads of a window beside its records: the window's records' inputs, from the window's first; the
// unit's call arguments; and the words. A local of node_facts_window's, so that the kernel's stores change none of it.
typedef struct NodeFactsView NodeFactsView;
struct NodeFactsView
{
    const uint32_t* inputs;
    const uint16_t* counts;
    const uint32_t* positions;
    const uint16_t* arguments;
    const uint64_t* lines;
    const NodeFactsRow* rows;
    NodeFactsMember* members;
    const NodeFactsSide* sides;
    const NodeFactsPair* pairs;
    const uint8_t* stands;
    uint32_t row_count;
    uint32_t member_bits;
    uint32_t void_word;
    uint32_t int_word;
    uint32_t ulong_word;
    // An integer constant expression's forms that make a zero a null pointer constant (node_machine_null's).
    uint32_t null_forms;
};

// A quadword of record `record`, the one at byte `offset`: a record's fields are written two a store, to their one line.
static inline __attribute__((always_inline)) void node_facts_store(NodeFactsRecord* record, uint32_t offset, uint64_t value)
{
    memcpy((uint8_t*)record + offset, &value, sizeof(value));
}

// Record `w`'s value, node_machine_record's columns, with its type's word: its first four quadwords, the last its
// standing, that a short path made it (BUSTER_NODE_FACTS_SHORT in its `taken`; node_facts_slow marks its own) and no
// exponent.
static inline __attribute__((always_inline)) void node_facts_record(NodeFactsRecord* records, uint32_t w, TypeId type, uint32_t word, uint8_t standing, uint32_t flags,
                                                                     uint32_t object, uint64_t payload)
{
    NodeFactsRecord* record = records + w;
    node_facts_store(record, 0, (uint64_t)type | (uint64_t)word << 32);
    node_facts_store(record, 8, (uint64_t)flags | (uint64_t)object << 32);
    node_facts_store(record, 16, payload);
    node_facts_store(record, 24, (uint64_t)standing | 0x100u);
}

// The same of a record that may be an integer constant (a fold's, a truth's, a size's): its exponent says its payload
// (node_facts_small).
static inline __attribute__((always_inline)) void node_facts_record_integer(NodeFactsRecord* records, uint32_t w, TypeId type, uint32_t word, uint8_t standing, uint32_t flags,
                                                                             uint32_t object, uint64_t payload)
{
    NodeFactsRecord* record = records + w;
    node_facts_store(record, 0, (uint64_t)type | (uint64_t)word << 32);
    node_facts_store(record, 8, (uint64_t)flags | (uint64_t)object << 32);
    node_facts_store(record, 16, payload);
    node_facts_store(record, 24, (uint64_t)standing | 0x100u | (uint64_t)node_facts_small(payload) << 16);
}

static inline __attribute__((always_inline)) void node_facts_conversions(NodeFactsRecord* records, uint32_t w, TypeId first, uint32_t first_flags, TypeId second,
                                                                          uint32_t second_flags, uint64_t member_offset)
{
    NodeFactsRecord* record = records + w;
    node_facts_store(record, 32, (uint64_t)first | (uint64_t)second << 32);
    node_facts_store(record, 40, (uint64_t)first_flags | (uint64_t)second_flags << 32);
    node_facts_store(record, 48, member_offset);
}

// An identifier's leaf `w`, node_machine_leaf's record of it, from its declaration's line: the line is the record's
// first half, and the second is its type alone (its own conversion's entry), which a lean kernel leaves out. One load
// of a vector and one store; a host without the vectors copies the line and stores the rest. Whether the line gives it
// (no register object's, whose flags follow its use, and no enumerator's value the line doesn't hold).
static inline __attribute__((always_inline)) bool node_facts_leaf(NodeFactsRecord* records, const NodeFactsView* view, uint32_t w, bool lean)
{
    const uint64_t* line = view->lines + 4 * (uint64_t)view->inputs[w];
    const bool taken = (uint32_t)(line[3] >> 32) == 0;
    const __m256i half = _mm256_loadu_si256((const __m256i*)(const void*)line);
    if (taken && lean)
    {
        _mm256_storeu_si256((__m256i*)(void*)(records + w), half);
    }
    else if (taken)
    {
        _mm512_storeu_si512(records + w, _mm512_inserti64x4(_mm512_castsi256_si512(half), _mm256_zextsi128_si256(_mm_cvtsi32_si128((int)(uint32_t)line[0])), 1));
    }
    return taken;
}

static inline __attribute__((always_inline)) const NodeFactsRow* node_facts_row_of(const NodeFactsView* view, TypeId type)
{
    return view->rows + (type_index(type) < view->row_count ? type_index(type) : view->row_count);
}

// Record `a`'s value as an operator's operand reads it, node_machine_operand_value's, from the kernel's records and the
// operand's type's row `row`: its lvalue's value type, an array's or a function's pointer. Whether the common cases took
// it, with a type that has no wrapper and a value the columns hold whole (the rule's otherwise).
static inline __attribute__((always_inline)) bool node_facts_operand(const NodeFactsRecord* records, const NodeFactsRow* row, uint32_t a, NodeFactsOperand* value,
                                                                      TypeId* converted, uint32_t* conversion_flags)
{
    const uint8_t standing = records[a].standing;
    const TypeId type = records[a].type;
    const uint32_t word = records[a].word;
    uint8_t kind = (uint8_t)(word >> 16);
    uint8_t category = standing & 0x03u;
    uint8_t constant_kind = (standing >> 2) & 0x07u;
    uint32_t flags = records[a].flags;
    TypeId result = type;
    bool taken = (word & BUSTER_NODE_MACHINE_WORD_PLAIN) != 0 && (standing & BUSTER_NODE_MACHINE_WIDE) == 0;
    *converted = type;
    *conversion_flags = 0;
    if (type_kind_is_array(kind))
    {
        const uint32_t qualifiers = type_qualifiers(type);
        const TypeId pointer = (flags & BUSTER_CHECK_REGISTER_NAME) == 0 && qualifiers <= BUSTER_TYPE_CONST ? row->decayed[qualifiers] : 0;
        taken = taken && pointer != 0;
        result = pointer;
        category = CHECK_RVALUE;
        flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM | BUSTER_CHECK_FLOAT_LITERAL | BUSTER_CHECK_BIT_FIELD | BUSTER_CHECK_REGISTER | BUSTER_CHECK_REGISTER_NAME);
        constant_kind = (flags & BUSTER_CHECK_STATIC_OBJECT) == 0 && constant_kind == CHECK_CONSTANT_ADDRESS ? CHECK_CONSTANT_NONE : constant_kind;
        kind = BUSTER_TYPE_KIND_POINTER;
        *converted = pointer;
        *conversion_flags = BUSTER_CHECK_CONVERT_ARRAY;
    }
    else if (kind == BUSTER_TYPE_KIND_FUNCTION)
    {
        const TypeId pointer = row->decayed[0];
        taken = taken && pointer != 0;
        result = pointer;
        category = CHECK_RVALUE;
        flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM | BUSTER_CHECK_FLOAT_LITERAL);
        kind = BUSTER_TYPE_KIND_POINTER;
        *converted = pointer;
        *conversion_flags = BUSTER_CHECK_CONVERT_FUNCTION;
    }
    else if (category == CHECK_LVALUE)
    {
        taken = taken && node_machine_plain_kind(kind) && (type_qualifiers(type) & BUSTER_TYPE_VOLATILE) == 0 && (flags & BUSTER_CHECK_VOLATILE_PART) == 0;
        constant_kind = constant_kind == CHECK_CONSTANT_ADDRESS || (flags & BUSTER_CHECK_ICE) == 0 ? CHECK_CONSTANT_NONE : constant_kind;
        result = row->value;
        category = CHECK_RVALUE;
        flags &= ~(BUSTER_CHECK_REGISTER | BUSTER_CHECK_REGISTER_NAME | BUSTER_CHECK_VOLATILE_PART);
        *converted = result;
        *conversion_flags = BUSTER_CHECK_CONVERT_LVALUE;
    }
    *value = (NodeFactsOperand){
        .type = result,
        .flags = flags,
        .integer = (standing & BUSTER_NODE_MACHINE_FIELD) == 0 ? records[a].payload : 0,
        .category = category,
        .constant_kind = constant_kind,
        .kind = kind,
    };
    return taken;
}

// What record `a`'s pointer, as node_facts_operand converted it, points at: its row's element, an array's with the array's
// qualifiers (type_element's).
static inline __attribute__((always_inline)) TypeId node_facts_pointee(const NodeFactsRecord* records, uint32_t a, const NodeFactsRow* row)
{
    return row->element | (type_kind_is_array((uint8_t)(records[a].word >> 16)) ? type_qualifiers(records[a].type) : 0);
}

// A type's word as T's records have it (node_machine_word): its row's, and no class or fact for a type past the rows.
static inline __attribute__((always_inline)) uint32_t node_facts_word_of(const NodeFactsView* view, TypeId type)
{
    return type_index(type) < view->row_count ? view->rows[type_index(type)].word : UINT8_MAX;
}

// The word of record `a`'s value's type as node_facts_operand converted it (`operand`): the record's own, an lvalue's
// value type having its type's row, or the pointer's that an array or a function decayed to.
static inline __attribute__((always_inline)) uint32_t node_facts_operand_word(const NodeFactsRecord* records, const NodeFactsView* view, uint32_t a,
                                                                              const NodeFactsOperand* operand)
{
    const uint8_t kind = (uint8_t)(records[a].word >> 16);
    return type_kind_is_array(kind) || kind == BUSTER_TYPE_KIND_FUNCTION ? node_facts_word_of(view, operand->type) : records[a].word;
}

// check_value_source_flags of an operator that is no leaf: a literal zero's spelling is its leaf's alone.
static inline __attribute__((always_inline)) uint32_t node_facts_source(uint32_t flags)
{
    return flags & ~(uint32_t)BUSTER_CHECK_LITERAL_ZERO;
}

// The member `name` of record type `record`, its slot: type_lookup_member's answer, kept the first time it is asked for.
static inline __attribute__((always_inline)) const NodeFactsMember* node_facts_member_slot(NodeFacts* facts, const NodeFactsView* view, TypeId record, uint32_t name)
{
    const NodeFactsMember* members = view->members;
    const uint64_t key = (uint64_t)record << 32 | name;
    uint32_t at = (uint32_t)((key * UINT64_C(0x9e3779b97f4a7c15)) >> (64 - view->member_bits));
    const NodeFactsMember* slot = members + at;
    while (slot->key != key && slot->flags != 0)
    {
        at = (at + 1) & ((1u << view->member_bits) - 1);
        slot = members + at;
    }
    if (__builtin_expect(slot->key != key, 0))
    {
        slot = node_facts_member_fill(facts, at, record, name);
    }
    return slot;
}

// The same slot for a short path, which fills none: the slot where the member's answer is kept, or one that holds no
// answer, which sends the access to its rule (and the rule fills the slot). No call is left in a short path's loop.
static const NodeFactsMember node_facts_no_member;

static inline __attribute__((always_inline)) const NodeFactsMember* node_facts_member_kept(const NodeFactsView* view, TypeId record, uint32_t name)
{
    const NodeFactsMember* members = view->members;
    const uint64_t key = (uint64_t)record << 32 | name;
    uint32_t at = (uint32_t)((key * UINT64_C(0x9e3779b97f4a7c15)) >> (64 - view->member_bits));
    const NodeFactsMember* slot = members + at;
    while (slot->key != key && slot->flags != 0)
    {
        at = (at + 1) & ((1u << view->member_bits) - 1);
        slot = members + at;
    }
    return slot->key == key ? slot : &node_facts_no_member;
}

// A member access `w` of record `a`, node_machine_member's steps: `->` of a pointer to a struct or union that has no
// wrapper, `.` of such a record itself, complete at the node, of a member that is no bit-field.
static inline __attribute__((always_inline)) bool node_facts_member(NodeFacts* facts, NodeFactsRecord* records, const NodeFactsView* view, uint32_t w, uint32_t a,
                                                                     bool arrow)
{
    const TypeId base_type = records[a].type;
    const uint8_t base_standing = records[a].standing;
    const NodeFactsRow* row = node_facts_row_of(view, base_type);
    NodeFactsOperand base = {
        .type = base_type,
        .flags = records[a].flags,
        .category = base_standing & 0x03u,
        .constant_kind = (base_standing >> 2) & 0x07u,
        .kind = (uint8_t)(records[a].word >> 16),
    };
    TypeId converted = base_type;
    uint32_t conversion_flags = 0;
    bool taken = (arrow ? node_facts_operand(records, row, a, &base, &converted, &conversion_flags) : (records[a].word & BUSTER_NODE_MACHINE_WORD_PLAIN) != 0) &&
                 (base_standing & BUSTER_NODE_MACHINE_WIDE) == 0;
    // The record: a pointer's pointee, or the base itself; its word says whether it is a struct or a union with no wrapper.
    const TypeId record = arrow ? node_facts_pointee(records, a, row) : base_type;
    const uint32_t record_word = arrow ? row->element_word : row->word;
    const uint32_t complete_at = arrow ? row->element_complete_at : row->complete_at;
    const uint8_t record_kind = (uint8_t)(record_word >> 16);
    const bool address = !arrow && base.constant_kind == CHECK_CONSTANT_ADDRESS;
    taken = taken && (!arrow || base.kind == BUSTER_TYPE_KIND_POINTER) && (record_word & BUSTER_NODE_MACHINE_WORD_PLAIN) != 0 &&
            (record_kind == BUSTER_TYPE_KIND_STRUCT || record_kind == BUSTER_TYPE_KIND_UNION) && (base.constant_kind == CHECK_CONSTANT_NONE || address) &&
            complete_at != UINT32_MAX && complete_at <= view->positions[w];
    if (taken)
    {
        const NodeFactsMember* slot = node_facts_member_slot(facts, view, record, view->inputs[w]);
        const uint64_t offset = slot->offset;
        const int64_t base_offset = (base_standing & BUSTER_NODE_MACHINE_FIELD) == BUSTER_NODE_MACHINE_OFFSET ? (int64_t)records[a].payload : 0;
        int64_t addend = 0;
        taken = (slot->flags & (BUSTER_NODE_FACTS_MEMBER_FOUND | BUSTER_NODE_FACTS_MEMBER_BIT_FIELD)) == BUSTER_NODE_FACTS_MEMBER_FOUND &&
                (!address || (offset % 8 == 0 && offset / 8 <= INT64_MAX && !__builtin_add_overflow(base_offset, (int64_t)(offset / 8), &addend)));
        if (taken)
        {
            uint32_t flags = (base.flags & BUSTER_CHECK_INHERITED) | (base.flags & (BUSTER_CHECK_STATIC_OBJECT | BUSTER_CHECK_CONST_READ_RESTRICTED | BUSTER_CHECK_NONNULL_ADDRESS));
            flags |= (slot->flags & BUSTER_NODE_FACTS_MEMBER_FLEXIBLE) != 0 ? BUSTER_CHECK_CONST_READ_RESTRICTED : 0;
            flags |= arrow && (base.flags & BUSTER_CHECK_INACTIVE_UNION_ADDRESS) != 0 ? BUSTER_CHECK_INACTIVE_UNION_READ : 0;
            flags |= !arrow ? base.flags & BUSTER_CHECK_REGISTER : 0;
            const CheckCategory category = arrow || base.category == CHECK_LVALUE ? CHECK_LVALUE : CHECK_RVALUE;
            const uint8_t standing = BUSTER_NODE_MACHINE_STANDING(category, address ? CHECK_CONSTANT_ADDRESS : CHECK_CONSTANT_NONE) |
                                     (addend != 0 ? BUSTER_NODE_MACHINE_OFFSET : 0);
            node_facts_record(records, w, slot->type, slot->word, standing, check_value_source_flags(arrow ? CHECK_OP_ARROW : CHECK_OP_MEMBER, flags),
                              address ? records[a].object : 0, (uint64_t)addend);
            node_facts_conversions(records, w, converted, conversion_flags, 0, 0, offset);
        }
    }
    return taken;
}

// Record `w`'s value: record `a`'s with its type, category, flags and constant kind replaced, node_machine_record_from's
// with no integer replaced, of a record the columns hold whole.
static inline __attribute__((always_inline)) void node_facts_from(NodeFactsRecord* records, uint32_t w, uint32_t a, TypeId type, uint32_t word, CheckCategory category,
                                                                   uint32_t flags, uint8_t constant_kind)
{
    const uint8_t standing = BUSTER_NODE_MACHINE_STANDING(category, constant_kind) | (records[a].standing & BUSTER_NODE_MACHINE_FIELD);
    NodeFactsRecord* record = records + w;
    node_facts_store(record, 0, (uint64_t)type | (uint64_t)word << 32);
    node_facts_store(record, 8, (uint64_t)flags | (uint64_t)records[a].object << 32);
    node_facts_store(record, 16, records[a].payload);
    node_facts_store(record, 24, (uint64_t)standing | 0x100u | (uint64_t)records[a].exponent << 16);
}

// Record `w`'s value: record `a`'s object with the type, standing (its category, constant kind and payload's field),
// flags and payload given: node_machine_record_from's with an integer replaced, and a value's that
// node_machine_put_value writes from record `a`'s made whole. Its exponent is record `a`'s where the payload is a
// floating value's, and else says the payload given (node_facts_small).
static inline __attribute__((always_inline)) void node_facts_from_with(NodeFactsRecord* records, uint32_t w, uint32_t a, TypeId type, uint32_t word, uint8_t standing,
                                                                        uint32_t flags, uint64_t payload)
{
    NodeFactsRecord* record = records + w;
    node_facts_store(record, 0, (uint64_t)type | (uint64_t)word << 32);
    node_facts_store(record, 8, (uint64_t)flags | (uint64_t)records[a].object << 32);
    node_facts_store(record, 16, payload);
    node_facts_store(record, 24, (uint64_t)standing | 0x100u | (uint64_t)node_facts_exponent(standing, payload, records[a].exponent) << 16);
}

// A dereference `w` of record `a`, node_machine_dereference's steps.
static inline __attribute__((always_inline)) bool node_facts_dereference(NodeFactsRecord* records, const NodeFactsView* view, uint32_t w, uint32_t a)
{
    const NodeFactsRow* row = node_facts_row_of(view, records[a].type);
    NodeFactsOperand pointer;
    TypeId converted = 0;
    uint32_t conversion_flags = 0;
    const bool taken = node_facts_operand(records, row, a, &pointer, &converted, &conversion_flags) && pointer.kind == BUSTER_TYPE_KIND_POINTER;
    if (taken)
    {
        const TypeId type = node_facts_pointee(records, a, row);
        const CheckCategory category = (uint8_t)(row->element_word >> 16) == BUSTER_TYPE_KIND_FUNCTION ? CHECK_FUNCTION : CHECK_LVALUE;
        uint32_t flags = pointer.flags;
        flags |= (flags & BUSTER_CHECK_INACTIVE_UNION_ADDRESS) != 0 ? BUSTER_CHECK_INACTIVE_UNION_READ : 0;
        flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM | BUSTER_CHECK_NULL_POINTER | BUSTER_CHECK_FLOAT_LITERAL | BUSTER_CHECK_ICE_UNEVALUATED);
        node_facts_from(records, w, a, type, row->element_word, category, check_value_source_flags(CHECK_OP_DEREFERENCE, flags), pointer.constant_kind);
        node_facts_conversions(records, w, converted, conversion_flags, 0, 0, 0);
    }
    return taken;
}

// A subscript, or a pointer plus or minus an integer (`op`), `w` of records `a` and `b`, node_machine_pointer_rule's
// steps: a pointer to a complete object type and an integer, the pointer no constant or an address constant.
static inline __attribute__((always_inline)) bool node_facts_pointer(NodeFactsRecord* records, const NodeFactsView* view, uint32_t w, uint32_t a, uint32_t b, CheckOp op)
{
    const bool subscript = op == CHECK_OP_SUBSCRIPT;
    const NodeFactsRow* rows[2] = { node_facts_row_of(view, records[a].type), node_facts_row_of(view, records[b].type) };
    // Zeroed: a first operand that isn't taken leaves the second unread, and the steps below read both before `taken`
    // decides.
    NodeFactsOperand operands[2] = { 0 };
    TypeId converted[2] = { 0, 0 };
    uint32_t conversion_flags[2] = { 0, 0 };
    bool taken = node_facts_operand(records, rows[0], a, operands, converted, conversion_flags) &&
                 node_facts_operand(records, rows[1], b, operands + 1, converted + 1, conversion_flags + 1);
    const bool left_pointer = taken && operands[0].kind == BUSTER_TYPE_KIND_POINTER;
    const bool right_pointer = taken && operands[1].kind == BUSTER_TYPE_KIND_POINTER;
    const bool reverse = !left_pointer && right_pointer && op != CHECK_OP_SUBTRACT;
    const NodeFactsOperand* pointer = operands + (reverse ? 1 : 0);
    const NodeFactsOperand* index = operands + (reverse ? 0 : 1);
    const uint32_t slot = reverse ? 0 : 1;
    const uint32_t raw = reverse ? b : a;
    const NodeFactsRow* row = rows[reverse ? 1 : 0];
    const NodeFactsRow* index_row = rows[reverse ? 0 : 1];
    const uint8_t element_kind = (uint8_t)(row->element_word >> 16);
    // An address constant's element: at an index that has no constant, or at an integer constant's of a builtin type,
    // unqualified, of at most 64 bits (node_machine_operand_width's), where the row has the element's size.
    const uint32_t width = (index->type & BUSTER_TYPE_QUALIFIERS) == 0 ? index_row->flags >> 8 & 0xffu : 0;
    const bool address = pointer->constant_kind == CHECK_CONSTANT_ADDRESS &&
                         (index->constant_kind == CHECK_CONSTANT_NONE || (index->constant_kind == CHECK_CONSTANT_INTEGER && width != 0));
    const bool constant = address && index->constant_kind == CHECK_CONSTANT_INTEGER;
    taken = taken && left_pointer != right_pointer && (left_pointer || reverse) && (index_row->flags & BUSTER_NODE_FACTS_INTEGER) != 0 &&
            (index->flags & BUSTER_CHECK_BIT_FIELD) == 0 && (pointer->constant_kind == CHECK_CONSTANT_NONE || address) &&
            (!constant || (row->flags & BUSTER_NODE_FACTS_STRIDE) != 0) && element_kind != BUSTER_TYPE_KIND_VOID && element_kind != BUSTER_TYPE_KIND_FUNCTION &&
            row->element_complete_at != UINT32_MAX && row->element_complete_at <= view->positions[w];
    if (taken)
    {
        // The index's promotion, check_action_promote's conversion.
        const TypeId promoted = index_row->promoted;
        conversion_flags[slot] |= promoted != index->type ? BUSTER_CHECK_CONVERT_PROMOTION : 0;
        converted[slot] = promoted != index->type ? promoted : converted[slot];
        uint32_t flags = (operands[0].flags | operands[1].flags) & BUSTER_CHECK_INHERITED;
        flags |= pointer->flags & (BUSTER_CHECK_STATIC_OBJECT | BUSTER_CHECK_INACTIVE_UNION_ADDRESS | BUSTER_CHECK_CONST_READ_RESTRICTED | BUSTER_CHECK_NONNULL_ADDRESS);
        flags |= subscript && (pointer->flags & BUSTER_CHECK_INACTIVE_UNION_ADDRESS) != 0 ? BUSTER_CHECK_INACTIVE_UNION_READ : 0;
        flags |= subscript && (records[raw].flags & (BUSTER_CHECK_REGISTER | BUSTER_CHECK_REGISTER_NAME)) == BUSTER_CHECK_REGISTER &&
                 type_kind_is_array((uint8_t)(records[raw].word >> 16)) ? BUSTER_CHECK_REGISTER : 0;
        // A constant index's element: the address constant's object, its offset moved by the index's elements.
        uint64_t addend = 0;
        if (constant)
        {
            const uint64_t mask = width == 64 ? UINT64_MAX : (UINT64_C(1) << width) - 1;
            const uint64_t stored = index->integer & mask;
            const bool negative = (index_row->flags & BUSTER_NODE_FACTS_SIGNED) != 0 && width < 64 && ((stored >> (width - 1)) & 1) != 0;
            const uint64_t offset = (records[raw].standing & BUSTER_NODE_MACHINE_FIELD) == BUSTER_NODE_MACHINE_OFFSET ? records[raw].payload : 0;
            const uint64_t delta = (negative ? stored | ~mask : stored) * row->stride;
            addend = op == CHECK_OP_SUBTRACT ? offset - delta : offset + delta;
        }
        // A subscript's element, an lvalue; or the pointer itself, moved.
        const TypeId type = subscript ? node_facts_pointee(records, raw, row) : pointer->type;
        const uint32_t word = subscript ? row->element_word : node_facts_operand_word(records, view, raw, pointer);
        node_facts_record(records, w, type, word,
                          BUSTER_NODE_MACHINE_STANDING(subscript ? CHECK_LVALUE : CHECK_RVALUE, constant ? CHECK_CONSTANT_ADDRESS : CHECK_CONSTANT_NONE) |
                              (addend != 0 ? BUSTER_NODE_MACHINE_OFFSET : 0),
                          node_facts_source(flags), constant ? records[raw].object : 0, addend);
        node_facts_conversions(records, w, converted[0], conversion_flags[0], converted[1], conversion_flags[1], 0);
    }
    return taken;
}

// What the census's tests on the window ask of a call's arguments (c_check_node_census.c's calls' rule), gathered where
// the call's short path reads each one's record: the kinds of constant among them, a bit each; whether an integer constant
// among them is not zero or 1 to 126, by what its record says of its value (node_facts_small); and their types' words
// together, for an aggregate's bit.
typedef struct NodeFactsArguments NodeFactsArguments;
struct NodeFactsArguments
{
    uint32_t kinds;
    uint32_t large;
    uint32_t words;
};

static inline __attribute__((always_inline)) void node_facts_argument(NodeFactsArguments* asked, const NodeFactsRecord* argument)
{
    const uint32_t kind = ((uint32_t)argument->standing >> 2) & 0x07u;
    asked->kinds |= 1u << kind;
    asked->large |= (kind == CHECK_CONSTANT_INTEGER) & ((uint32_t)argument->exponent - 1u > 126u);
    asked->words |= argument->word;
}

// Whether a call's arguments are quiet (BUSTER_NODE_FACTS_QUIET_CALL), of a callee whose type's row has `function`: no
// aggregate among them, no constant but an address's or an integer's of zero or 1 to 126, and such an integer only
// where each parameter takes it unchanged.
static inline __attribute__((always_inline)) uint32_t node_facts_arguments_quiet(const NodeFactsArguments* asked, uint32_t function)
{
    const uint32_t quiet_kinds = 1u << CHECK_CONSTANT_NONE | 1u << CHECK_CONSTANT_INTEGER | 1u << CHECK_CONSTANT_ADDRESS;
    const uint32_t loud = (asked->words >> BUSTER_NODE_MACHINE_WORD_TESTS_SHIFT & BUSTER_NODE_FACTS_TESTS_AGGREGATE) | (asked->kinds & ~quiet_kinds) | asked->large;
    const bool takes = (asked->kinds & 1u << CHECK_CONSTANT_INTEGER) == 0 || (function & BUSTER_NODE_FACTS_TAKES) != 0;
    return loud == 0 && takes ? BUSTER_NODE_FACTS_QUIET_CALL : 0;
}

// A call `w` of callee `a`: node_machine_call_rule's value, its callee's prototype's return type's rvalue with its
// callee's and its arguments' inherited flags, where the prototype takes the arguments' count and no argument's value is
// wide. Its arguments' conversions are the rule's. It says nothing of its arguments to the census's tests, as the short
// path does (node_facts_call_fast): a call it makes stays in their list.
static inline __attribute__((always_inline)) bool node_facts_call(NodeFactsRecord* records, const NodeFactsView* view, uint32_t w, uint32_t a)
{
    const NodeFactsRow* row = node_facts_row_of(view, records[a].type);
    const uint32_t function = row->function;
    const uint32_t parameters = function >> 16;
    const uint32_t count = view->counts[w];
    const uint16_t* arguments = view->arguments + view->inputs[w];
    NodeFactsOperand callee;
    TypeId converted = 0;
    uint32_t conversion_flags = 0;
    bool taken = (function & BUSTER_NODE_FACTS_FUNCTION) != 0 && node_facts_operand(records, row, a, &callee, &converted, &conversion_flags) &&
                 (function >> 8 & BUSTER_TYPE_FLAG_PROTOTYPE) != 0 && count >= parameters && ((function >> 8 & BUSTER_TYPE_FLAG_VARIADIC) != 0 || count == parameters) &&
                 ((uint8_t)(row->returned_word >> 16) == BUSTER_TYPE_KIND_VOID ||
                  (row->returned_complete_at != UINT32_MAX && row->returned_complete_at <= view->positions[w]));
    uint32_t flags = taken ? BUSTER_CHECK_SIDE_EFFECT | (callee.flags & BUSTER_CHECK_INHERITED) : 0;
#pragma clang loop vectorize(disable) interleave(disable) unroll(disable)
    for (uint32_t i = 0; taken && i < count; i += 1)
    {
        const uint32_t argument = w - arguments[i];
        taken = (records[argument].standing & BUSTER_NODE_MACHINE_WIDE) == 0;
        flags |= records[argument].flags & BUSTER_CHECK_INHERITED;
    }
    if (taken)
    {
        node_facts_record(records, w, row->returned, row->returned_word, BUSTER_NODE_MACHINE_STANDING(CHECK_RVALUE, CHECK_CONSTANT_NONE),
                          check_value_source_flags(CHECK_OP_CALL, flags), 0, 0);
        node_facts_conversions(records, w, converted, conversion_flags, 0, 0, 0);
    }
    return taken;
}

// A discard `w` of record `a`, node_machine_discard's steps on the columns: a cast to void of an rvalue the columns hold
// whole, of no array, function, vector or complex type and no _BitInt constant, in a unit with no overload candidates
// (`plain`).
static inline __attribute__((always_inline)) bool node_facts_discard(NodeFactsRecord* records, const NodeFactsView* view, bool plain, uint32_t w, uint32_t a)
{
    const uint8_t standing = records[a].standing;
    const uint8_t kind = (uint8_t)(records[a].word >> 16);
    const uint8_t constant_kind = (standing >> 2) & 0x07u;
    const bool taken = (standing & BUSTER_NODE_MACHINE_WIDE) == 0 && plain && (standing & 0x03u) == CHECK_RVALUE && !type_kind_is_array(kind) &&
                       kind != BUSTER_TYPE_KIND_FUNCTION && kind != BUSTER_TYPE_KIND_VECTOR && kind != BUSTER_TYPE_KIND_EXT_VECTOR && kind != BUSTER_TYPE_KIND_COMPLEX &&
                       (kind != BUSTER_TYPE_KIND_BIT_INT || constant_kind == CHECK_CONSTANT_NONE);
    if (taken)
    {
        const uint32_t restricted = kind == BUSTER_TYPE_KIND_POINTER ? BUSTER_CHECK_CONST_READ_RESTRICTED : 0;
        const uint32_t dropped = BUSTER_CHECK_BIT_FIELD | BUSTER_CHECK_REGISTER | BUSTER_CHECK_REGISTER_NAME | BUSTER_CHECK_NULL_POINTER | BUSTER_CHECK_ICE |
                                 BUSTER_CHECK_GNU_ICE_FORM | BUSTER_CHECK_FLOAT_LITERAL | BUSTER_CHECK_ICE_UNEVALUATED | BUSTER_CHECK_MODIFICATION |
                                 BUSTER_CHECK_LITERAL_ZERO;
        node_facts_from(records, w, a, BUSTER_TYPE_VOID, view->void_word, CHECK_RVALUE, (records[a].flags | restricted) & ~dropped, CHECK_CONSTANT_NONE);
        node_facts_conversions(records, w, BUSTER_TYPE_VOID, BUSTER_CHECK_CONVERT_CAST, 0, 0, 0);
    }
    return taken;
}

// The operators' kernel's rules (docs/one-core-path.md, "Step 2's first piece"): T's own rows' and rules' steps on the
// kernel's records, each query of the type table a field of a row, a side row or a slot. A rule takes only what T's
// takes, and of that what the words cover (a type with no wrapper, a value the record holds whole), with T's record;
// with `lean` it writes no conversion's entry.

_Static_assert(BUSTER_NODE_MACHINE_ROW_LVALUE(0) == 0x01u && BUSTER_NODE_MACHINE_ROW_CONSTANT(0) == 0x04u, "a row's standing: an lvalue's bits, then a constant's");

// Whether a value of standing `standing` and flags `flags`, operand `slot` of a row whose own standing is
// `row_standing`, takes the row's answer, the other operand's standing being `other`: node_machine_row_takes, as a word
// of 0 or 1 with no branch.
static inline __attribute__((always_inline)) uint32_t node_facts_row_takes(uint32_t row_standing, uint32_t allowed, uint32_t standing, uint32_t flags, uint32_t other,
                                                                           uint32_t slot)
{
    const uint32_t category = standing & 0x03u;
    const uint32_t kind = (standing >> 2) & 0x07u;
    const uint32_t lvalue = category == CHECK_LVALUE ? 1u : 0u;
    const uint32_t rvalue = category == CHECK_RVALUE ? 1u : 0u;
    const uint32_t none = kind == CHECK_CONSTANT_NONE ? 1u : 0u;
    const uint32_t address = kind == CHECK_CONSTANT_ADDRESS ? 1u : 0u;
    const uint32_t constant = (kind == CHECK_CONSTANT_INTEGER ? 1u : 0u) | (kind == CHECK_CONSTANT_FLOATING ? 1u : 0u);
    const uint32_t other_constant = ((other >> 2 & 0x07u) != CHECK_CONSTANT_NONE ? 1u : 0u) & ((other & 0x03u) == CHECK_RVALUE ? 1u : 0u);
    const uint32_t checked = flags & ~((uint32_t)BUSTER_NODE_MACHINE_ROW_CONSTANT_FLAGS & (0u - (constant & (lvalue ^ 1u))));
    const uint32_t as_lvalue = lvalue & (row_standing >> slot & 1u) & (none | address);
    const uint32_t as_rvalue = rvalue & (none | (constant & (other_constant ^ 1u) & (row_standing >> (2 + slot) & 1u)));
    return (as_lvalue | as_rvalue) & ((checked & ~allowed) == 0 ? 1u : 0u);
}

// A row's operator `w` of records `a` and `b` (`binary` 0 for a unary operator, whose `b` is any record of the window
// before it), node_machine_row's steps: operands of builtin arithmetic types, by T's table. Straight code: what the row
// asks of each operand's standing is one load of the standings' table, each test is a word of 0 or 1, and a unary
// operator's second operand is masked away, not branched on. With `always`, the record is written whether the row
// takes it or not, wide where it doesn't (the caller's rule then makes it): a class's loop, where one operator in five
// is no row's, has no branch on it.
static inline __attribute__((always_inline)) bool node_facts_row_made(NodeFactsRecord* records, const NodeFactsView* view, uint32_t w, uint32_t a, uint32_t b,
                                                                       uint32_t binary, CheckOp op, bool lean, bool always)
{
    const uint32_t o = node_machine_row_operators[op];
    const uint32_t both = 0u - binary;
    const uint32_t stands_a = view->stands[records[a].standing];
    const uint32_t stands_b = view->stands[records[b].standing & both];
    const uint32_t flags_a = records[a].flags;
    const uint32_t flags_b = records[b].flags & both;
    const uint32_t lvalue_a = stands_a & BUSTER_NODE_FACTS_STANDS_LVALUE;
    const uint32_t lvalue_b = stands_b & BUSTER_NODE_FACTS_STANDS_LVALUE;
    // An lvalue's const qualifier goes with its conversion; any other qualifier leaves the node to T.
    const TypeId type_a = records[a].type & ~((TypeId)BUSTER_TYPE_CONST & (0u - lvalue_a));
    const TypeId type_b = records[b].type & both & ~((TypeId)BUSTER_TYPE_CONST & (0u - lvalue_b));
    const uint32_t code_a = type_index(type_a);
    const uint32_t code_b = type_index(type_b);
    const uint32_t builtin = (o != NODE_MACHINE_ROW_NONE ? 1u : 0u) & (((type_a | type_b) & BUSTER_TYPE_QUALIFIERS) == 0 ? 1u : 0u) &
                             (code_a < BUSTER_NODE_MACHINE_KINDS ? 1u : 0u) & (code_b < BUSTER_NODE_MACHINE_KINDS ? 1u : 0u);
    const NodeMachineRow* row = &node_machine_rows[o][code_a & (0u - builtin)][code_b & (0u - builtin)];
    const uint32_t allowed = node_machine_row_allowed[o];
    const uint32_t row_standing = row->standing;
    const uint32_t result = row->result;
    // node_machine_row_takes of each: an lvalue or a literal's constant where the row says, the literal where the
    // other operand has no constant; and no flag outside the operator's allowed ones, a literal's own apart.
    _Static_assert(BUSTER_NODE_FACTS_STANDS_LVALUE == 0x01u && BUSTER_NODE_FACTS_STANDS_RVALUE == 0x02u && BUSTER_NODE_FACTS_STANDS_LITERAL == 0x04u &&
                   BUSTER_NODE_FACTS_STANDS_CONSTANT == 0x08u, "an operand's standing's bits, as the row's takes read them");
    const uint32_t takes_a = (lvalue_a & row_standing) | (stands_a >> 1) | (stands_a >> 2 & ~(stands_b >> 3) & row_standing >> 2);
    const uint32_t takes_b = (lvalue_b & row_standing >> 1) | (stands_b >> 1) | (stands_b >> 2 & ~(stands_a >> 3) & row_standing >> 3);
    const uint32_t literal_a = 0u - (stands_a >> 2 & 1u);
    const uint32_t literal_b = 0u - (stands_b >> 2 & 1u);
    const uint32_t refused = ((flags_a & ~((uint32_t)BUSTER_NODE_MACHINE_ROW_CONSTANT_FLAGS & literal_a)) | (flags_b & ~((uint32_t)BUSTER_NODE_MACHINE_ROW_CONSTANT_FLAGS & literal_b))) &
                             ~allowed;
    const bool taken = (builtin & takes_a & (takes_b | (binary ^ 1u)) & (result != 0 ? 1u : 0u) & (refused == 0 ? 1u : 0u)) != 0;
    if (taken || always)
    {
        const uint32_t flags = node_facts_source(row->flags | ((flags_a | flags_b) & node_machine_row_passed[o]));
        const uint8_t standing = taken ? BUSTER_NODE_MACHINE_STANDING(CHECK_RVALUE, CHECK_CONSTANT_NONE) : (uint8_t)BUSTER_NODE_MACHINE_WIDE;
        node_facts_record(records, w, BUSTER_TYPE_BUILTIN(result), view->rows[result].word, standing, flags, 0, 0);
        if (!lean)
        {
            node_facts_conversions(records, w, BUSTER_TYPE_BUILTIN(row->converted[0]), row->conversion_flags[0] | lvalue_a, BUSTER_TYPE_BUILTIN(row->converted[1]) & both,
                                   (row->conversion_flags[1] | lvalue_b) & both, 0);
        }
    }
    return taken;
}

// The integer constants' fold `w`, node_machine_fold's steps: unary +, - or ~ of record `a`, or a binary operator of the
// rows of records `a` and `b` (`binary`), each an rvalue integer constant the record holds whole, of an unqualified
// builtin integer type of at most 64 bits but bool, where the fold faults nothing.
static inline __attribute__((always_inline)) bool node_facts_fold(NodeFactsRecord* records, const NodeFactsView* view, uint32_t w, uint32_t a, uint32_t b, bool binary,
                                                                   CheckOp op)
{
    const bool unary = op == CHECK_OP_POSITIVE || op == CHECK_OP_NEGATIVE || op == CHECK_OP_BIT_NOT;
    const uint32_t right = unary ? a : b;
    const uint8_t o = op == CHECK_OP_POSITIVE ? (uint8_t)NODE_MACHINE_ROW_NEGATIVE : node_machine_row_operators[op];
    const uint8_t constant = BUSTER_NODE_MACHINE_STANDING(CHECK_RVALUE, CHECK_CONSTANT_INTEGER);
    const bool shaped = o != NODE_MACHINE_ROW_NONE && unary != binary;
    const TypeId type_a = records[a].type;
    const TypeId type_b = records[right].type;
    const uint32_t code_a = type_index(type_a);
    const uint32_t code_b = type_index(type_b);
    const uint32_t flags_a = records[a].flags;
    const uint32_t flags_b = records[right].flags;
    const bool constants = shaped && records[a].standing == constant && records[right].standing == constant && ((type_a | type_b) & BUSTER_TYPE_QUALIFIERS) == 0 &&
                           code_a < BUSTER_NODE_MACHINE_KINDS && code_b < BUSTER_NODE_MACHINE_KINDS && node_machine_integer_widths[code_a] != 0 &&
                           node_machine_integer_widths[code_b] != 0 && ((flags_a | flags_b) & BUSTER_CHECK_BIT_FIELD) == 0;
    const NodeMachineRow* row = constants ? &node_machine_rows[o][code_a][unary ? 0 : code_b] : NULL;
    const uint32_t converted_a = row != NULL ? row->converted[0] : 0;
    const uint32_t converted_b = row != NULL ? (unary ? converted_a : row->converted[1]) : 0;
    const uint32_t width = node_machine_integer_widths[converted_a];
    const uint32_t right_width = node_machine_integer_widths[converted_b];
    bool taken = row != NULL && row->result != 0 && width != 0 && right_width != 0;
    if (taken)
    {
        // A builtin integer type's sign is its row's (plain char's is the unit's, node_machine_integer_sign's).
        const bool is_signed = (view->rows[converted_a].flags & BUSTER_NODE_FACTS_SIGNED) != 0;
        const bool right_signed = (view->rows[converted_b].flags & BUSTER_NODE_FACTS_SIGNED) != 0;
        const uint64_t mask = width == 64 ? UINT64_MAX : (UINT64_C(1) << width) - 1;
        const uint64_t right_mask = right_width == 64 ? UINT64_MAX : (UINT64_C(1) << right_width) - 1;
        const uint64_t x = node_machine_convert_bits(records[a].payload, node_machine_integer_widths[code_a], (view->rows[code_a].flags & BUSTER_NODE_FACTS_SIGNED) != 0, width);
        const uint64_t y = unary ? 0 : node_machine_convert_bits(records[right].payload, node_machine_integer_widths[code_b],
                                                                 (view->rows[code_b].flags & BUSTER_NODE_FACTS_SIGNED) != 0, right_width);
        const uint64_t sign = UINT64_C(1) << (width - 1);
        const bool xn = is_signed && (x & sign) != 0;
        const bool yn = right_signed && ((y >> (right_width - 1)) & 1) != 0;
        const uint64_t xm = xn ? (0 - x) & mask : x;
        const uint64_t ym = yn ? (0 - y) & right_mask : y;
        uint64_t answer = 0;
        bool fault = false;
        switch (op)
        {
            case CHECK_OP_POSITIVE: answer = x; break;
            case CHECK_OP_NEGATIVE:
            {
                answer = (0 - x) & mask;
                fault = is_signed && x == sign;
            } break;
            case CHECK_OP_BIT_NOT: answer = ~x & mask; break;
            case CHECK_OP_ADD:
            {
                answer = (x + y) & mask;
                fault = is_signed && (~(x ^ y) & (x ^ answer) & sign) != 0;
            } break;
            case CHECK_OP_SUBTRACT:
            {
                answer = (x - y) & mask;
                fault = is_signed && ((x ^ y) & (x ^ answer) & sign) != 0;
            } break;
            case CHECK_OP_MULTIPLY:
            {
                answer = (x * y) & mask;
                const uint64_t limit = (sign - 1) + (xn != yn);
                fault = is_signed && ym != 0 && xm > limit / ym;
            } break;
            case CHECK_OP_DIVIDE:
            case CHECK_OP_REMAINDER:
            {
                fault = y == 0 || (is_signed && x == sign && y == mask);
                const uint64_t magnitude = fault ? 0 : op == CHECK_OP_DIVIDE ? xm / ym : xm % ym;
                const bool negative = op == CHECK_OP_DIVIDE ? xn != yn : xn;
                answer = (negative ? 0 - magnitude : magnitude) & mask;
            } break;
            case CHECK_OP_SHIFT_LEFT:
            case CHECK_OP_SHIFT_RIGHT:
            {
                fault = yn || y >= width;
                const uint32_t shift = fault ? 0 : (uint32_t)y;
                if (op == CHECK_OP_SHIFT_LEFT)
                {
                    answer = (x << shift) & mask;
                    fault = fault || (is_signed && (xn || x > mask >> shift));
                }
                else
                {
                    answer = (x >> shift) | (xn ? mask ^ (mask >> shift) : 0);
                }
            } break;
            case CHECK_OP_LESS: answer = xn != yn ? xn : x < y; break;
            case CHECK_OP_LESS_EQUAL: answer = xn != yn ? xn : x <= y; break;
            case CHECK_OP_GREATER: answer = xn != yn ? yn : x > y; break;
            case CHECK_OP_GREATER_EQUAL: answer = xn != yn ? yn : x >= y; break;
            case CHECK_OP_EQUAL: answer = x == y; break;
            case CHECK_OP_NOT_EQUAL: answer = x != y; break;
            case CHECK_OP_BIT_AND: answer = x & y; break;
            case CHECK_OP_BIT_OR: answer = x | y; break;
            case CHECK_OP_BIT_XOR: answer = x ^ y; break;
            default: fault = true; break;
        }
        taken = !fault;
        if (taken)
        {
            const uint32_t forms = BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM;
            const bool ice = (flags_a & BUSTER_CHECK_ICE) != 0 && (flags_b & BUSTER_CHECK_ICE) != 0;
            const bool gnu_ice = !ice && (flags_a & forms) != 0 && (flags_b & forms) != 0;
            const uint32_t flags = ((flags_a | flags_b) & BUSTER_CHECK_INHERITED) | (ice ? BUSTER_CHECK_ICE : gnu_ice ? BUSTER_CHECK_GNU_ICE_FORM : 0);
            node_facts_record_integer(records, w, BUSTER_TYPE_BUILTIN(row->result), view->rows[row->result].word, constant, node_facts_source(flags), 0, answer);
            node_facts_conversions(records, w, BUSTER_TYPE_BUILTIN(row->converted[0]), row->conversion_flags[0], unary ? 0 : BUSTER_TYPE_BUILTIN(row->converted[1]),
                                   unary ? 0 : row->conversion_flags[1], 0);
        }
    }
    return taken;
}

// Whether operand `operand` is a null pointer constant where the record can tell, node_machine_null's answer: one with
// the null pointer's flag, or an integer constant zero of an unqualified builtin integer type of at most 64 bits but bool
// in a form the unit takes.
static inline __attribute__((always_inline)) bool node_facts_null(const NodeFactsView* view, const NodeFactsOperand* operand)
{
    const uint32_t code = type_index(operand->type);
    const uint32_t width = (operand->type & BUSTER_TYPE_QUALIFIERS) == 0 && code < BUSTER_NODE_MACHINE_KINDS ? node_machine_integer_widths[code] : 0;
    const uint64_t mask = width == 64 ? UINT64_MAX : (UINT64_C(1) << width) - 1;
    const bool zero = operand->constant_kind == CHECK_CONSTANT_INTEGER && (operand->flags & view->null_forms) != 0 && width != 0 && (operand->integer & mask) == 0;
    return (operand->flags & BUSTER_CHECK_NULL_POINTER) != 0 || zero;
}

// The equality `w` of a pointer and a null pointer constant, records `a` and `b`: node_machine_null_compare's steps.
static inline __attribute__((always_inline)) bool node_facts_null_compare(NodeFactsRecord* records, const NodeFactsView* view, uint32_t w, uint32_t a, uint32_t b)
{
    const NodeFactsRow* rows[2] = { node_facts_row_of(view, records[a].type), node_facts_row_of(view, records[b].type) };
    NodeFactsOperand operands[2];
    TypeId converted[2] = { 0, 0 };
    uint32_t conversion_flags[2] = { 0, 0 };
    bool taken = node_facts_operand(records, rows[0], a, operands, converted, conversion_flags) &&
                 node_facts_operand(records, rows[1], b, operands + 1, converted + 1, conversion_flags + 1);
    const bool left_pointer = taken && operands[0].kind == BUSTER_TYPE_KIND_POINTER;
    const bool right_pointer = taken && operands[1].kind == BUSTER_TYPE_KIND_POINTER;
    const bool left_common = left_pointer && node_facts_null(view, operands + 1);
    const bool right_common = !left_common && right_pointer && node_facts_null(view, operands);
    const NodeFactsOperand* pointer = operands + (left_common ? 0 : 1);
    const uint32_t raw = left_common ? a : b;
    taken = (left_common || right_common) && !(pointer->constant_kind == CHECK_CONSTANT_ADDRESS && records[raw].object == 0);
    if (taken)
    {
        // The null pointer constant's conversion, where its type isn't the pointer's.
        const TypeId common = pointer->type;
        conversion_flags[0] |= operands[0].type == common ? 0 : BUSTER_CHECK_CONVERT_NULL;
        conversion_flags[1] |= operands[1].type == common ? 0 : BUSTER_CHECK_CONVERT_NULL;
        const uint32_t flags = (operands[0].flags | operands[1].flags) & BUSTER_CHECK_INHERITED;
        node_facts_record(records, w, BUSTER_TYPE_INT, view->int_word, BUSTER_NODE_MACHINE_STANDING(CHECK_RVALUE, CHECK_CONSTANT_NONE), node_facts_source(flags), 0, 0);
        node_facts_conversions(records, w, common, conversion_flags[0], common, conversion_flags[1], 0);
    }
    return taken;
}

// An operand's truth as a condition reads it, of record `a`'s value `operand` (node_facts_operand's, its type's row
// `row`): whether the rules here take it (a scalar with no constant, or an integer constant of an unqualified builtin
// type of at most 64 bits but bool; another constant's truth is T's to find), whether its truth is known, and its truth.
static inline __attribute__((always_inline)) bool node_facts_operand_truth(const NodeFactsRow* row, const NodeFactsOperand* operand, bool* known, bool* truth)
{
    // check_action_is_scalar of the value: a pointer (an array's and a function's too), or its type's own answer, which an
    // lvalue's value type shares.
    const bool scalar = operand->kind == BUSTER_TYPE_KIND_POINTER || (row->flags & BUSTER_NODE_FACTS_TRUTHY) != 0;
    const uint32_t width = (operand->type & BUSTER_TYPE_QUALIFIERS) == 0 && operand->kind != BUSTER_TYPE_KIND_POINTER ? row->flags >> 8 & 0xffu : 0;
    const uint64_t mask = width == 64 ? UINT64_MAX : (UINT64_C(1) << width) - 1;
    const bool constant = operand->constant_kind == CHECK_CONSTANT_INTEGER && width != 0;
    *known = constant;
    *truth = constant && (operand->integer & mask) != 0;
    return scalar && (operand->constant_kind == CHECK_CONSTANT_NONE || constant);
}

// A truth `w` of record `a`, a control's condition or with `negated` a `!`: node_machine_truth's steps.
static inline __attribute__((always_inline)) bool node_facts_truth(NodeFactsRecord* records, const NodeFactsView* view, uint32_t w, uint32_t a, bool negated, bool lean)
{
    const NodeFactsRow* row = node_facts_row_of(view, records[a].type);
    NodeFactsOperand operand;
    TypeId converted = 0;
    uint32_t conversion_flags = 0;
    bool known = false;
    bool truth = false;
    const bool taken = node_facts_operand(records, row, a, &operand, &converted, &conversion_flags) && node_facts_operand_truth(row, &operand, &known, &truth);
    if (taken)
    {
        const uint32_t forms = BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM;
        const uint32_t flags = (operand.flags & BUSTER_CHECK_INHERITED) |
                               ((operand.flags & BUSTER_CHECK_ICE) != 0 ? BUSTER_CHECK_ICE : (operand.flags & forms) != 0 ? BUSTER_CHECK_GNU_ICE_FORM : 0);
        node_facts_record_integer(records, w, BUSTER_TYPE_INT, view->int_word, BUSTER_NODE_MACHINE_STANDING(CHECK_RVALUE, known ? CHECK_CONSTANT_INTEGER : CHECK_CONSTANT_NONE),
                                  node_facts_source(flags), 0, known ? (uint64_t)(negated ? !truth : truth) : 0);
        if (!lean)
        {
            node_facts_conversions(records, w, converted, conversion_flags, 0, 0, 0);
        }
    }
    return taken;
}

// `&&` (`conjunction`) or `||`, `w` of records `a` and `b`: node_machine_logic's steps.
static inline __attribute__((always_inline)) bool node_facts_logic(NodeFactsRecord* records, const NodeFactsView* view, uint32_t w, uint32_t a, uint32_t b,
                                                                    bool conjunction, bool lean)
{
    const NodeFactsRow* rows[2] = { node_facts_row_of(view, records[a].type), node_facts_row_of(view, records[b].type) };
    NodeFactsOperand operands[2];
    TypeId converted[2] = { 0, 0 };
    uint32_t conversion_flags[2] = { 0, 0 };
    bool left_known = false;
    bool left_truth = false;
    bool right_known = false;
    bool right_truth = false;
    const bool taken = node_facts_operand(records, rows[0], a, operands, converted, conversion_flags) &&
                       node_facts_operand(records, rows[1], b, operands + 1, converted + 1, conversion_flags + 1) &&
                       node_facts_operand_truth(rows[0], operands, &left_known, &left_truth) && node_facts_operand_truth(rows[1], operands + 1, &right_known, &right_truth);
    if (taken)
    {
        const uint32_t left = operands[0].flags;
        const uint32_t right = operands[1].flags;
        // A condition that decides the value leaves the second operand unevaluated: its effects aren't the value's.
        const bool skips_right = left_known && (conjunction ? !left_truth : left_truth);
        const uint32_t inherited = (left & BUSTER_CHECK_INHERITED) | (skips_right ? 0 : right & BUSTER_CHECK_INHERITED);
        const uint32_t forms = BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM;
        const bool right_ice = (right & BUSTER_CHECK_ICE) != 0 || (skips_right && (right & (BUSTER_CHECK_ICE_UNEVALUATED | BUSTER_CHECK_GNU_ICE_FORM)) != 0);
        const bool ice = (left & BUSTER_CHECK_ICE) != 0 && right_ice;
        const bool right_form = (right & forms) != 0 || (skips_right && (right & BUSTER_CHECK_ICE_UNEVALUATED) != 0);
        const uint32_t flags = inherited | (ice ? BUSTER_CHECK_ICE : (left & forms) != 0 && right_form ? BUSTER_CHECK_GNU_ICE_FORM : 0);
        const bool constant = left_known && (skips_right || right_known);
        const bool value = conjunction ? left_truth && right_truth : left_truth || right_truth;
        node_facts_record_integer(records, w, BUSTER_TYPE_INT, view->int_word, BUSTER_NODE_MACHINE_STANDING(CHECK_RVALUE, constant ? CHECK_CONSTANT_INTEGER : CHECK_CONSTANT_NONE),
                                  node_facts_source(flags), 0, constant && value);
        if (!lean)
        {
            node_facts_conversions(records, w, converted[0], conversion_flags[0], converted[1], conversion_flags[1], 0);
        }
    }
    return taken;
}

// A miss of the pointer conversions' slots at slot `at`: the type table's answers for a pointer of type `source`
// converted to pointer type `target`, kept there; with the table half full, no answer, and the conversion is T's.
static __attribute__((noinline, cold)) const NodeFactsPair* node_facts_pair_fill(NodeFacts* facts, uint32_t at, TypeId target, TypeId source)
{
    const NodeMachine* machine = facts->machine;
    const TypeTable* types = machine->types;
    const bool room = facts->pair_count < (1u << BUSTER_NODE_FACTS_PAIR_BITS) / 2;
    NodeFactsPair* slot = room ? facts->pairs + at : &facts->no_pair;
    const TypeId from = type_unqualified(type_strip_aligned(types, type_element(types, source)));
    const TypeId to = type_unqualified(type_strip_aligned(types, type_element(types, target)));
    *slot = (NodeFactsPair){
        .key = (uint64_t)target << 32 | source,
        .flags = BUSTER_NODE_FACTS_PAIR_USED | (node_machine_pointer_assignable(machine, target, source) ? BUSTER_NODE_FACTS_PAIR_ASSIGNABLE : 0) |
                 (from == to ? BUSTER_NODE_FACTS_PAIR_SAME : 0),
    };
    facts->pair_count += room;
    return slot;
}

// The slot of a pointer of type `source` converted to pointer type `target`, each a pointer type with no wrapper: the
// one that holds its answers, or (without `facts`, a class's loop, which fills none) one that holds none, which leaves
// the conversion to its rule, and the rule fills the slot.
static const NodeFactsPair node_facts_no_pair;

static inline __attribute__((always_inline)) const NodeFactsPair* node_facts_pair(NodeFacts* facts, const NodeFactsView* view, TypeId target, TypeId source)
{
    const NodeFactsPair* pairs = view->pairs;
    const uint64_t key = (uint64_t)target << 32 | source;
    uint32_t at = (uint32_t)((key * UINT64_C(0x9e3779b97f4a7c15)) >> (64 - BUSTER_NODE_FACTS_PAIR_BITS));
    const NodeFactsPair* slot = pairs + at;
    while (slot->key != key && slot->flags != 0)
    {
        at = (at + 1) & ((1u << BUSTER_NODE_FACTS_PAIR_BITS) - 1);
        slot = pairs + at;
    }
    if (__builtin_expect(slot->key != key, 0))
    {
        slot = facts != NULL ? node_facts_pair_fill(facts, at, target, source) : &node_facts_no_pair;
    }
    return slot;
}

// A store `w`: `=` of records `a` and `b` (`store`), or a conversion or an initialization of record `a` to the node's
// type: node_machine_store_rule's steps. On the record, as T's rule has it on its columns: a source that is no
// constant, an integer constant between builtin integer types of at most 64 bits but bool, and an address to its own
// pointer type. And of the conversions T's rule makes whole, by check_action_convert_plain's steps: a pointer's to
// another pointer type, where its pair's slot says node_machine_pointer_assignable takes it, and a null pointer
// constant's to a pointer.
static inline __attribute__((always_inline)) bool node_facts_store_made(NodeFacts* facts, NodeFactsRecord* records, const NodeFactsView* view, uint32_t w, uint32_t a,
                                                                         uint32_t b, bool store, bool lean)
{
    const uint32_t s = store ? b : a;
    const NodeFactsRow* source_row = node_facts_row_of(view, records[s].type);
    NodeFactsOperand source;
    TypeId converted = 0;
    uint32_t conversion_flags = 0;
    bool taken = node_facts_operand(records, source_row, s, &source, &converted, &conversion_flags);
    // The target: `=`'s destination, a modifiable lvalue of a plain kind that the record holds whole, or the node's
    // type; with no wrapper, and neither atomic nor void. Its value type is then its row's.
    const TypeId destination = store ? records[a].type : view->inputs[w];
    const NodeFactsRow* target_row = node_facts_row_of(view, destination);
    const uint32_t target_word = target_row->word;
    const uint8_t target_kind = (uint8_t)(target_word >> 16);
    const uint32_t wanted = BUSTER_NODE_MACHINE_WORD_VALID | BUSTER_NODE_MACHINE_WORD_PLAIN;
    taken = taken && destination != 0 && (target_word & wanted) == wanted && target_kind != BUSTER_TYPE_KIND_ATOMIC && target_kind != BUSTER_TYPE_KIND_VOID &&
            source.kind != BUSTER_TYPE_KIND_ATOMIC &&
            (!store || ((records[a].standing & (0x03u | BUSTER_NODE_MACHINE_WIDE)) == CHECK_LVALUE &&
                        (records[a].flags & (BUSTER_CHECK_VECTOR_DUPLICATE | BUSTER_CHECK_BIT_FIELD)) == 0 && (type_qualifiers(destination) & BUSTER_TYPE_CONST) == 0 &&
                        node_machine_plain_kind(target_kind)));
    const TypeId target = target_row->value;
    const uint32_t source_code = type_index(source.type);
    const uint32_t target_code = type_index(target);
    const bool source_builtin = (source.type & BUSTER_TYPE_QUALIFIERS) == 0 && source_code < BUSTER_NODE_MACHINE_KINDS;
    const bool target_builtin = (target & BUSTER_TYPE_QUALIFIERS) == 0 && target_code < BUSTER_NODE_MACHINE_KINDS;
    const bool arithmetic = source_builtin && target_builtin && node_machine_row_kind(source_code) && node_machine_row_kind(target_code);
    const bool pointer = target_kind == BUSTER_TYPE_KIND_POINTER;
    const bool source_pointer = source.kind == BUSTER_TYPE_KIND_POINTER;
    const bool same = target == source.type;
    const uint32_t source_width = source_builtin ? node_machine_integer_widths[source_code] : 0;
    const uint32_t target_width = target_builtin ? node_machine_integer_widths[target_code] : 0;
    const uint8_t field = records[s].standing & BUSTER_NODE_MACHINE_FIELD;
    const bool constant = source.constant_kind == CHECK_CONSTANT_INTEGER;
    const bool flagged = (source.flags & BUSTER_CHECK_NULL_POINTER) != 0;
    // The three cases. On the record (node_machine_assignable's conversions): no pointer's null constant.
    const bool plain = (arithmetic || (same && pointer)) && !(pointer && flagged) &&
                       (source.constant_kind == CHECK_CONSTANT_NONE || (constant && source_width != 0 && target_width != 0 && field == 0) ||
                        (source.constant_kind == CHECK_CONSTANT_ADDRESS && pointer));
    // A null pointer constant to a pointer: an integer constant zero, or a pointer with the null pointer's flag.
    const bool null = pointer && (source_pointer ? flagged : !flagged && node_facts_null(view, &source));
    // A pointer to another pointer type: its pair's slot, which also says whether the two point at the same type.
    const bool other = pointer && source_pointer && !same;
    const NodeFactsPair* pair = taken && other ? node_facts_pair(facts, view, target, source.type) : &node_facts_no_pair;
    const bool answered = !other || (pair->flags & BUSTER_NODE_FACTS_PAIR_USED) != 0;
    const bool converts = other && !flagged && (pair->flags & BUSTER_NODE_FACTS_PAIR_ASSIGNABLE) != 0 &&
                          (source.constant_kind == CHECK_CONSTANT_NONE || source.constant_kind == CHECK_CONSTANT_ADDRESS);
    taken = taken && (plain || converts || (null && answered));
    if (taken)
    {
        const uint32_t stored = BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM | BUSTER_CHECK_FLOAT_LITERAL | BUSTER_CHECK_ICE_UNEVALUATED;
        const bool integer_target = target_builtin && node_machine_row_kind(target_code) && target_code != type_index(BUSTER_TYPE_FLOAT) &&
                                    target_code != type_index(BUSTER_TYPE_DOUBLE);
        // check_action_convert_plain's flag for a pointer converted to or from a type that doesn't point at the same one.
        const bool restricted = !plain && !(source_pointer && (same || (pair->flags & BUSTER_NODE_FACTS_PAIR_SAME) != 0));
        uint32_t flags = (source.flags | (restricted ? BUSTER_CHECK_CONST_READ_RESTRICTED : 0)) &
                         ~(BUSTER_CHECK_BIT_FIELD | BUSTER_CHECK_REGISTER | BUSTER_CHECK_REGISTER_NAME | BUSTER_CHECK_NULL_POINTER);
        flags &= integer_target ? ~0u : ~(uint32_t)(BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM);
        flags = store ? (flags & ~stored) | BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_MODIFICATION | (records[a].flags & BUSTER_CHECK_INHERITED) : flags;
        flags = node_facts_source(flags);
        conversion_flags |= BUSTER_CHECK_CONVERT_ASSIGNMENT | (!plain && null ? BUSTER_CHECK_CONVERT_NULL : 0);
        if (plain && constant)
        {
            // An integer constant's bits, as check_action_convert_plain converts them.
            const uint64_t source_mask = source_width == 64 ? UINT64_MAX : (UINT64_C(1) << source_width) - 1;
            const uint64_t target_mask = target_width == 64 ? UINT64_MAX : (UINT64_C(1) << target_width) - 1;
            uint64_t bits = source.integer & source_mask;
            bits |= (view->rows[source_code].flags & BUSTER_NODE_FACTS_SIGNED) != 0 && (bits >> (source_width - 1)) != 0 && target_width > source_width ? ~source_mask : 0;
            node_facts_from_with(records, w, s, target, target_word, BUSTER_NODE_MACHINE_STANDING(CHECK_RVALUE, CHECK_CONSTANT_INTEGER), flags, bits & target_mask);
        }
        else if (plain)
        {
            // A conversion to bool of no constant: its truth unknown, its integer 0.
            const bool cleared = target == BUSTER_TYPE_BOOL && field == 0;
            node_facts_from_with(records, w, s, target, target_word, BUSTER_NODE_MACHINE_STANDING(CHECK_RVALUE, source.constant_kind) | (cleared ? 0 : field), flags,
                                 cleared ? 0 : records[s].payload);
        }
        else if (null)
        {
            // The null pointer: an address constant with no object and no offset.
            node_facts_record(records, w, target, target_word, BUSTER_NODE_MACHINE_STANDING(CHECK_RVALUE, CHECK_CONSTANT_ADDRESS), flags, 0, source.integer);
        }
        else
        {
            // Another pointer type: the value's own object and offset.
            node_facts_from(records, w, s, target, target_word, CHECK_RVALUE, flags, source.constant_kind);
        }
        if (!lean && store)
        {
            node_facts_conversions(records, w, records[a].type, 0, target, conversion_flags, 0);
        }
        else if (!lean)
        {
            node_facts_conversions(records, w, target, conversion_flags, 0, 0, 0);
        }
    }
    return taken;
}

// `&x`, `w` of record `a`: node_machine_address_rule's steps, the pointer type its operand's type's side row's.
static inline __attribute__((always_inline)) bool node_facts_address(NodeFactsRecord* records, const NodeFactsView* view, uint32_t w, uint32_t a, bool lean)
{
    const uint32_t standing = records[a].standing;
    const uint32_t category = standing & 0x03u;
    const uint32_t kind = (standing >> 2) & 0x07u;
    uint32_t flags = records[a].flags;
    const TypeId type = records[a].type;
    const uint32_t index = type_index(type);
    const uint32_t qualifiers = type_qualifiers(type);
    const NodeFactsSide* side = view->sides + (index < view->row_count ? index : view->row_count);
    const TypeId pointer = qualifiers <= BUSTER_TYPE_CONST ? side->pointers[qualifiers] : 0;
    const bool taken = pointer != 0 && (flags & (BUSTER_CHECK_VECTOR_ELEMENT | BUSTER_CHECK_BIT_FIELD | BUSTER_CHECK_REGISTER)) == 0 &&
                       (category == CHECK_LVALUE || category == CHECK_FUNCTION) && (standing & BUSTER_NODE_MACHINE_WIDE) == 0;
    if (taken)
    {
        // check_action_address_provenance: an automatic object's address is no constant.
        const bool dropped = (flags & BUSTER_CHECK_STATIC_OBJECT) == 0 && kind == CHECK_CONSTANT_ADDRESS && records[a].object != 0;
        flags |= (flags & BUSTER_CHECK_INACTIVE_UNION_READ) != 0 ? BUSTER_CHECK_INACTIVE_UNION_ADDRESS : 0;
        flags &= ~(BUSTER_CHECK_INACTIVE_UNION_READ | BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM | BUSTER_CHECK_FLOAT_LITERAL | BUSTER_CHECK_ICE_UNEVALUATED |
                   BUSTER_CHECK_BIT_FIELD | BUSTER_CHECK_REGISTER | BUSTER_CHECK_VOLATILE_PART);
        node_facts_from(records, w, a, pointer, side->pointer_words[qualifiers], CHECK_RVALUE, node_facts_source(flags), dropped ? CHECK_CONSTANT_NONE : (uint8_t)kind);
        if (!lean)
        {
            node_facts_conversions(records, w, type, 0, 0, 0, 0);
        }
    }
    return taken;
}

// A cast `w` of record `a` to the node's type, a builtin integer type or a pointer: node_machine_cast's steps for a value
// of a builtin integer type or a pointer that is no constant (a constant's T makes whole; a cast to void is the
// discard's).
static inline __attribute__((always_inline)) bool node_facts_cast(NodeFacts* facts, NodeFactsRecord* records, const NodeFactsView* view, uint32_t w, uint32_t a, bool lean)
{
    const TypeId written = view->inputs[w];
    const NodeFactsRow* target_row = node_facts_row_of(view, written);
    const uint32_t target_word = target_row->word;
    const uint8_t target_kind = (uint8_t)(target_word >> 16);
    const uint32_t wanted = BUSTER_NODE_MACHINE_WORD_VALID | BUSTER_NODE_MACHINE_WORD_PLAIN;
    const NodeFactsRow* source_row = node_facts_row_of(view, records[a].type);
    // Zeroed: a target that isn't taken leaves the source unread, and the steps below read it before `taken` decides.
    NodeFactsOperand source = { 0 };
    TypeId converted = 0;
    uint32_t conversion_flags = 0;
    bool taken = written != 0 && (target_word & wanted) == wanted && node_facts_operand(records, source_row, a, &source, &converted, &conversion_flags);
    const TypeId target = type_unqualified(written);
    const bool target_integer = node_machine_integer_code(type_index(target)) && target_kind == BUSTER_TYPE_KIND_INTEGER;
    const bool target_pointer = target_kind == BUSTER_TYPE_KIND_POINTER;
    const bool source_integer = (source.type & BUSTER_TYPE_QUALIFIERS) == 0 && node_machine_integer_code(type_index(source.type)) &&
                                source.kind == BUSTER_TYPE_KIND_INTEGER;
    const bool source_pointer = source.kind == BUSTER_TYPE_KIND_POINTER;
    taken = taken && (target_integer || target_pointer) && (source_integer || source_pointer) && source.constant_kind == CHECK_CONSTANT_NONE;
    // Whether two pointer types point at the same type but for its qualifiers: their pair's slot's answer.
    const bool other = source_pointer && target_pointer && target != source.type;
    const NodeFactsPair* pair = taken && other ? node_facts_pair(facts, view, target, source.type) : &node_facts_no_pair;
    taken = taken && (!other || (pair->flags & BUSTER_NODE_FACTS_PAIR_USED) != 0);
    if (taken)
    {
        const bool ice = target_integer && (source.flags & (BUSTER_CHECK_ICE | BUSTER_CHECK_FLOAT_LITERAL)) != 0;
        const bool gnu_ice = target_integer && (source.flags & BUSTER_CHECK_GNU_ICE_FORM) != 0;
        const bool void_pointer = target_pointer && (uint8_t)(target_row->element_word >> 16) == BUSTER_TYPE_KIND_VOID;
        const bool restricted = (source_pointer || target_pointer) && !(source_pointer && target_pointer && (!other || (pair->flags & BUSTER_NODE_FACTS_PAIR_SAME) != 0));
        const bool null = void_pointer && (source.flags & BUSTER_CHECK_NULL_POINTER) != 0;
        uint32_t flags = source.flags | (restricted ? BUSTER_CHECK_CONST_READ_RESTRICTED : 0);
        flags &= ~(BUSTER_CHECK_BIT_FIELD | BUSTER_CHECK_REGISTER | BUSTER_CHECK_REGISTER_NAME | BUSTER_CHECK_NULL_POINTER | BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM |
                   BUSTER_CHECK_FLOAT_LITERAL | BUSTER_CHECK_ICE_UNEVALUATED);
        flags |= (flags & BUSTER_CHECK_GNU_FLOAT_RANGE) != 0 ? 0 : ice ? BUSTER_CHECK_ICE : gnu_ice ? BUSTER_CHECK_GNU_ICE_FORM : 0;
        flags |= null ? BUSTER_CHECK_NULL_POINTER : 0;
        // A conversion to bool of no constant: its truth unknown, its integer 0.
        const uint8_t field = records[a].standing & BUSTER_NODE_MACHINE_FIELD;
        const bool cleared = target == BUSTER_TYPE_BOOL && field == 0;
        node_facts_from_with(records, w, a, target, target_word, BUSTER_NODE_MACHINE_STANDING(CHECK_RVALUE, CHECK_CONSTANT_NONE) | (cleared ? 0 : field),
                             node_facts_source(flags), cleared ? 0 : records[a].payload);
        if (!lean)
        {
            node_facts_conversions(records, w, target, conversion_flags | BUSTER_CHECK_CONVERT_CAST, 0, 0, 0);
        }
    }
    return taken;
}

// An update `w` of destination `a`, an increment or (`binary`) a compound assignment of record `b`:
// node_machine_update_rule's steps for a destination of a builtin arithmetic type, by T's table, and
// node_machine_update_pointer_rule's for a pointer's. T's rules make the value whole to convert it as it is stored
// (node_machine_update_value); here the same steps on the record, where neither the destination nor the value is wide:
// a prefix's or a compound's value is the answer's, no constant, and a postfix's the destination's old one.
static inline __attribute__((always_inline)) bool node_facts_update(NodeFactsRecord* records, const NodeFactsView* view, uint32_t w, uint32_t a, uint32_t b, bool binary,
                                                                     CheckOp op, bool lean)
{
    const bool increment = op >= CHECK_OP_PRE_INCREMENT && op <= CHECK_OP_POST_DECREMENT;
    const bool postfix = op == CHECK_OP_POST_INCREMENT || op == CHECK_OP_POST_DECREMENT;
    const bool present = increment || binary;
    const uint8_t standing_a = records[a].standing;
    const uint32_t flags_a = records[a].flags;
    const TypeId type_a = records[a].type;
    // An increment's value is the int 1 (node_machine_one's standing and flags).
    const uint8_t standing_b = increment ? BUSTER_NODE_MACHINE_STANDING(CHECK_RVALUE, CHECK_CONSTANT_INTEGER) : binary ? records[b].standing : 0;
    const uint32_t flags_b = increment ? BUSTER_CHECK_ICE : binary ? records[b].flags : 0;
    const uint32_t lvalue_b = (standing_b & 0x03u) == CHECK_LVALUE ? 1u : 0u;
    const TypeId value_type = increment ? BUSTER_TYPE_INT : binary ? records[b].type & ~(lvalue_b != 0 ? (TypeId)BUSTER_TYPE_CONST : 0) : 0;
    const uint8_t o = node_machine_row_operators[node_machine_update_op(op)];
    const uint32_t code_a = type_index(type_a);
    const uint32_t code_b = type_index(value_type);
    const bool destination = (standing_a & (0x03u | BUSTER_NODE_MACHINE_WIDE)) == CHECK_LVALUE && (type_a & BUSTER_TYPE_QUALIFIERS) == 0 &&
                             (flags_a & (BUSTER_CHECK_VECTOR_DUPLICATE | BUSTER_CHECK_BIT_FIELD | BUSTER_CHECK_VOLATILE_PART)) == 0;
    const bool builtin = present && destination && o != NODE_MACHINE_ROW_NONE && code_a < BUSTER_NODE_MACHINE_KINDS && node_machine_row_kind(code_a) &&
                         (value_type & BUSTER_TYPE_QUALIFIERS) == 0 && code_b < BUSTER_NODE_MACHINE_KINDS && (standing_b & BUSTER_NODE_MACHINE_WIDE) == 0;
    const NodeMachineRow* row = builtin ? &node_machine_rows[o][code_a][code_b] : NULL;
    const uint32_t allowed = node_machine_row_allowed[o];
    const bool arithmetic = row != NULL && row->result != 0 && node_facts_row_takes(row->standing, allowed, standing_a, flags_a, standing_b, 0) != 0 &&
                            node_facts_row_takes(row->standing, allowed, standing_b, flags_b, standing_a, 1) != 0;
    // A pointer's: the destination's value as the common conversions take it, no constant, and the value an integer.
    const NodeFactsRow* row_a = node_facts_row_of(view, type_a);
    NodeFactsOperand pointer = { 0 };
    TypeId converted[2] = { 0, 0 };
    uint32_t conversion_flags[2] = { 0, 0 };
    uint32_t index_flags = 0;
    bool pointed = !arithmetic && op >= CHECK_OP_PRE_INCREMENT && op <= CHECK_OP_SUBTRACT_ASSIGN && destination &&
                   node_facts_operand(records, row_a, a, &pointer, converted, conversion_flags) && pointer.kind == BUSTER_TYPE_KIND_POINTER &&
                   pointer.constant_kind == CHECK_CONSTANT_NONE;
    if (pointed && !increment)
    {
        const NodeFactsRow* row_b = node_facts_row_of(view, records[b].type);
        NodeFactsOperand index;
        pointed = binary && node_facts_operand(records, row_b, b, &index, converted + 1, conversion_flags + 1) && (row_b->flags & BUSTER_NODE_FACTS_INTEGER) != 0 &&
                  (index.flags & BUSTER_CHECK_BIT_FIELD) == 0;
        if (pointed)
        {
            // The index's promotion, check_action_promote's conversion.
            const TypeId promoted = row_b->promoted;
            conversion_flags[1] |= promoted != index.type ? BUSTER_CHECK_CONVERT_PROMOTION : 0;
            converted[1] = promoted != index.type ? promoted : converted[1];
            index_flags = index.flags;
        }
    }
    const uint8_t element_kind = (uint8_t)(row_a->element_word >> 16);
    pointed = pointed && element_kind != BUSTER_TYPE_KIND_VOID && element_kind != BUSTER_TYPE_KIND_FUNCTION && row_a->element_complete_at != UINT32_MAX &&
              row_a->element_complete_at <= view->positions[w];
    const bool taken = arithmetic || pointed;
    if (taken)
    {
        // The computation's answer's flags, then as it is stored: check_action_convert_plain's of no constant to the
        // destination's own type.
        const bool integer_destination = arithmetic && code_a != type_index(BUSTER_TYPE_FLOAT) && code_a != type_index(BUSTER_TYPE_DOUBLE);
        uint32_t answer = arithmetic ? row->flags | ((flags_a | flags_b) & node_machine_row_passed[o]) :
                                       ((pointer.flags | index_flags) & BUSTER_CHECK_INHERITED) |
                                           (pointer.flags & (BUSTER_CHECK_STATIC_OBJECT | BUSTER_CHECK_INACTIVE_UNION_ADDRESS | BUSTER_CHECK_CONST_READ_RESTRICTED |
                                                             BUSTER_CHECK_NONNULL_ADDRESS));
        answer &= ~(BUSTER_CHECK_BIT_FIELD | BUSTER_CHECK_REGISTER | BUSTER_CHECK_REGISTER_NAME | BUSTER_CHECK_NULL_POINTER);
        answer &= integer_destination ? ~0u : ~(uint32_t)(BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM);
        // A postfix update's value is the destination's old one, as an lvalue's conversion gives it, with the answer's
        // effects.
        uint32_t flags = postfix ? ((flags_a & ~(BUSTER_CHECK_REGISTER | BUSTER_CHECK_REGISTER_NAME | BUSTER_CHECK_VOLATILE_PART)) | (answer & BUSTER_CHECK_INHERITED)) &
                                       ~(uint32_t)BUSTER_CHECK_BIT_FIELD :
                                   answer;
        flags &= ~(BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM | BUSTER_CHECK_FLOAT_LITERAL | BUSTER_CHECK_ICE_UNEVALUATED | BUSTER_CHECK_NULL_POINTER |
                   BUSTER_CHECK_REGISTER | BUSTER_CHECK_REGISTER_NAME);
        flags |= BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_MODIFICATION;
        flags = node_facts_source(flags);
        const TypeId type = row_a->value;
        if (postfix)
        {
            node_facts_from_with(records, w, a, type, records[a].word, BUSTER_NODE_MACHINE_STANDING(CHECK_RVALUE, CHECK_CONSTANT_NONE) | (standing_a & BUSTER_NODE_MACHINE_FIELD),
                                 flags, records[a].payload);
        }
        else
        {
            node_facts_record(records, w, type, records[a].word, BUSTER_NODE_MACHINE_STANDING(CHECK_RVALUE, CHECK_CONSTANT_NONE), flags, 0, 0);
        }
        if (!lean && arithmetic)
        {
            node_facts_conversions(records, w, BUSTER_TYPE_BUILTIN(row->converted[0]), row->conversion_flags[0] | BUSTER_CHECK_CONVERT_LVALUE,
                                   increment ? 0 : BUSTER_TYPE_BUILTIN(row->converted[1]), increment ? 0 : row->conversion_flags[1] | lvalue_b, 0);
        }
        else if (!lean)
        {
            node_facts_conversions(records, w, converted[0], conversion_flags[0], increment ? 0 : converted[1], increment ? 0 : conversion_flags[1], 0);
        }
    }
    return taken;
}

// A comma `w` of records `a` and `b`: node_machine_comma's steps, the right operand's value with the left's effects.
static inline __attribute__((always_inline)) bool node_facts_comma(NodeFactsRecord* records, const NodeFactsView* view, uint32_t w, uint32_t a, uint32_t b, bool lean)
{
    const NodeFactsRow* rows[2] = { node_facts_row_of(view, records[a].type), node_facts_row_of(view, records[b].type) };
    NodeFactsOperand operands[2];
    TypeId converted[2] = { 0, 0 };
    uint32_t conversion_flags[2] = { 0, 0 };
    const bool taken = node_facts_operand(records, rows[0], a, operands, converted, conversion_flags) &&
                       node_facts_operand(records, rows[1], b, operands + 1, converted + 1, conversion_flags + 1);
    if (taken)
    {
        const uint32_t allowed = BUSTER_CHECK_ICE | BUSTER_CHECK_ICE_UNEVALUATED | BUSTER_CHECK_GNU_ICE_FORM;
        uint32_t flags = operands[1].flags & ~(BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM | BUSTER_CHECK_FLOAT_LITERAL | BUSTER_CHECK_ICE_UNEVALUATED | BUSTER_CHECK_NULL_POINTER);
        flags |= (operands[0].flags & allowed) != 0 && (operands[1].flags & allowed) != 0 ? BUSTER_CHECK_ICE_UNEVALUATED : 0;
        flags |= operands[0].flags & BUSTER_CHECK_INHERITED;
        node_facts_from(records, w, b, operands[1].type, node_facts_operand_word(records, view, b, operands + 1), CHECK_RVALUE, node_facts_source(flags),
                        operands[1].constant_kind);
        if (!lean)
        {
            node_facts_conversions(records, w, converted[0], conversion_flags[0], converted[1], conversion_flags[1], 0);
        }
    }
    return taken;
}

// sizeof (`size`) or _Alignof, `w`, of the node's type name or (`operand`) of record `a`: node_machine_query's steps for
// a type with no wrapper whose side row holds its size and alignment, complete at the node.
static inline __attribute__((always_inline)) bool node_facts_query(NodeFactsRecord* records, const NodeFactsView* view, uint32_t w, uint32_t a, bool operand, bool size,
                                                                    bool lean)
{
    const TypeId named = view->inputs[w];
    const bool valued = named == 0;
    const TypeId queried = valued && operand ? records[a].type : named;
    const NodeFactsRow* row = node_facts_row_of(view, queried);
    const bool fit = valued ? operand && (records[a].standing & BUSTER_NODE_MACHINE_WIDE) == 0 &&
                              !((records[a].standing & 0x03u) == CHECK_LVALUE && (records[a].flags & BUSTER_CHECK_BIT_FIELD) != 0) :
                              !operand;
    const bool taken = fit && (row->flags & BUSTER_NODE_FACTS_SIZED) != 0 && row->complete_at <= view->positions[w];
    if (taken)
    {
        const NodeFactsSide* side = view->sides + type_index(queried);
        node_facts_record_integer(records, w, BUSTER_TYPE_ULONG, view->ulong_word, BUSTER_NODE_MACHINE_STANDING(CHECK_RVALUE, CHECK_CONSTANT_INTEGER), BUSTER_CHECK_ICE, 0,
                                  size ? side->size : side->alignment);
        if (!lean)
        {
            node_facts_conversions(records, w, valued ? queried : 0, 0, 0, 0, 0);
        }
    }
    return taken;
}

// `?:`, `w` of condition `a` and arms `b` and `c`: node_machine_conditional's steps where the condition's truth is not
// known (it has no constant), so that the value is no arm's: arms of builtin arithmetic types that the add's row takes,
// or either void. T's rule makes the values whole for the action's conversions; of no constant they change flags alone.
static inline __attribute__((always_inline)) bool node_facts_conditional(NodeFactsRecord* records, const NodeFactsView* view, uint32_t w, uint32_t a, uint32_t b, uint32_t c,
                                                                          bool lean)
{
    const NodeFactsRow* rows[3] = { node_facts_row_of(view, records[a].type), node_facts_row_of(view, records[b].type), node_facts_row_of(view, records[c].type) };
    NodeFactsOperand operands[3];
    TypeId converted[3] = { 0, 0, 0 };
    uint32_t conversion_flags[3] = { 0, 0, 0 };
    bool taken = node_facts_operand(records, rows[0], a, operands, converted, conversion_flags) &&
                 node_facts_operand(records, rows[1], b, operands + 1, converted + 1, conversion_flags + 1) &&
                 node_facts_operand(records, rows[2], c, operands + 2, converted + 2, conversion_flags + 2);
    const NodeFactsOperand* condition = operands;
    const NodeFactsOperand* left = operands + 1;
    const NodeFactsOperand* right = operands + 2;
    const bool scalar = taken && (condition->kind == BUSTER_TYPE_KIND_POINTER || (rows[0]->flags & BUSTER_NODE_FACTS_TRUTHY) != 0);
    const uint32_t code_left = taken ? type_index(left->type) : 0;
    const uint32_t code_right = taken ? type_index(right->type) : 0;
    const bool arithmetic = taken && ((left->type | right->type) & BUSTER_TYPE_QUALIFIERS) == 0 && code_left < BUSTER_NODE_MACHINE_KINDS &&
                            code_right < BUSTER_NODE_MACHINE_KINDS && node_machine_row_kind(code_left) && node_machine_row_kind(code_right);
    const NodeMachineRow* row = arithmetic ? &node_machine_rows[NODE_MACHINE_ROW_ADD][code_left][code_right] : NULL;
    const bool void_arm = taken && !arithmetic && (left->kind == BUSTER_TYPE_KIND_VOID || right->kind == BUSTER_TYPE_KIND_VOID);
    taken = scalar && ((row != NULL && row->result != 0) || void_arm) && condition->constant_kind == CHECK_CONSTANT_NONE &&
            ((left->flags | right->flags) & BUSTER_CHECK_BIT_FIELD) == 0;
    if (taken)
    {
        const TypeId common = row != NULL ? BUSTER_TYPE_BUILTIN(row->converted[0]) : BUSTER_TYPE_VOID;
        const bool integer_common = row != NULL && row->converted[0] != type_index(BUSTER_TYPE_FLOAT) && row->converted[0] != type_index(BUSTER_TYPE_DOUBLE);
        uint32_t arm_flags[2] = { 0, 0 };
        for (uint32_t k = 1; k < 3; k += 1)
        {
            // An arm's promotion (check_action_promote) and its conversion to the common type, whose flags are
            // check_action_convert_plain's of the arm's.
            const NodeFactsOperand* arm = operands + k;
            const TypeId promoted = row != NULL ? rows[k]->promoted : arm->type;
            conversion_flags[k] |= (promoted != arm->type ? BUSTER_CHECK_CONVERT_PROMOTION : 0) | (promoted != common ? BUSTER_CHECK_CONVERT_ARITHMETIC : 0);
            converted[k] = common;
            uint32_t flags = arm->flags & ~(BUSTER_CHECK_BIT_FIELD | BUSTER_CHECK_REGISTER | BUSTER_CHECK_REGISTER_NAME | BUSTER_CHECK_NULL_POINTER);
            flags &= integer_common ? ~0u : ~(uint32_t)(BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM);
            arm_flags[k - 1] = flags;
        }
        // check_action_conditional_provenance, its condition's truth unknown.
        const uint32_t forms = BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM;
        const bool ice = (condition->flags & BUSTER_CHECK_ICE) != 0 && (arm_flags[0] & BUSTER_CHECK_ICE) != 0 && (arm_flags[1] & BUSTER_CHECK_ICE) != 0;
        const bool gnu_ice = (condition->flags & forms) != 0 && (arm_flags[0] & forms) != 0 && (arm_flags[1] & forms) != 0;
        uint32_t flags = (arm_flags[0] | arm_flags[1]) & BUSTER_CHECK_INHERITED;
        flags |= integer_common ? (ice ? BUSTER_CHECK_ICE : gnu_ice ? BUSTER_CHECK_GNU_ICE_FORM : 0) : 0;
        flags |= condition->flags & BUSTER_CHECK_INHERITED;
        node_facts_record(records, w, common, node_facts_word_of(view, common), BUSTER_NODE_MACHINE_STANDING(CHECK_RVALUE, CHECK_CONSTANT_NONE), node_facts_source(flags), 0, 0);
        if (!lean)
        {
            node_facts_conversions(records, w, converted[0], conversion_flags[0], converted[1], conversion_flags[1], 0);
        }
    }
    return taken;
}

// The operators' short paths: each its rule above for the shapes nearly all of its operators have, as straight code
// with the rule's record, as the lookups' are. A path takes only what its rule takes; an operator it doesn't take goes
// to the rule (node_facts_slow).

// What keeps a record of standing `standing`, flags `flags` and type `type`, whose type's row's flags are `row_flags`,
// from being read as a scalar's value by a short path, as bits (0 where it is fit): node_facts_unfit's without its
// constants (an rvalue keeps its constant, and an lvalue of an integer constant expression is the rule's), and a type
// that is no integer, floating or pointer type with no wrapper.
static inline __attribute__((always_inline)) uint32_t node_facts_unfit_scalar(TypeId type, uint32_t standing, uint32_t flags, uint32_t lvalue, uint32_t row_flags)
{
    const uint32_t converted = (flags & (BUSTER_CHECK_VOLATILE_PART | BUSTER_CHECK_ICE)) | (type & BUSTER_TYPE_VOLATILE);
    return (standing & (BUSTER_NODE_MACHINE_WIDE | CHECK_FUNCTION)) | (lvalue & converted) | (~row_flags & BUSTER_NODE_FACTS_SCALAR);
}

// An integer constant expression's form in a truth's value: an ICE's flag, or else a GNU form's.
static inline __attribute__((always_inline)) uint32_t node_facts_form(uint32_t flags)
{
    const uint32_t ice = flags & BUSTER_CHECK_ICE;
    return ice | ((flags & BUSTER_CHECK_GNU_ICE_FORM) & (0u - (ice == 0 ? 1u : 0u)));
}

// A truth's short path: a control's condition or `!` of a scalar's value that has no constant.
static inline __attribute__((always_inline)) bool node_facts_truth_fast(NodeFactsRecord* records, const NodeFactsView* view, uint32_t w, uint32_t a, bool lean)
{
    const TypeId type = records[a].type;
    const uint32_t standing = records[a].standing;
    const uint32_t flags = records[a].flags;
    const NodeFactsRow* row = node_facts_row_of(view, type);
    const uint32_t lvalue = 0u - (standing & 0x01u);
    const uint32_t unfit = node_facts_unfit_scalar(type, standing, flags, lvalue, row->flags) | (~lvalue & standing & 0x1cu);
    const bool fast = unfit == 0;
    if (fast)
    {
        node_facts_record(records, w, BUSTER_TYPE_INT, view->int_word, BUSTER_NODE_MACHINE_STANDING(CHECK_RVALUE, CHECK_CONSTANT_NONE),
                          (flags & BUSTER_CHECK_INHERITED) | node_facts_form(flags), 0, 0);
        if (!lean)
        {
            node_facts_conversions(records, w, type ^ ((type ^ row->value) & lvalue), lvalue & 1u, 0, 0, 0);
        }
    }
    return fast;
}

// `&&`'s and `||`'s short path: two scalars' values that have no constant, so that neither's truth is known.
static inline __attribute__((always_inline)) bool node_facts_logic_fast(NodeFactsRecord* records, const NodeFactsView* view, uint32_t w, uint32_t a, uint32_t b, bool lean)
{
    const TypeId left_type = records[a].type;
    const uint32_t left_standing = records[a].standing;
    const uint32_t left = records[a].flags;
    const NodeFactsRow* left_row = node_facts_row_of(view, left_type);
    const TypeId right_type = records[b].type;
    const uint32_t right_standing = records[b].standing;
    const uint32_t right = records[b].flags;
    const NodeFactsRow* right_row = node_facts_row_of(view, right_type);
    const uint32_t left_lvalue = 0u - (left_standing & 0x01u);
    const uint32_t right_lvalue = 0u - (right_standing & 0x01u);
    const uint32_t unfit = node_facts_unfit_scalar(left_type, left_standing, left, left_lvalue, left_row->flags) | (~left_lvalue & left_standing & 0x1cu) |
                           node_facts_unfit_scalar(right_type, right_standing, right, right_lvalue, right_row->flags) | (~right_lvalue & right_standing & 0x1cu);
    const bool fast = unfit == 0;
    if (fast)
    {
        const uint32_t forms = BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM;
        const uint32_t ice = left & right & BUSTER_CHECK_ICE;
        const uint32_t gnu = (left & forms) != 0 && (right & forms) != 0 ? BUSTER_CHECK_GNU_ICE_FORM : 0;
        node_facts_record(records, w, BUSTER_TYPE_INT, view->int_word, BUSTER_NODE_MACHINE_STANDING(CHECK_RVALUE, CHECK_CONSTANT_NONE),
                          ((left | right) & BUSTER_CHECK_INHERITED) | (ice != 0 ? ice : gnu), 0, 0);
        if (!lean)
        {
            node_facts_conversions(records, w, left_type ^ ((left_type ^ left_row->value) & left_lvalue), left_lvalue & 1u,
                                   right_type ^ ((right_type ^ right_row->value) & right_lvalue), right_lvalue & 1u, 0);
        }
    }
    return fast;
}

// A store's short path, `=` (`store`) or a conversion to the node's type: a scalar's value (no array's or function's
// pointer) to a scalar, node_facts_store_made's cases. A value that has no constant goes one way, straight: to an
// arithmetic type or its own pointer type on the record, or to another pointer type, which is the one branch (its
// pair's slot is probed). A constant goes the other: an integer's bits to an integer type, an integer's zero to a
// pointer (the null pointer), an address to its own or another pointer type. Whether a store's value is a constant is
// one branch the predictor may miss; the tests on either side are words of 0 or 1. A pointer with the null pointer's
// flag is the rule's.
static inline __attribute__((always_inline)) bool node_facts_store_fast(NodeFactsRecord* records, const NodeFactsView* view, uint32_t w, uint32_t a, uint32_t b, bool store,
                                                                         bool lean)
{
    const uint32_t s = store ? b : a;
    const TypeId source_type = records[s].type;
    const uint32_t source_standing = records[s].standing;
    const uint32_t source_flags = records[s].flags;
    const NodeFactsRow* source_row = node_facts_row_of(view, source_type);
    const uint32_t source_facts = source_row->flags;
    const TypeId destination = store ? records[a].type : view->inputs[w];
    const NodeFactsRow* target_row = node_facts_row_of(view, destination);
    const uint32_t target_facts = target_row->flags;
    const TypeId target = target_row->value;
    const uint32_t lvalue = 0u - (source_standing & 0x01u);
    // The destination of `=`: a modifiable lvalue the record holds whole, of no bit-field and no vector's duplicate.
    const uint32_t held = store ? ((records[a].standing & (0x03u | BUSTER_NODE_MACHINE_WIDE)) ^ CHECK_LVALUE) |
                                      (records[a].flags & (BUSTER_CHECK_VECTOR_DUPLICATE | BUSTER_CHECK_BIT_FIELD)) | (destination & BUSTER_TYPE_CONST) :
                                  0u;
    const uint32_t unfit = node_facts_unfit_scalar(source_type, source_standing, source_flags, lvalue, source_facts) | (~target_facts & BUSTER_NODE_FACTS_SCALAR) | held;
    // The source's value: an lvalue's has its value type, no constant and no register's flag.
    const TypeId value_type = source_type ^ ((source_type ^ source_row->value) & lvalue);
    const uint32_t value_flags = source_flags & ~(lvalue & (BUSTER_CHECK_REGISTER | BUSTER_CHECK_REGISTER_NAME | BUSTER_CHECK_VOLATILE_PART));
    const uint32_t kind = ((source_standing >> 2) & 0x07u) & ~lvalue;
    const uint32_t field = source_standing & BUSTER_NODE_MACHINE_FIELD;
    // A builtin arithmetic type's row says so (its value type has its row); the source's must have no qualifier.
    const uint32_t unqualified = (value_type & BUSTER_TYPE_QUALIFIERS) == 0 ? 1u : 0u;
    const uint32_t arithmetic = unqualified & ((source_facts & target_facts & BUSTER_NODE_FACTS_ARITHMETIC) != 0 ? 1u : 0u);
    const uint32_t pointer = (target_facts & BUSTER_NODE_FACTS_POINTER) != 0 ? 1u : 0u;
    const uint32_t source_pointer = (source_facts & BUSTER_NODE_FACTS_POINTER) != 0 ? 1u : 0u;
    const uint32_t same = value_type == target ? 1u : 0u;
    const uint32_t unflagged = (value_flags & BUSTER_CHECK_NULL_POINTER) == 0 ? 1u : 0u;
    // Its own pointer type, or another: the pair's slot says whether the conversion is one T's rule takes.
    const uint32_t own = pointer & same & unflagged;
    const uint32_t other = pointer & source_pointer & (same ^ 1u) & unflagged;
    uint32_t restricted = 0;
    uint32_t null = 0;
    uint32_t standing = BUSTER_NODE_MACHINE_STANDING(CHECK_RVALUE, CHECK_CONSTANT_NONE) | kind << 2 | field;
    uint32_t object = records[s].object;
    uint64_t payload = records[s].payload;
    bool fast = unfit == 0;
    if (fast && kind == CHECK_CONSTANT_NONE)
    {
        // A conversion to bool of no constant has its integer 0.
        fast = (arithmetic | own | other) != 0;
        payload = (arithmetic & (target == BUSTER_TYPE_BOOL ? 1u : 0u) & (field == 0 ? 1u : 0u)) != 0 ? 0 : payload;
    }
    else if (fast)
    {
        // An integer constant: its bits as check_action_convert_plain converts them between builtin integer types of at
        // most 64 bits but bool (a row's width; 0 for another type), or the null pointer where it is a zero in a form
        // the unit takes, an address constant with no object. An address constant: to a pointer type.
        const uint32_t integer = kind == CHECK_CONSTANT_INTEGER && field == 0 ? 1u : 0u;
        const uint32_t source_width = (source_facts >> 8 & 0xffu) & (0u - unqualified);
        const uint32_t target_width = target_facts >> 8 & 0xffu;
        const uint64_t source_mask = (source_width == 64 ? 0 : UINT64_C(1) << (source_width & 63u)) - 1;
        const uint64_t target_mask = (target_width == 64 ? 0 : UINT64_C(1) << (target_width & 63u)) - 1;
        const uint64_t masked = payload & source_mask;
        const uint32_t sized = integer & (source_width != 0 ? 1u : 0u);
        const uint32_t extends = (source_facts & BUSTER_NODE_FACTS_SIGNED) != 0 && (masked >> ((source_width - 1) & 63u)) != 0 && target_width > source_width ? 1u : 0u;
        const uint32_t converted = arithmetic & sized & (target_width != 0 ? 1u : 0u);
        const uint32_t address = kind == CHECK_CONSTANT_ADDRESS ? 1u : 0u;
        null = pointer & (source_pointer ^ 1u) & unflagged & sized & ((value_flags & view->null_forms) != 0 ? 1u : 0u) & (masked == 0 ? 1u : 0u);
        fast = (converted | null | (address & (own | other))) != 0;
        payload = converted != 0 ? (masked | (~source_mask & (0 - (uint64_t)extends))) & target_mask : payload;
        standing = null != 0 ? BUSTER_NODE_MACHINE_STANDING(CHECK_RVALUE, CHECK_CONSTANT_ADDRESS) : standing;
        object &= 0u - (null ^ 1u);
        restricted = null;
    }
    if (fast && other != 0 && null == 0)
    {
        const uint32_t pair = node_facts_pair(NULL, view, target, value_type)->flags;
        fast = (pair & BUSTER_NODE_FACTS_PAIR_ASSIGNABLE) != 0;
        restricted = (pair & BUSTER_NODE_FACTS_PAIR_SAME) == 0 ? 1u : 0u;
    }
    if (fast)
    {
        const uint32_t stored = BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM | BUSTER_CHECK_FLOAT_LITERAL | BUSTER_CHECK_ICE_UNEVALUATED;
        const uint32_t forms = (target_facts & BUSTER_NODE_FACTS_INTEGRAL) != 0 ? 0u : (uint32_t)(BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM);
        uint32_t flags = (value_flags | ((uint32_t)BUSTER_CHECK_CONST_READ_RESTRICTED & (0u - restricted))) &
                         ~(BUSTER_CHECK_BIT_FIELD | BUSTER_CHECK_REGISTER | BUSTER_CHECK_REGISTER_NAME | BUSTER_CHECK_NULL_POINTER | forms);
        flags = store ? (flags & ~stored) | BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_MODIFICATION | (records[a].flags & BUSTER_CHECK_INHERITED) : flags;
        NodeFactsRecord* record = records + w;
        const uint32_t exponent = node_facts_exponent(standing, payload, records[s].exponent);
        node_facts_store(record, 0, (uint64_t)target | (uint64_t)target_row->word << 32);
        node_facts_store(record, 8, (uint64_t)node_facts_source(flags) | (uint64_t)object << 32);
        node_facts_store(record, 16, payload);
        node_facts_store(record, 24, (uint64_t)standing | 0x100u | (uint64_t)exponent << 16);
        if (!lean)
        {
            const uint32_t conversion = (lvalue & 1u) | BUSTER_CHECK_CONVERT_ASSIGNMENT | ((uint32_t)BUSTER_CHECK_CONVERT_NULL & (0u - null));
            node_facts_conversions(records, w, store ? records[a].type : target, store ? 0 : conversion, store ? target : 0, store ? conversion : 0, 0);
        }
    }
    return fast;
}

// The places of a window's leaves that have a line and of its made operators, from its `count` records' kinds (whose
// low two bits are 2 and 3), as two lists; the operators' classes beside their places (`keys`, from the records'
// `classes`); and the rounds that have an operator, as a mask (`rounds`). Sixteen records a step, each kind's places
// compressed to the front of a vector and the vector stored whole at the list's end (compress to a register and a plain
// store: Zen 5's compress to memory is 8 uops), with no branch a record. On a host without the vectors, a record at a
// time: the same lists (node_facts_lists_same is the twin's check on either).
static inline __attribute__((always_inline)) void node_facts_list(uint32_t* leaf_list, uint32_t* lookup_list, uint8_t* keys, const uint8_t* kinds, const uint8_t* classes,
                                                                   uint32_t count, uint32_t* leaf_count, uint32_t* lookup_count, uint32_t* rounds)
{
    uint32_t leaves = 0;
    uint32_t lookups = 0;
    uint32_t present = 0;
    const __m512i step = _mm512_set1_epi32(16);
    __m512i places = _mm512_set_epi32(15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);
    __m512i seen = _mm512_setzero_si512();
    for (uint32_t w = 0; w < count; w += 16)
    {
        const uint32_t live = count - w < 16 ? count - w : 16;
        const __mmask16 mask = (__mmask16)((1u << live) - 1);
        const __m128i block = _mm_and_si128(_mm_maskz_loadu_epi8(mask, kinds + w), _mm_set1_epi8(0x03));
        const __mmask16 leaf = _mm_mask_cmpeq_epi8_mask(mask, block, _mm_set1_epi8(0x02));
        const __mmask16 lookup = _mm_mask_cmpeq_epi8_mask(mask, block, _mm_set1_epi8(0x03));
        const __m128i own = _mm_maskz_loadu_epi8(lookup, classes + w);
        _mm512_storeu_si512(leaf_list + leaves, _mm512_maskz_compress_epi32(leaf, places));
        _mm512_storeu_si512(lookup_list + lookups, _mm512_maskz_compress_epi32(lookup, places));
        _mm_storeu_si128((__m128i*)(void*)(keys + lookups), _mm_maskz_compress_epi8(lookup, own));
        seen = _mm512_or_si512(seen, _mm512_maskz_sllv_epi32(lookup, _mm512_set1_epi32(1), _mm512_srli_epi32(_mm512_cvtepu8_epi32(own), 4)));
        leaves += (uint32_t)__builtin_popcount((uint32_t)leaf);
        lookups += (uint32_t)__builtin_popcount((uint32_t)lookup);
        places = _mm512_add_epi32(places, step);
    }
    present = (uint32_t)_mm512_reduce_or_epi32(seen);
    *leaf_count = leaves;
    *lookup_count = lookups;
    *rounds = present;
}

// The window's `lookups` made operators, whose classes are `keys`, each appended to its class's list (`batches`, a
// window's most a list; the deeper operators together in the last), in the window's order; `sizes`, each list's
// length, which the caller has zeroed. One pass, a load of the class's length and two stores an operator, with no
// branch. Scalar, where the listing is a vector's compress. A body's expressions have about ten classes of five
// operators each, and the vector ways to their lists measured slower on the 9700X (docs/one-core-path.md, "Step 2's
// first piece"): a compress a class scans the window's operators once for each class it has, and the scans' ends are
// branches the predictor misses; a compress a code in the listing's own steps is fifteen compresses a step whatever
// the window holds. What this pass costs is its lengths, counts in memory, each a load after the store before it.
static inline __attribute__((always_inline)) void node_facts_distribute(uint32_t* batches, uint32_t* sizes, const uint32_t* lookup_list, const uint8_t* keys, uint32_t lookups)
{
    for (uint32_t i = 0; i < lookups; i += 1)
    {
        const uint32_t key = keys[i];
        const uint32_t list = key < 16u * BUSTER_NODE_FACTS_ROUNDS ? key : 16u * BUSTER_NODE_FACTS_ROUNDS;
        const uint32_t at = sizes[list];
        batches[(uint64_t)list * BUSTER_NODE_FACTS_WINDOW + at] = lookup_list[i];
        sizes[list] = at + 1;
    }
}

// A record the kernel leaves, the window's record `w` (the unit's `at`): T's record of it, as the rule would make it.
static __attribute__((noinline)) void node_facts_leave(NodeFacts* facts, uint64_t at, uint32_t w)
{
    facts->records[w] = node_facts_expected(facts, at);
    facts->records[w].taken = BUSTER_NODE_FACTS_LEFT;
}

// The lookups' short paths. Each is its rule above for the shapes nearly every lookup has, as straight code: the
// operand's type's row says in one test of its flags what the rule asks the type table in several, what makes the
// operand unfit for the path is gathered into one word and tested once, and the few values that depend on the operand
// are selected, not branched on. A path takes only what its rule takes, with the rule's record; a lookup it doesn't
// take goes to the rule (node_facts_slow), so the two together make what the rule alone does.

// What keeps a record of standing `standing`, flags `flags` and type `type` from being an operand's value by the short
// path, as bits (0 where it is fit): a wide value or a function designator; of an lvalue, which converts to its value
// (node_facts_operand's third case), a volatile one or, with `constant`, one of an integer constant expression (whose
// value keeps its constant); of an rvalue, with `constant`, a constant. `lvalue` is all ones for an lvalue.
static inline __attribute__((always_inline)) uint32_t node_facts_unfit(TypeId type, uint32_t standing, uint32_t flags, uint32_t lvalue, bool constant)
{
    _Static_assert(CHECK_RVALUE == 0 && CHECK_LVALUE == 1 && CHECK_FUNCTION == 2 && BUSTER_CHECK_CONVERT_LVALUE == 1, "an lvalue's category is its conversion's flag");
    const uint32_t converted = (flags & (BUSTER_CHECK_VOLATILE_PART | (constant ? BUSTER_CHECK_ICE : 0))) | (type & BUSTER_TYPE_VOLATILE);
    const uint32_t kept = constant ? standing & 0x1cu : 0;
    return (standing & (BUSTER_NODE_MACHINE_WIDE | CHECK_FUNCTION)) | (lvalue & converted) | (~lvalue & kept);
}

// `->`'s short path: a pointer value, no constant, to a struct or union complete at the node, and a member that is no
// bit-field.
static inline __attribute__((always_inline)) bool node_facts_arrow_fast(NodeFactsRecord* records, const NodeFactsView* view, uint32_t w, uint32_t a, bool lean)
{
    const TypeId type = records[a].type;
    const uint32_t standing = records[a].standing;
    const uint32_t flags = records[a].flags;
    const NodeFactsRow* row = node_facts_row_of(view, type);
    const uint32_t lvalue = 0u - (standing & 0x01u);
    const uint32_t unfit = node_facts_unfit(type, standing, flags, lvalue, true) | (~row->flags & BUSTER_NODE_FACTS_ARROW);
    bool fast = unfit == 0 && row->element_complete_at <= view->positions[w];
    if (fast)
    {
        const NodeFactsMember* slot = node_facts_member_kept(view, row->element, view->inputs[w]);
        fast = (slot->flags & (BUSTER_NODE_FACTS_MEMBER_FOUND | BUSTER_NODE_FACTS_MEMBER_BIT_FIELD)) == BUSTER_NODE_FACTS_MEMBER_FOUND;
        if (fast)
        {
            // Its base's inherited flags and its object's; a read through an inactive union's address is one of an
            // inactive union (the flag below the address's).
            _Static_assert(BUSTER_CHECK_INACTIVE_UNION_ADDRESS >> 1 == BUSTER_CHECK_INACTIVE_UNION_READ, "an inactive union's address's flag shifts to its read's");
            const uint32_t kept = BUSTER_CHECK_INHERITED | BUSTER_CHECK_STATIC_OBJECT | BUSTER_CHECK_CONST_READ_RESTRICTED | BUSTER_CHECK_NONNULL_ADDRESS;
            const uint32_t result = (flags & kept) | ((flags >> 1) & BUSTER_CHECK_INACTIVE_UNION_READ) | slot->result_flags;
            node_facts_record(records, w, slot->type, slot->word, BUSTER_NODE_MACHINE_STANDING(CHECK_LVALUE, CHECK_CONSTANT_NONE), result, 0, 0);
            if (lean)
            {
                node_facts_store(records + w, 48, slot->offset);
            }
            else
            {
                node_facts_conversions(records, w, lvalue != 0 ? row->value : type, lvalue & 1u, 0, 0, slot->offset);
            }
        }
    }
    return fast;
}

// `.`'s short path: a struct or union complete at the node, no constant or an address constant, and a member that is no
// bit-field, an address constant's at a whole byte.
static inline __attribute__((always_inline)) bool node_facts_dot_fast(NodeFactsRecord* records, const NodeFactsView* view, uint32_t w, uint32_t a, bool lean)
{
    const TypeId type = records[a].type;
    const uint32_t standing = records[a].standing;
    const uint32_t flags = records[a].flags;
    const NodeFactsRow* row = node_facts_row_of(view, type);
    const uint32_t kind = (standing >> 2) & 0x07u;
    const bool address = kind == CHECK_CONSTANT_ADDRESS;
    const uint32_t unfit = (standing & BUSTER_NODE_MACHINE_WIDE) | (~row->flags & BUSTER_NODE_FACTS_RECORD) | (kind != CHECK_CONSTANT_NONE && !address ? 1u : 0u);
    bool fast = unfit == 0 && row->complete_at <= view->positions[w];
    if (fast)
    {
        // The operand's payload and object are read whether it is an address constant or not, and chosen by masks: a
        // choice with a load behind it is compiled as a branch, and whether a record is an address constant's (a static
        // object's member) is one the predictor misses.
        const NodeFactsMember* slot = node_facts_member_kept(view, type, view->inputs[w]);
        const uint64_t offset = slot->offset;
        const uint64_t payload = records[a].payload;
        const uint32_t object = records[a].object;
        const uint64_t addressed = 0 - (uint64_t)(address ? 1u : 0u);
        const uint64_t based = 0 - (uint64_t)((standing & BUSTER_NODE_MACHINE_FIELD) == BUSTER_NODE_MACHINE_OFFSET ? 1u : 0u);
        int64_t moved = 0;
        const bool overflow = __builtin_add_overflow((int64_t)(payload & based), (int64_t)(offset / 8), &moved);
        const uint32_t misplaced = (uint32_t)addressed & ((offset % 8 != 0 ? 1u : 0u) | (overflow ? 1u : 0u));
        fast = (((slot->flags ^ BUSTER_NODE_FACTS_MEMBER_FOUND) & (BUSTER_NODE_FACTS_MEMBER_FOUND | BUSTER_NODE_FACTS_MEMBER_BIT_FIELD)) | misplaced) == 0;
        if (fast)
        {
            const int64_t addend = (int64_t)((uint64_t)moved & addressed);
            const uint32_t kept = BUSTER_CHECK_INHERITED | BUSTER_CHECK_STATIC_OBJECT | BUSTER_CHECK_CONST_READ_RESTRICTED | BUSTER_CHECK_NONNULL_ADDRESS | BUSTER_CHECK_REGISTER;
            const uint32_t category = (standing & 0x03u) == CHECK_LVALUE ? CHECK_LVALUE : CHECK_RVALUE;
            _Static_assert(CHECK_CONSTANT_NONE == 0, "a standing with no constant is its category alone");
            const uint8_t made = (uint8_t)(BUSTER_NODE_MACHINE_STANDING(category, CHECK_CONSTANT_NONE) | ((uint32_t)addressed & BUSTER_NODE_MACHINE_STANDING(0, CHECK_CONSTANT_ADDRESS)) |
                                           (addend != 0 ? BUSTER_NODE_MACHINE_OFFSET : 0));
            node_facts_record(records, w, slot->type, slot->word, made, (flags & kept) | slot->result_flags, object & (uint32_t)addressed, (uint64_t)addend);
            if (lean)
            {
                node_facts_store(records + w, 48, offset);
            }
            else
            {
                node_facts_conversions(records, w, type, 0, 0, 0, offset);
            }
        }
    }
    return fast;
}

// A dereference's short path: a pointer's value. Its record is its operand's with the pointee's type, an lvalue or a
// function's designator (node_facts_dereference's steps).
static inline __attribute__((always_inline)) bool node_facts_dereference_fast(NodeFactsRecord* records, const NodeFactsView* view, uint32_t w, uint32_t a, bool lean)
{
    const TypeId type = records[a].type;
    const uint32_t standing = records[a].standing;
    const uint32_t flags = records[a].flags;
    const NodeFactsRow* row = node_facts_row_of(view, type);
    const uint32_t lvalue = 0u - (standing & 0x01u);
    const uint32_t kind = (standing >> 2) & 0x07u;
    const uint32_t unfit = node_facts_unfit(type, standing, flags, lvalue, false) | (~row->flags & BUSTER_NODE_FACTS_POINTER);
    const bool fast = unfit == 0;
    if (fast)
    {
        // The operand's value's flags (an lvalue's without its register's), then the dereference's.
        const uint32_t converted = lvalue != 0 ? flags & ~(BUSTER_CHECK_REGISTER | BUSTER_CHECK_REGISTER_NAME | BUSTER_CHECK_VOLATILE_PART) : flags;
        const uint32_t dropped = BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM | BUSTER_CHECK_NULL_POINTER | BUSTER_CHECK_FLOAT_LITERAL | BUSTER_CHECK_ICE_UNEVALUATED |
                                 BUSTER_CHECK_LITERAL_ZERO;
        const uint32_t result = (converted | ((converted >> 1) & BUSTER_CHECK_INACTIVE_UNION_READ)) & ~dropped;
        const uint32_t constant_kind = lvalue != 0 && (kind == CHECK_CONSTANT_ADDRESS || (flags & BUSTER_CHECK_ICE) == 0) ? CHECK_CONSTANT_NONE : kind;
        const CheckCategory category = (uint8_t)(row->element_word >> 16) == BUSTER_TYPE_KIND_FUNCTION ? CHECK_FUNCTION : CHECK_LVALUE;
        node_facts_from(records, w, a, row->element, row->element_word, category, result, (uint8_t)constant_kind);
        if (!lean)
        {
            node_facts_conversions(records, w, lvalue != 0 ? row->value : type, lvalue & 1u, 0, 0, 0);
        }
    }
    return fast;
}

// A subscript's short path: a pointer value or an array at its left, an integer value at its right, the element an
// object type complete at the node, and no constant index of an address constant (the rule folds that one).
static inline __attribute__((always_inline)) bool node_facts_subscript_fast(NodeFactsRecord* records, const NodeFactsView* view, uint32_t w, uint32_t a, uint32_t b,
                                                                             bool lean)
{
    const TypeId pointer_type = records[a].type;
    const uint32_t pointer_standing = records[a].standing;
    const uint32_t pointer_flags = records[a].flags;
    const NodeFactsRow* row = node_facts_row_of(view, pointer_type);
    const TypeId index_type = records[b].type;
    const uint32_t index_standing = records[b].standing;
    const uint32_t index_flags = records[b].flags;
    const NodeFactsRow* index_row = node_facts_row_of(view, index_type);
    const uint32_t pointer_lvalue = 0u - (pointer_standing & 0x01u);
    const uint32_t index_lvalue = 0u - (index_standing & 0x01u);
    const uint32_t pointer_kind = (pointer_standing >> 2) & 0x07u;
    const uint32_t index_kind = (index_standing >> 2) & 0x07u;
    // The pointer: a pointer's value, or an array's pointer to its element (node_facts_operand's first case), and what
    // constant is left of it. Every test below is a word of 0 or 1 and every choice a mask: whether the pointer is an
    // array, an lvalue or a constant, and the index a constant, differ from one subscript to the next, and a branch on
    // any of them is one the predictor misses. What a choice would load is loaded whichever way it goes.
    const uint32_t array = (row->flags & BUSTER_NODE_FACTS_ARRAY) != 0 ? 1u : 0u;
    const uint32_t arrayed = 0u - array;
    const uint32_t qualifiers = type_qualifiers(pointer_type);
    const TypeId pointer_of = row->decayed[qualifiers & BUSTER_TYPE_CONST];
    const uint32_t decays = ((pointer_flags & BUSTER_CHECK_REGISTER_NAME) == 0 ? 1u : 0u) & (qualifiers <= BUSTER_TYPE_CONST ? 1u : 0u);
    const TypeId decayed = pointer_of & (0u - decays);
    const uint32_t as_array = (pointer_standing & BUSTER_NODE_MACHINE_WIDE) | (decayed == 0 ? 1u : 0u);
    const uint32_t as_value = node_facts_unfit(pointer_type, pointer_standing, pointer_flags, pointer_lvalue, false) | (~row->flags & BUSTER_NODE_FACTS_POINTER);
    const uint32_t pointer_none = pointer_kind == CHECK_CONSTANT_NONE ? 1u : 0u;
    const uint32_t pointer_address = pointer_kind == CHECK_CONSTANT_ADDRESS ? 1u : 0u;
    const uint32_t pointer_value = pointer_lvalue == 0 ? 1u : 0u;
    const uint32_t pointer_ice = (pointer_flags & BUSTER_CHECK_ICE) != 0 ? 1u : 0u;
    const uint32_t pointer_static = (pointer_flags & BUSTER_CHECK_STATIC_OBJECT) != 0 ? 1u : 0u;
    const uint32_t array_constant = (pointer_none ^ 1u) & ((pointer_address & (pointer_static ^ 1u)) ^ 1u);
    const uint32_t value_constant = (pointer_none ^ 1u) & (pointer_value | ((pointer_address ^ 1u) & pointer_ice));
    const uint32_t constant = (array & array_constant) | ((array ^ 1u) & value_constant);
    // An address constant's element at an index that has no constant is no constant either; the lvalue of a pointer is
    // never an address constant, an array's is its static object's.
    const uint32_t address = (array & pointer_address & array_constant) | ((array ^ 1u) & pointer_value & pointer_address);
    // The index: an integer's value, promoted.
    const uint32_t index_none = index_kind == CHECK_CONSTANT_NONE ? 1u : 0u;
    const uint32_t index_address = index_kind == CHECK_CONSTANT_ADDRESS ? 1u : 0u;
    const uint32_t index_rvalue = index_lvalue == 0 ? 1u : 0u;
    const uint32_t index_ice = (index_flags & BUSTER_CHECK_ICE) != 0 ? 1u : 0u;
    const uint32_t index_constant = (index_none ^ 1u) & (index_rvalue | ((index_address ^ 1u) & index_ice));
    const uint32_t wanted = BUSTER_NODE_FACTS_SCALAR | BUSTER_NODE_FACTS_INTEGER;
    const uint32_t unfit = (arrayed & as_array) | (~arrayed & as_value) | node_facts_unfit(index_type, index_standing, index_flags, index_lvalue, false) |
                           (~index_row->flags & wanted) | (index_flags & BUSTER_CHECK_BIT_FIELD) | (~row->flags & BUSTER_NODE_FACTS_OBJECTS) |
                           (constant & ((address & (index_constant ^ 1u)) ^ 1u));
    const bool fast = unfit == 0 && row->element_complete_at <= view->positions[w];
    if (fast)
    {
        const TypeId index_value = index_type ^ ((index_type ^ index_row->value) & index_lvalue);
        const TypeId promoted = index_row->promoted;
        const uint32_t promotes = promoted != index_value ? 1u : 0u;
        const uint32_t kept = BUSTER_CHECK_STATIC_OBJECT | BUSTER_CHECK_INACTIVE_UNION_ADDRESS | BUSTER_CHECK_CONST_READ_RESTRICTED | BUSTER_CHECK_NONNULL_ADDRESS;
        const uint32_t held = arrayed & ((pointer_flags & (BUSTER_CHECK_REGISTER | BUSTER_CHECK_REGISTER_NAME)) == BUSTER_CHECK_REGISTER ? BUSTER_CHECK_REGISTER : 0);
        const uint32_t result = ((pointer_flags | index_flags) & BUSTER_CHECK_INHERITED) | (pointer_flags & kept) | ((pointer_flags >> 1) & BUSTER_CHECK_INACTIVE_UNION_READ) | held;
        node_facts_record(records, w, row->element | (arrayed & qualifiers), row->element_word, BUSTER_NODE_MACHINE_STANDING(CHECK_LVALUE, CHECK_CONSTANT_NONE), result, 0, 0);
        if (!lean)
        {
            const TypeId pointer_value_type = pointer_type ^ ((pointer_type ^ row->value) & pointer_lvalue);
            node_facts_conversions(records, w, (arrayed & decayed) | (~arrayed & pointer_value_type), (arrayed & BUSTER_CHECK_CONVERT_ARRAY) | (~arrayed & pointer_lvalue & 1u),
                                   promoted, (index_lvalue & 1u) | ((0u - promotes) & BUSTER_CHECK_CONVERT_PROMOTION), 0);
        }
    }
    return fast;
}

// A call's short path: a function's designator, or a pointer to a function's value, of a prototype that takes the
// arguments' count and whose return type is void or complete at the node, with no wide argument.
static inline __attribute__((always_inline)) bool node_facts_call_fast(NodeFactsRecord* records, const NodeFactsView* view, uint32_t w, uint32_t a, bool lean)
{
    const TypeId type = records[a].type;
    const uint32_t standing = records[a].standing;
    const uint32_t flags = records[a].flags;
    const NodeFactsRow* row = node_facts_row_of(view, type);
    const uint32_t function = row->function;
    const uint32_t parameters = function >> 16;
    const uint32_t count = view->counts[w];
    const uint16_t* arguments = view->arguments + view->inputs[w];
    const bool designator = (function & BUSTER_NODE_FACTS_DESIGNATOR) != 0;
    const TypeId pointer = row->decayed[0];
    const uint32_t lvalue = 0u - (standing & 0x01u);
    const uint32_t as_designator = (standing & BUSTER_NODE_MACHINE_WIDE) | (pointer == 0 ? 1u : 0u);
    const uint32_t as_value = node_facts_unfit(type, standing, flags, lvalue, false) | (~row->flags & BUSTER_NODE_FACTS_SCALAR);
    const uint32_t wanted = BUSTER_NODE_FACTS_FUNCTION | BUSTER_NODE_FACTS_RETURNS | BUSTER_TYPE_FLAG_PROTOTYPE << 8;
    // Every argument's flags and standing, gathered whether the call is taken or not. A loop of the arguments' count
    // would end at a branch the predictor misses once a call; the first four are read with no branch, the callee's own
    // record standing for an argument past the count (its flags are gathered already, and a wide callee is unfit), and
    // the few calls of more have a loop for the rest. An argument's distance is read whether the call has the argument
    // or not (the list has entries to spare past its last call's), so that the choice is between two places in
    // registers: a choice with a load behind it is compiled as a branch on the count, which the predictor misses.
    // What the census's tests ask of the arguments is gathered with them (node_facts_argument): the callee's record,
    // which stands for an argument past the count, has an address's constant or none, and no aggregate's type.
    uint32_t gathered = flags;
    uint32_t standings = 0;
    NodeFactsArguments asked = { 0 };
#pragma clang loop vectorize(disable) interleave(disable) unroll(full)
    for (uint32_t i = 0; i < 4; i += 1)
    {
        const uint32_t distance = arguments[i];
        const uint32_t place = i < count ? w - distance : a;
        gathered |= records[place].flags;
        standings |= records[place].standing;
        node_facts_argument(&asked, records + place);
    }
#pragma clang loop vectorize(disable) interleave(disable) unroll(disable)
    for (uint32_t i = 4; i < count; i += 1)
    {
        const NodeFactsRecord* argument = records + (w - arguments[i]);
        gathered |= argument->flags;
        standings |= argument->standing;
        node_facts_argument(&asked, argument);
    }
    const uint32_t unfit = (designator ? as_designator : as_value) | (~function & wanted) | (standings & BUSTER_NODE_MACHINE_WIDE) |
                           (count < parameters || ((function >> 8 & BUSTER_TYPE_FLAG_VARIADIC) == 0 && count != parameters) ? 1u : 0u);
    const bool fast = unfit == 0 && row->returned_ready_at <= view->positions[w];
    if (fast)
    {
        const uint8_t made = BUSTER_NODE_MACHINE_STANDING(CHECK_RVALUE, CHECK_CONSTANT_NONE);
        node_facts_record(records, w, row->returned, row->returned_word, made, BUSTER_CHECK_SIDE_EFFECT | (gathered & BUSTER_CHECK_INHERITED), 0, 0);
        node_facts_store(records + w, 24, (uint64_t)made | 0x100u | (uint64_t)node_facts_arguments_quiet(&asked, function) << 16);
        if (!lean)
        {
            node_facts_conversions(records, w, designator ? pointer : lvalue != 0 ? row->value : type, designator ? BUSTER_CHECK_CONVERT_FUNCTION : lvalue & 1u, 0, 0, 0);
        }
    }
    return fast;
}

// A discard's short path, its rule with its tests as one word: a cast to void of an rvalue the record holds whole, of no
// array, function, vector or complex type and no _BitInt constant, in a unit with no overload candidates (`plain`).
static inline __attribute__((always_inline)) bool node_facts_discard_fast(NodeFactsRecord* records, const NodeFactsView* view, bool plain, uint32_t w, uint32_t a)
{
    _Static_assert(BUSTER_TYPE_KIND_COUNT <= 32, "a type's kind is a bit of a word");
    const uint32_t refused = 1u << BUSTER_TYPE_KIND_ARRAY | 1u << BUSTER_TYPE_KIND_INCOMPLETE_ARRAY | 1u << BUSTER_TYPE_KIND_VARIABLE_ARRAY | 1u << BUSTER_TYPE_KIND_FUNCTION |
                             1u << BUSTER_TYPE_KIND_VECTOR | 1u << BUSTER_TYPE_KIND_EXT_VECTOR | 1u << BUSTER_TYPE_KIND_COMPLEX;
    const uint32_t standing = records[a].standing;
    const uint32_t kind = (records[a].word >> 16) & 0xffu;
    const uint32_t constant_kind = (standing >> 2) & 0x07u;
    const uint32_t unfit = (standing & (BUSTER_NODE_MACHINE_WIDE | 0x03u)) | (kind >= 32 ? 1u : (refused >> kind) & 1u) |
                           (kind == BUSTER_TYPE_KIND_BIT_INT && constant_kind != CHECK_CONSTANT_NONE ? 1u : 0u) | (plain ? 0u : 1u);
    const bool fast = unfit == 0;
    if (fast)
    {
        const uint32_t restricted = kind == BUSTER_TYPE_KIND_POINTER ? BUSTER_CHECK_CONST_READ_RESTRICTED : 0;
        const uint32_t dropped = BUSTER_CHECK_BIT_FIELD | BUSTER_CHECK_REGISTER | BUSTER_CHECK_REGISTER_NAME | BUSTER_CHECK_NULL_POINTER | BUSTER_CHECK_ICE |
                                 BUSTER_CHECK_GNU_ICE_FORM | BUSTER_CHECK_FLOAT_LITERAL | BUSTER_CHECK_ICE_UNEVALUATED | BUSTER_CHECK_MODIFICATION |
                                 BUSTER_CHECK_LITERAL_ZERO;
        node_facts_from(records, w, a, BUSTER_TYPE_VOID, view->void_word, CHECK_RVALUE, (records[a].flags | restricted) & ~dropped, CHECK_CONSTANT_NONE);
        node_facts_conversions(records, w, BUSTER_TYPE_VOID, BUSTER_CHECK_CONVERT_CAST, 0, 0, 0);
    }
    return fast;
}

// What the kernel reads of the window whose first record is the unit's `first`, beside its records.
static inline __attribute__((always_inline)) NodeFactsView node_facts_view(const NodeFacts* facts, uint32_t first)
{
    return (NodeFactsView){
        .inputs = facts->inputs + first,
        .counts = facts->counts + first,
        .positions = facts->positions + first,
        .arguments = facts->arguments,
        .lines = facts->lines,
        .rows = facts->rows,
        .members = facts->members,
        .sides = facts->sides,
        .pairs = facts->pairs,
        .stands = facts->stands,
        .row_count = facts->row_count,
        .member_bits = facts->member_bits,
        .void_word = facts->void_word,
        .int_word = facts->int_word,
        .ulong_word = facts->ulong_word,
        .null_forms = BUSTER_CHECK_ICE | ((facts->machine->graph->extensions & BUSTER_CHECK_EXTENSION_GNU_CONSTANT) != 0 ? BUSTER_CHECK_GNU_ICE_FORM : 0),
    };
}

// A made operator no class's loop took, or one of a level past the classes', record `w` of the window whose first
// record is the unit's `first`: its rule or, a row's operator's, its rules in T's order (its row, then pointer
// arithmetic or the constants' fold, or the fold or a pointer's equality with a null pointer constant), each making the
// record whole; and where none takes it, T's record.
static __attribute__((noinline)) void node_facts_slow(NodeFacts* facts, uint32_t first, uint32_t w)
{
    NodeFactsRecord* const records = facts->records;
    const NodeFactsView view = node_facts_view(facts, first);
    const CheckOp op = (CheckOp)facts->ops[first + w];
    const uint32_t left = facts->left[first + w];
    const uint32_t right = facts->right[first + w];
    const bool binary = right != 0;
    // A record with no operand (sizeof of a type name) reads none; a unary operator's second is its first.
    const uint32_t a = w - left;
    const uint32_t b = binary ? w - right : a;
    const bool operators = facts->kinds[first + w] == BUSTER_NODE_FACTS_OPERATOR;
    bool taken = false;
    switch (op)
    {
        case CHECK_OP_ARROW: case CHECK_OP_MEMBER:
        {
            taken = node_facts_member(facts, records, &view, w, a, op == CHECK_OP_ARROW);
        } break;
        case CHECK_OP_DEREFERENCE:
        {
            taken = node_facts_dereference(records, &view, w, a);
        } break;
        case CHECK_OP_SUBSCRIPT:
        {
            taken = binary && node_facts_pointer(records, &view, w, a, b, CHECK_OP_SUBSCRIPT);
        } break;
        case CHECK_OP_CALL:
        {
            taken = node_facts_call(records, &view, w, a);
        } break;
        case CHECK_OP_CAST:
        {
            taken = operators ? node_facts_cast(facts, records, &view, w, a, false) :
                                node_facts_discard(records, &view, facts->machine->graph->overload_candidate_count == 0, w, a);
        } break;
        case CHECK_OP_CONDITION: case CHECK_OP_NOT:
        {
            taken = node_facts_truth(records, &view, w, a, op == CHECK_OP_NOT, false);
        } break;
        case CHECK_OP_ASSIGN:
        {
            taken = binary && node_facts_store_made(facts, records, &view, w, a, b, true, false);
        } break;
        case CHECK_OP_CONVERT: case CHECK_OP_INITIALIZE:
        {
            taken = node_facts_store_made(facts, records, &view, w, a, a, false, false);
        } break;
        case CHECK_OP_ADDRESS:
        {
            taken = node_facts_address(records, &view, w, a, false);
        } break;
        case CHECK_OP_LOGICAL_AND: case CHECK_OP_LOGICAL_OR:
        {
            taken = binary && node_facts_logic(records, &view, w, a, b, op == CHECK_OP_LOGICAL_AND, false);
        } break;
        case CHECK_OP_SIZEOF: case CHECK_OP_ALIGNOF:
        {
            taken = node_facts_query(records, &view, w, a, left != 0, op == CHECK_OP_SIZEOF, false);
        } break;
        case CHECK_OP_COMMA:
        {
            taken = binary && node_facts_comma(records, &view, w, a, b, false);
        } break;
        case CHECK_OP_CONDITIONAL:
        {
            const uint32_t third = view.inputs[w];
            taken = binary && third != 0 && third <= w && node_facts_conditional(records, &view, w, a, b, w - third, false);
        } break;
        case CHECK_OP_PRE_INCREMENT: case CHECK_OP_PRE_DECREMENT: case CHECK_OP_POST_INCREMENT: case CHECK_OP_POST_DECREMENT: case CHECK_OP_ADD_ASSIGN:
        case CHECK_OP_SUBTRACT_ASSIGN: case CHECK_OP_MULTIPLY_ASSIGN: case CHECK_OP_DIVIDE_ASSIGN: case CHECK_OP_REMAINDER_ASSIGN: case CHECK_OP_SHIFT_LEFT_ASSIGN:
        case CHECK_OP_SHIFT_RIGHT_ASSIGN: case CHECK_OP_BIT_AND_ASSIGN: case CHECK_OP_BIT_OR_ASSIGN: case CHECK_OP_BIT_XOR_ASSIGN:
        {
            taken = node_facts_update(records, &view, w, a, b, binary, op, false);
        } break;
        default:
        {
            const bool row = node_machine_row_operators[op] != NODE_MACHINE_ROW_NONE;
            if (operators && row && node_facts_row_made(records, &view, w, a, b, binary ? 1u : 0u, op, false, false))
            {
                taken = true;
            }
            else if (operators && (op == CHECK_OP_ADD || op == CHECK_OP_SUBTRACT))
            {
                taken = (binary && node_facts_pointer(records, &view, w, a, b, op)) || node_facts_fold(records, &view, w, a, b, binary, op);
            }
            else if (operators && (row || op == CHECK_OP_POSITIVE))
            {
                taken = node_facts_fold(records, &view, w, a, b, binary, op) ||
                        (binary && (op == CHECK_OP_EQUAL || op == CHECK_OP_NOT_EQUAL) && node_facts_null_compare(records, &view, w, a, b));
            }
        } break;
    }
    if (taken)
    {
        records[w].taken = BUSTER_NODE_FACTS_RULED;
    }
    else
    {
        node_facts_leave(facts, (uint64_t)first + w, w);
    }
}

// The loads' parts: of each lookup's short path, what its load gives (its type, that type's word, a member's offset)
// and what the load itself tests (the row's flags, completeness at the node, the member found, a call's prototype and
// count). The rest of the record is given, and so is that the operand is fit (the caller's test).
static inline __attribute__((always_inline)) void node_facts_loaded(NodeFactsRecord* record, TypeId type, uint32_t word)
{
    node_facts_store(record, 0, (uint64_t)type | (uint64_t)word << 32);
}

static inline __attribute__((always_inline)) bool node_facts_arrow_load(NodeFactsRecord* records, const NodeFactsView* view, uint32_t w, uint32_t a)
{
    const NodeFactsRow* row = node_facts_row_of(view, records[a].type);
    bool taken = (row->flags & BUSTER_NODE_FACTS_ARROW) != 0 && row->element_complete_at <= view->positions[w];
    if (taken)
    {
        const NodeFactsMember* slot = node_facts_member_kept(view, row->element, view->inputs[w]);
        taken = (slot->flags & (BUSTER_NODE_FACTS_MEMBER_FOUND | BUSTER_NODE_FACTS_MEMBER_BIT_FIELD)) == BUSTER_NODE_FACTS_MEMBER_FOUND;
        if (taken)
        {
            node_facts_loaded(records + w, slot->type, slot->word);
            node_facts_store(records + w, 48, slot->offset);
        }
    }
    return taken;
}

static inline __attribute__((always_inline)) bool node_facts_dot_load(NodeFactsRecord* records, const NodeFactsView* view, uint32_t w, uint32_t a)
{
    const TypeId type = records[a].type;
    const NodeFactsRow* row = node_facts_row_of(view, type);
    bool taken = (row->flags & BUSTER_NODE_FACTS_RECORD) != 0 && row->complete_at <= view->positions[w];
    if (taken)
    {
        const NodeFactsMember* slot = node_facts_member_kept(view, type, view->inputs[w]);
        taken = (slot->flags & (BUSTER_NODE_FACTS_MEMBER_FOUND | BUSTER_NODE_FACTS_MEMBER_BIT_FIELD)) == BUSTER_NODE_FACTS_MEMBER_FOUND;
        if (taken)
        {
            node_facts_loaded(records + w, slot->type, slot->word);
            node_facts_store(records + w, 48, slot->offset);
        }
    }
    return taken;
}

static inline __attribute__((always_inline)) bool node_facts_dereference_load(NodeFactsRecord* records, const NodeFactsView* view, uint32_t w, uint32_t a)
{
    const NodeFactsRow* row = node_facts_row_of(view, records[a].type);
    const bool taken = (row->flags & BUSTER_NODE_FACTS_POINTER) != 0;
    if (taken)
    {
        node_facts_loaded(records + w, row->element, row->element_word);
    }
    return taken;
}

static inline __attribute__((always_inline)) bool node_facts_subscript_load(NodeFactsRecord* records, const NodeFactsView* view, uint32_t w, uint32_t a)
{
    const TypeId type = records[a].type;
    const NodeFactsRow* row = node_facts_row_of(view, type);
    const bool array = (row->flags & BUSTER_NODE_FACTS_ARRAY) != 0;
    const bool taken = (row->flags & BUSTER_NODE_FACTS_OBJECTS) != 0 && row->element_complete_at <= view->positions[w];
    if (taken)
    {
        node_facts_loaded(records + w, row->element | (array ? type_qualifiers(type) : 0), row->element_word);
    }
    return taken;
}

static inline __attribute__((always_inline)) bool node_facts_call_load(NodeFactsRecord* records, const NodeFactsView* view, uint32_t w, uint32_t a)
{
    const NodeFactsRow* row = node_facts_row_of(view, records[a].type);
    const uint32_t function = row->function;
    const uint32_t parameters = function >> 16;
    const uint32_t count = view->counts[w];
    const uint32_t wanted = BUSTER_NODE_FACTS_FUNCTION | BUSTER_NODE_FACTS_RETURNS | BUSTER_TYPE_FLAG_PROTOTYPE << 8;
    const bool taken = (function & wanted) == wanted && count >= parameters && ((function >> 8 & BUSTER_TYPE_FLAG_VARIADIC) != 0 || count == parameters) &&
                       row->returned_ready_at <= view->positions[w];
    if (taken)
    {
        node_facts_loaded(records + w, row->returned, row->returned_word);
    }
    return taken;
}

// The operators a loop's short path didn't take, `count` of them listed: each by its rule. A loop lists them and calls
// this once, after it: a call inside the loop would have every value the loop reads saved around it, a dozen loads an
// operator for a call a few in a hundred make. A loop marks each one it lists as a wide value's record
// (BUSTER_NODE_MACHINE_WIDE in its standing, which no short path takes), so that a later operator of the class that
// reads it is listed too, and its rule, after the loop and in the window's order, finds the record made.
static __attribute__((noinline)) void node_facts_slow_each(NodeFacts* facts, uint32_t first, uint32_t count)
{
    for (uint32_t i = 0; i < count; i += 1)
    {
        node_facts_slow(facts, first, facts->rare[i]);
    }
}

// The operators that have no loop of their own (a comma, sizeof, `?:`, a unary plus) and those of the levels past the
// classes', `count` of them listed in the window's order: each by its rule, up the postorder.
static __attribute__((noinline)) void node_facts_slow_list(NodeFacts* facts, uint32_t first, const uint32_t* list, uint32_t count)
{
    for (uint32_t i = 0; i < count; i += 1)
    {
        node_facts_slow(facts, first, list[i]);
    }
}

// The window's leaves that have a line, `leaves` of them listed: each from its line, a declaration's or a literal's
// own. The kernel's parts are functions of their own, each with the few values it reads in registers, and one a
// variant, each variant's choice a constant in its own.
static inline __attribute__((always_inline)) void node_facts_leaves_of(NodeFacts* facts, uint32_t first, uint32_t leaves, bool lean)
{
    NodeFactsRecord* const records = facts->records;
    const uint32_t* const leaf_list = facts->leaf_list;
    const NodeFactsView view = node_facts_view(facts, first);
    for (uint32_t i = 0; i < leaves; i += 1)
    {
        const uint32_t w = leaf_list[i];
        if (__builtin_expect(!node_facts_leaf(records, &view, w, lean), 0))
        {
            node_facts_leave(facts, (uint64_t)first + w, w);
        }
    }
}

static __attribute__((noinline)) void node_facts_leaves(NodeFacts* facts, uint32_t first, uint32_t leaves)
{
    node_facts_leaves_of(facts, first, leaves, false);
}

static __attribute__((noinline)) void node_facts_leaves_lean(NodeFacts* facts, uint32_t first, uint32_t leaves)
{
    node_facts_leaves_of(facts, first, leaves, true);
}

// A class's lookups, `batched` of them listed (`batch`) in the window's order: each by its operator's short path (the
// whole kernel's, the lean one's, or its load alone where the lean kernel found the operand fit), or its rule. One loop
// an operator and a variant.
static inline __attribute__((always_inline)) void node_facts_arrows_of(NodeFacts* facts, uint32_t first, const uint32_t* batch, uint32_t batched, uint32_t mode)
{
    NodeFactsRecord* const records = facts->records;
    uint32_t* const rare = facts->rare;
    uint32_t rare_count = 0;
    const uint16_t* const left = facts->left + first;
    const uint8_t* const fits = facts->fits + first;
    const NodeFactsView view = node_facts_view(facts, first);
    for (uint32_t i = 0; i < batched; i += 1)
    {
        const uint32_t w = batch[i];
        const uint32_t a = w - left[w];
        const bool taken = mode == BUSTER_NODE_FACTS_LOADS ? fits[w] != 0 && node_facts_arrow_load(records, &view, w, a)
                                                           : node_facts_arrow_fast(records, &view, w, a, mode == BUSTER_NODE_FACTS_LEAN);
        if (__builtin_expect(!taken, 0))
        {
            rare[rare_count] = w;
            rare_count += 1;
            records[w].standing = BUSTER_NODE_MACHINE_WIDE;
        }
    }
    if (rare_count != 0)
    {
        node_facts_slow_each(facts, first, rare_count);
    }
}

static inline __attribute__((always_inline)) void node_facts_dots_of(NodeFacts* facts, uint32_t first, const uint32_t* batch, uint32_t batched, uint32_t mode)
{
    NodeFactsRecord* const records = facts->records;
    uint32_t* const rare = facts->rare;
    uint32_t rare_count = 0;
    const uint16_t* const left = facts->left + first;
    const uint8_t* const fits = facts->fits + first;
    const NodeFactsView view = node_facts_view(facts, first);
    for (uint32_t i = 0; i < batched; i += 1)
    {
        const uint32_t w = batch[i];
        const uint32_t a = w - left[w];
        const bool taken = mode == BUSTER_NODE_FACTS_LOADS ? fits[w] != 0 && node_facts_dot_load(records, &view, w, a)
                                                           : node_facts_dot_fast(records, &view, w, a, mode == BUSTER_NODE_FACTS_LEAN);
        if (__builtin_expect(!taken, 0))
        {
            rare[rare_count] = w;
            rare_count += 1;
            records[w].standing = BUSTER_NODE_MACHINE_WIDE;
        }
    }
    if (rare_count != 0)
    {
        node_facts_slow_each(facts, first, rare_count);
    }
}

static inline __attribute__((always_inline)) void node_facts_dereferences_of(NodeFacts* facts, uint32_t first, const uint32_t* batch, uint32_t batched, uint32_t mode)
{
    NodeFactsRecord* const records = facts->records;
    uint32_t* const rare = facts->rare;
    uint32_t rare_count = 0;
    const uint16_t* const left = facts->left + first;
    const uint8_t* const fits = facts->fits + first;
    const NodeFactsView view = node_facts_view(facts, first);
    for (uint32_t i = 0; i < batched; i += 1)
    {
        const uint32_t w = batch[i];
        const uint32_t a = w - left[w];
        const bool taken = mode == BUSTER_NODE_FACTS_LOADS ? fits[w] != 0 && node_facts_dereference_load(records, &view, w, a)
                                                           : node_facts_dereference_fast(records, &view, w, a, mode == BUSTER_NODE_FACTS_LEAN);
        if (__builtin_expect(!taken, 0))
        {
            rare[rare_count] = w;
            rare_count += 1;
            records[w].standing = BUSTER_NODE_MACHINE_WIDE;
        }
    }
    if (rare_count != 0)
    {
        node_facts_slow_each(facts, first, rare_count);
    }
}

static inline __attribute__((always_inline)) void node_facts_subscripts_of(NodeFacts* facts, uint32_t first, const uint32_t* batch, uint32_t batched, uint32_t mode)
{
    NodeFactsRecord* const records = facts->records;
    uint32_t* const rare = facts->rare;
    uint32_t rare_count = 0;
    const uint16_t* const left = facts->left + first;
    const uint16_t* const right = facts->right + first;
    const uint8_t* const fits = facts->fits + first;
    const NodeFactsView view = node_facts_view(facts, first);
    for (uint32_t i = 0; i < batched; i += 1)
    {
        const uint32_t w = batch[i];
        const uint32_t a = w - left[w];
        const bool taken = right[w] != 0 && (mode == BUSTER_NODE_FACTS_LOADS ? fits[w] != 0 && node_facts_subscript_load(records, &view, w, a)
                                                                             : node_facts_subscript_fast(records, &view, w, a, w - right[w], mode == BUSTER_NODE_FACTS_LEAN));
        if (__builtin_expect(!taken, 0))
        {
            rare[rare_count] = w;
            rare_count += 1;
            records[w].standing = BUSTER_NODE_MACHINE_WIDE;
        }
    }
    if (rare_count != 0)
    {
        node_facts_slow_each(facts, first, rare_count);
    }
}

static inline __attribute__((always_inline)) void node_facts_calls_of(NodeFacts* facts, uint32_t first, const uint32_t* batch, uint32_t batched, uint32_t mode)
{
    NodeFactsRecord* const records = facts->records;
    uint32_t* const rare = facts->rare;
    uint32_t rare_count = 0;
    const uint16_t* const left = facts->left + first;
    const uint8_t* const fits = facts->fits + first;
    const NodeFactsView view = node_facts_view(facts, first);
    for (uint32_t i = 0; i < batched; i += 1)
    {
        const uint32_t w = batch[i];
        const uint32_t a = w - left[w];
        const bool taken = mode == BUSTER_NODE_FACTS_LOADS ? fits[w] != 0 && node_facts_call_load(records, &view, w, a)
                                                           : node_facts_call_fast(records, &view, w, a, mode == BUSTER_NODE_FACTS_LEAN);
        if (__builtin_expect(!taken, 0))
        {
            rare[rare_count] = w;
            rare_count += 1;
            records[w].standing = BUSTER_NODE_MACHINE_WIDE;
        }
    }
    if (rare_count != 0)
    {
        node_facts_slow_each(facts, first, rare_count);
    }
}

static __attribute__((noinline)) void node_facts_arrows(NodeFacts* facts, uint32_t first, const uint32_t* batch, uint32_t batched)
{
    node_facts_arrows_of(facts, first, batch, batched, BUSTER_NODE_FACTS_WHOLE);
}

static __attribute__((noinline)) void node_facts_arrows_lean(NodeFacts* facts, uint32_t first, const uint32_t* batch, uint32_t batched)
{
    node_facts_arrows_of(facts, first, batch, batched, BUSTER_NODE_FACTS_LEAN);
}

static __attribute__((noinline)) void node_facts_arrows_loads(NodeFacts* facts, uint32_t first, const uint32_t* batch, uint32_t batched)
{
    node_facts_arrows_of(facts, first, batch, batched, BUSTER_NODE_FACTS_LOADS);
}

static __attribute__((noinline)) void node_facts_dots(NodeFacts* facts, uint32_t first, const uint32_t* batch, uint32_t batched)
{
    node_facts_dots_of(facts, first, batch, batched, BUSTER_NODE_FACTS_WHOLE);
}

static __attribute__((noinline)) void node_facts_dots_lean(NodeFacts* facts, uint32_t first, const uint32_t* batch, uint32_t batched)
{
    node_facts_dots_of(facts, first, batch, batched, BUSTER_NODE_FACTS_LEAN);
}

static __attribute__((noinline)) void node_facts_dots_loads(NodeFacts* facts, uint32_t first, const uint32_t* batch, uint32_t batched)
{
    node_facts_dots_of(facts, first, batch, batched, BUSTER_NODE_FACTS_LOADS);
}

static __attribute__((noinline)) void node_facts_dereferences(NodeFacts* facts, uint32_t first, const uint32_t* batch, uint32_t batched)
{
    node_facts_dereferences_of(facts, first, batch, batched, BUSTER_NODE_FACTS_WHOLE);
}

static __attribute__((noinline)) void node_facts_dereferences_lean(NodeFacts* facts, uint32_t first, const uint32_t* batch, uint32_t batched)
{
    node_facts_dereferences_of(facts, first, batch, batched, BUSTER_NODE_FACTS_LEAN);
}

static __attribute__((noinline)) void node_facts_dereferences_loads(NodeFacts* facts, uint32_t first, const uint32_t* batch, uint32_t batched)
{
    node_facts_dereferences_of(facts, first, batch, batched, BUSTER_NODE_FACTS_LOADS);
}

static __attribute__((noinline)) void node_facts_subscripts(NodeFacts* facts, uint32_t first, const uint32_t* batch, uint32_t batched)
{
    node_facts_subscripts_of(facts, first, batch, batched, BUSTER_NODE_FACTS_WHOLE);
}

static __attribute__((noinline)) void node_facts_subscripts_lean(NodeFacts* facts, uint32_t first, const uint32_t* batch, uint32_t batched)
{
    node_facts_subscripts_of(facts, first, batch, batched, BUSTER_NODE_FACTS_LEAN);
}

static __attribute__((noinline)) void node_facts_subscripts_loads(NodeFacts* facts, uint32_t first, const uint32_t* batch, uint32_t batched)
{
    node_facts_subscripts_of(facts, first, batch, batched, BUSTER_NODE_FACTS_LOADS);
}

static __attribute__((noinline)) void node_facts_calls(NodeFacts* facts, uint32_t first, const uint32_t* batch, uint32_t batched)
{
    node_facts_calls_of(facts, first, batch, batched, BUSTER_NODE_FACTS_WHOLE);
}

static __attribute__((noinline)) void node_facts_calls_lean(NodeFacts* facts, uint32_t first, const uint32_t* batch, uint32_t batched)
{
    node_facts_calls_of(facts, first, batch, batched, BUSTER_NODE_FACTS_LEAN);
}

static __attribute__((noinline)) void node_facts_calls_loads(NodeFacts* facts, uint32_t first, const uint32_t* batch, uint32_t batched)
{
    node_facts_calls_of(facts, first, batch, batched, BUSTER_NODE_FACTS_LOADS);
}

static __attribute__((noinline)) void node_facts_discards(NodeFacts* facts, uint32_t first, const uint32_t* batch, uint32_t batched)
{
    NodeFactsRecord* const records = facts->records;
    uint32_t* const rare = facts->rare;
    uint32_t rare_count = 0;
    const uint16_t* const left = facts->left + first;
    const bool plain = facts->machine->graph->overload_candidate_count == 0;
    const NodeFactsView view = node_facts_view(facts, first);
    for (uint32_t i = 0; i < batched; i += 1)
    {
        const uint32_t w = batch[i];
        if (__builtin_expect(!node_facts_discard_fast(records, &view, plain, w, w - left[w]), 0))
        {
            rare[rare_count] = w;
            rare_count += 1;
            records[w].standing = BUSTER_NODE_MACHINE_WIDE;
        }
    }
    if (rare_count != 0)
    {
        node_facts_slow_each(facts, first, rare_count);
    }
}

// A row's operators that the table's row didn't type, `count` of them listed in the window's order: each by what T's
// order for one comes to, in one loop where node_facts_slow would be a call each. The constants' fold, of integer
// constants; else its row again (an operand of it may have been one of these, made since), then a pointer plus or minus
// an integer, or a pointer's equality with a null pointer constant. Each makes the record whole; where none takes it,
// T's record.
static __attribute__((noinline)) void node_facts_rows_rest(NodeFacts* facts, uint32_t first, uint32_t count)
{
    NodeFactsRecord* const records = facts->records;
    const uint32_t* const rare = facts->rare;
    const uint16_t* const left = facts->left + first;
    const uint16_t* const right = facts->right + first;
    const uint8_t* const ops = facts->ops + first;
    const NodeFactsView view = node_facts_view(facts, first);
    for (uint32_t i = 0; i < count; i += 1)
    {
        const uint32_t w = rare[i];
        const CheckOp op = (CheckOp)ops[w];
        const bool binary = right[w] != 0;
        const uint32_t a = w - left[w];
        const uint32_t b = binary ? w - right[w] : a;
        // Integer constants' operands are the fold's and nothing else's: no row takes two constants, and no pointer's
        // rule an integer's pair. The others: the row again, then the pointer's rules.
        const uint8_t constant = BUSTER_NODE_MACHINE_STANDING(CHECK_RVALUE, CHECK_CONSTANT_INTEGER);
        bool taken = false;
        if (records[a].standing == constant && records[b].standing == constant)
        {
            taken = node_facts_fold(records, &view, w, a, b, binary, op);
        }
        else if (node_facts_row_made(records, &view, w, a, b, binary ? 1u : 0u, op, false, false))
        {
            taken = true;
        }
        else if (op == CHECK_OP_ADD || op == CHECK_OP_SUBTRACT)
        {
            taken = binary && node_facts_pointer(records, &view, w, a, b, op);
        }
        else if (op == CHECK_OP_EQUAL || op == CHECK_OP_NOT_EQUAL)
        {
            taken = binary && node_facts_null_compare(records, &view, w, a, b);
        }
        if (taken)
        {
            records[w].taken = BUSTER_NODE_FACTS_RULED;
        }
        else
        {
            node_facts_leave(facts, (uint64_t)first + w, w);
        }
    }
}

// The operators' kernel's classes' loops, one an operator's code: each record by its rule's one path (with `lean`, with
// no conversion's entry), and those it doesn't take listed and made after the loop, by their rules in T's order or as
// T's records (node_facts_slow). A row's operator: T's table's; what its row doesn't type (pointer arithmetic, a
// constants' fold, a pointer's equality with a null pointer constant) is its rules'.
static __attribute__((noinline)) void node_facts_rows_made(NodeFacts* facts, uint32_t first, const uint32_t* batch, uint32_t batched, bool lean)
{
    NodeFactsRecord* const records = facts->records;
    uint32_t* const rare = facts->rare;
    uint32_t rare_count = 0;
    const uint16_t* const left = facts->left + first;
    const uint16_t* const right = facts->right + first;
    const uint8_t* const ops = facts->ops + first;
    const NodeFactsView view = node_facts_view(facts, first);
    for (uint32_t i = 0; i < batched; i += 1)
    {
        // A unary operator's second operand is its first, read and masked away.
        const uint32_t w = batch[i];
        const uint32_t second = right[w];
        const uint32_t a = w - left[w];
        const uint32_t b = w - (second != 0 ? second : left[w]);
        const bool taken = node_facts_row_made(records, &view, w, a, b, second != 0 ? 1u : 0u, (CheckOp)ops[w], lean, true);
        rare[rare_count] = w;
        rare_count += taken ? 0u : 1u;
    }
    if (rare_count != 0)
    {
        node_facts_rows_rest(facts, first, rare_count);
    }
}

// `=`. A loop fills no pointer conversion's slot: a store whose slot holds no answer is its rule's, which fills it.
static __attribute__((noinline)) void node_facts_stores(NodeFacts* facts, uint32_t first, const uint32_t* batch, uint32_t batched, bool lean)
{
    NodeFactsRecord* const records = facts->records;
    uint32_t* const rare = facts->rare;
    uint32_t rare_count = 0;
    const uint16_t* const left = facts->left + first;
    const uint16_t* const right = facts->right + first;
    const NodeFactsView view = node_facts_view(facts, first);
    for (uint32_t i = 0; i < batched; i += 1)
    {
        const uint32_t w = batch[i];
        const uint32_t a = w - left[w];
        if (__builtin_expect(!(right[w] != 0 && node_facts_store_fast(records, &view, w, a, w - right[w], true, lean)), 0))
        {
            rare[rare_count] = w;
            rare_count += 1;
            records[w].standing = BUSTER_NODE_MACHINE_WIDE;
        }
    }
    if (rare_count != 0)
    {
        node_facts_slow_each(facts, first, rare_count);
    }
}

// A conversion or an initialization, to its node's type.
static __attribute__((noinline)) void node_facts_converts(NodeFacts* facts, uint32_t first, const uint32_t* batch, uint32_t batched, bool lean)
{
    NodeFactsRecord* const records = facts->records;
    uint32_t* const rare = facts->rare;
    uint32_t rare_count = 0;
    const uint16_t* const left = facts->left + first;
    const NodeFactsView view = node_facts_view(facts, first);
    for (uint32_t i = 0; i < batched; i += 1)
    {
        const uint32_t w = batch[i];
        const uint32_t a = w - left[w];
        if (__builtin_expect(!(node_facts_store_fast(records, &view, w, a, a, false, lean)), 0))
        {
            rare[rare_count] = w;
            rare_count += 1;
            records[w].standing = BUSTER_NODE_MACHINE_WIDE;
        }
    }
    if (rare_count != 0)
    {
        node_facts_slow_each(facts, first, rare_count);
    }
}

// A control's condition and `!`.
static __attribute__((noinline)) void node_facts_truths(NodeFacts* facts, uint32_t first, const uint32_t* batch, uint32_t batched, bool lean)
{
    NodeFactsRecord* const records = facts->records;
    uint32_t* const rare = facts->rare;
    uint32_t rare_count = 0;
    const uint16_t* const left = facts->left + first;
    const NodeFactsView view = node_facts_view(facts, first);
    for (uint32_t i = 0; i < batched; i += 1)
    {
        const uint32_t w = batch[i];
        const uint32_t a = w - left[w];
        if (__builtin_expect(!(node_facts_truth_fast(records, &view, w, a, lean)), 0))
        {
            rare[rare_count] = w;
            rare_count += 1;
            records[w].standing = BUSTER_NODE_MACHINE_WIDE;
        }
    }
    if (rare_count != 0)
    {
        node_facts_slow_each(facts, first, rare_count);
    }
}

// `&x`.
static __attribute__((noinline)) void node_facts_addresses_made(NodeFacts* facts, uint32_t first, const uint32_t* batch, uint32_t batched, bool lean)
{
    NodeFactsRecord* const records = facts->records;
    uint32_t* const rare = facts->rare;
    uint32_t rare_count = 0;
    const uint16_t* const left = facts->left + first;
    const NodeFactsView view = node_facts_view(facts, first);
    for (uint32_t i = 0; i < batched; i += 1)
    {
        const uint32_t w = batch[i];
        const uint32_t a = w - left[w];
        if (__builtin_expect(!(node_facts_address(records, &view, w, a, lean)), 0))
        {
            rare[rare_count] = w;
            rare_count += 1;
            records[w].standing = BUSTER_NODE_MACHINE_WIDE;
        }
    }
    if (rare_count != 0)
    {
        node_facts_slow_each(facts, first, rare_count);
    }
}

// A cast to a type that is not void.
static __attribute__((noinline)) void node_facts_casts(NodeFacts* facts, uint32_t first, const uint32_t* batch, uint32_t batched, bool lean)
{
    NodeFactsRecord* const records = facts->records;
    uint32_t* const rare = facts->rare;
    uint32_t rare_count = 0;
    const uint16_t* const left = facts->left + first;
    const NodeFactsView view = node_facts_view(facts, first);
    for (uint32_t i = 0; i < batched; i += 1)
    {
        const uint32_t w = batch[i];
        const uint32_t a = w - left[w];
        if (__builtin_expect(!(node_facts_cast(NULL, records, &view, w, a, lean)), 0))
        {
            rare[rare_count] = w;
            rare_count += 1;
            records[w].standing = BUSTER_NODE_MACHINE_WIDE;
        }
    }
    if (rare_count != 0)
    {
        node_facts_slow_each(facts, first, rare_count);
    }
}

// `&&` and `||`.
static __attribute__((noinline)) void node_facts_logics(NodeFacts* facts, uint32_t first, const uint32_t* batch, uint32_t batched, bool lean)
{
    NodeFactsRecord* const records = facts->records;
    uint32_t* const rare = facts->rare;
    uint32_t rare_count = 0;
    const uint16_t* const left = facts->left + first;
    const uint16_t* const right = facts->right + first;
    const NodeFactsView view = node_facts_view(facts, first);
    for (uint32_t i = 0; i < batched; i += 1)
    {
        const uint32_t w = batch[i];
        const uint32_t a = w - left[w];
        if (__builtin_expect(!(right[w] != 0 && node_facts_logic_fast(records, &view, w, a, w - right[w], lean)), 0))
        {
            rare[rare_count] = w;
            rare_count += 1;
            records[w].standing = BUSTER_NODE_MACHINE_WIDE;
        }
    }
    if (rare_count != 0)
    {
        node_facts_slow_each(facts, first, rare_count);
    }
}

// The increments and the compound assignments.
static __attribute__((noinline)) void node_facts_updates(NodeFacts* facts, uint32_t first, const uint32_t* batch, uint32_t batched, bool lean)
{
    NodeFactsRecord* const records = facts->records;
    uint32_t* const rare = facts->rare;
    uint32_t rare_count = 0;
    const uint16_t* const left = facts->left + first;
    const uint16_t* const right = facts->right + first;
    const uint8_t* const ops = facts->ops + first;
    const NodeFactsView view = node_facts_view(facts, first);
    for (uint32_t i = 0; i < batched; i += 1)
    {
        const uint32_t w = batch[i];
        const uint32_t a = w - left[w];
        if (__builtin_expect(!(node_facts_update(records, &view, w, a, w - (right[w] != 0 ? right[w] : left[w]), right[w] != 0, (CheckOp)ops[w], lean)), 0))
        {
            rare[rare_count] = w;
            rare_count += 1;
            records[w].standing = BUSTER_NODE_MACHINE_WIDE;
        }
    }
    if (rare_count != 0)
    {
        node_facts_slow_each(facts, first, rare_count);
    }
}

// The kernel over window `window`, whose given records node_facts_given has put in place, as variant `mode` (WHOLE,
// LEAN or LOADS): its leaves and its made operators listed (node_facts_list); the leaves, each from its line; the
// operators appended each to its class's list (node_facts_distribute); then the classes, a round at a time and in a
// round a code at a time in the codes' order, each made by its operator's loop; and after the rounds the deeper
// operators, up the postorder, each by its rule. A class is one loop over one operator's records, which are
// independent but where one reads another of its class before it: a dispatch a record would be a branch the predictor
// misses, and a record after its operand in the postorder would wait for it. Which classes a round has is a test a
// code, each its own branch, where a jump by the class would be one the predictor misses. The operators go to their
// lists before the leaves are made: the lists' lengths settle while the leaves' loop, which reads none of them, runs.
// With `split`, the two parts' TSC ticks are added to the counts (three reads of the counter a window, which a pass
// that is counted whole leaves out): the lists and the leaves, and the classes.
static __attribute__((noinline)) void node_facts_window(NodeFacts* facts, uint32_t window, uint32_t mode, bool split)
{
    const uint32_t first = facts->windows[window];
    const uint32_t count = facts->windows[window + 1] - first;
    const bool whole = mode == BUSTER_NODE_FACTS_WHOLE;
    const bool lean = mode == BUSTER_NODE_FACTS_LEAN;
    uint32_t leaves = 0;
    uint32_t lookups = 0;
    uint32_t rounds = 0;
    const uint64_t t0 = split ? __rdtsc() : 0;
    node_facts_list(facts->leaf_list, facts->lookup_list, facts->keys, facts->kinds + first, facts->classes + first, count, &leaves, &lookups, &rounds);
    memset(facts->sizes, 0, sizeof(facts->sizes));
    node_facts_distribute(facts->batches, facts->sizes, facts->lookup_list, facts->keys, lookups);
    if (whole)
    {
        node_facts_leaves(facts, first, leaves);
    }
    else
    {
        node_facts_leaves_lean(facts, first, leaves);
    }
    const uint64_t t1 = split ? __rdtsc() : 0;
    for (uint32_t round = 0; round < BUSTER_NODE_FACTS_ROUNDS && (rounds >> round) != 0; round += 1)
    {
        const uint32_t* const lists = facts->batches + (uint64_t)16 * round * BUSTER_NODE_FACTS_WINDOW;
        const uint32_t* const sizes = facts->sizes + 16 * round;
        // The round's classes that have an operator.
        uint32_t present = 0;
        for (uint32_t code = 0; code < BUSTER_NODE_FACTS_CODES; code += 1)
        {
            present |= (sizes[code] != 0 ? 1u : 0u) << code;
        }
        if ((present >> BUSTER_NODE_FACTS_CODE_ARROW & 1) != 0)
        {
            const uint32_t* const batch = lists + (uint64_t)BUSTER_NODE_FACTS_CODE_ARROW * BUSTER_NODE_FACTS_WINDOW;
            const uint32_t batched = sizes[BUSTER_NODE_FACTS_CODE_ARROW];
            if (whole)
            {
                node_facts_arrows(facts, first, batch, batched);
            }
            else if (lean)
            {
                node_facts_arrows_lean(facts, first, batch, batched);
            }
            else
            {
                node_facts_arrows_loads(facts, first, batch, batched);
            }
        }
        if ((present >> BUSTER_NODE_FACTS_CODE_SUBSCRIPT & 1) != 0)
        {
            const uint32_t* const batch = lists + (uint64_t)BUSTER_NODE_FACTS_CODE_SUBSCRIPT * BUSTER_NODE_FACTS_WINDOW;
            const uint32_t batched = sizes[BUSTER_NODE_FACTS_CODE_SUBSCRIPT];
            if (whole)
            {
                node_facts_subscripts(facts, first, batch, batched);
            }
            else if (lean)
            {
                node_facts_subscripts_lean(facts, first, batch, batched);
            }
            else
            {
                node_facts_subscripts_loads(facts, first, batch, batched);
            }
        }
        if ((present >> BUSTER_NODE_FACTS_CODE_DOT & 1) != 0)
        {
            const uint32_t* const batch = lists + (uint64_t)BUSTER_NODE_FACTS_CODE_DOT * BUSTER_NODE_FACTS_WINDOW;
            const uint32_t batched = sizes[BUSTER_NODE_FACTS_CODE_DOT];
            if (whole)
            {
                node_facts_dots(facts, first, batch, batched);
            }
            else if (lean)
            {
                node_facts_dots_lean(facts, first, batch, batched);
            }
            else
            {
                node_facts_dots_loads(facts, first, batch, batched);
            }
        }
        if ((present >> BUSTER_NODE_FACTS_CODE_ADDRESS & 1) != 0)
        {
            node_facts_addresses_made(facts, first, lists + (uint64_t)BUSTER_NODE_FACTS_CODE_ADDRESS * BUSTER_NODE_FACTS_WINDOW, sizes[BUSTER_NODE_FACTS_CODE_ADDRESS], !whole);
        }
        if ((present >> BUSTER_NODE_FACTS_CODE_CAST & 1) != 0)
        {
            node_facts_casts(facts, first, lists + (uint64_t)BUSTER_NODE_FACTS_CODE_CAST * BUSTER_NODE_FACTS_WINDOW, sizes[BUSTER_NODE_FACTS_CODE_CAST], !whole);
        }
        if ((present >> BUSTER_NODE_FACTS_CODE_OTHER & 1) != 0)
        {
            node_facts_slow_list(facts, first, lists + (uint64_t)BUSTER_NODE_FACTS_CODE_OTHER * BUSTER_NODE_FACTS_WINDOW, sizes[BUSTER_NODE_FACTS_CODE_OTHER]);
        }
        if ((present >> BUSTER_NODE_FACTS_CODE_ROW & 1) != 0)
        {
            node_facts_rows_made(facts, first, lists + (uint64_t)BUSTER_NODE_FACTS_CODE_ROW * BUSTER_NODE_FACTS_WINDOW, sizes[BUSTER_NODE_FACTS_CODE_ROW], !whole);
        }
        if ((present >> BUSTER_NODE_FACTS_CODE_CALL & 1) != 0)
        {
            const uint32_t* const batch = lists + (uint64_t)BUSTER_NODE_FACTS_CODE_CALL * BUSTER_NODE_FACTS_WINDOW;
            const uint32_t batched = sizes[BUSTER_NODE_FACTS_CODE_CALL];
            if (whole)
            {
                node_facts_calls(facts, first, batch, batched);
            }
            else if (lean)
            {
                node_facts_calls_lean(facts, first, batch, batched);
            }
            else
            {
                node_facts_calls_loads(facts, first, batch, batched);
            }
        }
        if ((present >> BUSTER_NODE_FACTS_CODE_LOGIC & 1) != 0)
        {
            node_facts_logics(facts, first, lists + (uint64_t)BUSTER_NODE_FACTS_CODE_LOGIC * BUSTER_NODE_FACTS_WINDOW, sizes[BUSTER_NODE_FACTS_CODE_LOGIC], !whole);
        }
        if ((present >> BUSTER_NODE_FACTS_CODE_UPDATE & 1) != 0)
        {
            node_facts_updates(facts, first, lists + (uint64_t)BUSTER_NODE_FACTS_CODE_UPDATE * BUSTER_NODE_FACTS_WINDOW, sizes[BUSTER_NODE_FACTS_CODE_UPDATE], !whole);
        }
        if ((present >> BUSTER_NODE_FACTS_CODE_DEREFERENCE & 1) != 0)
        {
            const uint32_t* const batch = lists + (uint64_t)BUSTER_NODE_FACTS_CODE_DEREFERENCE * BUSTER_NODE_FACTS_WINDOW;
            const uint32_t batched = sizes[BUSTER_NODE_FACTS_CODE_DEREFERENCE];
            if (whole)
            {
                node_facts_dereferences(facts, first, batch, batched);
            }
            else if (lean)
            {
                node_facts_dereferences_lean(facts, first, batch, batched);
            }
            else
            {
                node_facts_dereferences_loads(facts, first, batch, batched);
            }
        }
        if ((present >> BUSTER_NODE_FACTS_CODE_STORE & 1) != 0)
        {
            node_facts_stores(facts, first, lists + (uint64_t)BUSTER_NODE_FACTS_CODE_STORE * BUSTER_NODE_FACTS_WINDOW, sizes[BUSTER_NODE_FACTS_CODE_STORE], !whole);
        }
        if ((present >> BUSTER_NODE_FACTS_CODE_CONVERT & 1) != 0)
        {
            node_facts_converts(facts, first, lists + (uint64_t)BUSTER_NODE_FACTS_CODE_CONVERT * BUSTER_NODE_FACTS_WINDOW, sizes[BUSTER_NODE_FACTS_CODE_CONVERT], !whole);
        }
        if ((present >> BUSTER_NODE_FACTS_CODE_DISCARD & 1) != 0)
        {
            node_facts_discards(facts, first, lists + (uint64_t)BUSTER_NODE_FACTS_CODE_DISCARD * BUSTER_NODE_FACTS_WINDOW, sizes[BUSTER_NODE_FACTS_CODE_DISCARD]);
        }
        if ((present >> BUSTER_NODE_FACTS_CODE_TRUTH & 1) != 0)
        {
            node_facts_truths(facts, first, lists + (uint64_t)BUSTER_NODE_FACTS_CODE_TRUTH * BUSTER_NODE_FACTS_WINDOW, sizes[BUSTER_NODE_FACTS_CODE_TRUTH], !whole);
        }
    }
    if ((rounds >> BUSTER_NODE_FACTS_DEEP & 1) != 0)
    {
        node_facts_slow_list(facts, first, facts->batches + (uint64_t)16 * BUSTER_NODE_FACTS_ROUNDS * BUSTER_NODE_FACTS_WINDOW, facts->sizes[16 * BUSTER_NODE_FACTS_ROUNDS]);
    }
    const uint64_t t2 = split ? __rdtsc() : 0;
    facts->leaf_ticks += t1 - t0;
    facts->lookup_ticks += t2 - t1;
    facts->leaf_count = leaves;
    facts->lookup_count = lookups;
}

// Whether the lists the kernel made of window `window`, which it ran last, are its records' of each kind in their
// order; each made operator's class its round's and its code's, the rounds made again from the operands' classes here;
// and each class's list its operators in their order: the listing's and the distribution's twin, a record at a time.
static bool node_facts_lists_same(const NodeFacts* facts, uint32_t window)
{
    const uint32_t first = facts->windows[window];
    const uint32_t count = facts->windows[window + 1] - first;
    uint8_t classes[BUSTER_NODE_FACTS_WINDOW];
    uint32_t cursors[BUSTER_NODE_FACTS_LISTS] = { 0 };
    uint32_t leaves = 0;
    uint32_t lookups = 0;
    bool same = count <= BUSTER_NODE_FACTS_WINDOW;
    for (uint32_t w = 0; same && w < count; w += 1)
    {
        const uint8_t kind = facts->kinds[first + w];
        const CheckOp op = (CheckOp)facts->ops[first + w];
        classes[w] = BUSTER_NODE_FACTS_NO_CLASS;
        if (kind == BUSTER_NODE_FACTS_IDENTIFIER || kind == BUSTER_NODE_FACTS_LITERAL_LINE)
        {
            same = leaves < facts->leaf_count && facts->leaf_list[leaves] == w;
            leaves += 1;
        }
        else if (kind == BUSTER_NODE_FACTS_LOOKUP || kind == BUSTER_NODE_FACTS_OPERATOR)
        {
            const uint32_t code = kind == BUSTER_NODE_FACTS_LOOKUP ? node_facts_lookup_codes[node_facts_lookup(op)] : node_facts_operator(op);
            const uint32_t distances[3] = { facts->left[first + w], facts->right[first + w], op == CHECK_OP_CONDITIONAL ? facts->inputs[first + w] : 0 };
            uint32_t round = 0;
            uint32_t latest = 0;
            for (uint32_t k = 0; k < 3; k += 1)
            {
                const uint32_t operand = distances[k] != 0 && distances[k] <= w ? classes[w - distances[k]] : BUSTER_NODE_FACTS_NO_CLASS;
                round = node_facts_round(round, code, operand);
                latest = node_facts_latest(latest, operand);
            }
            for (uint32_t k = 0; op == CHECK_OP_CALL && kind == BUSTER_NODE_FACTS_LOOKUP && k < facts->counts[first + w]; k += 1)
            {
                const uint32_t operand = classes[w - facts->arguments[facts->inputs[first + w] + k]];
                round = node_facts_round(round, code, operand);
                latest = node_facts_latest(latest, operand);
            }
            const uint8_t key = node_facts_class(round, code);
            const uint32_t list = key < 16u * BUSTER_NODE_FACTS_ROUNDS ? key : 16u * BUSTER_NODE_FACTS_ROUNDS;
            classes[w] = key;
            // The order itself, whatever gave the classes: an operand the kernel makes by class is on a list no later
            // than this operator's, a round's earlier class or this one, where it is before in the window's order. A
            // decoy doesn't show an operand read too early by an operator that sets the decoy's flag itself (`=`, a
            // call, an update).
            same = lookups < facts->lookup_count && facts->lookup_list[lookups] == w && facts->keys[lookups] == key && cursors[list] < facts->sizes[list] &&
                   facts->batches[(uint64_t)list * BUSTER_NODE_FACTS_WINDOW + cursors[list]] == w && latest <= list;
            cursors[list] += 1;
            lookups += 1;
        }
    }
    for (uint32_t list = 0; same && list < BUSTER_NODE_FACTS_LISTS; list += 1)
    {
        same = cursors[list] == facts->sizes[list];
    }
    return same && leaves == facts->leaf_count && lookups == facts->lookup_count;
}

// Whether the kernel's record `w` of the window whose first record is `base` is T's, in what variant `mode` makes of
// it: every column T writes of a record (and its exponent where its payload is a floating value's) for the whole
// kernel, and for any record its rule made or T's own; the record's first half and a member's offset for the lean
// kernel; and of a lookup its type, its word and a member's offset for the loads' kernel.
static bool node_facts_same(const NodeFacts* facts, uint32_t base, uint32_t w, uint32_t mode)
{
    const NodeFactsRecord* record = facts->records + w;
    const NodeFactsRecord expected = node_facts_expected(facts, (uint64_t)base + w);
    const CheckOp op = (CheckOp)facts->ops[base + w];
    const bool lookup = facts->kinds[base + w] == BUSTER_NODE_FACTS_LOOKUP;
    const bool member = lookup && (op == CHECK_OP_ARROW || op == CHECK_OP_MEMBER);
    const bool floating = (expected.standing & BUSTER_NODE_MACHINE_FIELD) == BUSTER_NODE_MACHINE_FLOATING;
    const bool loaded = record->type == expected.type && record->word == expected.word;
    const bool half = loaded && record->flags == expected.flags && record->object == expected.object && record->payload == expected.payload &&
                      record->standing == expected.standing && (!floating || record->exponent == expected.exponent);
    const bool rest = record->converted[0] == expected.converted[0] && record->converted[1] == expected.converted[1] &&
                      record->conversion_flags[0] == expected.conversion_flags[0] && record->conversion_flags[1] == expected.conversion_flags[1];
    const bool offset = record->member_offset == expected.member_offset;
    const bool whole = mode == BUSTER_NODE_FACTS_WHOLE || record->taken != BUSTER_NODE_FACTS_SHORT;
    return whole ? half && rest && offset : mode == BUSTER_NODE_FACTS_LOADS && lookup ? loaded && (!member || offset) : half && (!member || offset);
}

// What a lean kernel leaves out of record `w` of the window it ran last, made here by the record's rule from the
// kernel's own records of its operands, for the comparison alone: a lookup's two conversions' entries (C's, a
// function of the operator, its operands' records and their types' rows), or with `discard` a discard's whole record
// (a function of its operand's). Whether the rule takes it and what it makes is T's. The record is left as it was.
static bool node_facts_derived(NodeFacts* facts, uint32_t first, uint32_t w, bool discard)
{
    const NodeFactsRecord kept = facts->records[w];
    const NodeFactsRecord expected = node_facts_expected(facts, (uint64_t)first + w);
    memset(facts->records + w, BUSTER_NODE_FACTS_POISON, sizeof(NodeFactsRecord));
    node_facts_slow(facts, first, w);
    const NodeFactsRecord* made = facts->records + w;
    const bool conversions = made->converted[0] == expected.converted[0] && made->converted[1] == expected.converted[1] &&
                             made->conversion_flags[0] == expected.conversion_flags[0] && made->conversion_flags[1] == expected.conversion_flags[1];
    const bool derived = made->taken == BUSTER_NODE_FACTS_RULED && conversions && (!discard || node_facts_same(facts, first, w, BUSTER_NODE_FACTS_WHOLE));
    facts->records[w] = kept;
    return derived;
}

// The kernel at reader time (measure-node-machine --shadow --front-facts; PPOptions.node_reader's
// BUSTER_NODE_READER_FACTS). The reader's hook runs T on a chunk's roots (node_machine_reader_chunk); each expression T
// takes is appended to the batch's windows as T left it (node_facts_reader_expression), and after the chunk's roots the
// batch is run (node_facts_reader_flush): the kernel over each window, first as it finds the words, then over poison
// with every record compared with T's, then over decoys; and a timed turn, the kernel and T again on the same
// expressions. The words are the unit's as it stands there: the rows of the types made so far (the walk's and T's own
// derivations', whose rows are made before the expression that brought them is appended), the declarations' lines made
// at their first use, the members' and the conversions' slots filled as the kernel meets them.
//
// A window's inputs are made twice. node_facts_append makes them from T's columns and the walk's nodes, as after the
// pipeline. node_facts_front_expression makes them from the lean front's records and the reader's columns alone, with no
// node and no record of T's: what a compile without the walk's graph has. The two are compared field for field, and the
// checked run takes the second: the kernel's records from the front's inputs are then T's or a difference.

// A batch's room: the records of the expressions T takes in a chunk of the reader's (a chunk is
// BUSTER_CHECK_EXPRESSION_CHUNK tokens), and the expressions. A batch that would pass either is run where it stands.
// The literals' lines of a batch: one a literal's leaf from each making of its windows' inputs that takes one (T's
// columns', the copy of the front's records by place, and that copy again for its time). Where the front writes the
// window its own literals' lines are the first of them, a line a record of the front's (BUSTER_NODE_FRONT_RECORDS):
// a literal's leaf is one record of two at least, so a batch's three makings and the front's lines have room.
#define BUSTER_NODE_FACTS_READER_RECORDS (4u * BUSTER_NODE_FACTS_WINDOW)
#define BUSTER_NODE_FACTS_READER_EXPRESSIONS 4096u
#define BUSTER_NODE_FACTS_READER_LINES (3u * BUSTER_NODE_FACTS_READER_RECORDS)


// A window's records' inputs, what the kernel reads of a window beside its records and the words (NodeFacts' columns of
// the same names), and the call arguments so far.
typedef struct NodeFactsInputs NodeFactsInputs;
struct NodeFactsInputs
{
    uint8_t* ops;
    uint8_t* kinds;
    uint8_t* classes;
    uint8_t* fits;
    uint16_t* left;
    uint16_t* right;
    uint32_t* inputs;
    uint16_t* counts;
    uint32_t* positions;
    uint16_t* arguments;
    uint32_t argument_count;
};

// The census's tests on the window the front wrote (c_check_node_census.c, which follows this file in the unit;
// BUSTER_NODE_READER_WINDOW_TESTS): `tests` is the unit's tests' state, and `state` has just run the kernel on `front`'s
// chunk in place, whose records are the kernel's by the front's record.
struct NodeCensusReader;
struct NodeFactsReader;
static void node_tests_window_chunk(struct NodeCensusReader* tests, struct NodeFactsReader* state, const NodeFront* front);
static bool node_tests_window_lines(struct NodeCensusReader* tests, Arena* arena, uint32_t capacity);

// An expression's byte in the reader's batch (NodeFactsReader.placed).
#define BUSTER_NODE_FACTS_PLACED 0x01u
#define BUSTER_NODE_FACTS_PLACED_WARNED 0x02u

struct NodeFactsReader
{
    NodeFacts facts;
    NodeFrontTypes* types;
    CheckInputReader* reader;
    // The unit's tests' state where they run on the window too (BUSTER_NODE_READER_WINDOW_TESTS), or null.
    struct NodeCensusReader* tests;
    // The batch's windows' inputs as the front's records give them (node_facts_front_expression).
    NodeFactsInputs front;
    // Where the front writes the window (`windowed`), for the kernel's run on the front's columns in place
    // (node_facts_window_run), each by the front's record: whether the record is one of an expression of the batch
    // whose records are there in a row (the kernel makes those alone); the kinds as that run reads them, the front's
    // for those records and a given record's for every other; T's record of each, as the batch has it by its own
    // places (NodeFacts' expected_ columns); and zeros for the fits. `place_windows` is that run's one window.
    bool windowed;
    uint8_t* place_taken;
    uint8_t* place_kinds;
    uint8_t* place_fits;
    TypeId* place_types;
    uint32_t* place_flags;
    uint32_t* place_objects;
    uint64_t* place_payloads;
    uint8_t* place_standings;
    uint16_t* place_exponents;
    uint32_t* place_words;
    TypeId* place_converted;
    uint32_t* place_conversion_flags;
    uint64_t* place_member_offsets;
    uint32_t place_windows[2];
    // The names' pass's own: a chunk's names' places, a vector's past the front's records; and its scalar path's
    // scratch, a chunk's kinds and entries as the front left them.
    uint32_t* name_list;
    uint8_t* name_kinds;
    uint32_t* name_entries;
    // The batch's expressions as T ran on them: each one's first node, its root and its records; its first record in the
    // batch; the front's root and its operand's record; its calls' first argument's place among the batch's; and whether
    // the front's records gave its inputs.
    uint32_t* firsts;
    uint32_t* roots;
    uint32_t* counts;
    uint32_t* bases;
    uint32_t* records;
    uint32_t* operands;
    uint32_t* argument_bases;
    uint8_t* given;
    // Whether the expression's records are the front's window's in a row, which the kernel's run in place then makes
    // (node_facts_window_expression): BUSTER_NODE_FACTS_PLACED; and for such a one, whether T's actions gave a warning
    // in it (NodeMachine.action_warnings after T's run), which the tests on the window ask.
    uint8_t* placed;
    uint32_t expression_count;
    // By the batch's record: whether its expression is one the whole machine takes there.
    uint8_t* taken;
    // An operator's kind and code in a window, by its op (node_facts_append's): a lookup's, another made operator's, or a
    // given record's. A cast's are a cast's to a type that is not void.
    uint8_t op_kinds[CHECK_OP_COUNT];
    uint8_t op_codes[CHECK_OP_COUNT];
    // The batches run: which of the kernel and T runs first in a batch's timed turn.
    uint64_t batches;
    // Whether T runs again for its time: not where it checks itself against the action, which counts what it warns of.
    bool again;
};

static struct NodeFactsReader* node_facts_reader_begin(CheckInputReader* reader, NodeMachine* machine, NodeFrontTypes* types)
{
    Arena* arena = reader->scratch;
    const uint64_t records = (uint64_t)BUSTER_NODE_FACTS_READER_RECORDS + 64;
    struct NodeFactsReader* state = arena_push_array(arena, 1, sizeof(struct NodeFactsReader), 64);
    bool room = state != NULL;
    if (room)
    {
        *state = (struct NodeFactsReader){
            .types = types,
            .reader = reader,
            .again = (reader->pp->options.node_reader & BUSTER_NODE_READER_VERIFY) == 0,
        };
        state->firsts = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, BUSTER_NODE_FACTS_READER_EXPRESSIONS);
        state->roots = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, BUSTER_NODE_FACTS_READER_EXPRESSIONS);
        state->counts = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, BUSTER_NODE_FACTS_READER_EXPRESSIONS);
        state->bases = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, BUSTER_NODE_FACTS_READER_EXPRESSIONS);
        state->records = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, BUSTER_NODE_FACTS_READER_EXPRESSIONS);
        state->operands = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, BUSTER_NODE_FACTS_READER_EXPRESSIONS);
        state->argument_bases = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, BUSTER_NODE_FACTS_READER_EXPRESSIONS);
        state->given = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, BUSTER_NODE_FACTS_READER_EXPRESSIONS);
        state->placed = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, BUSTER_NODE_FACTS_READER_EXPRESSIONS);
        state->taken = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, records);
        state->front.ops = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, records);
        state->front.kinds = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, records);
        state->front.classes = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, records);
        state->front.fits = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, records);
        state->front.left = BUSTER_ARENA_PUSH_ARRAY(arena, uint16_t, records);
        state->front.right = BUSTER_ARENA_PUSH_ARRAY(arena, uint16_t, records);
        state->front.inputs = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, records);
        state->front.counts = BUSTER_ARENA_PUSH_ARRAY(arena, uint16_t, records);
        state->front.positions = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, records);
        state->front.arguments = BUSTER_ARENA_PUSH_ARRAY(arena, uint16_t, records);
        room = state->firsts != NULL && state->roots != NULL && state->counts != NULL && state->bases != NULL && state->records != NULL && state->operands != NULL &&
               state->argument_bases != NULL && state->given != NULL && state->placed != NULL && state->taken != NULL && state->front.ops != NULL && state->front.kinds != NULL &&
               state->front.classes != NULL && state->front.fits != NULL && state->front.left != NULL && state->front.right != NULL && state->front.inputs != NULL &&
               state->front.counts != NULL && state->front.positions != NULL && state->front.arguments != NULL;
    }
    const bool window = (reader->pp->options.node_reader & BUSTER_NODE_READER_WINDOW) != 0;
    if (room && window)
    {
        // By the front's record, each with a vector's records past the front's.
        const uint64_t places = (uint64_t)BUSTER_NODE_FRONT_RECORDS + 64;
        state->place_taken = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, places);
        state->place_kinds = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, places);
        state->place_fits = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, places);
        state->place_types = BUSTER_ARENA_PUSH_ARRAY(arena, TypeId, places);
        state->place_flags = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, places);
        state->place_objects = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, places);
        state->place_payloads = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, places);
        state->place_standings = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, places);
        state->place_exponents = BUSTER_ARENA_PUSH_ARRAY(arena, uint16_t, places);
        state->place_words = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, places);
        state->place_converted = BUSTER_ARENA_PUSH_ARRAY(arena, TypeId, 2 * places);
        state->place_conversion_flags = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, 2 * places);
        state->place_member_offsets = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, places);
        state->name_list = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, places);
        state->name_kinds = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, places);
        state->name_entries = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, places);
        room = state->place_taken != NULL && state->place_kinds != NULL && state->place_fits != NULL && state->place_types != NULL && state->place_flags != NULL &&
               state->place_objects != NULL && state->place_payloads != NULL && state->place_standings != NULL && state->place_exponents != NULL &&
               state->place_words != NULL && state->place_converted != NULL && state->place_conversion_flags != NULL && state->place_member_offsets != NULL &&
               state->name_list != NULL && state->name_kinds != NULL && state->name_entries != NULL;
        if (room)
        {
            // T's records of the places no expression has taken are read as given records': zeros, not what the arena had.
            memset(state->place_taken, 0, places);
            memset(state->place_kinds, 0, places);
            memset(state->place_fits, 0, places);
            memset(state->place_types, 0, places * sizeof(TypeId));
            memset(state->place_flags, 0, places * sizeof(uint32_t));
            memset(state->place_objects, 0, places * sizeof(uint32_t));
            memset(state->place_payloads, 0, places * sizeof(uint64_t));
            memset(state->place_standings, 0, places);
            memset(state->place_exponents, 0, places * sizeof(uint16_t));
            memset(state->place_words, 0, places * sizeof(uint32_t));
            memset(state->place_converted, 0, 2 * places * sizeof(TypeId));
            memset(state->place_conversion_flags, 0, 2 * places * sizeof(uint32_t));
            memset(state->place_member_offsets, 0, places * sizeof(uint64_t));
        }
    }
    room = room && node_facts_begin(&state->facts, machine, BUSTER_NODE_FACTS_READER_RECORDS, 0, true, false, true, BUSTER_NODE_FACTS_READER_LINES, arena);
    if (room)
    {
        state->facts.enum_values = reader->enum_values;
        memset(state->front.fits, 0, records);
        for (uint32_t op = 0; op < CHECK_OP_COUNT; op += 1)
        {
            const uint32_t lookup = op == CHECK_OP_CAST ? BUSTER_NODE_FACTS_LOOKUPS : node_facts_lookup((CheckOp)op);
            const uint32_t operation = node_facts_operator((CheckOp)op);
            const bool made = lookup < BUSTER_NODE_FACTS_LOOKUPS || operation < BUSTER_NODE_FACTS_CODES;
            state->op_kinds[op] = lookup < BUSTER_NODE_FACTS_LOOKUPS ? BUSTER_NODE_FACTS_LOOKUP : made ? BUSTER_NODE_FACTS_OPERATOR : BUSTER_NODE_FACTS_GIVEN;
            state->op_codes[op] = (uint8_t)(lookup < BUSTER_NODE_FACTS_LOOKUPS ? node_facts_lookup_codes[lookup] : operation);
        }
    }
    else
    {
        types->facts_counts[NODE_FACTS_COUNT_FAILURES] += 1;
    }
    return room ? state : NULL;
}

// The windows' inputs exchanged with `other`'s: the kernel then reads the other making's.
static void node_facts_inputs_exchange(NodeFacts* facts, NodeFactsInputs* other)
{
    const NodeFactsInputs own = {
        .ops = facts->ops,
        .kinds = facts->kinds,
        .classes = facts->classes,
        .fits = facts->fits,
        .left = facts->left,
        .right = facts->right,
        .inputs = facts->inputs,
        .counts = facts->counts,
        .positions = facts->positions,
        .arguments = facts->arguments,
        .argument_count = facts->argument_count,
    };
    facts->ops = other->ops;
    facts->kinds = other->kinds;
    facts->classes = other->classes;
    facts->fits = other->fits;
    facts->left = other->left;
    facts->right = other->right;
    facts->inputs = other->inputs;
    facts->counts = other->counts;
    facts->positions = other->positions;
    facts->arguments = other->arguments;
    facts->argument_count = other->argument_count;
    *other = own;
}

// A difference of the kernel's at reader time, of kind `kind`, at the batch's record `w`, whose token is `token`:
// counted by the caller, and listed among the unit's first.
static void node_facts_reader_difference(struct NodeFactsReader* state, uint32_t w, uint32_t token, uint32_t kind)
{
    const NodeFacts* facts = &state->facts;
    NodeFrontTypes* types = state->types;
    if (types->facts_listed < BUSTER_NODE_FRONT_LISTED)
    {
        types->facts_difference_tokens[types->facts_listed] = token;
        types->facts_difference_ops[types->facts_listed] = facts->ops[w];
        types->facts_difference_kinds[types->facts_listed] = (uint8_t)kind;
        types->facts_listed += 1;
    }
}

// The token of the batch's record `w`, by its node (node_facts_append's).
static uint32_t node_facts_reader_token(const NodeFacts* facts, uint32_t w)
{
    const CheckGraph* graph = facts->machine->graph;
    const uint32_t node = facts->nodes[w];
    return node < graph->count ? graph->tokens[node] : BUSTER_CHECK_NONE;
}

// A leaf's value as a line, the leaf's record's first half as T has it (node_machine_put_value's columns): whether the
// columns hold the value whole, which a line's leaf is.
static bool node_facts_value_line(const NodeMachine* machine, const CheckValue* value, NodeFactsRecord* line)
{
    const CheckConstantKind kind = value->constant_kind;
    const uint64_t low = (uint64_t)value->integer;
    const uint64_t high = (uint64_t)(value->integer >> 64);
    const uint64_t offset = (uint64_t)value->offset;
    const uint64_t significand = value->floating_storage[0];
    const uint64_t exponent = value->floating_storage[1] & 0xffffu;
    const bool floating = significand != 0 || exponent != 0;
    const bool rare = high != 0 || value->imaginary_storage[0] != 0 || (value->imaginary_storage[1] & 0xffffu) != 0 || value->overload_head != 0 ||
                      value->bit_width != 0 || value->fault != CHECK_FAULT_NONE || value->fault_node != 0 || value->object_bit_offset != 0 ||
                      (uint32_t)value->category > 0x03u || (uint32_t)kind > 0x07u;
    const uint32_t fields = (low != 0) + (offset != 0) + floating;
    const uint64_t payload = offset != 0 ? offset : floating ? significand : low;
    *line = (NodeFactsRecord){
        .type = value->type,
        .word = node_machine_word(machine, value->type),
        .flags = value->flags,
        .object = value->object,
        .payload = payload,
        .standing = (uint8_t)(BUSTER_NODE_MACHINE_STANDING(value->category & 0x03u, kind & 0x07u) | (offset != 0 ? BUSTER_NODE_MACHINE_OFFSET : floating ? BUSTER_NODE_MACHINE_FLOATING : 0)),
        .taken = 1,
        .exponent = (uint16_t)(offset == 0 && floating ? exponent : node_facts_small(payload)),
    };
    return !rare && fields <= 1;
}

// A literal's or a string's line of its own for `value`, as node_facts_literal makes one from T's record: its number,
// or 0 where the columns don't hold the value whole or the lines are full.
static uint32_t node_facts_front_literal(NodeFacts* facts, const CheckValue* value)
{
    NodeFactsRecord line;
    const bool whole = node_facts_value_line(facts->machine, value, &line);
    const uint32_t number = whole && facts->literal_count < facts->literal_capacity ? facts->literal_count + 1 : 0;
    if (number != 0)
    {
        facts->literal_count = number;
        memcpy(facts->lines + 4 * (uint64_t)number, &line, 4 * sizeof(uint64_t));
    }
    return number;
}

// The line of declaration `declaration` at the use `use`, its number: the declaration's last line where the use reads
// the type the line has and the unit has no symbol pragma (a weak one changes a line from its token on), one load and
// one test; and else node_facts_declaration's, which makes the line from the reader's columns and compares.
static inline uint32_t node_facts_front_line(NodeFacts* facts, uint32_t declaration, uint32_t use)
{
    const CheckInput* input = facts->input;
    const uint32_t number = facts->numbers[declaration];
    const bool settled = number != 0 && (TypeId)facts->lines[4 * (uint64_t)number] == input->declared_types[use] &&
                         (input->symbol_pragmas == NULL || input->symbol_pragmas->count == 0);
    return settled ? number : node_facts_declaration(facts, declaration, use);
}

// An integer literal's line of its own, check_input_literal_value's leaf of type `type` (whose type's index is `index`)
// and value `integer` at `token`: a constant expression's value, and the spelling of zero where it is a number's. Its
// number, or 0 where the lines are full.
static inline __attribute__((always_inline)) uint32_t node_facts_front_integer(NodeFacts* facts, const Parser* parser, uint32_t token, TypeId type, uint32_t index,
                                                                               uint64_t integer)
{
    const bool zero = integer == 0 && parser->tokens.kinds[token] == PARSE_KIND_NUMBER && parser->tokens.names[token] == 0;
    const NodeFactsRecord line = {
        .type = type,
        .word = facts->rows[index].word,
        .flags = BUSTER_CHECK_ICE | (zero ? BUSTER_CHECK_LITERAL_ZERO : 0),
        .payload = integer,
        .standing = BUSTER_NODE_MACHINE_STANDING(CHECK_RVALUE, CHECK_CONSTANT_INTEGER),
        .taken = 1,
        .exponent = (uint16_t)node_facts_small(integer),
    };
    const uint32_t number = facts->literal_count < facts->literal_capacity ? facts->literal_count + 1 : 0;
    if (number != 0)
    {
        facts->literal_count = number;
        memcpy(facts->lines + 4 * (uint64_t)number, &line, 4 * sizeof(uint64_t));
    }
    return number;
}

// The inputs of an expression's records from the lean front's records and the reader's columns alone, into `state`'s
// front inputs at the batch's records from `base`: the expression whose root is the front's `record`, with its operand's
// record `operand`, `count` records as T takes it (the operand's subtree, and the root but an initialization's). No node
// of the walk's and no record of T's is read. BUSTER_NODE_FACTS_WHY_GIVEN where the front's records give it whole, and
// else why not: its subtree's records are not all made in the chunk at hand (a chunk's end keeps the waiting values'
// places alone), an operand is past a byte's distance, a call has 255 operands or more, the reader doesn't decode a
// literal that is no integer again. What the front's records can't fail is not tested: a record's operands are records
// before it in its expression, a name's use has its declaration, and a call's arguments are its records, the front's own
// comparison with the walk's nodes having said so of each record. An expression that is not given leaves what it took
// of the batch's argument places and literals' lines: its caller takes the other making's counts for it.
//
// - An operand is a distance back in the postorder, which is the machine's records' order; a root's operand is the
//   record before it.
// - An identifier's leaf is its declaration's line (node_facts_front_line), unless T's record of it is no columns': an
//   enumerator's value past 64 bits (the line's BUSTER_NODE_FACTS_HIGH).
// - A literal's leaf is a line of its type and its integer as the reader decoded them (check_input_literal_value), a
//   floating literal's of its spelling decoded again; a string's of its type and its object.
// - A member's name is the token's after the operator's; a cast's type its token's query's (CheckInputReader.query_types),
//   a root's cast's void (a statement's discard), and a return's conversion's the function's
//   (CheckInputReader.return_types).
// - A call's arguments are the records before it, the last first, each one's subtree before the next
//   (NodeFront.sizes), and its callee the record before the first argument's subtree.
// - A record's source position is its token's expression's end (check_input_expression_end).
// - Its kind and its class are node_facts_append's of the same things.
//
// A record at a time, with a branch on its operator: the same by passes over a batch, with no branch a record, measured
// no faster on the 9700X (docs/one-core-path.md, "Step 2's third piece": the passes spend about 125 instructions a
// record, with no hot spot).
static uint32_t node_facts_front_expression(struct NodeFactsReader* state, const NodeFront* front, uint32_t base, uint32_t count, uint32_t record, uint32_t operand)
{
    NodeFacts* facts = &state->facts;
    NodeFactsInputs* set = &state->front;
    CheckInputReader* reader = state->reader;
    const Parser* parser = facts->parser;
    const bool initialization = front->ops[record] == CHECK_OP_INITIALIZE;
    const uint32_t size = operand < record ? front->sizes[operand] : 0;
    // Its subtree's records are the chunk's own, which are in a row: a record kept from an earlier chunk has its place
    // alone (node_front_keep), and the records below it are gone. Where the front writes the window it keeps a waiting
    // value's records whole, in a row, and says of each record whether its subtree is there.
    const bool row = front->window ? front->window_classes[operand < record ? operand : record] != BUSTER_NODE_FACTS_CLASS_CUT : operand + 1 - size >= front->made;
    bool whole = size != 0 && count == (initialization ? size : size + 1) && size <= operand + 1 && row;
    uint32_t why = whole ? BUSTER_NODE_FACTS_WHY_GIVEN : BUSTER_NODE_FACTS_WHY_CUT;
    const uint32_t start = whole ? operand + 1 - size : 0;
    for (uint32_t r = 0; whole && r < count; r += 1)
    {
        const bool root = r == size;
        const uint32_t x = root ? record : start + r;
        const uint32_t w = base + r;
        const CheckOp op = (CheckOp)front->ops[x];
        const uint32_t token = front->tokens[x];
        // A root's one operand is the record before it in the machine's order, whatever is between them in the front's.
        const uint32_t a = root ? 1 : front->distances[0][x];
        const uint32_t b = front->distances[1][x];
        const uint32_t c = front->distances[2][x];
        uint8_t kind = BUSTER_NODE_FACTS_GIVEN;
        uint32_t code = BUSTER_NODE_FACTS_CODES;
        uint32_t entry = 0;
        uint16_t arguments = 0;
        // Each operand is within a byte's distance; and within the expression, which its subtree's size has said already
        // (the test keeps a wrong size from reading before the batch).
        whole = a != BUSTER_NODE_FRONT_FAR && b != BUSTER_NODE_FRONT_FAR && c != BUSTER_NODE_FRONT_FAR && a <= r && b <= r && c <= r;
        why = whole ? why : BUSTER_NODE_FACTS_WHY_REACH;
        if (whole && op == CHECK_OP_VALUE)
        {
            const uint8_t category = front->leaf_categories[x];
            const TypeId type = front->leaf_types[x];
            const uint32_t index = type_index(type);
            if (category == BUSTER_NODE_FRONT_LEAF_IDENTIFIER)
            {
                const uint32_t number = node_facts_front_line(facts, (uint32_t)front->leaf_integers[x], token);
                // T's record of an enumerator past 64 bits is no columns', so its leaf is given (T refuses a name with an
                // overload set, the other leaf the columns don't hold).
                const bool high = number != 0 && ((uint32_t)(facts->lines[4 * (uint64_t)number + 3] >> 32) & BUSTER_NODE_FACTS_HIGH) != 0;
                kind = number != 0 && !high ? BUSTER_NODE_FACTS_IDENTIFIER : BUSTER_NODE_FACTS_LITERAL;
                entry = kind == BUSTER_NODE_FACTS_IDENTIFIER ? number : 0;
            }
            else if (category == BUSTER_NODE_FRONT_LEAF_LITERAL && index < facts->row_count && (facts->rows[index].flags & BUSTER_NODE_FACTS_INTEGER) != 0)
            {
                const uint32_t number = node_facts_front_integer(facts, parser, token, type, index, front->leaf_integers[x]);
                kind = number != 0 ? BUSTER_NODE_FACTS_LITERAL_LINE : BUSTER_NODE_FACTS_LITERAL;
                entry = number;
            }
            else
            {
                // Another literal's, of its spelling decoded again, and a string's, of its type and its object.
                CheckValue value = { 0 };
                if (category == BUSTER_NODE_FRONT_LEAF_LITERAL)
                {
                    CheckLiteral decoded = { 0 };
                    whole = node_front_literal(reader, token, &decoded);
                    why = whole ? why : BUSTER_NODE_FACTS_WHY_LITERAL;
                    value = check_input_literal_value(reader, token, decoded);
                }
                else
                {
                    const uint32_t object = (uint32_t)front->leaf_integers[x];
                    value = (CheckValue){
                        .type = type,
                        .category = CHECK_LVALUE,
                        .flags = BUSTER_CHECK_STATIC_OBJECT | (object != 0 ? BUSTER_CHECK_NONNULL_ADDRESS : 0),
                        .constant_kind = CHECK_CONSTANT_ADDRESS,
                        .object = object,
                    };
                }
                const uint32_t number = whole ? node_facts_front_literal(facts, &value) : 0;
                kind = number != 0 ? BUSTER_NODE_FACTS_LITERAL_LINE : BUSTER_NODE_FACTS_LITERAL;
                entry = number;
            }
        }
        else if (whole)
        {
            kind = state->op_kinds[op];
            code = state->op_codes[op];
            if (op == CHECK_OP_CAST)
            {
                // A root's cast is a statement's discard; another's type is its token's query's.
                const TypeId written = root ? BUSTER_TYPE_VOID : reader->query_types[token];
                const bool discard = written == BUSTER_TYPE_VOID;
                entry = written;
                kind = !discard ? kind : facts->lean ? BUSTER_NODE_FACTS_DISCARD : BUSTER_NODE_FACTS_LOOKUP;
                code = discard ? BUSTER_NODE_FACTS_CODE_DISCARD : code;
            }
            else if (op == CHECK_OP_ARROW || op == CHECK_OP_MEMBER)
            {
                entry = parser->tokens.names[token + 1];
            }
            else if (op == CHECK_OP_CONVERT)
            {
                entry = reader->return_types[token];
            }
            else if (op == CHECK_OP_CONDITIONAL)
            {
                entry = c;
            }
            else if (op == CHECK_OP_CALL)
            {
                // Its arguments from the last back: each is the record before the subtree of the one after it.
                // The callee is one of its operands, and their count is a byte's, 255 for that or more.
                const uint32_t operands = front->counts[x];
                const uint32_t argument_count = operands - 1;
                uint32_t distance = 1;
                whole = operands != 255 && (uint64_t)set->argument_count + argument_count <= facts->record_capacity;
                why = whole ? why : BUSTER_NODE_FACTS_WHY_CALL;
                for (uint32_t k = argument_count; whole && k-- != 0;)
                {
                    set->arguments[set->argument_count + k] = (uint16_t)distance;
                    distance += front->sizes[x - distance];
                }
                entry = set->argument_count;
                arguments = (uint16_t)argument_count;
                set->argument_count += whole ? argument_count : 0;
            }
        }
        if (whole)
        {
            const bool made = kind == BUSTER_NODE_FACTS_LOOKUP || kind == BUSTER_NODE_FACTS_OPERATOR;
            uint32_t round = 0;
            if (made)
            {
                round = a != 0 ? node_facts_round(round, code, set->classes[w - a]) : round;
                round = b != 0 ? node_facts_round(round, code, set->classes[w - b]) : round;
                round = c != 0 ? node_facts_round(round, code, set->classes[w - c]) : round;
                for (uint32_t i = 0; i < arguments; i += 1)
                {
                    round = node_facts_round(round, code, set->classes[w - set->arguments[entry + i]]);
                }
            }
            set->classes[w] = made ? node_facts_class(round, code) : BUSTER_NODE_FACTS_NO_CLASS;
            set->ops[w] = (uint8_t)op;
            set->kinds[w] = kind;
            set->left[w] = (uint16_t)a;
            set->right[w] = (uint16_t)b;
            set->inputs[w] = entry;
            set->counts[w] = arguments;
            set->positions[w] = check_input_expression_end(reader, token);
        }
    }
    return why;
}

// The inputs of the batch's records from `base`, `count` of them, and of their calls' arguments, from `argument_base` to
// `argument_end`, taken into `set` from the windows' own, node_facts_append's: an expression `set`'s making doesn't give.
static void node_facts_inputs_take(NodeFactsInputs* set, const NodeFacts* facts, uint32_t base, uint32_t count, uint32_t argument_base, uint32_t argument_end)
{
    memcpy(set->ops + base, facts->ops + base, count);
    memcpy(set->kinds + base, facts->kinds + base, count);
    memcpy(set->classes + base, facts->classes + base, count);
    memcpy(set->left + base, facts->left + base, (uint64_t)count * sizeof(uint16_t));
    memcpy(set->right + base, facts->right + base, (uint64_t)count * sizeof(uint16_t));
    memcpy(set->inputs + base, facts->inputs + base, (uint64_t)count * sizeof(uint32_t));
    memcpy(set->counts + base, facts->counts + base, (uint64_t)count * sizeof(uint16_t));
    memcpy(set->positions + base, facts->positions + base, (uint64_t)count * sizeof(uint32_t));
    memcpy(set->arguments + argument_base, facts->arguments + argument_base, (uint64_t)(argument_end - argument_base) * sizeof(uint16_t));
}

// The window the front writes (BUSTER_NODE_READER_WINDOW; measure-node-machine --shadow --front-window;
// docs/one-core-path.md, "The front writes the window"). The copy above reads a record's fields back from the front's
// columns, by a branch on its operator, after the front made the chunk: the front had each of those values in a register
// when it made the record, and had branched on what the record is. Here the front's pass writes a record's inputs as it
// makes the record, into columns of its own beside its others (NodeFront.window_kinds and the rest), and the kernel
// reads them there: a chunk's records are its window, with nothing copied (node_facts_window_run).
//
// What the front has when it makes a record is all of a record's inputs but one:
// - A literal's leaf's line is a batch's own. The batch's first lines have the front's records' numbers, so a
//   literal's line is its record's, written as the record is.
// - A call's arguments are the records the front has just taken off its stack: their distances go to a list of the
//   front's, a call's in a row.
// - A name's leaf's line is its declaration's, two loads from the leaf (the line's number by the declaration, then the
//   line) that miss the nearest caches where the declaration is far. In the front's own pass, which retires some two
//   hundred instructions a record, a load that misses holds the pass up: measured there on the 9700X the two cost
//   three times what they cost in a loop of their own (docs/one-core-path.md). So the front writes the declaration and
//   asks the cache for its line's number, and a pass over the chunk's records right after it gives each name's leaf
//   its line (node_facts_window_names), by sweeps that ask for a load before they read it. A line is made at its
//   declaration's first use, which is there.
//
// A record's subtree is the `NodeFront.sizes` records that end at it: the front makes an expression's records in its
// postorder, each operand's subtree in a row before the next's, and the walk acts at no token within an expression
// whose value no token takes (such a record among them would show in the front's comparison with the walk's nodes and
// in T's own test of an expression's range). A chunk's end keeps a waiting value's row whole, or keeps its place alone
// and cuts it (node_front_keep; BUSTER_NODE_FACTS_CLASS_CUT in its class), and a record with a cut operand is cut as it
// is made. An operand's distance is the difference of the two records' places. A root's operand is wherever its value
// waited (a control's condition's, for its body): the kernel reads an operand by its distance, whatever lies between.
//
// The kernel makes the records of the expressions T took whose records are there in a row, and reads every other
// record of the window as given (`place_kinds`): the shadow has T's records for those alone, and a compile's kernel
// would make every record and refuse by expression.

// Whether the front writes the window in the unit `reader` reads, whose kernel at reader time is `front`'s typer's: the
// options ask, and the kernel runs there on the lean front's records. With it, the operators' kinds and codes for the
// writers, and the same lean front without the window on the reader's scratch, run beside this one for its ticks
// (NodeFront.plain; without room for it the window is still written, and its ticks have nothing to be read against).
static void node_facts_window_begin(NodeFront* front, CheckInputReader* reader)
{
    struct NodeFactsReader* state = front->typer != NULL ? front->typer->facts : NULL;
    _Static_assert(CHECK_OP_COUNT <= 256, "an operator's kind and code are a byte's index away");
    if (state != NULL && front->lean && state->place_taken != NULL)
    {
        memset(front->window_op_kinds, BUSTER_NODE_FACTS_GIVEN, sizeof(front->window_op_kinds));
        memset(front->window_op_codes, BUSTER_NODE_FACTS_CODES, sizeof(front->window_op_codes));
        memcpy(front->window_op_kinds, state->op_kinds, CHECK_OP_COUNT);
        memcpy(front->window_op_codes, state->op_codes, CHECK_OP_COUNT);
        NodeFront* plain = arena_push_array(reader->scratch, 1, sizeof(NodeFront), 64);
        NodeFrontShadow* shadow = arena_push_array(reader->scratch, 1, sizeof(NodeFrontShadow), _Alignof(NodeFrontShadow));
        if (plain != NULL && shadow != NULL)
        {
            if ((reader->pp->options.node_front & BUSTER_NODE_FRONT_WARM) != 0)
            {
                memset(plain, 0, sizeof(NodeFront));
            }
            // Its report is its own: no node of the walk's is its to mark (a node's capacity of none).
            memset(shadow, 0, sizeof(*shadow));
            plain->shadow = shadow;
            plain->count = 0;
            plain->pending = 0;
            plain->made = 0;
            plain->stack_count = 0;
            plain->group_count = 0;
            plain->full = false;
            plain->lean = true;
            plain->ordinal = 0;
            plain->argument_count = 0;
            plain->wide_count = 0;
            plain->typer = NULL;
            plain->flat = NULL;
            plain->window = false;
            plain->window_facts = NULL;
            plain->window_numbers = NULL;
            plain->window_argument_count = 0;
            plain->plain = NULL;
            plain->chunks = 0;
            front->plain = plain;
        }
        front->window = true;
        front->window_facts = &state->facts;
        front->window_numbers = state->facts.numbers;
        front->window_argument_count = 0;
        state->windowed = true;
        // The lines' first numbers are the front's records': a literal's leaf's line has its record's.
        state->facts.literal_count = BUSTER_NODE_FRONT_RECORDS;
    }
}

// A record the front kept at a chunk's end (node_front_keep), at `to` from `from`. A literal's or a string's leaf's
// line has its record's number, and moves with it. A call's arguments are kept after those of the calls kept before
// it (`arguments` of them), where the next chunk's follow.
static inline void node_facts_window_kept(NodeFront* front, uint32_t to, uint32_t from, uint32_t* arguments)
{
    if (front->window_kinds[to] == BUSTER_NODE_FACTS_LITERAL_LINE && to != from)
    {
        NodeFacts* facts = front->window_facts;
        memcpy(facts->lines + 4 * ((uint64_t)to + 1), facts->lines + 4 * ((uint64_t)from + 1), 4 * sizeof(uint64_t));
        front->window_entries[to] = to + 1;
    }
    else if (front->ops[to] == CHECK_OP_CALL)
    {
        const uint32_t count = front->window_counts[to];
        memmove(front->window_arguments + *arguments, front->window_arguments + front->window_entries[to], (uint64_t)count * sizeof(uint16_t));
        front->window_entries[to] = *arguments;
        *arguments += count;
    }
}

// A literal's leaf whose type's row is no integer's, or a string's: its line, as node_facts_front_expression makes it
// (of the reader's decoding `decoded` of the literal, which the front has at hand; of the string's type and object),
// at the lines' number `number`. Whether the columns hold the value whole, which a line's leaf is.
static __attribute__((noinline)) bool node_facts_window_rare_leaf(NodeFront* front, CheckInputReader* reader, uint32_t record, uint32_t token, uint32_t category,
                                                                  const CheckLiteral* decoded, uint32_t number)
{
    NodeFacts* facts = front->window_facts;
    const TypeId type = front->leaf_types[record];
    CheckValue value = { 0 };
    if (category == BUSTER_NODE_FRONT_LEAF_LITERAL)
    {
        value = check_input_literal_value(reader, token, *decoded);
    }
    else
    {
        const uint32_t object = (uint32_t)front->leaf_integers[record];
        value = (CheckValue){
            .type = type,
            .category = CHECK_LVALUE,
            .flags = BUSTER_CHECK_STATIC_OBJECT | (object != 0 ? BUSTER_CHECK_NONNULL_ADDRESS : 0),
            .constant_kind = CHECK_CONSTANT_ADDRESS,
            .object = object,
        };
    }
    // Its type's word is the machine's since it began: a literal's type is a basic one, and a string's array was made
    // when the unit's names were resolved, before the walk.
    NodeFactsRecord line;
    const bool whole = node_facts_value_line(facts->machine, &value, &line);
    if (whole)
    {
        memcpy(facts->lines + 4 * (uint64_t)number, &line, 4 * sizeof(uint64_t));
    }
    return whole;
}

// A leaf's record's inputs, as the lean front makes the record `record` at `token`, of `category`
// (BUSTER_NODE_FRONT_LEAF_*): node_facts_front_expression's of the same record, from what the front holds.
// - A name's (its declaration `declaration`): an identifier's kind and its declaration, whose line the names' pass
//   over the chunk finds (node_facts_window_names).
// - A literal's or a string's: a line of its own, the batch's line of its record's number (the lines' first numbers
//   are the front's records': node_facts_window_begin), made here of the type and the value the front has just written
//   in its own columns. `decoded` is the reader's decoding of a literal.
static inline __attribute__((always_inline)) void node_facts_window_leaf(NodeFront* front, CheckInputReader* reader, uint32_t record, uint32_t token, uint32_t category,
                                                                         uint32_t declaration, const CheckLiteral* decoded)
{
    uint8_t kind = BUSTER_NODE_FACTS_IDENTIFIER;
    uint32_t entry = declaration;
    if (category == BUSTER_NODE_FRONT_LEAF_IDENTIFIER)
    {
        // Its declaration's line's number is asked for, not read: the names' pass reads it, once the chunk is made.
        __builtin_prefetch(front->window_numbers + declaration);
    }
    else
    {
        NodeFacts* facts = front->window_facts;
        const uint32_t number = record + 1;
        const TypeId type = front->leaf_types[record];
        const uint32_t index = type_index(type);
        bool whole = true;
        // A literal of an integer's type (a string's is an array's, which has no integer's row). Its type has a row: it
        // was the unit's before the walk, as node_facts_window_rare_leaf says.
        if ((facts->rows[index].flags & BUSTER_NODE_FACTS_INTEGER) != 0)
        {
            // check_input_literal_value's leaf: a constant expression's value, and the spelling of zero where it is a
            // number's (node_facts_front_integer's line).
            // The line's four quadwords, each from registers (a record's first half: its type and word; its flags and
            // object; its payload; its standing and `taken`): a record built in memory and copied would have the copy's
            // load wait for its fields' stores.
            const Parser* parser = facts->parser;
            const uint64_t integer = front->leaf_integers[record];
            const bool zero = integer == 0 && parser->tokens.kinds[token] == PARSE_KIND_NUMBER && parser->tokens.names[token] == 0;
            uint64_t* const line = facts->lines + 4 * (uint64_t)number;
            line[0] = (uint64_t)type | (uint64_t)facts->rows[index].word << 32;
            line[1] = BUSTER_CHECK_ICE | (zero ? BUSTER_CHECK_LITERAL_ZERO : 0);
            line[2] = integer;
            line[3] = (uint64_t)BUSTER_NODE_MACHINE_STANDING(CHECK_RVALUE, CHECK_CONSTANT_INTEGER) | UINT64_C(1) << 8 | (uint64_t)node_facts_small(integer) << 16;
        }
        else
        {
            whole = node_facts_window_rare_leaf(front, reader, record, token, category, decoded, number);
        }
        kind = whole ? BUSTER_NODE_FACTS_LITERAL_LINE : BUSTER_NODE_FACTS_LITERAL;
        entry = whole ? number : 0;
    }
    front->window_kinds[record] = kind;
    front->window_classes[record] = BUSTER_NODE_FACTS_NO_CLASS;
    front->window_left[record] = 0;
    front->window_right[record] = 0;
    front->window_entries[record] = entry;
    front->window_positions[record] = check_input_expression_end(reader, token);
}

// An operator's record's inputs, as the lean front makes the record `record` of `op` at `token` with operands the
// records a, b and c (BUSTER_NODE_FRONT_UNKNOWN for none; a conditional with its middle omitted has its first twice):
// node_facts_front_expression's of the same record, from what the front holds. `entry` is a member's name, a cast's
// type (void: a discard, a root's a statement's) or a return's conversion's; a conditional's is its third operand's
// distance. A root's operand is wherever its value waited, and its distance says so.
static inline __attribute__((always_inline)) void node_facts_window_operator(NodeFront* front, const CheckInputReader* reader, uint32_t record, CheckOp op, uint32_t token,
                                                                             uint32_t a, uint32_t b, uint32_t c, uint32_t entry)
{
    const bool second = b != BUSTER_NODE_FRONT_UNKNOWN;
    const bool third = c != BUSTER_NODE_FRONT_UNKNOWN;
    const uint32_t class_a = front->window_classes[a];
    const uint32_t class_b = second ? front->window_classes[b] : BUSTER_NODE_FACTS_NO_CLASS;
    const uint32_t class_c = third ? front->window_classes[c] : BUSTER_NODE_FACTS_NO_CLASS;
    const bool discard = op == CHECK_OP_CAST && entry == BUSTER_TYPE_VOID;
    const uint8_t kind = discard ? BUSTER_NODE_FACTS_LOOKUP : front->window_op_kinds[op];
    const uint32_t code = discard ? BUSTER_NODE_FACTS_CODE_DISCARD : front->window_op_codes[op];
    const bool made = kind == BUSTER_NODE_FACTS_LOOKUP || kind == BUSTER_NODE_FACTS_OPERATOR;
    // Its round: the greatest its operands ask (node_facts_round's).
    const uint32_t round_a = node_facts_round_of(code, class_a);
    const uint32_t round_b = node_facts_round_of(code, class_b);
    const uint32_t round_c = node_facts_round_of(code, class_c);
    const uint32_t round_ab = round_a > round_b ? round_a : round_b;
    const uint32_t round = round_ab > round_c ? round_ab : round_c;
    // Its records are in a row that ends at it unless an operand's are not.
    const bool cut = class_a == BUSTER_NODE_FACTS_CLASS_CUT || class_b == BUSTER_NODE_FACTS_CLASS_CUT || class_c == BUSTER_NODE_FACTS_CLASS_CUT;
    front->window_kinds[record] = kind;
    front->window_classes[record] = (uint8_t)(cut ? BUSTER_NODE_FACTS_CLASS_CUT : made ? node_facts_class(round, code) : BUSTER_NODE_FACTS_NO_CLASS);
    front->window_left[record] = (uint16_t)(record - a);
    front->window_right[record] = (uint16_t)(second ? record - b : 0);
    front->window_entries[record] = op == CHECK_OP_CONDITIONAL ? (third ? record - c : 0) : entry;
    front->window_positions[record] = check_input_expression_end(reader, token);
}

// A call's record's inputs, as the lean front makes the record `record` at `token`: its callee's record and its
// arguments' are `values`, `count` of them. Its arguments' distances are the next of the front's list.
static inline __attribute__((always_inline)) void node_facts_window_call(NodeFront* front, const CheckInputReader* reader, uint32_t record, uint32_t token,
                                                                         const uint32_t* values, uint32_t count)
{
    const uint32_t code = front->window_op_codes[CHECK_OP_CALL];
    const uint32_t arguments = front->window_argument_count;
    uint32_t round = 0;
    bool cut = false;
    for (uint32_t k = 0; k < count; k += 1)
    {
        const uint32_t operand = values[k];
        const uint32_t class = front->window_classes[operand];
        const uint32_t asked = node_facts_round_of(code, class);
        round = asked > round ? asked : round;
        cut = cut || class == BUSTER_NODE_FACTS_CLASS_CUT;
        // The list has the arguments' distances: the callee's is written where the first argument's then is.
        front->window_arguments[arguments + k - (k != 0 ? 1u : 0u)] = (uint16_t)(record - operand);
    }
    front->window_kinds[record] = front->window_op_kinds[CHECK_OP_CALL];
    front->window_classes[record] = (uint8_t)(cut ? BUSTER_NODE_FACTS_CLASS_CUT : node_facts_class(round, code));
    front->window_left[record] = (uint16_t)(record - values[0]);
    front->window_right[record] = 0;
    front->window_entries[record] = arguments;
    front->window_counts[record] = (uint16_t)(count - 1);
    front->window_positions[record] = check_input_expression_end(reader, token);
    front->window_argument_count = arguments + count - 1;
}

// A name's leaf of the front's, at `r` among the records whose kinds and entries are `kinds` and `entries`, its token
// `token`: its entry, its declaration, becomes its declaration's line's number, found by a load where an earlier use
// made the line and made here at its first (node_facts_front_line); a literal's kind where the line is no leaf's of the
// columns' (an enumerator past 64 bits, whose record T's columns don't hold).
static inline __attribute__((always_inline)) void node_facts_window_name(NodeFacts* facts, uint8_t* kinds, uint32_t* entries, uint32_t r, uint32_t token)
{
    const uint32_t number = node_facts_front_line(facts, entries[r], token);
    const bool high = number != 0 && ((uint32_t)(facts->lines[4 * (uint64_t)number + 3] >> 32) & BUSTER_NODE_FACTS_HIGH) != 0;
    const bool named = number != 0 && !high;
    kinds[r] = named ? BUSTER_NODE_FACTS_IDENTIFIER : BUSTER_NODE_FACTS_LITERAL;
    entries[r] = named ? number : 0;
}

// The names' leaves among `count` records from the front's `first`, whose kinds and entries are `kinds` and `entries`
// (the front's own columns, or a copy of them): each gets its declaration's line (node_facts_window_name). How many
// there were. By three sweeps, each a loop of a few loads that the next record's don't wait for:
// - the names' places into `list`, by a compare of the kinds and a compress of the lanes, sixteen records a step;
// - each name's line asked for: its number is a load (the front asked for it as it made the leaf), and the line a
//   prefetch;
// - each name's line read and its kind and entry written.
// A name's two loads are then in the cache when they are read, where one loop a name has the second wait for the
// first (docs/one-core-path.md). The scalar path (`scalar`, a constant where this is inlined; a host without the
// vectors has no other) is that loop, a test a record: the same kinds and entries.
static inline __attribute__((always_inline)) uint32_t node_facts_window_names_by(NodeFacts* facts, const NodeFront* front, uint8_t* kinds, uint32_t* entries, uint32_t* list,
                                                                                 uint32_t first, uint32_t count, const bool scalar)
{
    uint32_t names = 0;
    if (BUSTER_SCALAR_PATH(scalar))
    {
        for (uint32_t r = 0; r < count; r += 1)
        {
            if (kinds[r] == BUSTER_NODE_FACTS_IDENTIFIER)
            {
                node_facts_window_name(facts, kinds, entries, r, front->tokens[first + r]);
                names += 1;
            }
        }
    }
    else
    {
        const __m512i step = _mm512_set1_epi32(16);
        __m512i places = _mm512_set_epi32(15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);
        for (uint32_t i = 0; i < count; i += 16)
        {
            const uint32_t live = count - i < 16 ? count - i : 16;
            const __mmask16 mask = (__mmask16)((1u << live) - 1);
            const __mmask16 found = _mm_mask_cmpeq_epi8_mask(mask, _mm_maskz_loadu_epi8(mask, kinds + i), _mm_set1_epi8(BUSTER_NODE_FACTS_IDENTIFIER));
            _mm512_storeu_si512(list + names, _mm512_maskz_compress_epi32(found, places));
            names += (uint32_t)__builtin_popcount((uint32_t)found);
            places = _mm512_add_epi32(places, step);
        }
        const uint32_t* numbers = facts->numbers;
        const uint64_t* lines = facts->lines;
        for (uint32_t k = 0; k < names; k += 1)
        {
            __builtin_prefetch(lines + 4 * (uint64_t)numbers[entries[list[k]]]);
        }
        for (uint32_t k = 0; k < names; k += 1)
        {
            const uint32_t r = list[k];
            node_facts_window_name(facts, kinds, entries, r, front->tokens[first + r]);
        }
    }
    return names;
}

static __attribute__((noinline)) uint32_t node_facts_window_names_vector(NodeFacts* facts, const NodeFront* front, uint8_t* kinds, uint32_t* entries, uint32_t* list,
                                                                         uint32_t first, uint32_t count)
{
    return node_facts_window_names_by(facts, front, kinds, entries, list, first, count, false);
}

static __attribute__((noinline)) uint32_t node_facts_window_names_scalar(NodeFacts* facts, const NodeFront* front, uint8_t* kinds, uint32_t* entries, uint32_t* list,
                                                                         uint32_t first, uint32_t count)
{
    return node_facts_window_names_by(facts, front, kinds, entries, list, first, count, true);
}

// The names' pass over the chunk the front has just made (its records from `made`): each name's leaf gets its
// declaration's line, in the front's own columns. A line made here reads its declaration's type's word, which the
// machine has had since it began: the reader made the declarations' types before the walk. The pass's ticks, the names
// and the lines it made with theirs go to the front's report. Then the same by the scalar path, on a copy of the
// chunk's kinds and entries as the front left them: a leaf whose kind or line is not the pass's is counted.
static void node_facts_window_names(NodeFront* front)
{
    struct NodeFactsReader* state = front->typer->facts;
    NodeFacts* facts = &state->facts;
    NodeFrontShadow* shadow = front->shadow;
    const uint32_t first = front->made;
    const uint32_t count = front->count - first;
    memcpy(state->name_kinds, front->window_kinds + first, count);
    memcpy(state->name_entries, front->window_entries + first, (uint64_t)count * sizeof(uint32_t));
    const uint64_t lines = facts->line_count;
    const uint64_t line_ticks = facts->line_ticks;
    const uint64_t t0 = __rdtsc();
    const uint32_t names = node_facts_window_names_vector(facts, front, front->window_kinds + first, front->window_entries + first, state->name_list, first, count);
    shadow->window_name_ticks += __rdtsc() - t0;
    shadow->window_names += names;
    shadow->window_lines += facts->line_count - lines;
    shadow->window_line_ticks += facts->line_ticks - line_ticks;
    const uint32_t again = node_facts_window_names_scalar(facts, front, state->name_kinds, state->name_entries, state->name_list, first, count);
    uint32_t differences = again != names;
    for (uint32_t r = 0; r < count; r += 1)
    {
        differences += state->name_kinds[r] != front->window_kinds[first + r] || state->name_entries[r] != front->window_entries[first + r];
    }
    shadow->window_name_differences += differences;
}

// Whether the inputs the front wrote of its record `x` are the batch's record `w`'s, as `facts` has them now
// (node_facts_append's, or the copy's while they are exchanged for those): every field the kernel reads of a record, a
// leaf's line by its number or its words, and a call's arguments. `root`: the record is its expression's root, whose
// operand the front has at `distance` and the batch's record at one.
static bool node_facts_window_same(const NodeFacts* facts, const NodeFront* front, uint32_t x, uint32_t w, bool root, uint32_t distance)
{
    const uint8_t kind = facts->kinds[w];
    const CheckOp op = (CheckOp)facts->ops[w];
    bool same = front->ops[x] == facts->ops[w] && front->window_kinds[x] == kind && front->window_classes[x] == facts->classes[w] &&
                (root ? front->window_left[x] == distance && facts->left[w] == 1 : front->window_left[x] == facts->left[w]) &&
                front->window_right[x] == facts->right[w] && front->window_positions[x] == facts->positions[w];
    if (same && (kind == BUSTER_NODE_FACTS_LITERAL_LINE || kind == BUSTER_NODE_FACTS_IDENTIFIER))
    {
        same = front->window_entries[x] == facts->inputs[w] ||
               memcmp(facts->lines + 4 * (uint64_t)front->window_entries[x], facts->lines + 4 * (uint64_t)facts->inputs[w], 4 * sizeof(uint64_t)) == 0;
    }
    else if (same && op == CHECK_OP_CALL && kind == BUSTER_NODE_FACTS_LOOKUP)
    {
        same = front->window_counts[x] == facts->counts[w];
        for (uint32_t i = 0; same && i < facts->counts[w]; i += 1)
        {
            same = front->window_arguments[front->window_entries[x] + i] == facts->arguments[facts->inputs[w] + i];
        }
    }
    else if (same && kind != BUSTER_NODE_FACTS_LITERAL)
    {
        same = front->window_entries[x] == facts->inputs[w];
    }
    return same;
}

// T's records as the batch has them by its own places, exchanged with `state`'s by the front's records (and back): the
// kernel's run in place reads a given record and compares a made one by the front's place.
static void node_facts_window_expected_exchange(struct NodeFactsReader* state)
{
    NodeFacts* facts = &state->facts;
#define BUSTER_NODE_FACTS_EXCHANGE(type, mine, theirs) \
    do \
    { \
        type* const kept = facts->theirs; \
        facts->theirs = state->mine; \
        state->mine = kept; \
    } while (0)
    BUSTER_NODE_FACTS_EXCHANGE(TypeId, place_types, expected_types);
    BUSTER_NODE_FACTS_EXCHANGE(uint32_t, place_flags, expected_flags);
    BUSTER_NODE_FACTS_EXCHANGE(uint32_t, place_objects, expected_objects);
    BUSTER_NODE_FACTS_EXCHANGE(uint64_t, place_payloads, expected_payloads);
    BUSTER_NODE_FACTS_EXCHANGE(uint8_t, place_standings, expected_standings);
    BUSTER_NODE_FACTS_EXCHANGE(uint16_t, place_exponents, expected_exponents);
    BUSTER_NODE_FACTS_EXCHANGE(uint32_t, place_words, expected_words);
    BUSTER_NODE_FACTS_EXCHANGE(TypeId, place_converted, expected_converted);
    BUSTER_NODE_FACTS_EXCHANGE(uint32_t, place_conversion_flags, expected_conversion_flags);
    BUSTER_NODE_FACTS_EXCHANGE(uint64_t, place_member_offsets, expected_member_offsets);
#undef BUSTER_NODE_FACTS_EXCHANGE
}

// An expression of the batch whose records the front has in a row, the front's from `start` (`size` of them, its
// operand's subtree) and its root `record` (but an initialization's), the batch's `count` from `base`: each of its
// records is one the kernel's run in place makes (`place_taken`), with T's record of it at the front's place; and the
// inputs the front wrote of it are compared with the batch's, node_facts_append's and, where the copy gave the
// expression (`copied`), the copy's.
static void node_facts_window_expression(struct NodeFactsReader* state, const NodeFront* front, uint32_t base, uint32_t count, uint32_t record, uint32_t start, uint32_t size,
                                         bool copied)
{
    NodeFacts* facts = &state->facts;
    NodeFrontTypes* types = state->types;
    const uint32_t distance = record - (start + size - 1);
    for (uint32_t twin = 0; twin < (copied ? 2u : 1u); twin += 1)
    {
        if (twin != 0)
        {
            node_facts_inputs_exchange(facts, &state->front);
        }
        for (uint32_t r = 0; r < count; r += 1)
        {
            const uint32_t x = r < size ? start + r : record;
            if (!node_facts_window_same(facts, front, x, base + r, r == size, distance))
            {
                types->facts_counts[NODE_FACTS_COUNT_WINDOW_DIFFERENCES] += 1;
                node_facts_reader_difference(state, base + r, front->tokens[x], BUSTER_NODE_FACTS_DIFFERENCE_WINDOW);
            }
        }
        if (twin != 0)
        {
            node_facts_inputs_exchange(facts, &state->front);
        }
    }
    for (uint32_t r = 0; r < count; r += 1)
    {
        const uint32_t x = r < size ? start + r : record;
        const uint64_t w = (uint64_t)base + r;
        state->place_taken[x] = 1;
        state->place_types[x] = facts->expected_types[w];
        state->place_flags[x] = facts->expected_flags[w];
        state->place_objects[x] = facts->expected_objects[w];
        state->place_payloads[x] = facts->expected_payloads[w];
        state->place_standings[x] = facts->expected_standings[w];
        state->place_exponents[x] = facts->expected_exponents[w];
        state->place_words[x] = facts->expected_words[w];
        state->place_converted[2 * (uint64_t)x] = facts->expected_converted[2 * w];
        state->place_converted[2 * (uint64_t)x + 1] = facts->expected_converted[2 * w + 1];
        state->place_conversion_flags[2 * (uint64_t)x] = facts->expected_conversion_flags[2 * w];
        state->place_conversion_flags[2 * (uint64_t)x + 1] = facts->expected_conversion_flags[2 * w + 1];
        state->place_member_offsets[x] = facts->expected_member_offsets[w];
    }
}

// The kernel on the front's columns in place: the chunk's records the front holds are one window, read where the
// front wrote them. Nothing is copied but, for the shadow, the kinds: the kernel makes the records of the batch's
// expressions whose records are there in a row (node_facts_window_expression), and every other record is a given one.
// Three runs, as the batch's: over poison, with every record it made compared with T's and its lists with their
// twin's; over decoys; and a timed one, the later run's ticks. The batch's own inputs, T's records and windows are
// exchanged for the front's while it runs, and are the batch's again after.
static void node_facts_window_run(struct NodeFactsReader* state, const NodeFront* front)
{
    NodeFacts* facts = &state->facts;
    uint64_t* counts = state->types->facts_counts;
    const uint32_t mode = BUSTER_NODE_FACTS_WHOLE;
    const uint32_t count = front->count;
    for (uint32_t x = 0; x < count; x += 1)
    {
        state->place_kinds[x] = state->place_taken[x] != 0 ? front->window_kinds[x] : BUSTER_NODE_FACTS_GIVEN;
    }
    // The kernel reads its inputs through these; it writes none of them.
    NodeFactsInputs place = {
        .ops = (uint8_t*)(uintptr_t)front->ops,
        .kinds = state->place_kinds,
        .classes = (uint8_t*)(uintptr_t)front->window_classes,
        .fits = state->place_fits,
        .left = (uint16_t*)(uintptr_t)front->window_left,
        .right = (uint16_t*)(uintptr_t)front->window_right,
        .inputs = (uint32_t*)(uintptr_t)front->window_entries,
        .counts = (uint16_t*)(uintptr_t)front->window_counts,
        .positions = (uint32_t*)(uintptr_t)front->window_positions,
        .arguments = (uint16_t*)(uintptr_t)front->window_arguments,
        .argument_count = front->window_argument_count,
    };
    uint32_t* const windows = facts->windows;
    const uint32_t window_count = facts->window_count;
    node_facts_inputs_exchange(facts, &place);
    node_facts_window_expected_exchange(state);
    state->place_windows[0] = 0;
    state->place_windows[1] = count;
    facts->windows = state->place_windows;
    facts->window_count = 0;
    node_facts_given(facts, 0, BUSTER_NODE_FACTS_POISON_RECORDS);
    node_facts_window(facts, 0, mode, false);
    if (!node_facts_lists_same(facts, 0))
    {
        counts[NODE_FACTS_COUNT_WINDOW_KERNEL_DIFFERENCES] += 1;
        node_facts_reader_difference(state, 0, front->tokens[0], BUSTER_NODE_FACTS_DIFFERENCE_LISTS);
    }
    for (uint32_t x = 0; x < count; x += 1)
    {
        const uint8_t kind = facts->kinds[x];
        const bool line = kind == BUSTER_NODE_FACTS_LITERAL_LINE;
        const bool wanted = kind == BUSTER_NODE_FACTS_IDENTIFIER || kind == BUSTER_NODE_FACTS_LOOKUP || kind == BUSTER_NODE_FACTS_OPERATOR;
        if (line || wanted)
        {
            const uint8_t by = facts->records[x].taken;
            counts[NODE_FACTS_COUNT_WINDOW_WANTED] += wanted;
            counts[NODE_FACTS_COUNT_WINDOW_MADE] += wanted && (by == BUSTER_NODE_FACTS_SHORT || by == BUSTER_NODE_FACTS_RULED);
            if (!node_facts_same(facts, 0, x, mode))
            {
                counts[NODE_FACTS_COUNT_WINDOW_KERNEL_DIFFERENCES] += 1;
                node_facts_reader_difference(state, x, front->tokens[x], BUSTER_NODE_FACTS_DIFFERENCE_PLACE);
            }
        }
    }
    node_facts_given(facts, 0, BUSTER_NODE_FACTS_POISON_DECOY);
    node_facts_window(facts, 0, mode, false);
    for (uint32_t x = 0; x < count; x += 1)
    {
        if ((facts->kinds[x] & 0x02u) != 0 && !node_facts_same(facts, 0, x, mode))
        {
            counts[NODE_FACTS_COUNT_WINDOW_KERNEL_DIFFERENCES] += 1;
            node_facts_reader_difference(state, x, front->tokens[x], BUSTER_NODE_FACTS_DIFFERENCE_DECOY);
        }
    }
    node_facts_given(facts, 0, BUSTER_NODE_FACTS_POISON_NONE);
    const uint64_t t0 = __rdtsc();
    node_facts_window(facts, 0, mode, false);
    counts[NODE_FACTS_COUNT_WINDOW_KERNEL_TICKS] += __rdtsc() - t0;
    if (state->tests != NULL)
    {
        // The tests of the chunk's expressions, from the records the kernel has just made in place.
        node_tests_window_chunk(state->tests, state, front);
    }
    facts->windows = windows;
    facts->window_count = window_count;
    node_facts_window_expected_exchange(state);
    node_facts_inputs_exchange(facts, &place);
    memset(state->place_taken, 0, count);
}

// Whether the front's inputs of the batch's record `w` are node_facts_append's: every field the kernel reads of a record,
// a leaf's line by its number or its words, and a call's arguments.
static bool node_facts_front_same(const NodeFacts* facts, const NodeFactsInputs* set, uint32_t w)
{
    const uint8_t kind = facts->kinds[w];
    bool same = set->ops[w] == facts->ops[w] && set->kinds[w] == kind && set->classes[w] == facts->classes[w] && set->left[w] == facts->left[w] &&
                set->right[w] == facts->right[w] && set->counts[w] == facts->counts[w] && set->positions[w] == facts->positions[w];
    if (same && (kind == BUSTER_NODE_FACTS_LITERAL_LINE || kind == BUSTER_NODE_FACTS_IDENTIFIER))
    {
        // A literal's line is each making's own. A declaration's is one, unless it was made again between two makings'
        // reads of it (its type as a use reads it changed): then the two lines' words are the same.
        same = set->inputs[w] == facts->inputs[w] ||
               memcmp(facts->lines + 4 * (uint64_t)set->inputs[w], facts->lines + 4 * (uint64_t)facts->inputs[w], 4 * sizeof(uint64_t)) == 0;
    }
    else if (same && kind != BUSTER_NODE_FACTS_LITERAL)
    {
        same = set->inputs[w] == facts->inputs[w];
        for (uint32_t i = 0; same && facts->ops[w] == CHECK_OP_CALL && i < facts->counts[w]; i += 1)
        {
            same = set->arguments[set->inputs[w] + i] == facts->arguments[facts->inputs[w] + i];
        }
    }
    return same;
}

// The batch run and taken away: its windows' kernel checked against T's records and timed, into the unit's counts.
static void node_facts_reader_flush(struct NodeFactsReader* state, const NodeFront* front)
{
    NodeFacts* facts = &state->facts;
    NodeFrontTypes* types = state->types;
    NodeMachine* machine = facts->machine;
    uint64_t* counts = types->facts_counts;
    const uint32_t windows = facts->record_count != 0 ? facts->window_count + 1 : 0;
    const uint32_t mode = BUSTER_NODE_FACTS_WHOLE;
    if (!facts->room)
    {
        // An expression the windows had no room for (node_facts_append): the batch isn't run, and the next starts anew.
        counts[NODE_FACTS_COUNT_FAILURES] += 1;
        facts->room = true;
        if (state->windowed)
        {
            memset(state->place_taken, 0, front->count);
        }
    }
    else if (windows != 0)
    {
        // The kernel's first run on the batch, as it finds the words: the members' and the pointer conversions' slots
        // are memos, filled by a rule the first time. The later runs find them filled, and the short paths take in each
        // what they take in the others.
        for (uint32_t window = 0; window < windows; window += 1)
        {
            node_facts_given(facts, window, BUSTER_NODE_FACTS_POISON_NONE);
            const uint64_t t0 = __rdtsc();
            node_facts_window(facts, window, mode, false);
            counts[NODE_FACTS_COUNT_FIRST_TICKS] += __rdtsc() - t0;
        }
        // The front's inputs made again, the batch's expressions in one loop, for their time: the lines are there by now,
        // as a declaration's is at every use but its first. Each record's are then compared with node_facts_append's, as
        // the first making's were.
        uint32_t again = 0;
        uint32_t lost = 0;
        const uint64_t f0 = __rdtsc();
        for (uint32_t e = 0; e < state->expression_count; e += 1)
        {
            if (state->given[e] != 0)
            {
                // Its calls' arguments where they were: the other expressions' places are node_facts_append's.
                state->front.argument_count = state->argument_bases[e];
                const uint32_t why = node_facts_front_expression(state, front, state->bases[e], state->counts[e], state->records[e], state->operands[e]);
                again += why == BUSTER_NODE_FACTS_WHY_GIVEN ? state->counts[e] : 0;
                lost += why != BUSTER_NODE_FACTS_WHY_GIVEN;
            }
        }
        counts[NODE_FACTS_COUNT_FRONT_AGAIN_TICKS] += __rdtsc() - f0;
        counts[NODE_FACTS_COUNT_FRONT_AGAIN_RECORDS] += again;
        counts[NODE_FACTS_COUNT_FAILURES] += lost;
        state->front.argument_count = facts->argument_count;
        for (uint32_t e = 0; e < state->expression_count; e += 1)
        {
            for (uint32_t r = 0; state->given[e] != 0 && r < state->counts[e]; r += 1)
            {
                const uint32_t w = state->bases[e] + r;
                if (!node_facts_front_same(facts, &state->front, w))
                {
                    counts[NODE_FACTS_COUNT_FRONT_DIFFERENCES] += 1;
                    node_facts_reader_difference(state, w, node_facts_reader_token(facts, w), BUSTER_NODE_FACTS_DIFFERENCE_FRONT);
                }
            }
        }
        // The checked run, over records no rule has written, from the front's inputs.
        node_facts_inputs_exchange(facts, &state->front);
        for (uint32_t window = 0; window < windows; window += 1)
        {
            const uint32_t first = facts->windows[window];
            const uint32_t count = facts->windows[window + 1] - first;
            node_facts_given(facts, window, BUSTER_NODE_FACTS_POISON_RECORDS);
            node_facts_window(facts, window, mode, false);
            if (!node_facts_lists_same(facts, window))
            {
                counts[NODE_FACTS_COUNT_LIST_DIFFERENCES] += 1;
                node_facts_reader_difference(state, first, node_facts_reader_token(facts, first), BUSTER_NODE_FACTS_DIFFERENCE_LISTS);
            }
            for (uint32_t i = 0; i < count; i += 1)
            {
                const uint32_t w = first + i;
                const uint8_t kind = facts->kinds[w];
                const CheckOp op = (CheckOp)facts->ops[w];
                const bool line = kind == BUSTER_NODE_FACTS_LITERAL_LINE;
                const bool literal = line || kind == BUSTER_NODE_FACTS_LITERAL;
                const bool wanted = kind == BUSTER_NODE_FACTS_IDENTIFIER || kind == BUSTER_NODE_FACTS_LOOKUP || kind == BUSTER_NODE_FACTS_OPERATOR;
                const bool taken = state->taken[w] != 0;
                counts[NODE_FACTS_COUNT_LITERALS] += literal;
                counts[NODE_FACTS_COUNT_TAKEN_RECORDS] += taken;
                counts[NODE_FACTS_COUNT_TAKEN_LITERALS] += taken && literal;
                if (line || wanted)
                {
                    const uint8_t by = facts->records[i].taken;
                    const bool made = by == BUSTER_NODE_FACTS_SHORT || by == BUSTER_NODE_FACTS_RULED;
                    counts[NODE_FACTS_COUNT_WANTED] += wanted;
                    counts[NODE_FACTS_COUNT_MADE] += wanted && made;
                    counts[NODE_FACTS_COUNT_SHORT] += wanted && by == BUSTER_NODE_FACTS_SHORT;
                    counts[NODE_FACTS_COUNT_LINES] += line;
                    counts[NODE_FACTS_COUNT_LINES_MADE] += line && made;
                    counts[NODE_FACTS_COUNT_TAKEN_WANTED] += taken && wanted;
                    counts[NODE_FACTS_COUNT_TAKEN_MADE] += taken && wanted && made;
                    types->facts_wanted_ops[op] += wanted;
                    types->facts_left_ops[op] += wanted && !made;
                    if (!node_facts_same(facts, first, i, mode))
                    {
                        counts[NODE_FACTS_COUNT_DIFFERENCES] += 1;
                        node_facts_reader_difference(state, w, node_facts_reader_token(facts, w), BUSTER_NODE_FACTS_DIFFERENCE_RECORD);
                    }
                }
            }
        }
        node_facts_inputs_exchange(facts, &state->front);
        // And over decoys, from node_facts_append's inputs: an operator made before its operand takes the decoy for it,
        // and differs.
        for (uint32_t window = 0; window < windows; window += 1)
        {
            const uint32_t first = facts->windows[window];
            const uint32_t count = facts->windows[window + 1] - first;
            node_facts_given(facts, window, BUSTER_NODE_FACTS_POISON_DECOY);
            node_facts_window(facts, window, mode, false);
            for (uint32_t i = 0; i < count; i += 1)
            {
                if ((facts->kinds[first + i] & 0x02u) != 0 && !node_facts_same(facts, first, i, mode))
                {
                    counts[NODE_FACTS_COUNT_DIFFERENCES] += 1;
                    counts[NODE_FACTS_COUNT_DECOY_DIFFERENCES] += 1;
                    node_facts_reader_difference(state, first + i, node_facts_reader_token(facts, first + i), BUSTER_NODE_FACTS_DIFFERENCE_DECOY);
                }
            }
        }
        // The timed turn: the kernel on the batch's windows, and T on the batch's expressions, each finding the batch as
        // the other left it; the kernel first in every other batch.
        const bool kernel_first = (state->batches & 1) == 0;
        for (uint32_t turn = 0; turn < 2; turn += 1)
        {
            if ((turn == 0) == kernel_first)
            {
                for (uint32_t window = 0; window < windows; window += 1)
                {
                    node_facts_given(facts, window, BUSTER_NODE_FACTS_POISON_NONE);
                    const uint64_t t0 = __rdtsc();
                    node_facts_window(facts, window, mode, false);
                    counts[NODE_FACTS_COUNT_TICKS] += __rdtsc() - t0;
                }
            }
            else if (state->again)
            {
                const uint64_t t0 = __rdtsc();
                for (uint32_t e = 0; e < state->expression_count; e += 1)
                {
                    (void)node_machine_type(machine, state->firsts[e], state->roots[e], state->counts[e]);
                }
                counts[NODE_FACTS_COUNT_AGAIN_TICKS] += __rdtsc() - t0;
                counts[NODE_FACTS_COUNT_AGAIN_RECORDS] += facts->record_count;
            }
        }
        counts[NODE_FACTS_COUNT_EXPRESSIONS] += state->expression_count;
        counts[NODE_FACTS_COUNT_RECORDS] += facts->record_count;
        counts[NODE_FACTS_COUNT_WINDOWS] += windows;
        if (state->windowed)
        {
            // The same kernel on the front's own columns, where the front wrote them.
            node_facts_window_run(state, front);
        }
    }
    // The words as they stand: the unit's totals so far.
    counts[NODE_FACTS_COUNT_ROWS] = facts->row_count;
    counts[NODE_FACTS_COUNT_ROW_TICKS] = facts->row_ticks;
    counts[NODE_FACTS_COUNT_DECLARATION_LINES] = facts->line_count - facts->literal_capacity;
    counts[NODE_FACTS_COUNT_LINE_TICKS] = facts->line_ticks;
    counts[NODE_FACTS_COUNT_LINE_VERSIONS] = facts->line_versions;
    counts[NODE_FACTS_COUNT_MEMBER_SLOTS] = facts->member_count;
    counts[NODE_FACTS_COUNT_PAIR_SLOTS] = facts->pair_count;
    facts->record_count = 0;
    facts->argument_count = 0;
    facts->window_count = 0;
    facts->window_body = UINT32_MAX;
    facts->windows[0] = 0;
    facts->literal_count = state->windowed ? BUSTER_NODE_FRONT_RECORDS : 0;
    state->front.argument_count = 0;
    state->expression_count = 0;
    state->batches += 1;
}

static void node_facts_reader_expression(struct NodeFactsReader* state, const NodeFront* front, uint32_t first, uint32_t root, uint32_t count, uint32_t record,
                                         uint32_t operand, bool taken)
{
    NodeFacts* facts = &state->facts;
    NodeMachine* machine = facts->machine;
    NodeFrontTypes* types = state->types;
    if ((uint64_t)facts->record_count + count > facts->record_capacity || (uint64_t)facts->argument_count + machine->argument_total > facts->record_capacity ||
        state->expression_count == BUSTER_NODE_FACTS_READER_EXPRESSIONS)
    {
        // The batch is full: it is run where it stands, and T types this expression again, whose records the batch's own
        // runs of T wrote over.
        node_facts_reader_flush(state, front);
        (void)node_machine_type(machine, first, root, count);
    }
    const uint32_t base = facts->record_count;
    const uint32_t e = state->expression_count;
    const uint32_t argument_base = facts->argument_count;
    // The rows of the types made since: the walk's, and T's own derivations' for this expression.
    node_facts_settle(facts);
    // The front's inputs first: a declaration's line is then made by whichever of the makings meets its use first, here.
    const uint64_t t0 = __rdtsc();
    const uint32_t why = node_facts_front_expression(state, front, base, count, record, operand);
    const bool given = why == BUSTER_NODE_FACTS_WHY_GIVEN;
    const uint64_t t1 = __rdtsc();
    node_facts_append(facts, 0);
    types->facts_counts[NODE_FACTS_COUNT_APPEND_TICKS] += __rdtsc() - t1;
    const bool appended = facts->room && facts->record_count == base + count;
    if (appended && given)
    {
        types->facts_counts[NODE_FACTS_COUNT_FRONT_TICKS] += t1 - t0;
        types->facts_counts[NODE_FACTS_COUNT_FRONT_EXPRESSIONS] += 1;
        types->facts_counts[NODE_FACTS_COUNT_FRONT_RECORDS] += count;
        for (uint32_t r = 0; r < count; r += 1)
        {
            if (!node_facts_front_same(facts, &state->front, base + r))
            {
                types->facts_counts[NODE_FACTS_COUNT_FRONT_DIFFERENCES] += 1;
                node_facts_reader_difference(state, base + r, node_facts_reader_token(facts, base + r), BUSTER_NODE_FACTS_DIFFERENCE_FRONT);
            }
        }
        // As many arguments as node_facts_append's: the two's places are then the same through the batch.
        types->facts_counts[NODE_FACTS_COUNT_FRONT_DIFFERENCES] += state->front.argument_count != facts->argument_count;
    }
    const bool kept = given && state->front.argument_count == facts->argument_count;
    bool placed = false;
    if (appended && !kept)
    {
        // The front's records don't give the expression: its inputs are node_facts_append's.
        node_facts_inputs_take(&state->front, facts, base, count, argument_base, facts->argument_count);
        state->front.argument_count = facts->argument_count;
        types->facts_counts[NODE_FACTS_COUNT_FRONT_SKIPPED] += !given;
        if (!given)
        {
            types->facts_skipped_whys[why] += 1;
            if (types->facts_skipped_listed < BUSTER_NODE_FRONT_LISTED)
            {
                types->facts_skipped_tokens[types->facts_skipped_listed] = front->tokens[record];
                types->facts_skipped_reasons[types->facts_skipped_listed] = (uint8_t)why;
                types->facts_skipped_listed += 1;
            }
        }
    }
    if (appended && state->windowed)
    {
        // The front's own window: the expression's records are there in a row, its operand's subtree's and its root's
        // (but an initialization's), or it is not the window's.
        const bool initialization = front->ops[record] == CHECK_OP_INITIALIZE;
        const uint32_t size = operand < record ? front->sizes[operand] : 0;
        const bool whole = size != 0 && count == (initialization ? size : size + 1) && size <= operand + 1 &&
                           front->window_classes[operand] != BUSTER_NODE_FACTS_CLASS_CUT;
        placed = whole;
        if (whole)
        {
            node_facts_window_expression(state, front, base, count, record, operand + 1 - size, size, kept);
            types->facts_counts[NODE_FACTS_COUNT_WINDOW_EXPRESSIONS] += 1;
            types->facts_counts[NODE_FACTS_COUNT_WINDOW_RECORDS] += count;
        }
        else
        {
            types->facts_counts[NODE_FACTS_COUNT_WINDOW_SKIPPED] += 1;
            types->facts_window_whys[BUSTER_NODE_FACTS_WHY_CUT] += 1;
            if (types->facts_window_listed < BUSTER_NODE_FRONT_LISTED)
            {
                types->facts_window_tokens[types->facts_window_listed] = front->tokens[record];
                types->facts_window_reasons[types->facts_window_listed] = BUSTER_NODE_FACTS_WHY_CUT;
                types->facts_window_listed += 1;
            }
        }
    }
    if (appended)
    {
        memset(state->taken + base, taken ? 1 : 0, count);
        state->firsts[e] = first;
        state->roots[e] = root;
        state->counts[e] = count;
        state->bases[e] = base;
        state->records[e] = record;
        state->operands[e] = operand;
        state->argument_bases[e] = argument_base;
        state->given[e] = kept;
        state->placed[e] = (uint8_t)(placed ? BUSTER_NODE_FACTS_PLACED | ((machine->action_warnings & 0x01ffffffu) != 0 ? BUSTER_NODE_FACTS_PLACED_WARNED : 0u) : 0u);
        state->expression_count = e + 1;
    }
}

static void node_facts_reader_chunk(struct NodeFactsReader* state, const NodeFront* front)
{
    node_facts_reader_flush(state, front);
}

static void node_facts_reader_tests(struct NodeFactsReader* state, struct NodeCensusReader* tests)
{
    // The tests keep a byte a declaration's line: without room for them they don't run on the window.
    state->tests = node_tests_window_lines(tests, state->reader->scratch, state->facts.line_capacity) ? tests : NULL;
}

#endif
