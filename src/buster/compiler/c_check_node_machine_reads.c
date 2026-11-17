// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #2850's step 2's second piece (docs/one-core-path.md): a census of what reads T's record, in the node machine's
// shadow (measure-node-machine --shadow --facts-reads, c_check_node_census.c's). It is a count: no compile takes it.
//
// The first piece made T's whole record of every operator a loop a class and found it too dear; the design gives the
// operators' facts to the lanes, bytes a register holds 64 of. This counts what else of a record has to exist: which of
// its wide fields (its type, its flags' bits, its object, its payload, a member's offset, its operands' conversions) a
// later reader reads, and where each comes from.
//
// A read is found by changing the field and running its reader again, not by reading the reader's code:
//
// - A later rule of T: the record's consumer's rule, the fact words' kernel's (node_facts_slow, T's exact twin), on the
//   window's records with one field of one operand changed (node_reads_consumers). A call's is the machine's own
//   (node_machine_call_rule, on the expression's columns: node_reads_call): the kernel's leaves each argument's
//   conversion to its parameter's type to T, and that conversion reads the argument's type.
// - C and X: the machine's own sweeps (node_machine_contexts, node_machine_expand) on the expression's columns with one
//   field of one record changed (node_reads_sweeps). X runs on C's unchanged columns, so that what it reads itself is
//   apart from what reaches it through C.
//
// What changed says how the field was read. A byte-level answer changed (the consumer's standing, flags, type's word or
// conversions' flags; C's context, template or target's word; the tape's kinds, classes or length; a refusal): the
// field decides. Only a wide value changed (a type's identity, an object, a payload, a tape's value): it is carried.
// A carried value is a read only where it arrives: on the tape, or at a reader that decides on it. A consumer's rule
// that copies its operand's object into its own record reads nothing if nobody reads that record's object, so a
// rule's carries are kept as edges, from the operand's field to the consumer's fields that changed, and followed
// (node_reads_classify); and where C alone carries a value (a target's identity), X runs on C's changed columns to see
// whether it arrives.
//
// What a field is changed to says what of it is read. A type becomes another of the unit with the same word and the
// same row's facts (its identity alone differs), and one with another word. Of the types with its word, the other is
// one whose element, size and stride differ from its own where there is one, so that a reader of any of them shows:
// `int*` becomes `long*` before it becomes `volatile int*`. An object becomes another of its kind (a local, a global),
// and one of the other kind. A payload takes values its type holds. A flag's bit is flipped, one at a time.
//
// And where a wide field comes from (node_reads_sources): a builtin type, which a code names; an operand's same field;
// a type its operand's type's row gives; the node's own input (a cast's type name); or none of them, new at the
// record. A payload is its operand's only where the rule was seen to copy it: one that equals an operand's and is
// computed (`1 << 0`) is new, and the changes show which (node_reads_consumers). A record whose read fields are all
// nameable or copies needs no wide value made at it; one with a new field that somebody reads needs a step no lane
// makes (node_reads_classify).
//
// What the counts can't see: a type's identity is changed to one other type, so two types that give a reader the same
// answer hide its read. C asks of every callee's type whether its parameters are all plain scalars
// (node_machine_classify_call), and the census shows that only for the calls whose callee's other type differs in it.
// And the operands of a consumer the kernel leaves to T, or of a call T types by its action, are not measured.
#ifndef BUSTER_C_CHECK_NODE_MACHINE_READS_INCLUDED
#define BUSTER_C_CHECK_NODE_MACHINE_READS_INCLUDED

// The readers.
#define BUSTER_NODE_READS_T 0u
#define BUSTER_NODE_READS_C 1u
#define BUSTER_NODE_READS_X 2u
#define BUSTER_NODE_READS_READERS 3u

// A reader's bits of a record's mask (sixteen a reader): which change of which field changed what it made.
// Another word's type changed a byte-level answer.
#define BUSTER_NODE_READS_TYPE_WORD 0x0001u
// Another type of the same word and row's facts changed a byte-level answer; or either changed a wide value alone.
#define BUSTER_NODE_READS_TYPE_DECIDES 0x0002u
#define BUSTER_NODE_READS_TYPE_CARRIED 0x0004u
// An object of the other kind changed a byte-level answer; another of its kind did; or either changed a wide value
// alone.
#define BUSTER_NODE_READS_OBJECT_KIND 0x0008u
#define BUSTER_NODE_READS_OBJECT_DECIDES 0x0010u
#define BUSTER_NODE_READS_OBJECT_CARRIED 0x0020u
// Another payload, and another member's offset.
#define BUSTER_NODE_READS_PAYLOAD_DECIDES 0x0040u
#define BUSTER_NODE_READS_PAYLOAD_CARRIED 0x0080u
#define BUSTER_NODE_READS_OFFSET_DECIDES 0x0100u
#define BUSTER_NODE_READS_OFFSET_CARRIED 0x0200u
// One of its operands' conversions' types, as its own type above; C and X alone read them.
#define BUSTER_NODE_READS_CONVERTED_WORD 0x0400u
#define BUSTER_NODE_READS_CONVERTED_DECIDES 0x0800u
#define BUSTER_NODE_READS_CONVERTED_CARRIED 0x1000u
// A flag's bit, and a conversion's flag's bit (the counts have which). A flag a consumer's rule only hands on, the same
// bit of its own flags and nothing else, is no read of it: the consumer that drops it or acts on it reads it.
#define BUSTER_NODE_READS_FLAGS 0x2000u
#define BUSTER_NODE_READS_CONVERSION_FLAGS 0x4000u
#define BUSTER_NODE_READS_BITS 15u

// A wide field's reads: of a value a byte doesn't hold.
#define BUSTER_NODE_READS_WIDE_TYPE (BUSTER_NODE_READS_TYPE_DECIDES | BUSTER_NODE_READS_TYPE_CARRIED)
#define BUSTER_NODE_READS_WIDE_OBJECT (BUSTER_NODE_READS_OBJECT_DECIDES | BUSTER_NODE_READS_OBJECT_CARRIED)
#define BUSTER_NODE_READS_WIDE_PAYLOAD (BUSTER_NODE_READS_PAYLOAD_DECIDES | BUSTER_NODE_READS_PAYLOAD_CARRIED)
#define BUSTER_NODE_READS_WIDE_OFFSET (BUSTER_NODE_READS_OFFSET_DECIDES | BUSTER_NODE_READS_OFFSET_CARRIED)
#define BUSTER_NODE_READS_WIDE_CONVERTED (BUSTER_NODE_READS_CONVERTED_DECIDES | BUSTER_NODE_READS_CONVERTED_CARRIED)
// The reads by an identity that decide: a type's, an object's, a conversion's type's.
#define BUSTER_NODE_READS_IDENTITY_DECIDES (BUSTER_NODE_READS_TYPE_DECIDES | BUSTER_NODE_READS_OBJECT_DECIDES | BUSTER_NODE_READS_CONVERTED_DECIDES)

// The fields with a source (node_reads_sources), three bits each of a record's sources.
#define BUSTER_NODE_READS_FIELD_TYPE 0u
#define BUSTER_NODE_READS_FIELD_OBJECT 1u
#define BUSTER_NODE_READS_FIELD_PAYLOAD 2u
#define BUSTER_NODE_READS_FIELD_CONVERTED 3u
#define BUSTER_NODE_READS_FIELD_OFFSET 5u
#define BUSTER_NODE_READS_FIELDS 6u

// Where a field's value comes from. None: no object, no payload, no conversion. A leaf's: its line's. A builtin type: a
// code names it. An operand's same field (a type's with other qualifiers too). A row's: a type the row of an operand's
// type or of the node's type name gives (its value type, its promotion, its element, an array's or a function's
// pointer, a function's return, the pointer to it), or that type's size or alignment. The node's own input: a cast's,
// a conversion's or a sizeof's type name. Or new at the record: none of them.
#define BUSTER_NODE_READS_SOURCE_NONE 0u
#define BUSTER_NODE_READS_SOURCE_LEAF 1u
#define BUSTER_NODE_READS_SOURCE_BUILTIN 2u
#define BUSTER_NODE_READS_SOURCE_OPERAND 3u
#define BUSTER_NODE_READS_SOURCE_ROW 4u
#define BUSTER_NODE_READS_SOURCE_INPUT 5u
#define BUSTER_NODE_READS_SOURCE_NEW 6u
#define BUSTER_NODE_READS_SOURCES 7u

// What a record needs made at it, by what is read of it and where that comes from: nothing wide is read; what is read a
// code names (a builtin type); what is read is a copy a selector could name (an operand's field, a row's, the node's
// input); or something read is new at the record.
#define BUSTER_NODE_READS_CLASS_NONE 0u
#define BUSTER_NODE_READS_CLASS_NAMED 1u
#define BUSTER_NODE_READS_CLASS_COPIED 2u
#define BUSTER_NODE_READS_CLASS_NEW 3u
#define BUSTER_NODE_READS_CLASSES 4u

// A record's place in the counts: its operator, with a literal's leaf and a discard (a cast to void) past them; an
// identifier's leaf is CHECK_OP_VALUE and another cast CHECK_OP_CAST. And its kind: a leaf, a lookup (a member, a
// dereference, a subscript, a call, a discard) or another operator, which are the ones the lanes would make.
#define BUSTER_NODE_READS_LITERAL CHECK_OP_COUNT
#define BUSTER_NODE_READS_DISCARD (CHECK_OP_COUNT + 1u)
#define BUSTER_NODE_READS_OPS (CHECK_OP_COUNT + 2u)
#define BUSTER_NODE_READS_KIND_LEAF 0u
#define BUSTER_NODE_READS_KIND_LOOKUP 1u
#define BUSTER_NODE_READS_KIND_OPERATOR 2u
#define BUSTER_NODE_READS_KINDS 3u

// The payloads a record's is changed to (node_reads_changes): its own with its low bit flipped, then five values, of
// which those its type holds (a rule may take an operand's payload as a value of the operand's type, and one that
// isn't says nothing of what the rule does with those that are).
#define BUSTER_NODE_READS_PAYLOADS 6u
// The flags' bits a value has (BUSTER_CHECK_ICE to BUSTER_CHECK_REGISTER_NAME) and a conversion's (BUSTER_CHECK_CONVERT_*).
#define BUSTER_NODE_READS_FLAG_BITS 25u
#define BUSTER_NODE_READS_CONVERSION_BITS 12u

// The longest expression C and X are run again on, in records: each change of each record runs both over all of it.
#define BUSTER_NODE_READS_MOST 256u
// The most types of a type's ring that are looked at for its other self.
#define BUSTER_NODE_READS_RING_MOST 64u
#define BUSTER_NODE_READS_ENTRIES                                                                                                \
    (BUSTER_NODE_READS_MOST * (BUSTER_NODE_MACHINE_ROW_MOST + BUSTER_NODE_MACHINE_HOOK_MOST) + BUSTER_NODE_READS_MOST / 2u * BUSTER_NODE_MACHINE_ROW_MOST + \
     BUSTER_NODE_MACHINE_RECORD_MOST)

// Whether a reader was run again for a record: its consumer's rule (it has none, or one the kernel made), and C and X.
#define BUSTER_NODE_READS_RAN_T 0x01u
#define BUSTER_NODE_READS_RAN_SWEEPS 0x02u

// What a record's payload did when its rule ran again with an operand's payload changed, over the changes that left
// the rule's byte-level answer as it was (a change that decides is the operand's read, and says nothing of the copy).
// Unseen: its rule didn't run (a leaf, a call, a consumer the kernel left). It followed one operand's, as each value it
// was given, with no other operand's reaching it: a copy. It followed one operand's for some values and not for others:
// a conversion to a type that doesn't hold them all, a copy where the value fits. It changed otherwise: computed (a
// fold). No change of some operand's payload left the answer as it was: that value chose the rule's path (a null
// pointer constant's zero), and the payload is the path's. Or it stayed whatever the operands' payloads were.
#define BUSTER_NODE_READS_PAYLOAD_UNSEEN 0u
#define BUSTER_NODE_READS_PAYLOAD_FOLLOWS 1u
#define BUSTER_NODE_READS_PAYLOAD_FITS 2u
#define BUSTER_NODE_READS_PAYLOAD_COMPUTED 3u
#define BUSTER_NODE_READS_PAYLOAD_PATH 4u
#define BUSTER_NODE_READS_PAYLOAD_STAYS 5u

// The census's counts, over every file.
typedef struct NodeReadsCounts NodeReadsCounts;
struct NodeReadsCounts
{
    // The windows' records by operator, and those every reader was run again for.
    uint64_t records[BUSTER_NODE_READS_OPS];
    uint64_t measured[BUSTER_NODE_READS_OPS];
    // Of the measured: the records with each bit of each reader's, with each flag's bit read by each reader, and with
    // each conversion's flag's bit read.
    uint64_t bits[BUSTER_NODE_READS_READERS][BUSTER_NODE_READS_OPS][16];
    uint64_t flag_bits[BUSTER_NODE_READS_READERS][BUSTER_NODE_READS_OPS][BUSTER_NODE_READS_FLAG_BITS];
    uint64_t conversion_bits[BUSTER_NODE_READS_READERS][BUSTER_NODE_READS_OPS][BUSTER_NODE_READS_CONVERSION_BITS];
    // The records whose payload's change `k` changed a byte-level answer of C's or X's.
    uint64_t payload_values[BUSTER_NODE_READS_OPS][BUSTER_NODE_READS_PAYLOADS];
    // The measured records by class, and for each field somebody reads the wide value of, by its source.
    // A field that is read where the record has none (no object, no payload, no conversion's type) is its source's
    // BUSTER_NODE_READS_SOURCE_NONE: the reader depends on its being none.
    uint64_t classes[BUSTER_NODE_READS_OPS][BUSTER_NODE_READS_CLASSES];
    uint64_t sources[BUSTER_NODE_READS_FIELDS][BUSTER_NODE_READS_OPS][BUSTER_NODE_READS_SOURCES];
    // By kind: the records, the measured ones, those by class, and those C or X decide on an identity of (a type's, an
    // object's, a conversion's type's).
    uint64_t kind_records[BUSTER_NODE_READS_KINDS];
    uint64_t kind_measured[BUSTER_NODE_READS_KINDS];
    uint64_t kind_classes[BUSTER_NODE_READS_KINDS][BUSTER_NODE_READS_CLASSES];
    uint64_t kind_identities[BUSTER_NODE_READS_KINDS];
    // By kind too: the measured records each of whose wide fields somebody reads (its type's identity, its object,
    // its payload, a member's offset, a conversion's type's identity: BUSTER_NODE_READS_NEED_*'s order), those each
    // reader reads a wide field of, and those whose read field is new at the record, by field (the conversions' as one).
    uint64_t kind_fields[BUSTER_NODE_READS_KINDS][5];
    uint64_t kind_readers[BUSTER_NODE_READS_KINDS][BUSTER_NODE_READS_READERS];
    uint64_t kind_new[BUSTER_NODE_READS_KINDS][5];
    // And of those with a new payload, the ones whose payload is an operand's and whose rule didn't run again (it is
    // T's own): new for all the census saw, a copy if the rule copies. And the ones whose payload is an operand's that
    // the rule copies where it fits: a narrowing conversion's, which a test of the value and a copy make.
    uint64_t kind_unseen[BUSTER_NODE_READS_KINDS];
    uint64_t kind_fits[BUSTER_NODE_READS_KINDS];
    // And the measured records whose type no code names and no other type of the unit has the word and the row's facts
    // of: its identity's reads can't be found by changing it.
    uint64_t kind_lone[BUSTER_NODE_READS_KINDS];
    // The expressions and their records; those past BUSTER_NODE_READS_MOST records, which C and X weren't run again
    // on; the consumers whose rule is T's own (the kernel left them), whose operands' reads aren't known; the records
    // whose place in the windows isn't their place in the machine's expressions (none, or the census is wrong).
    uint64_t expressions;
    uint64_t long_expressions;
    uint64_t long_records;
    uint64_t left_consumers;
    uint64_t misplaced;
    // The runs: of a consumer's rule and of the sweeps with a field changed; and with none changed, before a consumer's
    // or an expression's changes and after them all, which must make what they made (`same_differences`: none, or the
    // census is wrong: a reader that isn't a function of what the census changes, or a change not taken back).
    uint64_t rule_runs;
    uint64_t sweep_runs;
    uint64_t same_runs;
    uint64_t same_differences;
};

// One change of one field of a record: the field, which change of it (a flag's bit, a payload's place), the new value,
// and the reader's bit where what was made changed in a byte-level answer and where in a wide value alone.
typedef struct NodeReadsChange NodeReadsChange;
struct NodeReadsChange
{
    uint64_t value;
    uint16_t decides;
    uint16_t carried;
    uint8_t field;
    uint8_t detail;
};

#define BUSTER_NODE_READS_CHANGE_TYPE 0u
#define BUSTER_NODE_READS_CHANGE_FLAGS 1u
#define BUSTER_NODE_READS_CHANGE_OBJECT 2u
#define BUSTER_NODE_READS_CHANGE_PAYLOAD 3u
#define BUSTER_NODE_READS_CHANGE_OFFSET 4u
// The most changes of a record's own fields: two types, the flags' bits, two objects, the payloads, two offsets.
#define BUSTER_NODE_READS_CHANGES (2u + BUSTER_NODE_READS_FLAG_BITS + 2u + BUSTER_NODE_READS_PAYLOADS + 2u)

// C's columns of an expression and X's tape, as the unchanged run made them.
typedef struct NodeReadsKept NodeReadsKept;
struct NodeReadsKept
{
    uint8_t* states;
    uint8_t* contexts;
    uint8_t* templates;
    TypeId* targets;
    uint32_t* hooks;
    uint32_t* label_bases;
    uint32_t* call_depths;
    uint32_t* consumers;
    NodeMachineUpdate* updates;
    uint8_t* kinds;
    uint8_t* classes;
    uint64_t* tape;
    uint32_t* label_entries;
    uint32_t* calls_written;
    uint32_t* call_records;
    uint32_t label_count;
    uint32_t update_count;
    uint32_t entry_count;
    uint32_t call_count;
    uint32_t temporary_count;
};

// A record's wide fields, as bits: its type's identity, its object, its payload, a member's offset, and its operands'
// conversions' types' identities.
#define BUSTER_NODE_READS_NEED_TYPE 0x01u
#define BUSTER_NODE_READS_NEED_OBJECT 0x02u
#define BUSTER_NODE_READS_NEED_PAYLOAD 0x04u
#define BUSTER_NODE_READS_NEED_OFFSET 0x08u
#define BUSTER_NODE_READS_NEED_CONVERTED 0x10u

// The census's state for a file.
typedef struct NodeReads NodeReads;
struct NodeReads
{
    NodeReadsCounts* counts;
    // By the unit's record (its place in the windows): each reader's bits (sixteen each, then C's and X's payloads'
    // values that decided, from bit 48), each reader's flags' bits and conversions' flags' bits read, its fields'
    // sources, and which readers ran.
    uint64_t* masks;
    uint32_t* flags[BUSTER_NODE_READS_READERS];
    uint16_t* conversions[BUSTER_NODE_READS_READERS];
    uint32_t* sources;
    uint8_t* ran;
    // What its consumer's rule carries of it: for each of its fields (BUSTER_NODE_READS_CHANGE_TYPE, _OBJECT, _PAYLOAD and
    // _OFFSET, five bits each from bit 0) the consumer's wide fields that changed with it (BUSTER_NODE_READS_NEED_*);
    // its consumer's distance ahead (0 for none); and, once classified, which of its own wide fields somebody reads.
    uint32_t* edges;
    uint16_t* ahead;
    uint8_t* needs;
    // And what its payload did when its operands' changed (BUSTER_NODE_READS_PAYLOAD_*).
    uint8_t* payloads;
    // By type index, the next type of the ring of those with its word and its row's facts (itself for none), and its
    // other self, the one of the ring it is changed to; by object, another of its kind (itself for none); and the
    // unit's first local and first global (0 for none).
    uint32_t* same_types;
    uint32_t* other_types;
    uint32_t* same_objects;
    uint32_t first_local;
    uint32_t first_global;
    NodeReadsKept kept;
};

// A record's place in the counts, and its kind.
static uint32_t node_reads_op(const NodeFacts* facts, uint64_t at)
{
    const uint8_t kind = facts->kinds[at];
    const CheckOp op = (CheckOp)facts->ops[at];
    return kind == BUSTER_NODE_FACTS_LITERAL || kind == BUSTER_NODE_FACTS_LITERAL_LINE ? BUSTER_NODE_READS_LITERAL :
           op == CHECK_OP_CAST && kind != BUSTER_NODE_FACTS_OPERATOR ? BUSTER_NODE_READS_DISCARD : (uint32_t)op;
}

static uint32_t node_reads_kind(const NodeFacts* facts, uint64_t at)
{
    const uint8_t kind = facts->kinds[at];
    const CheckOp op = (CheckOp)facts->ops[at];
    return op == CHECK_OP_VALUE ? BUSTER_NODE_READS_KIND_LEAF :
           kind == BUSTER_NODE_FACTS_OPERATOR || node_facts_lookup(op) == BUSTER_NODE_FACTS_LOOKUPS ? BUSTER_NODE_READS_KIND_OPERATOR : BUSTER_NODE_READS_KIND_LOOKUP;
}

// The census's arrays for `facts`'s unit, from `arena`: whether there was room. The types' and the objects' other
// selves are found here: a type's among those with its word and its row's facts, linked in a ring by a table of the
// keys, the one of the ring that differs most from it; an object's among the unit's locals or its globals.
static bool node_reads_begin(NodeReads* reads, const NodeFacts* facts, NodeReadsCounts* counts, Arena* arena)
{
    const NodeMachine* machine = facts->machine;
    const CheckProgram* program = machine->program;
    const uint32_t types = facts->row_count;
    const uint32_t objects = program->object_count;
    const uint64_t records = (uint64_t)facts->record_capacity + 1;
    uint32_t bits = 4;
    while ((UINT64_C(1) << bits) < 2 * (uint64_t)types + 16)
    {
        bits += 1;
    }
    *reads = (NodeReads){
        .counts = counts,
    };
    reads->masks = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, records);
    bool detailed = true;
    for (uint32_t reader = 0; reader < BUSTER_NODE_READS_READERS; reader += 1)
    {
        reads->flags[reader] = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, records);
        reads->conversions[reader] = BUSTER_ARENA_PUSH_ARRAY(arena, uint16_t, records);
        detailed = detailed && reads->flags[reader] != NULL && reads->conversions[reader] != NULL;
    }
    reads->sources = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, records);
    reads->ran = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, records);
    reads->edges = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, records);
    reads->ahead = BUSTER_ARENA_PUSH_ARRAY(arena, uint16_t, records);
    reads->needs = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, records);
    reads->payloads = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, records);
    reads->same_types = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)types + 1);
    reads->other_types = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)types + 1);
    reads->same_objects = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)objects + 1);
    uint64_t* keys = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, UINT64_C(1) << bits);
    uint32_t* lasts = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, UINT64_C(1) << bits);
    NodeReadsKept* kept = &reads->kept;
    kept->states = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, BUSTER_NODE_READS_MOST);
    kept->contexts = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, BUSTER_NODE_READS_MOST);
    kept->templates = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, BUSTER_NODE_READS_MOST);
    kept->targets = BUSTER_ARENA_PUSH_ARRAY(arena, TypeId, BUSTER_NODE_READS_MOST);
    kept->hooks = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, BUSTER_NODE_READS_MOST);
    kept->label_bases = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, BUSTER_NODE_READS_MOST);
    kept->call_depths = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, BUSTER_NODE_READS_MOST);
    kept->consumers = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, BUSTER_NODE_READS_MOST);
    kept->updates = BUSTER_ARENA_PUSH_ARRAY(arena, NodeMachineUpdate, BUSTER_NODE_READS_MOST);
    kept->kinds = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, BUSTER_NODE_READS_ENTRIES);
    kept->classes = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, BUSTER_NODE_READS_ENTRIES);
    kept->tape = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, BUSTER_NODE_READS_ENTRIES);
    kept->label_entries = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, 2 * BUSTER_NODE_READS_MOST);
    kept->calls_written = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, BUSTER_NODE_READS_MOST);
    kept->call_records = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, BUSTER_NODE_READS_MOST);
    const bool room = detailed && reads->masks != NULL && reads->sources != NULL && reads->ran != NULL && reads->edges != NULL && reads->ahead != NULL &&
                      reads->needs != NULL && reads->payloads != NULL && reads->same_types != NULL && reads->other_types != NULL && reads->same_objects != NULL && keys != NULL &&
                      lasts != NULL && kept->states != NULL && kept->contexts != NULL && kept->templates != NULL && kept->targets != NULL && kept->hooks != NULL &&
                      kept->label_bases != NULL && kept->call_depths != NULL && kept->consumers != NULL && kept->updates != NULL && kept->kinds != NULL &&
                      kept->classes != NULL && kept->tape != NULL && kept->label_entries != NULL && kept->calls_written != NULL && kept->call_records != NULL;
    if (room)
    {
        memset(reads->masks, 0, records * sizeof(uint64_t));
        for (uint32_t reader = 0; reader < BUSTER_NODE_READS_READERS; reader += 1)
        {
            memset(reads->flags[reader], 0, records * sizeof(uint32_t));
            memset(reads->conversions[reader], 0, records * sizeof(uint16_t));
        }
        memset(reads->sources, 0, records * sizeof(uint32_t));
        memset(reads->ran, 0, records * sizeof(uint8_t));
        memset(reads->edges, 0, records * sizeof(uint32_t));
        memset(reads->ahead, 0, records * sizeof(uint16_t));
        memset(reads->needs, 0, records * sizeof(uint8_t));
        memset(reads->payloads, 0, records * sizeof(uint8_t));
        memset(lasts, 0xff, (UINT64_C(1) << bits) * sizeof(uint32_t));
        const uint64_t mask = (UINT64_C(1) << bits) - 1;
        for (uint32_t index = 0; index < types; index += 1)
        {
            const uint32_t word = machine->type_words[index];
            reads->same_types[index] = index;
            if ((word & BUSTER_NODE_MACHINE_WORD_VALID) != 0)
            {
                // A ring of the types with this key: each new one goes after the last, and points at the first.
                const uint64_t key = (uint64_t)word << 32 | facts->rows[index].flags;
                uint64_t at = (key * UINT64_C(0x9e3779b97f4a7c15)) >> (64 - bits);
                while (lasts[at] != UINT32_MAX && keys[at] != key)
                {
                    at = (at + 1) & mask;
                }
                if (lasts[at] != UINT32_MAX)
                {
                    reads->same_types[index] = reads->same_types[lasts[at]];
                    reads->same_types[lasts[at]] = index;
                }
                keys[at] = key;
                lasts[at] = index;
            }
        }
        // Each type's other self: of its ring, the first whose element and whose size or stride differ from its own,
        // else the first with one of them different, else the next.
        for (uint32_t index = 0; index < types; index += 1)
        {
            uint32_t chosen = reads->same_types[index];
            uint32_t rank = 0;
            uint32_t at = chosen;
            for (uint32_t steps = 0; steps < BUSTER_NODE_READS_RING_MOST && at != index && rank < 2; steps += 1)
            {
                const bool element = type_index(facts->rows[at].element) != type_index(facts->rows[index].element);
                const bool sized = facts->sides[at].size != facts->sides[index].size || facts->rows[at].stride != facts->rows[index].stride;
                const uint32_t found = element && sized ? 2u : element || sized ? 1u : 0u;
                chosen = found > rank ? at : chosen;
                rank = found > rank ? found : rank;
                at = reads->same_types[at];
            }
            reads->other_types[index] = chosen;
        }
        uint32_t last_local = 0;
        uint32_t last_global = 0;
        reads->same_objects[0] = 0;
        for (uint32_t object = 1; object < objects; object += 1)
        {
            const bool local = program->object_symbols[object] == 0;
            const uint32_t last = local ? last_local : last_global;
            const uint32_t first = local ? reads->first_local : reads->first_global;
            reads->same_objects[object] = first != 0 ? first : object;
            if (last != 0)
            {
                reads->same_objects[last] = object;
            }
            reads->first_local = local && first == 0 ? object : reads->first_local;
            reads->first_global = !local && first == 0 ? object : reads->first_global;
            last_local = local ? object : last_local;
            last_global = local ? last_global : object;
        }
    }
    return room;
}

// Type `type`'s other self (its qualifiers kept): another type with its word and its row's facts, or itself.
static TypeId node_reads_same_type(const NodeReads* reads, const NodeFacts* facts, TypeId type)
{
    const uint32_t index = type_index(type);
    return index < facts->row_count ? ((TypeId)reads->other_types[index] << BUSTER_TYPE_INDEX_SHIFT) | (type & BUSTER_TYPE_QUALIFIERS) : type;
}

// A type whose word isn't `type`'s: int, or long for a type with int's word.
static TypeId node_reads_other_type(const NodeFacts* facts, TypeId type)
{
    const NodeMachine* machine = facts->machine;
    return node_machine_word(machine, type) != node_machine_word(machine, BUSTER_TYPE_INT) ? BUSTER_TYPE_INT : BUSTER_TYPE_LONG;
}

// The changes of a record's own fields into `changes` (BUSTER_NODE_READS_CHANGES of them at most), from its type, its
// flags, its object, its payload, its standing and its member's offset: how many. A change that would leave a field as
// it is isn't made; a wide value's payload is its place among the wide values, and isn't changed; and a payload is
// changed to values its type holds.
static uint32_t node_reads_changes(const NodeReads* reads, const NodeFacts* facts, TypeId type, uint32_t flags, uint32_t object, uint64_t payload, uint8_t standing,
                                   uint64_t member_offset, NodeReadsChange* changes)
{
    const CheckProgram* program = facts->machine->program;
    uint32_t count = 0;
    const TypeId same = node_reads_same_type(reads, facts, type);
    if (type != 0 && same != type)
    {
        changes[count++] = (NodeReadsChange){ .value = same, .decides = BUSTER_NODE_READS_TYPE_DECIDES, .carried = BUSTER_NODE_READS_TYPE_CARRIED, .field = BUSTER_NODE_READS_CHANGE_TYPE };
    }
    if (type != 0)
    {
        changes[count++] = (NodeReadsChange){ .value = node_reads_other_type(facts, type), .decides = BUSTER_NODE_READS_TYPE_WORD, .carried = BUSTER_NODE_READS_TYPE_CARRIED,
                                              .field = BUSTER_NODE_READS_CHANGE_TYPE };
    }
    for (uint32_t bit = 0; bit < BUSTER_NODE_READS_FLAG_BITS; bit += 1)
    {
        changes[count++] = (NodeReadsChange){ .value = flags ^ (1u << bit), .decides = BUSTER_NODE_READS_FLAGS, .carried = BUSTER_NODE_READS_FLAGS,
                                              .field = BUSTER_NODE_READS_CHANGE_FLAGS, .detail = (uint8_t)bit };
    }
    const bool local = object != 0 && object < program->object_count && program->object_symbols[object] == 0;
    const uint32_t alike = object < program->object_count ? reads->same_objects[object] : object;
    const uint32_t unlike = object == 0 || !local ? reads->first_local : reads->first_global;
    if (alike != object)
    {
        changes[count++] = (NodeReadsChange){ .value = alike, .decides = BUSTER_NODE_READS_OBJECT_DECIDES, .carried = BUSTER_NODE_READS_OBJECT_CARRIED,
                                              .field = BUSTER_NODE_READS_CHANGE_OBJECT };
    }
    if (unlike != 0 && unlike != object)
    {
        changes[count++] = (NodeReadsChange){ .value = unlike, .decides = BUSTER_NODE_READS_OBJECT_KIND, .carried = BUSTER_NODE_READS_OBJECT_CARRIED,
                                              .field = BUSTER_NODE_READS_CHANGE_OBJECT };
    }
    // The values its type holds: below the sign's bit of a sized type under eight bytes (a char's are under 128), and
    // any value else.
    const uint32_t index = type_index(type);
    const uint64_t size = type != 0 && index < facts->row_count && (facts->rows[index].flags & BUSTER_NODE_FACTS_SIZED) != 0 ? facts->sides[index].size : 8;
    const uint64_t bound = size != 0 && size < 8 ? UINT64_C(1) << (8 * size - 1) : UINT64_MAX;
    const uint64_t payloads[BUSTER_NODE_READS_PAYLOADS] = { payload ^ 1, 0, 3, 8, 70000, (UINT64_C(1) << 40) + 5 };
    for (uint32_t k = 0; k < BUSTER_NODE_READS_PAYLOADS && (standing & BUSTER_NODE_MACHINE_WIDE) == 0; k += 1)
    {
        if (payloads[k] != payload && (k == 0 || payloads[k] < bound))
        {
            changes[count++] = (NodeReadsChange){ .value = payloads[k], .decides = BUSTER_NODE_READS_PAYLOAD_DECIDES, .carried = BUSTER_NODE_READS_PAYLOAD_CARRIED,
                                                  .field = BUSTER_NODE_READS_CHANGE_PAYLOAD, .detail = (uint8_t)k };
        }
    }
    const uint64_t offsets[2] = { member_offset ^ 64, member_offset != 0 ? 0 : 128 };
    for (uint32_t k = 0; k < 2; k += 1)
    {
        changes[count++] = (NodeReadsChange){ .value = offsets[k], .decides = BUSTER_NODE_READS_OFFSET_DECIDES, .carried = BUSTER_NODE_READS_OFFSET_CARRIED,
                                              .field = BUSTER_NODE_READS_CHANGE_OFFSET };
    }
    return count;
}

// A reader's bits for record `at` of the unit after a change `change` changed what it made by `differs` (0 not at all,
// 1 a wide value alone, 2 a byte-level answer): the change's bit, which flag's bit it was, and for C and X which
// payload's value decided.
static void node_reads_note(NodeReads* reads, uint64_t at, uint32_t reader, const NodeReadsChange* change, uint32_t differs)
{
    if (differs != 0)
    {
        const uint32_t bit = differs == 2 ? change->decides : change->carried;
        reads->masks[at] |= (uint64_t)bit << (16 * reader);
        if (bit == BUSTER_NODE_READS_FLAGS)
        {
            reads->flags[reader][at] |= 1u << change->detail;
        }
        else if (bit == BUSTER_NODE_READS_CONVERSION_FLAGS)
        {
            reads->conversions[reader][at] |= (uint16_t)(1u << change->detail);
        }
        else if (bit == BUSTER_NODE_READS_PAYLOAD_DECIDES && reader != BUSTER_NODE_READS_T)
        {
            reads->masks[at] |= UINT64_C(1) << (48 + change->detail);
        }
    }
}

// How record `made` differs from `kept`, a consumer's as its rule made it again: 0 not at all; 1 in a wide value alone
// (its type's identity, its object, its payload, a member's offset, a conversion's type's identity: which, into
// `fields` as BUSTER_NODE_READS_NEED_*) or in the flags `passed` alone (a flag of its operand's that it hands on); 2 in
// a byte-level answer (whether the rule took it, its standing, another of its flags, its type's word, a conversion's
// flags or type's word).
static uint32_t node_reads_record_differs(const NodeFactsView* view, const NodeFactsRecord* made, const NodeFactsRecord* kept, uint32_t passed, uint32_t* fields)
{
    const bool decided = (made->taken != 0) != (kept->taken != 0) || made->standing != kept->standing || ((made->flags ^ kept->flags) & ~passed) != 0 || made->word != kept->word ||
                         made->conversion_flags[0] != kept->conversion_flags[0] || made->conversion_flags[1] != kept->conversion_flags[1] ||
                         node_facts_word_of(view, made->converted[0]) != node_facts_word_of(view, kept->converted[0]) ||
                         node_facts_word_of(view, made->converted[1]) != node_facts_word_of(view, kept->converted[1]);
    const uint32_t wide = (made->type != kept->type ? BUSTER_NODE_READS_NEED_TYPE : 0) | (made->object != kept->object ? BUSTER_NODE_READS_NEED_OBJECT : 0) |
                          (made->payload != kept->payload || made->exponent != kept->exponent ? BUSTER_NODE_READS_NEED_PAYLOAD : 0) |
                          (made->member_offset != kept->member_offset ? BUSTER_NODE_READS_NEED_OFFSET : 0) |
                          (made->converted[0] != kept->converted[0] || made->converted[1] != kept->converted[1] ? BUSTER_NODE_READS_NEED_CONVERTED : 0);
    *fields = wide;
    return decided ? 2u : wide != 0 || made->flags != kept->flags ? 1u : 0u;
}

// Where type `type` comes from, of the `operand_count` operand types `operands` and the node's own type name `input`
// (0 for none): BUSTER_NODE_READS_SOURCE_*.
static uint32_t node_reads_type_source(const NodeFacts* facts, TypeId type, const TypeId* operands, uint32_t operand_count, TypeId input)
{
    const uint32_t index = type_index(type);
    uint32_t source = type == 0 ? BUSTER_NODE_READS_SOURCE_NONE : index < BUSTER_TYPE_CODE_COUNT ? BUSTER_NODE_READS_SOURCE_BUILTIN : BUSTER_NODE_READS_SOURCE_NEW;
    for (uint32_t k = 0; k < operand_count && source == BUSTER_NODE_READS_SOURCE_NEW; k += 1)
    {
        source = operands[k] != 0 && type_index(operands[k]) == index ? BUSTER_NODE_READS_SOURCE_OPERAND : source;
    }
    source = source == BUSTER_NODE_READS_SOURCE_NEW && input != 0 && type_index(input) == index ? BUSTER_NODE_READS_SOURCE_INPUT : source;
    for (uint32_t k = 0; k <= operand_count && source == BUSTER_NODE_READS_SOURCE_NEW; k += 1)
    {
        const TypeId from = k < operand_count ? operands[k] : input;
        if (from != 0 && type_index(from) < facts->row_count)
        {
            const NodeFactsRow* row = facts->rows + type_index(from);
            const NodeFactsSide* side = facts->sides + type_index(from);
            const TypeId given[8] = { row->value, row->promoted, row->element, row->decayed[0], row->decayed[1], row->returned, side->pointers[0], side->pointers[1] };
            for (uint32_t g = 0; g < 8; g += 1)
            {
                source = given[g] != 0 && type_index(given[g]) == index ? BUSTER_NODE_READS_SOURCE_ROW : source;
            }
            // The pointer to it with other qualifiers than the side row's two: `type`'s own row names it as its element.
            const TypeId pointee = index < facts->row_count ? facts->rows[index].element : 0;
            source = source == BUSTER_NODE_READS_SOURCE_NEW && pointee != 0 && type_index(pointee) == type_index(from) &&
                             check_action_kind(facts->machine->types, type) == BUSTER_TYPE_KIND_POINTER ?
                         BUSTER_NODE_READS_SOURCE_ROW :
                         source;
        }
    }
    return source;
}

// Record `w`'s fields' sources, of the window whose first record is `first`, made (its records are the kernel's):
// three bits a field (BUSTER_NODE_READS_FIELD_*), its two conversions' in fields 3 and 4.
static uint32_t node_reads_sources(const NodeFacts* facts, uint32_t first, uint32_t w)
{
    const NodeFactsRecord* records = facts->records;
    const NodeFactsRecord* record = records + w;
    const uint64_t at = (uint64_t)first + w;
    const uint8_t kind = facts->kinds[at];
    const CheckOp op = (CheckOp)facts->ops[at];
    const bool leaf = kind == BUSTER_NODE_FACTS_LITERAL || kind == BUSTER_NODE_FACTS_IDENTIFIER || kind == BUSTER_NODE_FACTS_LITERAL_LINE;
    const bool valued = ((record->standing >> 2) & 0x07u) != CHECK_CONSTANT_NONE || (record->standing & BUSTER_NODE_MACHINE_FIELD) != 0 || record->payload != 0;
    uint32_t sources[BUSTER_NODE_READS_FIELDS] = { 0 };
    if (leaf)
    {
        sources[BUSTER_NODE_READS_FIELD_TYPE] = BUSTER_NODE_READS_SOURCE_LEAF;
        sources[BUSTER_NODE_READS_FIELD_OBJECT] = record->object != 0 ? BUSTER_NODE_READS_SOURCE_LEAF : BUSTER_NODE_READS_SOURCE_NONE;
        sources[BUSTER_NODE_READS_FIELD_PAYLOAD] = valued ? BUSTER_NODE_READS_SOURCE_LEAF : BUSTER_NODE_READS_SOURCE_NONE;
    }
    else
    {
        // Its operands: its first two, a conditional's third, and a call's callee alone (its arguments give it no type).
        const bool typed = op == CHECK_OP_CAST || op == CHECK_OP_CONVERT || op == CHECK_OP_INITIALIZE || op == CHECK_OP_SIZEOF || op == CHECK_OP_ALIGNOF;
        const TypeId input = typed ? (TypeId)facts->inputs[at] : 0;
        const uint32_t distances[3] = { facts->left[at], op == CHECK_OP_CALL ? 0u : facts->right[at], op == CHECK_OP_CONDITIONAL ? facts->inputs[at] : 0 };
        TypeId types[3] = { 0, 0, 0 };
        uint32_t places[3] = { 0, 0, 0 };
        uint32_t count = 0;
        for (uint32_t k = 0; k < 3; k += 1)
        {
            if (distances[k] != 0 && distances[k] <= w)
            {
                places[count] = w - distances[k];
                types[count] = records[places[count]].type;
                count += 1;
            }
        }
        const uint32_t own = node_reads_type_source(facts, record->type, types, count, input);
        sources[BUSTER_NODE_READS_FIELD_TYPE] = own;
        uint32_t object = record->object != 0 ? BUSTER_NODE_READS_SOURCE_NEW : BUSTER_NODE_READS_SOURCE_NONE;
        uint32_t payload = valued ? BUSTER_NODE_READS_SOURCE_NEW : BUSTER_NODE_READS_SOURCE_NONE;
        for (uint32_t k = 0; k < count; k += 1)
        {
            const NodeFactsRecord* operand = records + places[k];
            object = record->object != 0 && operand->object == record->object ? BUSTER_NODE_READS_SOURCE_OPERAND : object;
            payload = valued && operand->payload == record->payload && ((operand->standing ^ record->standing) & BUSTER_NODE_MACHINE_FIELD) == 0 &&
                              (((operand->standing >> 2) & 0x07u) != CHECK_CONSTANT_NONE || (operand->standing & BUSTER_NODE_MACHINE_FIELD) != 0 || operand->payload != 0) ?
                          BUSTER_NODE_READS_SOURCE_OPERAND :
                          payload;
        }
        // A size or an alignment of the node's type name or of an operand's type: its side row's.
        for (uint32_t k = 0; k <= count && payload == BUSTER_NODE_READS_SOURCE_NEW && (op == CHECK_OP_SIZEOF || op == CHECK_OP_ALIGNOF); k += 1)
        {
            const TypeId from = k < count ? types[k] : input;
            if (from != 0 && type_index(from) < facts->row_count && (facts->rows[type_index(from)].flags & BUSTER_NODE_FACTS_SIZED) != 0)
            {
                const NodeFactsSide* side = facts->sides + type_index(from);
                payload = record->payload == side->size || record->payload == side->alignment ? BUSTER_NODE_READS_SOURCE_ROW : payload;
            }
        }
        sources[BUSTER_NODE_READS_FIELD_OBJECT] = object;
        sources[BUSTER_NODE_READS_FIELD_PAYLOAD] = payload;
        for (uint32_t k = 0; k < 2; k += 1)
        {
            // A conversion's type: its record's own type's source where it is that type, and else an operand's (either
            // one's: a null pointer constant converts to the other operand's type).
            const TypeId converted = record->converted[k];
            sources[BUSTER_NODE_READS_FIELD_CONVERTED + k] = converted != 0 && type_index(converted) == type_index(record->type) && type_index(converted) >= BUSTER_TYPE_CODE_COUNT ?
                                                                 (own == BUSTER_NODE_READS_SOURCE_NEW ? BUSTER_NODE_READS_SOURCE_NEW : BUSTER_NODE_READS_SOURCE_OPERAND) :
                                                                 node_reads_type_source(facts, converted, types, count, input);
        }
        sources[BUSTER_NODE_READS_FIELD_OFFSET] = record->member_offset != 0 ? BUSTER_NODE_READS_SOURCE_NEW : BUSTER_NODE_READS_SOURCE_NONE;
    }
    uint32_t packed = 0;
    for (uint32_t field = 0; field < BUSTER_NODE_READS_FIELDS; field += 1)
    {
        packed |= sources[field] << (3 * field);
    }
    return packed;
}

// Record `c`'s rule again on the window's records as they are, compared with what it made (`kept`): nothing is changed,
// so it must make that. The record is then `kept`.
static void node_reads_rule_same(NodeReads* reads, NodeFacts* facts, const NodeFactsView* view, uint32_t first, uint32_t c, const NodeFactsRecord* kept)
{
    NodeFactsRecord* records = facts->records;
    uint32_t fields = 0;
    node_facts_slow(facts, first, c);
    reads->counts->same_runs += 1;
    reads->counts->same_differences += node_reads_record_differs(view, records + c, kept, 0, &fields) != 0;
    records[c] = *kept;
}

// What a later rule of T reads of window `window`'s records but a call's, and every record's fields' sources. The window
// is made first (its records are then T's, which the checked pass compared). Each consumer the kernel made is made again
// by its rule (node_facts_slow) with one field of one operand changed, and what it makes compared with what it made; a
// consumer whose rule is T's own leaves its operands' reads unknown. Before the changes and after them all the rule runs
// with nothing changed.
static void node_reads_consumers(NodeReads* reads, NodeFacts* facts, uint32_t window)
{
    NodeReadsCounts* counts = reads->counts;
    const uint32_t first = facts->windows[window];
    const uint32_t count = facts->windows[window + 1] - first;
    node_facts_given(facts, window, BUSTER_NODE_FACTS_POISON_NONE);
    node_facts_window(facts, window, BUSTER_NODE_FACTS_WHOLE, false);
    NodeFactsRecord* records = facts->records;
    const NodeFactsView view = node_facts_view(facts, first);
    for (uint32_t w = 0; w < count; w += 1)
    {
        reads->sources[first + w] = node_reads_sources(facts, first, w);
        reads->ran[first + w] |= BUSTER_NODE_READS_RAN_T;
    }
    for (uint32_t c = 0; c < count; c += 1)
    {
        const uint64_t at = (uint64_t)first + c;
        const uint8_t kind = facts->kinds[at];
        const CheckOp op = (CheckOp)facts->ops[at];
        if ((kind == BUSTER_NODE_FACTS_LOOKUP || kind == BUSTER_NODE_FACTS_OPERATOR) && op != CHECK_OP_CALL)
        {
            // Its operands' places: its first two and a conditional's third. A call's are node_reads_call's.
            const uint32_t arguments = 0;
            const uint32_t distances[3] = { facts->left[at], facts->right[at], op == CHECK_OP_CONDITIONAL ? facts->inputs[at] : 0 };
            const NodeFactsRecord kept = records[c];
            const bool made = kept.taken != 0;
            if (made)
            {
                node_reads_rule_same(reads, facts, &view, first, c, &kept);
            }
            counts->left_consumers += !made;
            // What this record's payload does when an operand's changes, an operand a bit: the operands whose payload
            // was changed; and over the changes that left the rule's byte-level answer as it was, the operands with
            // such a change, those one of whose arrived as the value given, those one of whose arrived as another, and
            // those one of whose left this payload as it was.
            uint32_t changed_payloads = 0;
            uint32_t tried = 0;
            uint32_t followed = 0;
            uint32_t other = 0;
            uint32_t unmoved = 0;
            for (uint32_t k = 0; k < 3 + arguments; k += 1)
            {
                const uint32_t distance = distances[k];
                const bool operand = distance != 0 && distance <= c && !(k == 1 && distances[1] == distances[0]);
                if (operand && !made)
                {
                    reads->ran[at - distance] &= (uint8_t)~BUSTER_NODE_READS_RAN_T;
                }
                else if (operand)
                {
                    const uint32_t o = c - distance;
                    const NodeFactsRecord own = records[o];
                    reads->ahead[at - distance] = (uint16_t)distance;
                    NodeReadsChange changes[BUSTER_NODE_READS_CHANGES];
                    const uint32_t changed = node_reads_changes(reads, facts, own.type, own.flags, own.object, own.payload, own.standing, own.member_offset, changes);
                    for (uint32_t i = 0; i < changed; i += 1)
                    {
                        const NodeReadsChange* change = changes + i;
                        switch (change->field)
                        {
                            case BUSTER_NODE_READS_CHANGE_TYPE:
                            {
                                records[o].type = (TypeId)change->value;
                                records[o].word = node_facts_word_of(&view, (TypeId)change->value);
                            } break;
                            case BUSTER_NODE_READS_CHANGE_FLAGS:
                            {
                                records[o].flags = (uint32_t)change->value;
                            } break;
                            case BUSTER_NODE_READS_CHANGE_OBJECT:
                            {
                                records[o].object = (uint32_t)change->value;
                            } break;
                            case BUSTER_NODE_READS_CHANGE_PAYLOAD:
                            {
                                records[o].payload = change->value;
                            } break;
                            default:
                            {
                                records[o].member_offset = change->value;
                            } break;
                        }
                        node_facts_slow(facts, first, c);
                        counts->rule_runs += 1;
                        // A flag handed on, the same bit and nothing else, is no read; a wide value carried is an edge to
                        // the consumer's fields that changed, a read only if one of those is (node_reads_classify).
                        const bool flag = change->field == BUSTER_NODE_READS_CHANGE_FLAGS;
                        uint32_t fields = 0;
                        const uint32_t differs = node_reads_record_differs(&view, records + c, &kept, flag ? own.flags ^ (uint32_t)change->value : 0, &fields);
                        node_reads_note(reads, at - distance, BUSTER_NODE_READS_T, change, differs == 2 ? 2 : 0);
                        changed_payloads |= change->field == BUSTER_NODE_READS_CHANGE_PAYLOAD ? 1u << k : 0;
                        if (change->field == BUSTER_NODE_READS_CHANGE_PAYLOAD && differs != 2)
                        {
                            const bool moved = records[c].payload != kept.payload || records[c].exponent != kept.exponent;
                            const bool exact = records[c].payload == change->value && records[c].exponent == kept.exponent;
                            tried |= 1u << k;
                            followed |= moved && exact ? 1u << k : 0;
                            other |= moved && !exact ? 1u << k : 0;
                            unmoved |= moved ? 0 : 1u << k;
                        }
                        if (differs == 1 && !flag)
                        {
                            const uint32_t place = change->field == BUSTER_NODE_READS_CHANGE_TYPE ? 0 : change->field == BUSTER_NODE_READS_CHANGE_OBJECT ? 1 :
                                                   change->field == BUSTER_NODE_READS_CHANGE_PAYLOAD ? 2 : 3;
                            reads->edges[at - distance] |= fields << (5 * place);
                        }
                        records[o] = own;
                        records[c] = kept;
                    }
                }
            }
            if (made)
            {
                // Every change taken back: the rule makes what it made.
                node_reads_rule_same(reads, facts, &view, first, c, &kept);
                const uint32_t reached = followed | other;
                const bool one = reached != 0 && (reached & (reached - 1)) == 0;
                reads->payloads[at] = one && other == 0 && (reached & unmoved) == 0 ? BUSTER_NODE_READS_PAYLOAD_FOLLOWS :
                                      one && followed != 0 ? BUSTER_NODE_READS_PAYLOAD_FITS :
                                      reached != 0 ? BUSTER_NODE_READS_PAYLOAD_COMPUTED :
                                      (changed_payloads & ~tried) != 0 ? BUSTER_NODE_READS_PAYLOAD_PATH : BUSTER_NODE_READS_PAYLOAD_STAYS;
            }
        }
    }
}

// The most arguments of a call whose rule is run again (node_reads_call).
#define BUSTER_NODE_READS_ARGUMENTS 32u

// A call's record as T's rule made it: its columns, and its arguments' conversions.
typedef struct NodeReadsCall NodeReadsCall;
struct NodeReadsCall
{
    CheckConversion arguments[BUSTER_NODE_READS_ARGUMENTS];
    uint64_t payload;
    uint64_t member_offset;
    TypeId type;
    uint32_t flags;
    uint32_t object;
    uint32_t word;
    TypeId converted[2];
    uint32_t conversion_flags[2];
    uint16_t exponent;
    uint8_t standing;
    uint8_t facts;
    uint8_t class;
};

// Call record `r`'s columns and its `count` arguments' conversions into `kept`, or back from it.
static void node_reads_call_keep(const NodeMachine* machine, uint32_t r, uint32_t count, NodeReadsCall* kept)
{
    const NodeMachineColumns* columns = machine->columns;
    *kept = (NodeReadsCall){
        .payload = columns->payloads[r],
        .member_offset = columns->member_offsets[r],
        .type = columns->value_types[r],
        .flags = columns->value_flags[r],
        .object = columns->objects[r],
        .word = columns->words[r],
        .converted = { columns->converted[2 * (uint64_t)r], columns->converted[2 * (uint64_t)r + 1] },
        .conversion_flags = { columns->conversion_flags[2 * (uint64_t)r], columns->conversion_flags[2 * (uint64_t)r + 1] },
        .exponent = columns->exponents[r],
        .standing = columns->standings[r],
        .facts = columns->facts[r],
        .class = columns->value_classes[r],
    };
    memcpy(kept->arguments, machine->argument_conversions + columns->argument_first[r], count * sizeof(CheckConversion));
}

static void node_reads_call_restore(NodeMachine* machine, uint32_t r, uint32_t count, const NodeReadsCall* kept)
{
    NodeMachineColumns* columns = machine->columns;
    columns->payloads[r] = kept->payload;
    columns->member_offsets[r] = kept->member_offset;
    columns->value_types[r] = kept->type;
    columns->value_flags[r] = kept->flags;
    columns->objects[r] = kept->object;
    columns->words[r] = kept->word;
    columns->converted[2 * (uint64_t)r] = kept->converted[0];
    columns->converted[2 * (uint64_t)r + 1] = kept->converted[1];
    columns->conversion_flags[2 * (uint64_t)r] = kept->conversion_flags[0];
    columns->conversion_flags[2 * (uint64_t)r + 1] = kept->conversion_flags[1];
    columns->exponents[r] = kept->exponent;
    columns->standings[r] = kept->standing;
    columns->facts[r] = kept->facts;
    columns->value_classes[r] = kept->class;
    memcpy(machine->argument_conversions + columns->argument_first[r], kept->arguments, count * sizeof(CheckConversion));
}

// How call record `r` and its `count` arguments' conversions differ from `kept` after its rule ran again and answered
// `taken`: node_reads_record_differs's answers, a wide value's among them an argument's conversion's type's identity.
static uint32_t node_reads_call_differs(const NodeMachine* machine, uint32_t r, uint32_t count, const NodeReadsCall* kept, bool taken, uint32_t passed, uint32_t* fields)
{
    const NodeMachineColumns* columns = machine->columns;
    const CheckConversion* arguments = machine->argument_conversions + columns->argument_first[r];
    bool decided = !taken || columns->standings[r] != kept->standing || ((columns->value_flags[r] ^ kept->flags) & ~passed) != 0 ||
                   node_machine_word(machine, columns->value_types[r]) != node_machine_word(machine, kept->type) ||
                   columns->conversion_flags[2 * (uint64_t)r] != kept->conversion_flags[0] || columns->conversion_flags[2 * (uint64_t)r + 1] != kept->conversion_flags[1] ||
                   node_machine_word(machine, columns->converted[2 * (uint64_t)r]) != node_machine_word(machine, kept->converted[0]) ||
                   node_machine_word(machine, columns->converted[2 * (uint64_t)r + 1]) != node_machine_word(machine, kept->converted[1]);
    uint32_t wide = (columns->value_types[r] != kept->type ? BUSTER_NODE_READS_NEED_TYPE : 0) | (columns->objects[r] != kept->object ? BUSTER_NODE_READS_NEED_OBJECT : 0) |
                    (columns->payloads[r] != kept->payload || columns->exponents[r] != kept->exponent ? BUSTER_NODE_READS_NEED_PAYLOAD : 0) |
                    (columns->converted[2 * (uint64_t)r] != kept->converted[0] || columns->converted[2 * (uint64_t)r + 1] != kept->converted[1] ? BUSTER_NODE_READS_NEED_CONVERTED : 0);
    for (uint32_t i = 0; i < count && taken; i += 1)
    {
        decided = decided || arguments[i].flags != kept->arguments[i].flags ||
                  node_machine_word(machine, arguments[i].type) != node_machine_word(machine, kept->arguments[i].type);
        wide |= arguments[i].type != kept->arguments[i].type ? BUSTER_NODE_READS_NEED_CONVERTED : 0;
    }
    *fields = wide;
    return decided ? 2u : wide != 0 || columns->value_flags[r] != kept->flags ? 1u : 0u;
}

// What T's rule of call record `r` of the machine's expression reads of its callee's and its arguments' records, each
// the unit's record `base` + its place: the rule runs again with one field of one of them changed in T's columns, and
// the call's record and its arguments' conversions are compared with what it made. A call past
// BUSTER_NODE_READS_ARGUMENTS arguments leaves its operands' reads unknown.
static void node_reads_call(NodeReads* reads, const NodeFacts* facts, NodeMachine* machine, uint64_t base, uint32_t r)
{
    NodeReadsCounts* counts = reads->counts;
    NodeMachineColumns* columns = machine->columns;
    const uint32_t node = node_machine_node(machine, r);
    const uint32_t callee = columns->left[r];
    const uint32_t first = columns->argument_first[r];
    const uint32_t count = columns->argument_counts[r];
    // The rule's own calls: one T typed by its action (the rule doesn't take it), or whose value the columns don't hold
    // whole, leaves its operands' reads unknown, as a consumer the kernel left does.
    bool run = callee < r && count <= BUSTER_NODE_READS_ARGUMENTS && (columns->standings[r] & BUSTER_NODE_MACHINE_WIDE) == 0;
    NodeReadsCall kept;
    if (run)
    {
        node_reads_call_keep(machine, r, count, &kept);
        uint32_t fields = 0;
        run = node_machine_call_rule(machine, r, node, callee);
        counts->same_runs += run;
        counts->same_differences += run && node_reads_call_differs(machine, r, count, &kept, true, 0, &fields) != 0;
        node_reads_call_restore(machine, r, count, &kept);
    }
    counts->left_consumers += !run;
    for (uint32_t k = 0; k <= count; k += 1)
    {
        const uint32_t o = k == 0 ? callee : machine->arguments[first + k - 1];
        if (o < r && !run)
        {
            reads->ran[base + o] &= (uint8_t)~BUSTER_NODE_READS_RAN_T;
        }
        else if (o < r)
        {
            const uint64_t at = base + o;
            const TypeId type = columns->value_types[o];
            const uint32_t flags = columns->value_flags[o];
            const uint32_t object = columns->objects[o];
            const uint64_t payload = columns->payloads[o];
            const uint64_t member_offset = columns->member_offsets[o];
            NodeReadsChange changes[BUSTER_NODE_READS_CHANGES];
            const uint32_t changed = node_reads_changes(reads, facts, type, flags, object, payload, columns->standings[o], member_offset, changes);
            reads->ahead[at] = (uint16_t)(r - o);
            for (uint32_t i = 0; i < changed; i += 1)
            {
                const NodeReadsChange* change = changes + i;
                const bool flag = change->field == BUSTER_NODE_READS_CHANGE_FLAGS;
                switch (change->field)
                {
                    case BUSTER_NODE_READS_CHANGE_TYPE:
                    {
                        columns->value_types[o] = (TypeId)change->value;
                        columns->words[o] = node_machine_word(machine, (TypeId)change->value);
                    } break;
                    case BUSTER_NODE_READS_CHANGE_FLAGS:
                    {
                        columns->value_flags[o] = (uint32_t)change->value;
                    } break;
                    case BUSTER_NODE_READS_CHANGE_OBJECT:
                    {
                        columns->objects[o] = (uint32_t)change->value;
                    } break;
                    case BUSTER_NODE_READS_CHANGE_PAYLOAD:
                    {
                        columns->payloads[o] = change->value;
                    } break;
                    default:
                    {
                        columns->member_offsets[o] = change->value;
                    } break;
                }
                uint32_t fields = 0;
                const bool taken = node_machine_call_rule(machine, r, node, callee);
                const uint32_t differs = node_reads_call_differs(machine, r, count, &kept, taken, flag ? flags ^ (uint32_t)change->value : 0, &fields);
                counts->rule_runs += 1;
                node_reads_note(reads, at, BUSTER_NODE_READS_T, change, differs == 2 ? 2 : 0);
                if (differs == 1 && !flag)
                {
                    const uint32_t place = change->field == BUSTER_NODE_READS_CHANGE_TYPE ? 0 : change->field == BUSTER_NODE_READS_CHANGE_OBJECT ? 1 :
                                           change->field == BUSTER_NODE_READS_CHANGE_PAYLOAD ? 2 : 3;
                    reads->edges[at] |= fields << (5 * place);
                }
                columns->value_types[o] = type;
                columns->words[o] = node_machine_word(machine, type);
                columns->value_flags[o] = flags;
                columns->objects[o] = object;
                columns->payloads[o] = payload;
                columns->member_offsets[o] = member_offset;
                node_reads_call_restore(machine, r, count, &kept);
            }
        }
    }
    if (run)
    {
        // Every change taken back: the rule makes what it made.
        uint32_t fields = 0;
        const bool taken = node_machine_call_rule(machine, r, node, callee);
        counts->same_runs += 1;
        counts->same_differences += node_reads_call_differs(machine, r, count, &kept, taken, 0, &fields) != 0;
        node_reads_call_restore(machine, r, count, &kept);
    }
}

// C's columns and X's tape of the machine's expression into `kept`.
static void node_reads_keep(const NodeMachine* machine, NodeReadsKept* kept)
{
    const NodeMachineColumns* columns = machine->columns;
    const uint32_t count = machine->count;
    memcpy(kept->states, columns->states, count * sizeof(uint8_t));
    memcpy(kept->contexts, columns->contexts, count * sizeof(uint8_t));
    memcpy(kept->templates, columns->templates, count * sizeof(uint8_t));
    memcpy(kept->targets, columns->targets, count * sizeof(TypeId));
    memcpy(kept->hooks, columns->hooks, count * sizeof(uint32_t));
    memcpy(kept->label_bases, columns->label_bases, count * sizeof(uint32_t));
    memcpy(kept->call_depths, columns->call_depths, count * sizeof(uint32_t));
    memcpy(kept->consumers, machine->consumers, count * sizeof(uint32_t));
    memcpy(kept->updates, machine->updates, machine->update_count * sizeof(NodeMachineUpdate));
    memcpy(kept->kinds, machine->kinds, machine->entry_count * sizeof(uint8_t));
    memcpy(kept->classes, machine->classes, machine->entry_count * sizeof(uint8_t));
    memcpy(kept->tape, machine->tape, machine->entry_count * sizeof(uint64_t));
    memcpy(kept->label_entries, machine->label_entries, machine->label_count * sizeof(uint32_t));
    memcpy(kept->calls_written, machine->calls_written, machine->call_count * sizeof(uint32_t));
    memcpy(kept->call_records, machine->call_records, machine->call_count * sizeof(uint32_t));
    kept->label_count = machine->label_count;
    kept->update_count = machine->update_count;
    kept->entry_count = machine->entry_count;
    kept->call_count = machine->call_count;
    kept->temporary_count = machine->temporary_count;
}

// C's columns back as `kept` has them, for X to run on them and for the next change's C.
static void node_reads_restore(NodeMachine* machine, const NodeReadsKept* kept)
{
    NodeMachineColumns* columns = machine->columns;
    const uint32_t count = machine->count;
    memcpy(columns->states, kept->states, count * sizeof(uint8_t));
    memcpy(columns->contexts, kept->contexts, count * sizeof(uint8_t));
    memcpy(columns->templates, kept->templates, count * sizeof(uint8_t));
    memcpy(columns->targets, kept->targets, count * sizeof(TypeId));
    memcpy(columns->hooks, kept->hooks, count * sizeof(uint32_t));
    memcpy(columns->label_bases, kept->label_bases, count * sizeof(uint32_t));
    memcpy(columns->call_depths, kept->call_depths, count * sizeof(uint32_t));
    memcpy(machine->consumers, kept->consumers, count * sizeof(uint32_t));
    memcpy(machine->updates, kept->updates, kept->update_count * sizeof(NodeMachineUpdate));
    machine->label_count = kept->label_count;
    machine->update_count = kept->update_count;
}

// How C's columns of the machine's expression differ from `kept`'s after C ran again and answered `refusal`: 0 not at
// all; 1 in a wide value alone (a target's identity, an update's destination, constant or stride, or its types'
// identities); 2 in a byte-level answer (a refusal, a record's state, context, template, call depth, hook or label, a
// target's word, an update's kinds).
static uint32_t node_reads_contexts_differ(const NodeMachine* machine, const NodeReadsKept* kept, NodeMachineRefusal refusal)
{
    const NodeMachineColumns* columns = machine->columns;
    const uint32_t count = machine->count;
    bool decided = refusal != NODE_MACHINE_TAKEN || machine->label_count != kept->label_count || machine->update_count != kept->update_count;
    bool carried = false;
    for (uint32_t r = 0; r < count && !decided; r += 1)
    {
        const uint8_t state = kept->states[r];
        const bool evaluated = (state & BUSTER_NODE_MACHINE_EVALUATED) != 0;
        const bool hooked = (state & (BUSTER_NODE_MACHINE_AFTER_CONDITION | BUSTER_NODE_MACHINE_AFTER_ARM | BUSTER_NODE_MACHINE_AFTER_BRANCH | BUSTER_NODE_MACHINE_BRANCHED)) != 0;
        const bool labeled = evaluated && (kept->templates[r] == (uint8_t)NODE_MACHINE_TEMPLATE_BRANCH ||
                                           (kept->templates[r] == (uint8_t)NODE_MACHINE_TEMPLATE_BRANCHES && node_machine_record_past(machine, r)));
        const bool planned = evaluated && ((state & BUSTER_NODE_MACHINE_SPLIT) != 0 || kept->templates[r] == (uint8_t)NODE_MACHINE_TEMPLATE_UPDATE);
        decided = columns->states[r] != state ||
                  (evaluated && (columns->contexts[r] != kept->contexts[r] || columns->templates[r] != kept->templates[r] || columns->call_depths[r] != kept->call_depths[r] ||
                                 node_machine_word(machine, columns->targets[r]) != node_machine_word(machine, kept->targets[r]))) ||
                  (hooked && columns->hooks[r] != kept->hooks[r]) || (labeled && columns->label_bases[r] != kept->label_bases[r]) ||
                  (planned && machine->consumers[r] != kept->consumers[r]);
        carried = carried || (evaluated && columns->targets[r] != kept->targets[r]);
    }
    for (uint32_t k = 0; k < kept->update_count && !decided; k += 1)
    {
        const NodeMachineUpdate* made = machine->updates + k;
        const NodeMachineUpdate* was = kept->updates + k;
        decided = made->stored_class != was->stored_class || made->calculated_class != was->calculated_class || made->computing != was->computing || made->kind != was->kind ||
                  made->get != was->get || made->store != was->store || made->increment != was->increment || made->postfix != was->postfix || made->subtract != was->subtract ||
                  made->pointer != was->pointer || made->power != was->power || made->direct != was->direct ||
                  node_machine_word(machine, made->stored) != node_machine_word(machine, was->stored) ||
                  node_machine_word(machine, made->calculated) != node_machine_word(machine, was->calculated) ||
                  node_machine_word(machine, made->right) != node_machine_word(machine, was->right);
        carried = carried || made->destination != was->destination || made->constant != was->constant || made->stride != was->stride || made->stored != was->stored ||
                  made->calculated != was->calculated || made->right != was->right;
    }
    return decided ? 2u : carried ? 1u : 0u;
}

// How X's tape of the machine's expression differs from `kept`'s after X ran again and answered `refusal`: 0 not at
// all; 1 in its values alone; 2 in a byte-level answer (a refusal, its length, an entry's kind or class, a label's
// place, the calls or the temporaries).
static uint32_t node_reads_tape_differs(const NodeMachine* machine, const NodeReadsKept* kept, NodeMachineRefusal refusal)
{
    const uint32_t entries = kept->entry_count;
    const bool decided = refusal != NODE_MACHINE_TAKEN || machine->entry_count != entries || machine->call_count != kept->call_count ||
                         machine->temporary_count != kept->temporary_count || memcmp(machine->kinds, kept->kinds, entries * sizeof(uint8_t)) != 0 ||
                         memcmp(machine->classes, kept->classes, entries * sizeof(uint8_t)) != 0 ||
                         memcmp(machine->label_entries, kept->label_entries, kept->label_count * sizeof(uint32_t)) != 0 ||
                         memcmp(machine->calls_written, kept->calls_written, kept->call_count * sizeof(uint32_t)) != 0 ||
                         memcmp(machine->call_records, kept->call_records, kept->call_count * sizeof(uint32_t)) != 0;
    return decided ? 2u : memcmp(machine->tape, kept->tape, entries * sizeof(uint64_t)) != 0 ? 1u : 0u;
}

// X on C's kept columns, then C, each compared with what `kept` has, for a change `change` of record `at` of the unit
// that is in place; C's columns are then as they were. Where C's columns differ in a wide value alone, X runs on them:
// the change is C's read by what the tape then shows.
static void node_reads_sweep(NodeReads* reads, NodeMachine* machine, uint64_t at, const NodeReadsChange* change, uint8_t context, TypeId target, uint32_t object)
{
    const NodeReadsKept* kept = &reads->kept;
    const NodeMachineRefusal expanded = node_machine_expand(machine, object);
    node_reads_note(reads, at, BUSTER_NODE_READS_X, change, node_reads_tape_differs(machine, kept, expanded));
    const NodeMachineRefusal classified = node_machine_contexts(machine, context, target, object);
    uint32_t contexts = node_reads_contexts_differ(machine, kept, classified);
    if (contexts == 1)
    {
        // C carried a wide value alone (a target's identity, an update's value): a read if it arrives on X's tape.
        contexts = node_reads_tape_differs(machine, kept, node_machine_expand(machine, object));
    }
    node_reads_note(reads, at, BUSTER_NODE_READS_C, change, contexts);
    node_reads_restore(machine, kept);
    reads->counts->sweep_runs += 1;
}

// The sweeps again on the machine's expression as it is, compared with what the census kept: nothing is changed, so they
// must make what they made. C's columns are then the kept ones, and the tape is X's of them.
static void node_reads_sweeps_same(NodeReads* reads, NodeMachine* machine, uint8_t context, TypeId target, uint32_t object)
{
    const NodeReadsKept* kept = &reads->kept;
    const NodeMachineRefusal again = node_machine_contexts(machine, context, target, object);
    const uint32_t contexts = node_reads_contexts_differ(machine, kept, again);
    const NodeMachineRefusal expanded = again == NODE_MACHINE_TAKEN ? node_machine_expand(machine, object) : again;
    reads->counts->same_runs += 1;
    reads->counts->same_differences += contexts != 0 || node_reads_tape_differs(machine, kept, expanded) != 0;
    node_reads_restore(machine, kept);
}

// What C and X read of the records of the machine's expression, which T has typed and whose consumer evaluates it with
// `context` and converts it to `target` (`object` as node_machine_run's): record `r` is the unit's record `base` + r.
// Each field of each record is changed in T's columns, one change at a time, and the sweeps run again
// (node_reads_sweep); before the changes and after them all the sweeps run with nothing changed. An expression past
// BUSTER_NODE_READS_MOST records is counted and left.
static void node_reads_sweeps(NodeReads* reads, const NodeFacts* facts, NodeMachine* machine, uint64_t base, uint8_t context, TypeId target, uint32_t object)
{
    NodeReadsCounts* counts = reads->counts;
    NodeMachineColumns* columns = machine->columns;
    NodeReadsKept* kept = &reads->kept;
    const uint32_t count = machine->count;
    bool placed = base + count <= facts->record_count;
    for (uint32_t r = 0; r < count && placed; r += 1)
    {
        placed = facts->nodes[base + r] == node_machine_node(machine, r);
    }
    counts->expressions += 1;
    counts->misplaced += !placed;
    // Its calls' rules first, on T's columns as T left them.
    for (uint32_t r = 0; r < count && placed; r += 1)
    {
        if (columns->codes[r] == CHECK_OP_CALL)
        {
            node_reads_call(reads, facts, machine, base, r);
        }
    }
    counts->long_expressions += placed && count > BUSTER_NODE_READS_MOST;
    counts->long_records += placed && count > BUSTER_NODE_READS_MOST ? count : 0;
    NodeMachineRefusal refusal = placed && count <= BUSTER_NODE_READS_MOST ? node_machine_contexts(machine, context, target, object) : NODE_MACHINE_REFUSED_SHAPE;
    refusal = refusal == NODE_MACHINE_TAKEN ? node_machine_expand(machine, object) : refusal;
    if (refusal == NODE_MACHINE_TAKEN && machine->entry_count <= BUSTER_NODE_READS_ENTRIES)
    {
        node_reads_keep(machine, kept);
        // Nothing changed: the sweeps make what they made.
        node_reads_sweeps_same(reads, machine, context, target, object);
        for (uint32_t r = 0; r < count; r += 1)
        {
            const uint64_t at = base + r;
            const TypeId type = columns->value_types[r];
            const uint32_t flags = columns->value_flags[r];
            const uint32_t own_object = columns->objects[r];
            const uint64_t payload = columns->payloads[r];
            const uint64_t member_offset = columns->member_offsets[r];
            NodeReadsChange changes[BUSTER_NODE_READS_CHANGES];
            const uint32_t changed = node_reads_changes(reads, facts, type, flags, own_object, payload, columns->standings[r], member_offset, changes);
            for (uint32_t i = 0; i < changed; i += 1)
            {
                const NodeReadsChange* change = changes + i;
                switch (change->field)
                {
                    case BUSTER_NODE_READS_CHANGE_TYPE:
                    {
                        columns->value_types[r] = (TypeId)change->value;
                    } break;
                    case BUSTER_NODE_READS_CHANGE_FLAGS:
                    {
                        columns->value_flags[r] = (uint32_t)change->value;
                    } break;
                    case BUSTER_NODE_READS_CHANGE_OBJECT:
                    {
                        columns->objects[r] = (uint32_t)change->value;
                    } break;
                    case BUSTER_NODE_READS_CHANGE_PAYLOAD:
                    {
                        columns->payloads[r] = change->value;
                    } break;
                    default:
                    {
                        columns->member_offsets[r] = change->value;
                    } break;
                }
                node_reads_sweep(reads, machine, at, change, context, target, object);
                columns->value_types[r] = type;
                columns->value_flags[r] = flags;
                columns->objects[r] = own_object;
                columns->payloads[r] = payload;
                columns->member_offsets[r] = member_offset;
            }
            // Its operands' conversions: a call's arguments' after its first two's. Each one's type (none is left
            // none) and its flags' bits.
            const uint32_t arguments = columns->codes[r] == CHECK_OP_CALL ? columns->argument_counts[r] : 0;
            for (uint32_t k = 0; k < 2 + arguments; k += 1)
            {
                TypeId* converted = k < 2 ? columns->converted + 2 * (uint64_t)r + k : &machine->argument_conversions[columns->argument_first[r] + (k - 2)].type;
                uint32_t* conversion_flags = k < 2 ? columns->conversion_flags + 2 * (uint64_t)r + k : &machine->argument_conversions[columns->argument_first[r] + (k - 2)].flags;
                const TypeId was = *converted;
                const uint32_t were = *conversion_flags;
                const TypeId same = node_reads_same_type(reads, facts, was);
                if (was != 0 && same != was)
                {
                    const NodeReadsChange change = { .value = same, .decides = BUSTER_NODE_READS_CONVERTED_DECIDES, .carried = BUSTER_NODE_READS_CONVERTED_CARRIED,
                                                     .field = BUSTER_NODE_READS_CHANGE_TYPE };
                    *converted = same;
                    node_reads_sweep(reads, machine, at, &change, context, target, object);
                    *converted = was;
                }
                if (was != 0)
                {
                    const NodeReadsChange change = { .value = node_reads_other_type(facts, was), .decides = BUSTER_NODE_READS_CONVERTED_WORD,
                                                     .carried = BUSTER_NODE_READS_CONVERTED_CARRIED, .field = BUSTER_NODE_READS_CHANGE_TYPE };
                    *converted = (TypeId)change.value;
                    node_reads_sweep(reads, machine, at, &change, context, target, object);
                    *converted = was;
                }
                for (uint32_t bit = 0; bit < BUSTER_NODE_READS_CONVERSION_BITS && was != 0; bit += 1)
                {
                    const NodeReadsChange change = { .value = were ^ (1u << bit), .decides = BUSTER_NODE_READS_CONVERSION_FLAGS, .carried = BUSTER_NODE_READS_CONVERSION_FLAGS,
                                                     .field = BUSTER_NODE_READS_CHANGE_FLAGS, .detail = (uint8_t)bit };
                    *conversion_flags = (uint32_t)change.value;
                    node_reads_sweep(reads, machine, at, &change, context, target, object);
                    *conversion_flags = were;
                }
            }
            reads->ran[at] |= BUSTER_NODE_READS_RAN_SWEEPS;
        }
        // Every change taken back: the sweeps make what they made, and the tape is as it was for whoever reads the
        // machine next.
        node_reads_sweeps_same(reads, machine, context, target, object);
    }
}

// The unit's records into the counts, once both sides ran, from the last to the first, so that a record's consumer is
// classified before it: what the consumer's rule carries of a record's field is a read where one of the consumer's
// fields it arrives in is read (the rule's carried bits are set here). Then each record by its operator: the
// readers' bits of the measured ones, each read wide field's source, and the record's class.
static void node_reads_classify(NodeReads* reads, const NodeFacts* facts)
{
    NodeReadsCounts* counts = reads->counts;
    static const uint16_t carried[4] = { BUSTER_NODE_READS_TYPE_CARRIED, BUSTER_NODE_READS_OBJECT_CARRIED, BUSTER_NODE_READS_PAYLOAD_CARRIED,
                                         BUSTER_NODE_READS_OFFSET_CARRIED };
    for (uint64_t next = facts->record_count; next != 0; next -= 1)
    {
        const uint64_t at = next - 1;
        const uint32_t op = node_reads_op(facts, at);
        const uint32_t kind = node_reads_kind(facts, at);
        const uint32_t sources = reads->sources[at];
        const bool measured = reads->ran[at] == (BUSTER_NODE_READS_RAN_T | BUSTER_NODE_READS_RAN_SWEEPS);
        const uint32_t ahead = reads->ahead[at];
        const uint32_t arrives = ahead != 0 ? reads->needs[at + ahead] : 0;
        uint64_t mask = reads->masks[at];
        for (uint32_t place = 0; place < 4; place += 1)
        {
            mask |= ((reads->edges[at] >> (5 * place)) & 0x1fu & arrives) != 0 ? (uint64_t)carried[place] << (16 * BUSTER_NODE_READS_T) : 0;
        }
        uint32_t all = 0;
        for (uint32_t reader = 0; reader < BUSTER_NODE_READS_READERS; reader += 1)
        {
            all |= (uint32_t)(mask >> (16 * reader)) & 0xffffu;
        }
        reads->masks[at] = mask;
        reads->needs[at] = (uint8_t)(((all & BUSTER_NODE_READS_WIDE_TYPE) != 0 ? BUSTER_NODE_READS_NEED_TYPE : 0) |
                                     ((all & BUSTER_NODE_READS_WIDE_OBJECT) != 0 ? BUSTER_NODE_READS_NEED_OBJECT : 0) |
                                     ((all & BUSTER_NODE_READS_WIDE_PAYLOAD) != 0 ? BUSTER_NODE_READS_NEED_PAYLOAD : 0) |
                                     ((all & BUSTER_NODE_READS_WIDE_OFFSET) != 0 ? BUSTER_NODE_READS_NEED_OFFSET : 0) |
                                     ((all & BUSTER_NODE_READS_WIDE_CONVERTED) != 0 ? BUSTER_NODE_READS_NEED_CONVERTED : 0));
        counts->records[op] += 1;
        counts->measured[op] += measured;
        counts->kind_records[kind] += 1;
        counts->kind_measured[kind] += measured;
        if (measured)
        {
            for (uint32_t reader = 0; reader < BUSTER_NODE_READS_READERS; reader += 1)
            {
                const uint32_t bits = (uint32_t)(mask >> (16 * reader)) & 0xffffu;
                for (uint32_t bit = 0; bit < BUSTER_NODE_READS_BITS; bit += 1)
                {
                    counts->bits[reader][op][bit] += bits >> bit & 1;
                }
                for (uint32_t bit = 0; bit < BUSTER_NODE_READS_FLAG_BITS; bit += 1)
                {
                    counts->flag_bits[reader][op][bit] += reads->flags[reader][at] >> bit & 1;
                }
                for (uint32_t bit = 0; bit < BUSTER_NODE_READS_CONVERSION_BITS; bit += 1)
                {
                    counts->conversion_bits[reader][op][bit] += reads->conversions[reader][at] >> bit & 1;
                }
            }
            for (uint32_t k = 0; k < BUSTER_NODE_READS_PAYLOADS; k += 1)
            {
                counts->payload_values[op][k] += mask >> (48 + k) & 1;
            }
            // Each wide field somebody reads, with its source: the two conversions' types are read as one field, and
            // each has its source.
            const uint32_t wide[BUSTER_NODE_READS_FIELDS] = { BUSTER_NODE_READS_WIDE_TYPE,      BUSTER_NODE_READS_WIDE_OBJECT,    BUSTER_NODE_READS_WIDE_PAYLOAD,
                                                              BUSTER_NODE_READS_WIDE_CONVERTED, BUSTER_NODE_READS_WIDE_CONVERTED, BUSTER_NODE_READS_WIDE_OFFSET };
            // The fields' places among BUSTER_NODE_READS_NEED_*.
            static const uint8_t need[BUSTER_NODE_READS_FIELDS] = { 0, 1, 2, 4, 4, 3 };
            uint32_t class = BUSTER_NODE_READS_CLASS_NONE;
            uint32_t fresh = 0;
            bool unseen = false;
            bool fits = false;
            for (uint32_t field = 0; field < BUSTER_NODE_READS_FIELDS; field += 1)
            {
                // A payload that equals an operand's is that operand's where the rule was seen to copy it, or where the
                // operand's value chose the rule's path; else it is new: computed, or its rule didn't run again.
                const uint32_t given = (sources >> (3 * field)) & 0x07u;
                const uint8_t did = reads->payloads[at];
                const bool equal = field == BUSTER_NODE_READS_FIELD_PAYLOAD && given == BUSTER_NODE_READS_SOURCE_OPERAND;
                const uint32_t source = equal && did != BUSTER_NODE_READS_PAYLOAD_FOLLOWS && did != BUSTER_NODE_READS_PAYLOAD_PATH ? BUSTER_NODE_READS_SOURCE_NEW : given;
                unseen = unseen || (equal && did == BUSTER_NODE_READS_PAYLOAD_UNSEEN && (all & wide[field]) != 0);
                fits = fits || (equal && did == BUSTER_NODE_READS_PAYLOAD_FITS && (all & wide[field]) != 0);
                if ((all & wide[field]) != 0)
                {
                    const uint32_t needs = source == BUSTER_NODE_READS_SOURCE_NEW ? BUSTER_NODE_READS_CLASS_NEW :
                                           source == BUSTER_NODE_READS_SOURCE_NONE ? BUSTER_NODE_READS_CLASS_NONE :
                                           source == BUSTER_NODE_READS_SOURCE_BUILTIN ? BUSTER_NODE_READS_CLASS_NAMED : BUSTER_NODE_READS_CLASS_COPIED;
                    counts->sources[field][op][source] += 1;
                    class = needs > class ? needs : class;
                    fresh |= source == BUSTER_NODE_READS_SOURCE_NEW ? 1u << need[field] : 0;
                }
            }
            counts->classes[op][class] += 1;
            counts->kind_classes[kind][class] += 1;
            counts->kind_unseen[kind] += unseen;
            counts->kind_fits[kind] += fits;
            for (uint32_t field = 0; field < 5; field += 1)
            {
                counts->kind_fields[kind][field] += reads->needs[at] >> field & 1;
                counts->kind_new[kind][field] += fresh >> field & 1;
            }
            for (uint32_t reader = 0; reader < BUSTER_NODE_READS_READERS; reader += 1)
            {
                const uint32_t bits = (uint32_t)(mask >> (16 * reader)) & 0xffffu;
                counts->kind_readers[kind][reader] += (bits & (BUSTER_NODE_READS_WIDE_TYPE | BUSTER_NODE_READS_WIDE_OBJECT | BUSTER_NODE_READS_WIDE_PAYLOAD |
                                                               BUSTER_NODE_READS_WIDE_OFFSET | BUSTER_NODE_READS_WIDE_CONVERTED)) != 0;
            }
            counts->kind_identities[kind] += ((uint32_t)(mask >> 16 | mask >> 32) & BUSTER_NODE_READS_IDENTITY_DECIDES) != 0;
            const uint32_t index = type_index(facts->expected_types[at]);
            counts->kind_lone[kind] += index >= BUSTER_TYPE_CODE_COUNT && index < facts->row_count && reads->same_types[index] == index;
        }
    }
}

#endif
