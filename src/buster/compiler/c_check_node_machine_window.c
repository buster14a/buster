// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #2850's step 2, C on the window (measure-node-machine --shadow --window-contexts; docs/one-core-path.md, "C on the
// window"): C's answers for a chunk's records, made where the fact words' kernel has just made the records in place
// (c_check_node_machine_facts.c's node_facts_window_run), 64 records a step, with no walk down a tree. C
// (c_check_node_machine_contexts.c) goes from each root down and gives every evaluated record its template and its
// operands their contexts and targets, a record at a time, from T's columns. A census of its answers (the measurement
// record's c-answers-census-2850) found that only one of them chains down a tree, whether a record is evaluated, and
// that it breaks at a leaf nearly always: every other answer is a consumer's bytes and a record's own.
//
// Four sweeps over a chunk's records:
// - The records' bytes (c_check_node_census.c's node_tests_window_bytes, which makes the tests' bytes from the same
//   gathers, 16 records a step): a record's type's class, facts and kind, its standing, and a byte of its flags and of
//   what its object is (BUSTER_NODE_CONTEXTS_*).
// - The loop (node_contexts_window_loop, 64 records a step, from the chunk's last block to its first):
//   - a record's context by its consumer's operator and its place among the consumer's operands, the consumer's bytes
//     through its distance forward (the front's `window_ups`);
//   - its template by masks, C's rules (node_machine_classify's) term by term: its operator's case over its own bytes,
//     its context and its two operands' bytes (through their distances back). C reads T's conversion of each operand;
//     an operand's own type stands for it where the two say the same (an array's and a function's are a pointer's;
//     two operands of a number's class convert to a number's);
//   - whether its consumer evaluates it and its target's source, by the consumer's template and its place (a table of
//     64 rows, one register), the block's own templates read through the same distances forward. A record its
//     consumer doesn't evaluate, with records below, takes their range out (a record at a time: few have any).
// - The targets (node_contexts_window_targets, 16 records a step): the type a source names, one gather from the
//   consumer's record's line (T's conversion of the operand, or the consumer's own type) or, for a call's argument,
//   from its own (its conversion's type, which the kernel's call rule made there).
// - An expression's part (node_contexts_window_chunk, an expression at a time): whether it is decided here (none of its
//   records is listed), whether C refuses it (an evaluated record with no template), and its root's target, given.
//
// A call (node_machine_classify_call) is the one operator whose operands' targets are in no consumer's line: an
// argument's is its conversion's type, which the kernel's call rule makes with the call's record
// (c_check_node_machine_facts.c's node_facts_argument_made) and keeps in the argument's record, and what C's `plain`
// asks of the callee's prototype and of those conversions is a bit of the call's record. The call's template is then
// one mask, its callee is evaluated unless the lowering calls it directly (its own bytes), and its arguments are, each
// to its own record's target.
//
// A statement's CONDITION over `&&`, `||` and `!` is evaluated as branches and no value (node_machine_classify_branches),
// which C says of a record from the root down. Here a record says it of itself, from its own bytes, its first operand's
// and its consumer's class, and for a record that has its consumer's answer, the next record, from that one's, down a
// run of such records; what that cannot say lists the expression (the loop's comment).
//
// An `&&`, an `||` and a conditional as a value (node_machine_logical) have two labels, a condition converted and then
// tested or evaluated as branches, and their other operands converted: a logical operator's second to T's conversion
// of it, a conditional's arms to its own type. A comma passes its second operand, its first converted to void, and is
// discarded where its consumer's conversion of it is to void. Each is a mask in the loop, as the other templates are,
// with three of C's terms a record at a time (below).
//
// What is listed, for C on T's records as before: an `&&` or an `||` whose first operand's truth may be known, one
// that is a value whose first operand is a `!`, a `!` over an `&&` or an `||` under a statement's CONDITION, a run of
// more than three records that each have their consumer's answer, a conditional whose condition's truth may be known
// or that is folded, of one operand twice, an operator's value whose consumer's conversion the tasks put into its arms,
// a comma that is read for its address, that is folded or that is a comma's second operand, a selection, an update the
// masks don't take, a division or a remainder by an integer constant (the tasks' when its divisor is a power of two:
// the divisor's value), a record whose value the columns don't hold whole, a cast to a union, a leaf C's rules give a
// template the loop doesn't make, a call whose arguments' conversions the kernel didn't all make (one T's rule or its
// action makes, or a call the kernel left to T), a callee with an object that is no name's leaf (its symbol's bit is a
// leaf's line's), and any operator without a case here. Listing is always safe: C decides the expression. A refusal
// has to be C's own, so each of C's terms is here unless what makes the records implies it, and the comments say what
// does.
//
// Seven things are a record at a time: a pointer's stride, its type's row's (a subscript's and a pointer's sum's rule);
// C's rule on T's two conversions themselves (node_contexts_converted), for a sum or a binary operator whose operands'
// own classes don't say what their conversions' are; the range under a record that isn't evaluated; a consumer or a
// first operand out of the two registers' reach; whether a comma's consumer's type is void itself; whether an
// operator's value's consumer's conversion is one the tasks put into its arms (node_contexts_sinks), and a root's in
// its expression's part; and a discarded comma's context's byte.
// C on T's columns at reader time is the reference: every record of every expression decided here and taken is compared
// with its answers, and every decided expression with its verdict (node_contexts_window_compare). A refused
// expression's records are compared with nothing: no one reads them. No compile takes any of it.
//
// The rules lean on what the lowering's classes say of a type (c_check_program_lower.c's check_lower_type_class and
// check_lower_type_facts): a type with the aggregate's fact has no class of a number's and its value is its address,
// and an array's or a function's class is a pointer's. T's record of an aggregate has an address's class
// (node_machine_type), which C's wrapper's rule reads.
#ifndef BUSTER_C_CHECK_NODE_MACHINE_WINDOW_INCLUDED
#define BUSTER_C_CHECK_NODE_MACHINE_WINDOW_INCLUDED

// A record's byte of its flags and its object (NodeFactsReader.window_c_flags): an effect (a side effect or the
// floating-point environment's); a vector's element (a vector's subscript and its member: check_vector_subscript and
// check_vector_member alone set it); whether it has an object, and whether that object has a symbol of its own (a
// name's leaf's alone: a global's); and whether the record is one of an expression the kernel made in place. Three
// things C's rules read are in no byte, each for what makes it:
// - a bit-field's flag: check_action_member and check_action_bit_field_store set it with the member's width, and a
//   value with a width is one the columns don't hold whole (node_machine_put_value), which lists its expression;
// - a fault and an overload set: a wide value's alone, too;
// - whether an object is one the unit knows: at reader time the machine's tables have an entry a token
//   (node_machine_begin_reader), so every object is, and its address is a template's (node_machine_address_takes).
#define BUSTER_NODE_CONTEXTS_EFFECTS 0x01u
#define BUSTER_NODE_CONTEXTS_VECTOR 0x02u
#define BUSTER_NODE_CONTEXTS_OBJECT 0x04u
#define BUSTER_NODE_CONTEXTS_SYMBOL 0x08u
#define BUSTER_NODE_CONTEXTS_PLACED 0x10u
// And two bits of a call's record alone, what the kernel's call rule says of it (BUSTER_NODE_FACTS_CONVERTED_CALL and
// _PLAIN_CALL): each of its arguments' conversions is made, in the argument's record; and the call is a plain one,
// its callee's prototype's result and parameters and its arguments' conversions' types plain scalars. Another record's
// bits are its exponent's, for another reason: the loop reads them of calls alone.
#define BUSTER_NODE_CONTEXTS_CONVERTED 0x20u
#define BUSTER_NODE_CONTEXTS_PLAIN 0x40u
// And a register object's flag (BUSTER_CHECK_REGISTER), which a name's use has where its declaration has the register's
// bit (check_input_register_flags): C's update's plan takes no register variable by name.
#define BUSTER_NODE_CONTEXTS_REGISTER 0x80u

// An operator's class in the loop, by its code (NodeContextsTables.classes): the cases the loop has, the kinds it
// lists, and whether the operator evaluates its first operand for its address (BUSTER_NODE_CONTEXTS_CLASS_PLACE).
// An operator with no class reads as BUSTER_NODE_CONTEXTS_CLASS_OTHER, and the kinds from _CONDITIONAL up are listed.
#define BUSTER_NODE_CONTEXTS_CLASS_OTHER 0u
#define BUSTER_NODE_CONTEXTS_CLASS_VALUE 1u
#define BUSTER_NODE_CONTEXTS_CLASS_WRAP 2u
#define BUSTER_NODE_CONTEXTS_CLASS_MEMBER 3u
#define BUSTER_NODE_CONTEXTS_CLASS_SUM 4u
#define BUSTER_NODE_CONTEXTS_CLASS_SUBSCRIPT 5u
#define BUSTER_NODE_CONTEXTS_CLASS_UNARY 6u
#define BUSTER_NODE_CONTEXTS_CLASS_TRUTH 7u
#define BUSTER_NODE_CONTEXTS_CLASS_ASSIGN 8u
#define BUSTER_NODE_CONTEXTS_CLASS_ADDRESS 9u
#define BUSTER_NODE_CONTEXTS_CLASS_DEREFERENCE 10u
#define BUSTER_NODE_CONTEXTS_CLASS_BINARY 11u
#define BUSTER_NODE_CONTEXTS_CLASS_DIVIDE 12u
#define BUSTER_NODE_CONTEXTS_CLASS_CALL 13u
#define BUSTER_NODE_CONTEXTS_CLASS_UPDATE 14u
#define BUSTER_NODE_CONTEXTS_CLASS_LOGICAL 15u
#define BUSTER_NODE_CONTEXTS_CLASS_CONDITIONAL 16u
#define BUSTER_NODE_CONTEXTS_CLASS_COMMA 17u
#define BUSTER_NODE_CONTEXTS_CLASS_KIND 0x1fu
// A truth's bit: a statement's CONDITION, where `!` has none. (An update's first bit's place: a class's kind says whose
// it is.)
#define BUSTER_NODE_CONTEXTS_CLASS_CONDITION 0x20u
// An update's two bits: `++` and `--`, which have no second operand; and `/=` and `%=`, of whose divisor C's plan asks
// a power of two (node_machine_power).
#define BUSTER_NODE_CONTEXTS_CLASS_INCREMENT 0x20u
#define BUSTER_NODE_CONTEXTS_CLASS_QUOTIENT 0x40u
#define BUSTER_NODE_CONTEXTS_CLASS_PLACE 0x80u

// Where an operand's target comes from (a row's low bits, NodeFactsReader.window_c_sources): none; T's conversion of
// the operand, in its consumer's record; its consumer's own type (a wrapper's operand and a store's value: a cast to an
// atomic type converts its operand to the value type and has the atomic one); long (a pointer's index).
#define BUSTER_NODE_CONTEXTS_SOURCE_NONE 0u
#define BUSTER_NODE_CONTEXTS_SOURCE_GIVEN 1u
#define BUSTER_NODE_CONTEXTS_SOURCE_NODE 2u
#define BUSTER_NODE_CONTEXTS_SOURCE_LONG 3u
// And its own record's: a call's argument's conversion's type, which the kernel's call rule wrote there.
#define BUSTER_NODE_CONTEXTS_SOURCE_OWN 4u
// And void: a comma's first operand's, and a discarded comma's second's.
#define BUSTER_NODE_CONTEXTS_SOURCE_VOID 5u
#define BUSTER_NODE_CONTEXTS_SOURCE 0x07u
// A row's bit: the consumer evaluates the operand.
#define BUSTER_NODE_CONTEXTS_EVALUATES 0x80u
// Why an expression is listed (the first of its records' reasons counts for it; node_contexts_window_reason, from the
// record's operator's class in NodeFactsReader.window_c_reasons and its block's masks): a call; a logical operator or
// a conditional; an update; a comma or a selection; a division or a remainder by an integer constant; a value the
// columns don't hold whole (a leaf with no line of its own among them); any other operator (a cast to a union among
// them).
#define BUSTER_NODE_CONTEXTS_REASON_NONE 0u
#define BUSTER_NODE_CONTEXTS_REASON_CALL 1u
#define BUSTER_NODE_CONTEXTS_REASON_LOGICAL 2u
#define BUSTER_NODE_CONTEXTS_REASON_UPDATE 3u
#define BUSTER_NODE_CONTEXTS_REASON_COMMA 4u
#define BUSTER_NODE_CONTEXTS_REASON_DIVISION 5u
#define BUSTER_NODE_CONTEXTS_REASON_WIDE 6u
#define BUSTER_NODE_CONTEXTS_REASON_OPERATOR 7u
_Static_assert(BUSTER_NODE_CONTEXTS_REASON_OPERATOR + 1 == BUSTER_NODE_CONTEXTS_REASONS, "the unit's counts have a place a reason");
static const char* const node_contexts_reason_names[BUSTER_NODE_CONTEXTS_REASONS] = {
    "none", "call", "logical-or-conditional", "update", "comma", "division-by-a-constant", "wide-value", "operator",
};

// A block's masks (NodeFactsReader.window_c_masks, a line of the cache a block of 64 records): the records C
// evaluates, those of them without a template, and the records that list their expression; then, for the shadow's
// count of why, those that list it for a divisor that is an integer constant and for a value the columns don't hold
// whole.
#define BUSTER_NODE_CONTEXTS_MASK_ALIVE 0u
#define BUSTER_NODE_CONTEXTS_MASK_NONE 1u
#define BUSTER_NODE_CONTEXTS_MASK_LISTED 2u
#define BUSTER_NODE_CONTEXTS_MASK_DIVISOR 3u
#define BUSTER_NODE_CONTEXTS_MASK_WIDE 4u
#define BUSTER_NODE_CONTEXTS_MASKS 8u

// What the window made of an expression (NodeFactsReader.c_verdicts): it didn't run on it; it is listed; decided and
// taken; decided and refused for a template.
#define BUSTER_NODE_CONTEXTS_LISTED 1u
#define BUSTER_NODE_CONTEXTS_TAKEN 2u
#define BUSTER_NODE_CONTEXTS_REFUSED 3u

// The loop's two tables: an operator's class by its code, 128 bytes read as two registers by one byte permute; and an
// operand's row by its consumer's template and its place, 64 bytes read as one (the template times two and the place,
// one for a second operand or a later one: no operator with a case here has a third, and the operands of a consumer
// with no template read one row wherever they are).
//
// A consumer's key is its template's number but for an update, whose operands' rows are four where its template is
// one: its template's number and two bits, whether its destination is evaluated for its address and whether it is a
// pointer's. The loop keeps the keys in a register for the block below; the column it stores is C's templates'.
#define BUSTER_NODE_CONTEXTS_KEY_ADDRESS 2u
#define BUSTER_NODE_CONTEXTS_KEY_POINTER 4u
// And three keys past an update's: a comma's template is a wrapper's and a conditional's is a logical operator's
// value's, with other rows. A comma's first operand is converted to void and its second to the comma's type, or to
// void too where the comma is discarded; a conditional's condition is converted as a logical operator's is, and its
// arms to its own type.
#define BUSTER_NODE_CONTEXTS_KEY_COMMA 27u
#define BUSTER_NODE_CONTEXTS_KEY_COMMA_DISCARD 28u
#define BUSTER_NODE_CONTEXTS_KEY_CONDITIONAL 29u
typedef struct NodeContextsTables NodeContextsTables;
struct NodeContextsTables
{
    _Alignas(64) uint8_t classes[128];
    _Alignas(64) uint8_t rows[64];
};

_Static_assert(sizeof(NodeContextsTables) <= 256, "the reader's state has room for the two tables (node_facts_reader_begin)");

static void node_contexts_tables(NodeContextsTables* tables)
{
    memset(tables, 0, sizeof(*tables));
    for (uint32_t op = 0; op < CHECK_OP_COUNT && op < 128; op += 1)
    {
        const uint8_t binary = check_lower_binary((CheckOp)op);
        uint8_t class = BUSTER_NODE_CONTEXTS_CLASS_OTHER;
        switch ((CheckOp)op)
        {
            case CHECK_OP_VALUE: class = BUSTER_NODE_CONTEXTS_CLASS_VALUE; break;
            case CHECK_OP_CAST: case CHECK_OP_CONVERT: case CHECK_OP_POSITIVE: class = BUSTER_NODE_CONTEXTS_CLASS_WRAP; break;
            case CHECK_OP_MEMBER: class = BUSTER_NODE_CONTEXTS_CLASS_MEMBER | BUSTER_NODE_CONTEXTS_CLASS_PLACE; break;
            case CHECK_OP_ARROW: class = BUSTER_NODE_CONTEXTS_CLASS_MEMBER; break;
            case CHECK_OP_ADD: case CHECK_OP_SUBTRACT: class = BUSTER_NODE_CONTEXTS_CLASS_SUM; break;
            case CHECK_OP_SUBSCRIPT: class = BUSTER_NODE_CONTEXTS_CLASS_SUBSCRIPT; break;
            case CHECK_OP_NEGATIVE: case CHECK_OP_BIT_NOT: class = BUSTER_NODE_CONTEXTS_CLASS_UNARY; break;
            case CHECK_OP_NOT: class = BUSTER_NODE_CONTEXTS_CLASS_TRUTH; break;
            case CHECK_OP_CONDITION: class = BUSTER_NODE_CONTEXTS_CLASS_TRUTH | BUSTER_NODE_CONTEXTS_CLASS_CONDITION; break;
            case CHECK_OP_ASSIGN: class = BUSTER_NODE_CONTEXTS_CLASS_ASSIGN | BUSTER_NODE_CONTEXTS_CLASS_PLACE; break;
            case CHECK_OP_ADDRESS: class = BUSTER_NODE_CONTEXTS_CLASS_ADDRESS | BUSTER_NODE_CONTEXTS_CLASS_PLACE; break;
            case CHECK_OP_DEREFERENCE: class = BUSTER_NODE_CONTEXTS_CLASS_DEREFERENCE; break;
            case CHECK_OP_DIVIDE: case CHECK_OP_REMAINDER: class = BUSTER_NODE_CONTEXTS_CLASS_DIVIDE; break;
            case CHECK_OP_CALL: class = BUSTER_NODE_CONTEXTS_CLASS_CALL; break;
            case CHECK_OP_LOGICAL_AND: case CHECK_OP_LOGICAL_OR: class = BUSTER_NODE_CONTEXTS_CLASS_LOGICAL; break;
            case CHECK_OP_CONDITIONAL: class = BUSTER_NODE_CONTEXTS_CLASS_CONDITIONAL; break;
            case CHECK_OP_COMMA: class = BUSTER_NODE_CONTEXTS_CLASS_COMMA; break;
            default:
            {
                const bool update = op >= CHECK_OP_PRE_INCREMENT && op <= CHECK_OP_BIT_XOR_ASSIGN;
                const uint8_t increment = op <= CHECK_OP_POST_DECREMENT ? BUSTER_NODE_CONTEXTS_CLASS_INCREMENT : 0;
                const uint8_t quotient = op == CHECK_OP_DIVIDE_ASSIGN || op == CHECK_OP_REMAINDER_ASSIGN ? BUSTER_NODE_CONTEXTS_CLASS_QUOTIENT : 0;
                class = update ? BUSTER_NODE_CONTEXTS_CLASS_UPDATE | BUSTER_NODE_CONTEXTS_CLASS_PLACE | increment | quotient :
                        binary != BUSTER_TYPED_NONE && op < CHECK_OP_PRE_INCREMENT ? BUSTER_NODE_CONTEXTS_CLASS_BINARY : BUSTER_NODE_CONTEXTS_CLASS_OTHER;
            } break;
        }
        tables->classes[op] = class;
    }
    // An operand's row by its consumer's template (check_lower_template_rules', as C's cases give them): a wrapper's
    // operand to the wrapper's type (`&x`'s to none: the loop's, by the consumer's operator); a dereference's, a
    // member's, a unary operator's and a truth's to T's conversion of it; a binary operator's two; a pointer's, and its
    // index to long; a store's value to the store's type, and its destination for its address unless the store names it.
    // A consumer with no template has its expression refused or listed, and its operands' answers are read by no one:
    // the loop takes them for evaluated, so that none takes a range out below it.
    const uint8_t given = BUSTER_NODE_CONTEXTS_EVALUATES | BUSTER_NODE_CONTEXTS_SOURCE_GIVEN;
    const uint8_t node = BUSTER_NODE_CONTEXTS_EVALUATES | BUSTER_NODE_CONTEXTS_SOURCE_NODE;
    tables->rows[CHECK_LOWER_TEMPLATE_NONE << 1] = BUSTER_NODE_CONTEXTS_EVALUATES | BUSTER_NODE_CONTEXTS_SOURCE_NONE;
    tables->rows[(CHECK_LOWER_TEMPLATE_NONE << 1) + 1] = BUSTER_NODE_CONTEXTS_EVALUATES | BUSTER_NODE_CONTEXTS_SOURCE_NONE;
    tables->rows[CHECK_LOWER_TEMPLATE_PASS << 1] = node;
    tables->rows[CHECK_LOWER_TEMPLATE_DEREFERENCE << 1] = given;
    tables->rows[CHECK_LOWER_TEMPLATE_MEMBER << 1] = given;
    tables->rows[CHECK_LOWER_TEMPLATE_UNARY << 1] = given;
    tables->rows[CHECK_LOWER_TEMPLATE_TRUTH << 1] = given;
    tables->rows[CHECK_LOWER_TEMPLATE_BINARY << 1] = given;
    tables->rows[(CHECK_LOWER_TEMPLATE_BINARY << 1) + 1] = given;
    tables->rows[CHECK_LOWER_TEMPLATE_POINTER << 1] = given;
    tables->rows[(CHECK_LOWER_TEMPLATE_POINTER << 1) + 1] = BUSTER_NODE_CONTEXTS_EVALUATES | BUSTER_NODE_CONTEXTS_SOURCE_LONG;
    tables->rows[(CHECK_LOWER_TEMPLATE_STORE_LOCAL << 1) + 1] = node;
    tables->rows[(CHECK_LOWER_TEMPLATE_STORE_GLOBAL << 1) + 1] = node;
    tables->rows[CHECK_LOWER_TEMPLATE_STORE << 1] = BUSTER_NODE_CONTEXTS_EVALUATES | BUSTER_NODE_CONTEXTS_SOURCE_NONE;
    tables->rows[(CHECK_LOWER_TEMPLATE_STORE << 1) + 1] = node;
    // A plain call's (node_machine_classify_call): its callee for its value with no target (the loop takes a callee
    // that is called directly out), and each argument to its conversion's type, its own record's.
    tables->rows[BUSTER_NODE_MACHINE_TEMPLATE_CALL << 1] = BUSTER_NODE_CONTEXTS_EVALUATES | BUSTER_NODE_CONTEXTS_SOURCE_NONE;
    tables->rows[(BUSTER_NODE_MACHINE_TEMPLATE_CALL << 1) + 1] = BUSTER_NODE_CONTEXTS_EVALUATES | BUSTER_NODE_CONTEXTS_SOURCE_OWN;
    // An operator evaluated as branches (node_machine_classify_branches): each operand evaluated, a value's to T's
    // conversion of it, which is then tested. An operand that is branched in its turn has no target, by its own record's
    // bit in the loop.
    tables->rows[NODE_MACHINE_TEMPLATE_BRANCHES << 1] = given;
    tables->rows[(NODE_MACHINE_TEMPLATE_BRANCHES << 1) + 1] = given;
    // A logical operator's value (node_machine_logical's): its condition to T's conversion of it, which is then
    // tested, and its second operand to T's. A condition that is branches has no target, by its own record's bit.
    tables->rows[NODE_MACHINE_TEMPLATE_BRANCH << 1] = given;
    tables->rows[(NODE_MACHINE_TEMPLATE_BRANCH << 1) + 1] = given;
    // A conditional's: its condition so too, and each arm to the conditional's own type.
    tables->rows[BUSTER_NODE_CONTEXTS_KEY_CONDITIONAL << 1] = given;
    tables->rows[(BUSTER_NODE_CONTEXTS_KEY_CONDITIONAL << 1) + 1] = node;
    // A comma's (node_machine_classify's case): its first operand to void, and its second to the comma's type, or to
    // void where the comma is discarded.
    const uint8_t voided = BUSTER_NODE_CONTEXTS_EVALUATES | BUSTER_NODE_CONTEXTS_SOURCE_VOID;
    tables->rows[BUSTER_NODE_CONTEXTS_KEY_COMMA << 1] = voided;
    tables->rows[(BUSTER_NODE_CONTEXTS_KEY_COMMA << 1) + 1] = node;
    tables->rows[BUSTER_NODE_CONTEXTS_KEY_COMMA_DISCARD << 1] = voided;
    tables->rows[(BUSTER_NODE_CONTEXTS_KEY_COMMA_DISCARD << 1) + 1] = voided;
    // An update's (node_machine_classify_update), by its key (its template's number and its destination's bits): its
    // destination for its address unless the update names it, and its second operand to T's conversion of it, or to
    // long where the destination is a pointer.
    for (uint32_t bits = 0; bits <= (BUSTER_NODE_CONTEXTS_KEY_ADDRESS | BUSTER_NODE_CONTEXTS_KEY_POINTER); bits += BUSTER_NODE_CONTEXTS_KEY_ADDRESS)
    {
        const uint32_t key = BUSTER_NODE_MACHINE_TEMPLATE_UPDATE + bits;
        tables->rows[key << 1] = (bits & BUSTER_NODE_CONTEXTS_KEY_ADDRESS) != 0 ? BUSTER_NODE_CONTEXTS_EVALUATES | BUSTER_NODE_CONTEXTS_SOURCE_NONE : 0;
        tables->rows[(key << 1) + 1] = BUSTER_NODE_CONTEXTS_EVALUATES | ((bits & BUSTER_NODE_CONTEXTS_KEY_POINTER) != 0 ? BUSTER_NODE_CONTEXTS_SOURCE_LONG : BUSTER_NODE_CONTEXTS_SOURCE_GIVEN);
    }
}

_Static_assert(CHECK_OP_LOGICAL_OR == CHECK_OP_LOGICAL_AND + 1, "the loop reads an operand that is `&&` or `||` as a range of two codes");
_Static_assert(CHECK_LOWER_TEMPLATE_STORE < 16 && BUSTER_NODE_MACHINE_TEMPLATE_CALL < 32 && CHECK_OP_COUNT <= 128,
               "a row's place in the table is a template and a place in six bits, the loop's own templates' numbers are four, and an operator's code is seven");
_Static_assert(BUSTER_NODE_MACHINE_TEMPLATE_UPDATE + BUSTER_NODE_CONTEXTS_KEY_ADDRESS > NODE_MACHINE_TEMPLATE_BRANCHES &&
               BUSTER_NODE_CONTEXTS_KEY_POINTER == 2 * BUSTER_NODE_CONTEXTS_KEY_ADDRESS &&
               BUSTER_NODE_MACHINE_TEMPLATE_UPDATE + BUSTER_NODE_CONTEXTS_KEY_ADDRESS + BUSTER_NODE_CONTEXTS_KEY_POINTER < 32,
               "an update's keys with a bit are past every template's number and in the table");
_Static_assert(BUSTER_NODE_CONTEXTS_KEY_COMMA > BUSTER_NODE_MACHINE_TEMPLATE_UPDATE + BUSTER_NODE_CONTEXTS_KEY_ADDRESS + BUSTER_NODE_CONTEXTS_KEY_POINTER &&
               BUSTER_NODE_CONTEXTS_KEY_CONDITIONAL < 32 && NODE_MACHINE_TEMPLATE_BRANCH < 32,
               "a comma's keys and a conditional's are past an update's and in the table, and so is a logical operator's value's template");
_Static_assert(CHECK_LOWER_TEMPLATE_GLOBAL == (CHECK_LOWER_TEMPLATE_LOCAL | 1), "a global's template's number is a local's and one bit");
_Static_assert(BUSTER_NODE_MACHINE_STANDING(3, 7) == 0x1f && BUSTER_NODE_MACHINE_STANDING(0, CHECK_CONSTANT_NONE) == 0,
               "a standing's category and its constant's kind are its low five bits, and no constant's kind is zero: the loop reads a first operand's truth from them");
_Static_assert(BUSTER_NODE_CONTEXTS_CLASS_DIVIDE == BUSTER_NODE_CONTEXTS_CLASS_BINARY + 1 && BUSTER_NODE_CONTEXTS_CLASS_CALL == BUSTER_NODE_CONTEXTS_CLASS_DIVIDE + 1 &&
               BUSTER_NODE_CONTEXTS_CLASS_UPDATE == BUSTER_NODE_CONTEXTS_CLASS_CALL + 1 && BUSTER_NODE_CONTEXTS_CLASS_LOGICAL == BUSTER_NODE_CONTEXTS_CLASS_UPDATE + 1 &&
               BUSTER_NODE_CONTEXTS_CLASS_CONDITIONAL == BUSTER_NODE_CONTEXTS_CLASS_LOGICAL + 1 && BUSTER_NODE_CONTEXTS_CLASS_COMMA <= BUSTER_NODE_CONTEXTS_CLASS_KIND,
               "the loop reads the binary classes as a range, and a class's kind is five bits");

// A 128-entry byte table by a key of seven bits, as two registers: one byte permute of two registers.
static inline __attribute__((always_inline)) __m512i node_window_table(const uint8_t* table, __m512i keys)
{
    return _mm512_permutex2var_epi8(_mm512_loadu_si512(table), keys, _mm512_loadu_si512(table + 64));
}

// Where a block's records' operands of one side are, by their distances `distances` (16 bits a record, 0 for none): each
// one's place among this block's records and the block before's, as an index of the two registers' bytes; into `near`
// the records whose operand is there, and into `far` those whose operand is before the block before.
static inline __attribute__((always_inline)) __m512i node_window_reach(const uint16_t* distances, __mmask64* near, __mmask64* far)
{
    const __m512i lanes = _mm512_set_epi8(63, 62, 61, 60, 59, 58, 57, 56, 55, 54, 53, 52, 51, 50, 49, 48, 47, 46, 45, 44, 43, 42, 41, 40, 39, 38, 37, 36, 35, 34, 33, 32,
                                          31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);
    const __m256i low = _mm512_cvtusepi16_epi8(_mm512_loadu_si512(distances));
    const __m256i high = _mm512_cvtusepi16_epi8(_mm512_loadu_si512(distances + 32));
    const __m512i distance = _mm512_inserti64x4(_mm512_castsi256_si512(low), high, 1);
    const __m512i reach = _mm512_add_epi8(lanes, _mm512_set1_epi8(64));
    const __mmask64 some = _mm512_test_epi8_mask(distance, distance);
    const __mmask64 beyond = _mm512_cmpgt_epu8_mask(distance, reach);
    *near = some & ~beyond;
    *far = some & beyond;
    return _mm512_sub_epi8(reach, distance);
}

// The operands' bytes of a column, `current` this block's and `previous` the block before's, at the places `index` of
// the records `near`: zero for a record with no operand there.
static inline __attribute__((always_inline)) __m512i node_window_operands(__m512i index, __mmask64 near, __m512i previous, __m512i current)
{
    return _mm512_maskz_permutex2var_epi8(near, previous, index, current);
}

// Where a block's records' consumers are, by their distances forward `distances` (16 bits a record, the front's
// window_ups) of the records that have one (`taken`, a byte a record, the front's ups: 0 for a record nothing takes yet,
// whose distance nothing wrote): each one's place among this block's records and the block after's, as an index of the
// two registers' bytes; into `near` the records whose consumer is there, and into `far` those whose consumer is past
// the block after.
static inline __attribute__((always_inline)) __m512i node_window_consumers(const uint16_t* distances, const uint8_t* taken, __mmask64* near, __mmask64* far)
{
    const __m512i lanes = _mm512_set_epi8(63, 62, 61, 60, 59, 58, 57, 56, 55, 54, 53, 52, 51, 50, 49, 48, 47, 46, 45, 44, 43, 42, 41, 40, 39, 38, 37, 36, 35, 34, 33, 32,
                                          31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);
    const __m256i low = _mm512_cvtusepi16_epi8(_mm512_loadu_si512(distances));
    const __m256i high = _mm512_cvtusepi16_epi8(_mm512_loadu_si512(distances + 32));
    const __m512i ups = _mm512_inserti64x4(_mm512_castsi256_si512(low), high, 1);
    // A lane's room forward: to the last record of the block after.
    const __m512i room = _mm512_sub_epi8(_mm512_set1_epi8(127), lanes);
    const __m512i takers = _mm512_loadu_si512(taken);
    const __mmask64 some = _mm512_test_epi8_mask(takers, takers);
    const __mmask64 beyond = _mm512_cmpgt_epu8_mask(ups, room);
    *near = some & ~beyond;
    *far = some & beyond;
    return _mm512_add_epi8(lanes, ups);
}

// Whether a pointer's sum, a subscript or a pointer's update at the window's record `x` has a stride
// (check_action_pointer_stride's of its first operand's conversion's type, a pointer's, which its record holds): its
// type's row's, or the type table's where the row doesn't hold it (an element past 4 GB), by a call the loop keeps out
// of its way. The stride is kept by the record (NodeFactsReader.window_c_strides): it is the value of the PTR_ADD X
// writes for the record, which X's rows' values read there.
static __attribute__((noinline, cold)) uint64_t node_contexts_stride_asked(const NodeFacts* facts, TypeId pointer)
{
    return pointer != 0 ? check_action_pointer_stride(facts->machine->types, pointer) : 0;
}

static inline __attribute__((always_inline)) bool node_contexts_stride(const struct NodeFactsReader* state, uint32_t x, uint64_t* asked)
{
    const NodeFacts* facts = &state->facts;
    const TypeId pointer = facts->records[x].converted[0];
    const uint32_t index = type_index(pointer);
    const NodeFactsRow* row = facts->rows + (index < facts->row_count ? index : facts->row_count);
    uint64_t stride = row->stride;
    if (__builtin_expect((row->flags & BUSTER_NODE_FACTS_STRIDE) == 0, 0))
    {
        stride = node_contexts_stride_asked(facts, pointer);
        *asked += 1;
    }
    state->window_c_strides[x] = stride;
    return stride != 0;
}

// Whether T's conversions of the two operands of the window's record `x`, a sum or a binary operator, are both of a
// number's class: C's rule's terms on them (node_machine_classify), for a record of a number's class whose operands'
// own classes are not both a number's. The usual arithmetic conversions are where an operand's class and its
// conversion's differ: a 128-bit integer beside a double is converted to it. (Any other operand is converted as a value
// alone, an lvalue to its value and an array or a function to a pointer (check_action_operand), and a type's class and
// facts are its value's already (check_lower_type_class): a truth's operand, a subscript's and a pointer's sum's read
// the same from the operand's own type. A class of a number's is no aggregate's and, here, no pointer's: the loop asks
// for no sum with a pointer on either side.)
static __attribute__((noinline, cold)) bool node_contexts_converted(const struct NodeFactsReader* state, uint32_t x)
{
    const NodeFacts* facts = &state->facts;
    const NodeMachine* machine = facts->machine;
    const uint32_t word_a = node_machine_word(machine, facts->records[x].converted[0]);
    const uint32_t word_b = node_machine_word(machine, facts->records[x].converted[1]);
    return node_machine_scalar_class((uint8_t)word_a) && node_machine_scalar_class((uint8_t)word_b);
}

// Whether the tasks may put the conversion of the window's record `x`, an operator's value the masks took, to its
// target `to` into its arms (node_machine_sinks, C's rule, which leaves such a record to the tasks;
// check_lower_conditional_target, #2678): the rule's own terms that a record here separates, and no more, so that
// what this lists is all the rule leaves and a little else:
// - a target that is another type than its own (the rule's first; to none, nothing is converted);
// - of an integer's class, a pointer's and a bool's among them (check_lower_conditional_sinks' first: the conversion
//   folds into a constant of such a class alone);
// - with an arm that is a constant: a logical operator's 0 or 1 is, and a conditional's arm is where it is an integer
//   constant (check_lower_integer_constant's kind; its second operand's record, and its third, the record before its
//   own).
// The rule asks more, each a term that takes: that the conversion write entries (two types of one class convert by
// none: `int` to an enumeration's), that it fold for these classes and facts, and that the arm be no lvalue, of an
// integer's class and with no effect. An expression those would take is listed here (the measurement record has
// their count on the corpora). An aggregate's target has no class's number: an argument's of a transparent union, in
// a call that is not plain, and a cast's to a union, which lists its expression itself. Out of line: one record in
// some hundreds asks.
static __attribute__((noinline, cold)) bool node_contexts_sinks(const struct NodeFactsReader* state, const NodeFront* front, uint32_t x, TypeId to)
{
    const NodeFacts* facts = &state->facts;
    bool sinks = false;
    if (to != 0 && facts->records[x].type != to)
    {
        const uint8_t target = node_machine_class(facts->machine, to);
        const uint8_t first = state->window_c_standings[x - front->window_right[x]] & 0x1c;
        const uint8_t second = state->window_c_standings[x - 1] & 0x1c;
        const bool conditional = (front->ops[x] & 0x7f) == CHECK_OP_CONDITIONAL;
        const bool constant = !conditional || first == (CHECK_CONSTANT_INTEGER << 2) || second == (CHECK_CONSTANT_INTEGER << 2);
        sinks = constant && target >= BUSTER_TYPED_CLASS_I8 && target <= BUSTER_TYPED_CLASS_U64;
    }
    return sinks;
}

// The far lanes' bytes' rows in the loop's block of its frame: a consumer's operator's class and its template (its
// code is asked of `&` alone, whose operand is its record's neighbour); a first operand's code and its type's class,
// facts and kind, its flags' byte and its standing.
#define BUSTER_NODE_CONTEXTS_FAR_CONSUMER_CLASS 0u
#define BUSTER_NODE_CONTEXTS_FAR_TEMPLATE 1u
#define BUSTER_NODE_CONTEXTS_FAR_LEFT_CODE 2u
#define BUSTER_NODE_CONTEXTS_FAR_LEFT_CLASS 3u
#define BUSTER_NODE_CONTEXTS_FAR_LEFT_FACTS 4u
#define BUSTER_NODE_CONTEXTS_FAR_LEFT_KIND 5u
#define BUSTER_NODE_CONTEXTS_FAR_LEFT_FLAGS 6u
#define BUSTER_NODE_CONTEXTS_FAR_LEFT_STANDING 7u
#define BUSTER_NODE_CONTEXTS_FAR_ROWS 8u

// The loop's counts, the shadow's: the records whose stride was read, those of them asked of the type table, the
// records whose rule read T's conversions (node_contexts_converted), and the consumers and the operands out of the two
// registers' reach, read by a load each.
typedef struct NodeContextsLoopCounts NodeContextsLoopCounts;
struct NodeContextsLoopCounts
{
    uint64_t strides;
    uint64_t asked;
    uint64_t converted;
    uint64_t far;
    // The updates C asks its plan of, and those of them the loop's masks took.
    uint64_t updates;
    uint64_t updates_taken;
    // The records the loop evaluates as branches, and those of them a statement's CONDITION.
    uint64_t branched;
    uint64_t branched_roots;
    // The records it gives an operator's value's two labels (a conditional's, an `&&`'s and an `||`'s), those of them
    // whose rule on their target's type listed their expression (node_contexts_sinks), and the commas it takes.
    uint64_t valued;
    uint64_t sunk;
    uint64_t commas;
};

// The loop over a chunk's `count` records, 64 a step, from its last block to its first: each record's template, its
// context, its target's source and why it lists its expression, into the reader's columns; and a block's masks.
//
// A function of its own, and in this order, for its registers: a step holds some twenty of them at once, and one
// spilled to a frame that is aligned to 16 bytes is written and read across two cache lines three times in four (as
// part of node_facts_reader_flush twenty were, on the 9700X: 3.2 ticks a record). So an operand's and a consumer's
// bytes become masks as soon as they are read; the one call in the loop (a stride the type's row doesn't hold) is
// where no register lives across it but a mask's, and the far lanes' bytes go through a block of the frame, which
// aligns the frame to 64 bytes for whatever is spilled all the same. It starts at a cache line's first byte, as the
// fact words' kernel does (node_facts_window), so that a build's layout doesn't move its ticks.
static __attribute__((noinline, aligned(64))) void node_contexts_window_loop(struct NodeFactsReader* state, const NodeFront* front, uint32_t count, NodeContextsLoopCounts* counts)
{
    const NodeContextsTables* tables = state->c_tables;
    const __m512i zero = _mm512_setzero_si512();
    __m512i next_codes = zero;
    __m512i next_classes = zero;
    __m512i next_keys = zero;
    // Whether the block after's first record is evaluated as branches: a record chained to its consumer, the next
    // record, has its consumer's answer.
    uint64_t carry_branched = 0;
    uint64_t valued_count = 0;
    uint64_t sunk_count = 0;
    uint64_t comma_count = 0;
    // The lowest record of a range that isn't evaluated and reaches below the block at hand (UINT32_MAX: none yet). A
    // range that ended in a block above reads as none too: its lowest record is at or above that block's first.
    uint32_t dead_low = UINT32_MAX;
    uint64_t strides = 0;
    uint64_t asked = 0;
    uint64_t converted = 0;
    uint64_t far_count = 0;
    uint64_t updates = 0;
    uint64_t updates_taken = 0;
    uint64_t branched_count = 0;
    uint64_t branched_roots = 0;
    // Whether the unit's type table has extended qualifiers, an address space among them: no type of a table without
    // any is in a segment's space (type_object_qualifiers, which check_lower_segment_override reads).
    const bool extended = state->facts.machine->types->extended;
    // The far lanes' bytes, a row a column: written a byte a lane where a block has any, and read by one masked load a
    // column. Each selected byte is written under the same lane mask before the load.
    _Alignas(64) uint8_t far_bytes[BUSTER_NODE_CONTEXTS_FAR_ROWS][64];
    const uint32_t blocks = (count + 63) / 64;
    for (uint32_t block = blocks; block != 0; block -= 1)
    {
        const uint32_t x = (block - 1) * 64;
        // The block before's bytes, read with this block's: an operand's are in either. The first block has none
        // before it and reads itself there, which no lane's operand's place names.
        const uint32_t back = x != 0 ? x - 64 : 0;
        const __m512i low_seven = _mm512_set1_epi8(0x7f);
        const __m512i codes = _mm512_and_si512(_mm512_loadu_si512(front->ops + x), low_seven);
        const __m512i classes = node_window_table(tables->classes, codes);
        const __m512i type_classes = _mm512_loadu_si512(state->window_c_classes + x);
        const __m512i facts = _mm512_loadu_si512(state->window_c_facts + x);
        const __m512i kinds = _mm512_loadu_si512(state->window_c_kinds + x);
        const __m512i standings = _mm512_loadu_si512(state->window_c_standings + x);
        const __m512i flags = _mm512_loadu_si512(state->window_c_flags + x);
        // Its consumer's operator, through its distance forward, and its operands' bytes, through their distances
        // back.
        __mmask64 near_up = 0;
        __mmask64 far_up = 0;
        const __m512i at_up = node_window_consumers(front->window_ups + x, front->ups + x, &near_up, &far_up);
        __m512i consumer_classes = _mm512_maskz_permutex2var_epi8(near_up, classes, at_up, next_classes);
        const __m512i consumer_codes = _mm512_maskz_permutex2var_epi8(near_up, codes, at_up, next_codes);
        __mmask64 near_left = 0;
        __mmask64 far_left = 0;
        __mmask64 near_right = 0;
        __mmask64 far_right = 0;
        const __m512i at_left = node_window_reach(front->window_left + x, &near_left, &far_left);
        const __m512i at_right = node_window_reach(front->window_right + x, &near_right, &far_right);
        // The far lanes of the records the kernel placed alone: another record's distances may be of records that are
        // not here (a waiting value a chunk's end kept without its row), and its answers are read by no one. A second
        // operand before the block before is a conditional's alone, whose third operand's records lie between: every
        // operator with a case here has its last operand's record right before its own. It reads as no operand.
        const __mmask64 placed = _mm512_test_epi8_mask(flags, _mm512_set1_epi8(BUSTER_NODE_CONTEXTS_PLACED));
        far_up &= placed;
        far_left &= placed;
        const __m512i previous_codes = _mm512_and_si512(_mm512_loadu_si512(front->ops + back), low_seven);
        const __m512i previous_classes = _mm512_loadu_si512(state->window_c_classes + back);
        const __m512i previous_facts = _mm512_loadu_si512(state->window_c_facts + back);
        const __m512i previous_kinds = _mm512_loadu_si512(state->window_c_kinds + back);
        const __m512i previous_standings = _mm512_loadu_si512(state->window_c_standings + back);
        const __m512i previous_flags = _mm512_loadu_si512(state->window_c_flags + back);
        __m512i left_codes = node_window_operands(at_left, near_left, previous_codes, codes);
        __m512i left_classes = node_window_operands(at_left, near_left, previous_classes, type_classes);
        __m512i left_facts = node_window_operands(at_left, near_left, previous_facts, facts);
        __m512i left_kinds = node_window_operands(at_left, near_left, previous_kinds, kinds);
        __m512i left_flags = node_window_operands(at_left, near_left, previous_flags, flags);
        __m512i left_standings = node_window_operands(at_left, near_left, previous_standings, standings);
        const __m512i right_classes = node_window_operands(at_right, near_right, previous_classes, type_classes);
        const __m512i right_facts = node_window_operands(at_right, near_right, previous_facts, facts);
        const __m512i right_kinds = node_window_operands(at_right, near_right, previous_kinds, kinds);
        const __m512i right_standings = node_window_operands(at_right, near_right, previous_standings, standings);
        // A consumer past the block after (its template is its own block's step's, stored), an operand before the
        // block before: their bytes by loads, a record at a time, into the frame's block. A control's root far from
        // its condition is most.
        if (__builtin_expect((far_up | far_left) != 0, 0))
        {
            for (uint64_t pending = far_up; pending != 0; pending &= pending - 1)
            {
                const uint32_t lane = (uint32_t)_tzcnt_u64(pending);
                const uint32_t consumer = x + lane + front->window_ups[x + lane];
                const uint8_t code = front->ops[consumer] & 0x7f;
                // Its key, which its own block's step stored (NodeFactsReader.window_c_keys: the column of C's
                // templates has no update's bits, no comma's key and no conditional's).
                far_bytes[BUSTER_NODE_CONTEXTS_FAR_CONSUMER_CLASS][lane] = tables->classes[code];
                far_bytes[BUSTER_NODE_CONTEXTS_FAR_TEMPLATE][lane] = state->window_c_keys[consumer];
            }
            for (uint64_t pending = far_left; pending != 0; pending &= pending - 1)
            {
                const uint32_t lane = (uint32_t)_tzcnt_u64(pending);
                const uint32_t operand = x + lane - front->window_left[x + lane];
                far_bytes[BUSTER_NODE_CONTEXTS_FAR_LEFT_CODE][lane] = front->ops[operand] & 0x7f;
                far_bytes[BUSTER_NODE_CONTEXTS_FAR_LEFT_CLASS][lane] = state->window_c_classes[operand];
                far_bytes[BUSTER_NODE_CONTEXTS_FAR_LEFT_FACTS][lane] = state->window_c_facts[operand];
                far_bytes[BUSTER_NODE_CONTEXTS_FAR_LEFT_KIND][lane] = state->window_c_kinds[operand];
                far_bytes[BUSTER_NODE_CONTEXTS_FAR_LEFT_FLAGS][lane] = state->window_c_flags[operand];
                far_bytes[BUSTER_NODE_CONTEXTS_FAR_LEFT_STANDING][lane] = state->window_c_standings[operand];
            }
            consumer_classes = _mm512_mask_loadu_epi8(consumer_classes, far_up, far_bytes[BUSTER_NODE_CONTEXTS_FAR_CONSUMER_CLASS]);
            left_codes = _mm512_mask_loadu_epi8(left_codes, far_left, far_bytes[BUSTER_NODE_CONTEXTS_FAR_LEFT_CODE]);
            left_classes = _mm512_mask_loadu_epi8(left_classes, far_left, far_bytes[BUSTER_NODE_CONTEXTS_FAR_LEFT_CLASS]);
            left_facts = _mm512_mask_loadu_epi8(left_facts, far_left, far_bytes[BUSTER_NODE_CONTEXTS_FAR_LEFT_FACTS]);
            left_kinds = _mm512_mask_loadu_epi8(left_kinds, far_left, far_bytes[BUSTER_NODE_CONTEXTS_FAR_LEFT_KIND]);
            left_flags = _mm512_mask_loadu_epi8(left_flags, far_left, far_bytes[BUSTER_NODE_CONTEXTS_FAR_LEFT_FLAGS]);
            left_standings = _mm512_mask_loadu_epi8(left_standings, far_left, far_bytes[BUSTER_NODE_CONTEXTS_FAR_LEFT_STANDING]);
            far_count += (uint64_t)_mm_popcnt_u64(far_up) + (uint64_t)_mm_popcnt_u64(far_left);
            near_left |= far_left;
        }
        // What its consumer says of it: its place among the consumer's operands, and whether the consumer takes that
        // operand for its address. `&x` passes its operand with no target. A record with no consumer is a root, and so
        // is an initialization's value, whose INITIALIZE is no record of the machine's. Neither asks for a mask: the
        // bytes of a consumer there is none of are zeros, an operator with no class, and no template's row says a
        // record is evaluated with no target, which is what a root is until its expression's part gives it its own.
        const __m512i places = _mm512_min_epu8(_mm512_loadu_si512(front->places + x), _mm512_set1_epi8(1));
        const __mmask64 first_place = _mm512_testn_epi8_mask(places, places);
        const __mmask64 address = first_place & _mm512_test_epi8_mask(consumer_classes, _mm512_set1_epi8((char)BUSTER_NODE_CONTEXTS_CLASS_PLACE));
        const __mmask64 addressed = _mm512_cmpeq_epi8_mask(consumer_codes, _mm512_set1_epi8(CHECK_OP_ADDRESS));
        // Its operands, as masks. C's rules read T's conversion of each operand; here an operand's own type stands for
        // it where the two say the same: an array's and a function's are a pointer's, whose class their types have
        // already (the lowering's classes: an address's), with no pointer's fact; two operands of a number's class
        // convert to a number's. Where they may not, the record's rule reads the conversions (below).
        const __m512i arrays = _mm512_set1_epi8(BUSTER_TYPE_KIND_ARRAY);
        const __m512i decayed = _mm512_set1_epi8(BUSTER_TYPE_KIND_FUNCTION - BUSTER_TYPE_KIND_ARRAY);
        const __mmask64 left_decays = near_left & _mm512_cmple_epu8_mask(_mm512_sub_epi8(left_kinds, arrays), decayed);
        const __mmask64 right_decays = near_right & _mm512_cmple_epu8_mask(_mm512_sub_epi8(right_kinds, arrays), decayed);
        const __mmask64 left_pointer = left_decays | _mm512_test_epi8_mask(left_facts, _mm512_set1_epi8(BUSTER_CHECK_LOWER_TYPE_POINTER));
        const __mmask64 right_pointer = right_decays | _mm512_test_epi8_mask(right_facts, _mm512_set1_epi8(BUSTER_CHECK_LOWER_TYPE_POINTER));
        const __mmask64 left_atomic = _mm512_test_epi8_mask(left_facts, _mm512_set1_epi8(BUSTER_CHECK_LOWER_TYPE_ATOMIC));
        const __m512i scalars = _mm512_set1_epi8(BUSTER_TYPED_CLASS_F64 - BUSTER_TYPED_CLASS_I8);
        const __m512i i8 = _mm512_set1_epi8(BUSTER_TYPED_CLASS_I8);
        const __mmask64 left_scalar = _mm512_cmple_epu8_mask(_mm512_sub_epi8(left_classes, i8), scalars);
        const __mmask64 right_scalar = _mm512_cmple_epu8_mask(_mm512_sub_epi8(right_classes, i8), scalars);
        // A truth's operand, and a branched operator's: C asks that its conversion's type be one a truth's entries test
        // (node_machine_testable): no aggregate, of a class below a vector's that is not void's. Its conversion's
        // facts are its own type's (node_contexts_converted's comment), and the aggregate's is a mask: a __float128
        // has it, and its record a template all the same. The class asks no mask. The checker takes only a scalar
        // there (check_action_logic), and a scalar whose record has no such class is a half-precision float's: no
        // record of that type has a template (each template's mask asks a number's class of its record, a wrapper's
        // one below a vector's, and a plain call's result is a plain scalar's), and C evaluates the operand. So the
        // expression is refused for the operand where C refuses it for its consumer.
        const __mmask64 left_tested = _mm512_testn_epi8_mask(left_facts, _mm512_set1_epi8(BUSTER_CHECK_LOWER_TYPE_AGGREGATE));
        const __mmask64 right_tested = _mm512_testn_epi8_mask(right_facts, _mm512_set1_epi8(BUSTER_CHECK_LOWER_TYPE_AGGREGATE));
        const __mmask64 named = near_left & _mm512_cmpeq_epi8_mask(left_codes, _mm512_set1_epi8(CHECK_OP_VALUE)) &
                                _mm512_test_epi8_mask(left_flags, _mm512_set1_epi8(BUSTER_NODE_CONTEXTS_OBJECT));
        const __mmask64 named_global = _mm512_test_epi8_mask(left_flags, _mm512_set1_epi8(BUSTER_NODE_CONTEXTS_SYMBOL));
        const __mmask64 right_constant = near_right & _mm512_cmpeq_epi8_mask(_mm512_and_si512(right_standings, _mm512_set1_epi8(0x1c)),
                                                                             _mm512_set1_epi8(CHECK_CONSTANT_INTEGER << 2));
        // The record's own bits.
        const __mmask64 unfit = _mm512_test_epi8_mask(facts, _mm512_set1_epi8((char)(BUSTER_CHECK_LOWER_TYPE_BIT_INT | BUSTER_CHECK_LOWER_TYPE_COMPLEX)));
        const __mmask64 address_value = _mm512_test_epi8_mask(facts, _mm512_set1_epi8(BUSTER_CHECK_LOWER_TYPE_ADDRESS));
        const __mmask64 atomic = _mm512_test_epi8_mask(facts, _mm512_set1_epi8(BUSTER_CHECK_LOWER_TYPE_ATOMIC));
        // A class of a number's: I8 to F64, the integers of 64 bits or fewer, float and double.
        const __mmask64 numeric = _mm512_cmple_epu8_mask(_mm512_sub_epi8(type_classes, i8), scalars);
        // A number's class is no aggregate's, whose value is its address (the fact above); void's class is void's alone.
        // A wrapper's class is below a vector's, or its type an aggregate's: T's record of an aggregate has an address's
        // class (node_machine_type), where its type's word has none.
        const __mmask64 small = _mm512_cmplt_epu8_mask(type_classes, _mm512_set1_epi8(BUSTER_TYPED_CLASS_VECTOR)) |
                                _mm512_test_epi8_mask(facts, _mm512_set1_epi8(BUSTER_CHECK_LOWER_TYPE_AGGREGATE));
        const __mmask64 void_type = placed & _mm512_testn_epi8_mask(type_classes, type_classes);
        const __mmask64 union_type = _mm512_cmpeq_epi8_mask(kinds, _mm512_set1_epi8(BUSTER_TYPE_KIND_UNION));
        const __m512i categories = _mm512_and_si512(standings, _mm512_set1_epi8(0x03));
        const __mmask64 rvalue = _mm512_cmpeq_epi8_mask(categories, _mm512_set1_epi8(CHECK_RVALUE));
        const __mmask64 lvalue = _mm512_cmpeq_epi8_mask(categories, _mm512_set1_epi8(CHECK_LVALUE));
        const __m512i constants = _mm512_and_si512(standings, _mm512_set1_epi8(0x1c));
        const __mmask64 constant = _mm512_test_epi8_mask(constants, constants);
        const __mmask64 address_constant = _mm512_cmpeq_epi8_mask(constants, _mm512_set1_epi8(CHECK_CONSTANT_ADDRESS << 2));
        const __mmask64 wide = _mm512_test_epi8_mask(standings, _mm512_set1_epi8((char)BUSTER_NODE_MACHINE_WIDE));
        const __mmask64 effects = _mm512_test_epi8_mask(flags, _mm512_set1_epi8(BUSTER_NODE_CONTEXTS_EFFECTS));
        const __mmask64 vector = _mm512_test_epi8_mask(flags, _mm512_set1_epi8(BUSTER_NODE_CONTEXTS_VECTOR));
        const __mmask64 has_object = _mm512_test_epi8_mask(flags, _mm512_set1_epi8(BUSTER_NODE_CONTEXTS_OBJECT));
        const __mmask64 symbol = _mm512_test_epi8_mask(flags, _mm512_set1_epi8(BUSTER_NODE_CONTEXTS_SYMBOL));
        // Its operator's case.
        const __m512i class_kinds = _mm512_and_si512(classes, _mm512_set1_epi8(BUSTER_NODE_CONTEXTS_CLASS_KIND));
        const __mmask64 is_value = _mm512_cmpeq_epi8_mask(class_kinds, _mm512_set1_epi8(BUSTER_NODE_CONTEXTS_CLASS_VALUE));
        const __mmask64 is_wrap = _mm512_cmpeq_epi8_mask(class_kinds, _mm512_set1_epi8(BUSTER_NODE_CONTEXTS_CLASS_WRAP));
        const __mmask64 is_member = _mm512_cmpeq_epi8_mask(class_kinds, _mm512_set1_epi8(BUSTER_NODE_CONTEXTS_CLASS_MEMBER));
        const __mmask64 is_sum = _mm512_cmpeq_epi8_mask(class_kinds, _mm512_set1_epi8(BUSTER_NODE_CONTEXTS_CLASS_SUM));
        const __mmask64 is_subscript = _mm512_cmpeq_epi8_mask(class_kinds, _mm512_set1_epi8(BUSTER_NODE_CONTEXTS_CLASS_SUBSCRIPT));
        const __mmask64 is_unary = _mm512_cmpeq_epi8_mask(class_kinds, _mm512_set1_epi8(BUSTER_NODE_CONTEXTS_CLASS_UNARY));
        const __mmask64 is_truth = _mm512_cmpeq_epi8_mask(class_kinds, _mm512_set1_epi8(BUSTER_NODE_CONTEXTS_CLASS_TRUTH));
        const __mmask64 is_assign = _mm512_cmpeq_epi8_mask(class_kinds, _mm512_set1_epi8(BUSTER_NODE_CONTEXTS_CLASS_ASSIGN));
        const __mmask64 is_address = _mm512_cmpeq_epi8_mask(class_kinds, _mm512_set1_epi8(BUSTER_NODE_CONTEXTS_CLASS_ADDRESS));
        const __mmask64 is_dereference = _mm512_cmpeq_epi8_mask(class_kinds, _mm512_set1_epi8(BUSTER_NODE_CONTEXTS_CLASS_DEREFERENCE));
        const __mmask64 is_divide = _mm512_cmpeq_epi8_mask(class_kinds, _mm512_set1_epi8(BUSTER_NODE_CONTEXTS_CLASS_DIVIDE));
        // The two binary classes are one range, and so are the kinds the loop has no case for, with no class at all.
        const __mmask64 is_binary = _mm512_cmple_epu8_mask(_mm512_sub_epi8(class_kinds, _mm512_set1_epi8(BUSTER_NODE_CONTEXTS_CLASS_BINARY)),
                                                           _mm512_set1_epi8(BUSTER_NODE_CONTEXTS_CLASS_DIVIDE - BUSTER_NODE_CONTEXTS_CLASS_BINARY));
        const __mmask64 is_call = _mm512_cmpeq_epi8_mask(class_kinds, _mm512_set1_epi8(BUSTER_NODE_CONTEXTS_CLASS_CALL));
        const __mmask64 is_update = _mm512_cmpeq_epi8_mask(class_kinds, _mm512_set1_epi8(BUSTER_NODE_CONTEXTS_CLASS_UPDATE));
        const __mmask64 is_logical = _mm512_cmpeq_epi8_mask(class_kinds, _mm512_set1_epi8(BUSTER_NODE_CONTEXTS_CLASS_LOGICAL));
        const __mmask64 is_uncased = _mm512_testn_epi8_mask(class_kinds, class_kinds);
        const __mmask64 both_scalar = left_scalar & right_scalar;
        // The templates, C's rules (node_machine_classify's) as a mask a template: what every operator shares comes
        // first (a type no template takes, an atomic lvalue's read, a folded constant), then an operator's own case,
        // the cases apart by the operator's class. C's `plain` is the class alone: a type with the aggregate's fact has
        // an address's and no number's class (check_lower_type_class, check_lower_type_facts).
        const __mmask64 folds = constant & ~effects & ((rvalue & ~address) | ((address | address_value) & address_constant));
        // A folded constant is taken where it is an address's or its class is a number's (node_machine_constant_takes:
        // an integer's class of 64 bits or fewer, and float's or double's for a floating constant).
        const __m512i integers = _mm512_set1_epi8(BUSTER_TYPED_CLASS_U64 - BUSTER_TYPED_CLASS_I8);
        const __mmask64 integral = _mm512_cmple_epu8_mask(_mm512_sub_epi8(type_classes, i8), integers);
        const __mmask64 floating_constant = _mm512_cmpeq_epi8_mask(constants, _mm512_set1_epi8(CHECK_CONSTANT_FLOATING << 2));
        const __mmask64 takes = address_constant | integral | (floating_constant & numeric);
        const __mmask64 shared = unfit | (~address & lvalue & atomic);
        const __mmask64 free = ~shared & ~folds;
        const __mmask64 placed_value = address | address_value;
        const __mmask64 named_value = is_value & ~placed_value & lvalue & has_object & numeric;
        const __mmask64 storable = is_assign & numeric & ~left_atomic;
        const __mmask64 made_constant = ~shared & folds & takes;
        // The leaves the loop lists and makes no template for: one of void's type and one read for its address that
        // is no address constant, which C's rules give a template of their own (0 of the census's 763,283 evaluated
        // records); and one with an effect (a compound literal's that is not static, which C's rules refuse).
        const __mmask64 odd_leaf = is_value & (effects | (free & (void_type | placed_value)));
        // A name's value: a global's template's number is a local's and one bit, its symbol's.
        const __mmask64 made_name = free & named_value;
        const __mmask64 made_global = made_name & symbol;
        const __mmask64 made_pass = free & ((is_wrap & small) | is_address);
        const __mmask64 made_dereference = free & is_dereference & (placed_value | numeric);
        // A member's record is a struct's or a union's member (check_action_member diagnoses any other) or a vector's
        // elements, which no template takes.
        const __mmask64 made_member = free & is_member & ~vector & (placed_value | numeric);
        const __mmask64 made_unary = free & is_unary & numeric;
        // An operator evaluated as branches (node_machine_classify_branches: `!`, `&&` and `||` under a condition,
        // which make no value), and an `&&` or an `||` that is a value (node_machine_logical). C branches a record from
        // the root down; here each record says it of itself, from its own bytes, its first operand's, its consumer's
        // class and its place there, and for a record whose answer is its consumer's, the next record, from that
        // record's:
        // - a branch operator (node_machine_branch_operator) is a `!`, an `&&` or an `||` that is no folded constant
        //   (`free`), and for `&&` and `||` one whose first operand's truth is not known: no constant, or an object's
        //   lvalue, whose constant is its address (check_action_truth). That its value is an int's and no lvalue is the
        //   checker's, which takes no vector's `!`, `&&` or `||`. An `&&` or an `||` that is none lists its expression
        //   (a folded one, and one whose condition's truth may be known, whose templates are not made here);
        // - a branch operator is entered where it is the first operand of an `&&` or an `||`: branched, whatever that
        //   operator is. Under a branched one it is its operand, which a branch operator is as branches. Under a
        //   value's it is the value's condition, which is branches where an `&&` or an `||` is under its `!`s
        //   (node_machine_branch_condition's peel): an `&&` and an `||` are, and a value whose first operand is a `!`
        //   lists its expression, since a `!` is not such a condition by itself;
        // - a `!` is deep where an `&&` or an `||` that is a branch operator is under it, with `!`s alone between: the
        //   runs of `!` records right after such an operator's record, a `!`'s operand being the record before it, by
        //   one sum's carries. A deep `!` and such an `&&` or `||` are entered too as a statement's CONDITION's
        //   operand and as a conditional's condition, where a `!` that is not deep is a value. A CONDITION over a deep
        //   `!` is branched itself, two levels away: the `!` lists its expression. And a run that starts a block is
        //   not seen there: the block before's last record lists its expression where it is a branch operator under a
        //   `!`;
        // - a branch operator is chained where it is an `&&`'s or an `||`'s second operand or a `!`'s operand: it has
        //   the answer of its consumer, the next record. The answers go down each run of chained records by two
        //   doubling steps, three records up, a block's last record's from the block after's first; a record with
        //   three chained ones after it lists its expression;
        // - any other is a value: an `&&` or an `||` with its two labels (below), a `!` with its truth's template;
        // - a CONDITION is branched where its operand is `&&` or `||`. One that is no branch operator after all is no
        //   branched record at its own lane, and lists its expression there.
        // A branched record's operands are evaluated, each a branch in its turn or a value C tests: its template is
        // none where an operand is none a truth's entries test (above). A `!` and a CONDITION have no second operand,
        // whose bytes then read zero.
        const __m512i consumer_kinds = _mm512_and_si512(consumer_classes, _mm512_set1_epi8(BUSTER_NODE_CONTEXTS_CLASS_KIND));
        const __mmask64 consumer_logical = _mm512_cmpeq_epi8_mask(consumer_kinds, _mm512_set1_epi8(BUSTER_NODE_CONTEXTS_CLASS_LOGICAL));
        const __mmask64 consumer_truth = _mm512_cmpeq_epi8_mask(consumer_kinds, _mm512_set1_epi8(BUSTER_NODE_CONTEXTS_CLASS_TRUTH));
        const __mmask64 consumer_condition = consumer_truth & _mm512_test_epi8_mask(consumer_classes, _mm512_set1_epi8(BUSTER_NODE_CONTEXTS_CLASS_CONDITION));
        const __mmask64 consumer_not = consumer_truth & ~consumer_condition;
        const __mmask64 consumer_conditional = _mm512_cmpeq_epi8_mask(consumer_kinds, _mm512_set1_epi8(BUSTER_NODE_CONTEXTS_CLASS_CONDITIONAL));
        const __mmask64 consumer_comma = _mm512_cmpeq_epi8_mask(consumer_kinds, _mm512_set1_epi8(BUSTER_NODE_CONTEXTS_CLASS_COMMA));
        const __mmask64 consumer_wrap = _mm512_cmpeq_epi8_mask(consumer_kinds, _mm512_set1_epi8(BUSTER_NODE_CONTEXTS_CLASS_WRAP));
        const __mmask64 is_condition = is_truth & _mm512_test_epi8_mask(classes, _mm512_set1_epi8(BUSTER_NODE_CONTEXTS_CLASS_CONDITION));
        const __mmask64 is_not = is_truth & ~is_condition;
        const __mmask64 left_logical = _mm512_cmple_epu8_mask(_mm512_sub_epi8(left_codes, _mm512_set1_epi8(CHECK_OP_LOGICAL_AND)), _mm512_set1_epi8(1));
        const __mmask64 left_not = _mm512_cmpeq_epi8_mask(left_codes, _mm512_set1_epi8(CHECK_OP_NOT));
        // A first operand's standing: its category and its constant's kind are five bits of one byte.
        const __mmask64 left_variable = _mm512_testn_epi8_mask(left_standings, _mm512_set1_epi8(0x1c));
        const __mmask64 left_placed = _mm512_cmpeq_epi8_mask(_mm512_and_si512(left_standings, _mm512_set1_epi8(0x1f)), _mm512_set1_epi8((CHECK_CONSTANT_ADDRESS << 2) | CHECK_LVALUE));
        const __mmask64 left_object = _mm512_test_epi8_mask(left_flags, _mm512_set1_epi8(BUSTER_NODE_CONTEXTS_OBJECT));
        const __mmask64 left_unknown = left_variable | (left_placed & left_object);
        const uint64_t operator_logical = _cvtmask64_u64(is_logical & free & left_unknown);
        const uint64_t operator_not = _cvtmask64_u64(is_not & free);
        const uint64_t deep_not = operator_not & ~(operator_not + (operator_logical << 1));
        const uint64_t operators = operator_logical | operator_not;
        const uint64_t deep = operator_logical | deep_not;
        // The block's last record, a branch operator under a `!`, the block after's first record, whose run that block
        // did not see.
        const uint64_t edge = operators & _cvtmask64_u64(consumer_not) & (UINT64_C(1) << 63);
        const uint64_t entered = (operators & _cvtmask64_u64(consumer_logical & first_place)) | (deep & _cvtmask64_u64(consumer_condition | (consumer_conditional & first_place)));
        const uint64_t chained = operators & _cvtmask64_u64((consumer_logical & ~first_place) | consumer_not);
        uint64_t generate = entered | (chained & (carry_branched << 63));
        uint64_t propagate = chained;
        generate |= propagate & (generate >> 1);
        propagate &= propagate >> 1;
        generate |= propagate & (generate >> 2);
        propagate &= propagate >> 2;
        const uint64_t unreached = propagate;
        const __mmask64 branched_root = is_condition & left_logical;
        const __mmask64 branched = _cvtu64_mask64(generate) | branched_root;
        const __mmask64 made_branches = branched & left_tested & right_tested;
        const __mmask64 valued_logical = _cvtu64_mask64(operator_logical & ~generate & ~unreached);
        const __mmask64 unbranched = (is_logical & ~_cvtu64_mask64(operator_logical)) | (_cvtu64_mask64(deep_not) & consumer_condition) | (valued_logical & left_not) |
                                     _cvtu64_mask64(unreached | edge);
        // An operator's value (node_machine_logical, which C asks of one that is not read for its address: none is,
        // since an operator that takes its first operand's address asks an lvalue or a struct's of the checker, and
        // these are neither): an `&&` or an `||` that is a branch operator and not branched, whose first operand is
        // no `!` (its class is a number's, an int's: the checker's), and a conditional of
        // a number's class or of void's whose condition's truth is not known, where its condition's conversion's type
        // is one a truth's entries test, and an `&&`'s or an `||`'s second operand's is too. Its two labels and its
        // operands' segments are X's to write. A conditional whose condition's truth may be known lists its
        // expression, and so does a folded one (C's rules give them other templates: one arm is not evaluated). Three
        // of C's terms are a record at a time, below: the conversion its consumer puts into its arms
        // (node_contexts_sinks), a conditional of one operand twice, and, in its expression's part, a root's.
        const __mmask64 is_conditional = _mm512_cmpeq_epi8_mask(class_kinds, _mm512_set1_epi8(BUSTER_NODE_CONTEXTS_CLASS_CONDITIONAL));
        const __mmask64 open_conditional = is_conditional & free & left_unknown;
        const __mmask64 made_conditional = open_conditional & (numeric | void_type) & left_tested;
        const __mmask64 made_valued = made_conditional | (valued_logical & ~left_not & left_tested & right_tested);
        // A comma (node_machine_classify's case): of a number's class, its second operand alone. One read for its
        // address passes that to its second operand, which no mask here gives: it lists its expression, whatever
        // template it has. Its own context is the discard's where its target is void (node_machine_child_context),
        // which is its consumer's to say: a comma's first operand's is; a wrapper's operand's and a conditional's
        // arm's are where that record's type is void's (a byte's load a comma, below: a cast's type has no qualifier,
        // and void's class is void's alone); and a comma's second operand's is where that comma is discarded, its own
        // consumer's answer: listed. A comma that is no free record lists its expression too: a folded one has the constant's template
        // with the discard's context, which writes it no entry (`(void)(1, 2)`), and the masks give a discard to the
        // commas they take alone.
        const __mmask64 is_comma = _mm512_cmpeq_epi8_mask(class_kinds, _mm512_set1_epi8(BUSTER_NODE_CONTEXTS_CLASS_COMMA));
        const __mmask64 made_comma = free & is_comma & numeric;
        const __mmask64 uncommaed = is_comma & (~free | address | (consumer_comma & ~first_place));
        // A truth's value is an int's (check_action_logic): its own class asks no mask.
        const __mmask64 made_truth = free & is_truth & left_tested;
        const __mmask64 either_pointer = left_pointer | right_pointer;
        // (C's sum asks that its record be no vector's element. Only a subscript's and a member's record are
        // (check_vector_subscript, check_vector_member), and one below a sum has no template, which refuses the
        // expression whatever the sum's is.)
        const __mmask64 own_binary = free & numeric & both_scalar & ((is_sum & ~either_pointer) | is_binary);
        const __mmask64 made_store_local = free & storable & named & ~named_global;
        const __mmask64 made_store_global = free & storable & named & named_global;
        const __mmask64 made_store = free & storable & ~named;
        // A call (node_machine_classify_call, which C runs of a call before any rule the operators share): a plain one,
        // by what the kernel's call rule says of its callee's prototype and of its arguments' conversions, evaluated for
        // its value. A call whose arguments' conversions the kernel didn't all make has no targets to give them here,
        // and lists its expression.
        const __mmask64 converted_call = _mm512_test_epi8_mask(flags, _mm512_set1_epi8(BUSTER_NODE_CONTEXTS_CONVERTED));
        const __mmask64 plain_call = _mm512_test_epi8_mask(flags, _mm512_set1_epi8(BUSTER_NODE_CONTEXTS_PLAIN));
        const __mmask64 made_call = is_call & ~address & plain_call;
        // Its callee, where the lowering calls it directly (node_machine_direct_callee): no lvalue, an address constant
        // with no offset, of an object that has a symbol, and no effect. C doesn't evaluate it. Whether an object has a
        // symbol is a name's leaf's line's: another record with an object's address, as a callee, lists its expression.
        const __mmask64 callee = first_place & _mm512_cmpeq_epi8_mask(consumer_kinds, _mm512_set1_epi8(BUSTER_NODE_CONTEXTS_CLASS_CALL));
        const __mmask64 offset = _mm512_test_epi8_mask(standings, _mm512_set1_epi8(BUSTER_NODE_MACHINE_OFFSET));
        const __mmask64 called = callee & ~lvalue & address_constant & ~offset & has_object & ~effects;
        const __mmask64 direct = called & symbol;
        // What lists its expression: an operator with no case here, and the records the cases don't take. A compound
        // division or remainder by an integer constant is listed as a plain one is.
        // (An integer's alone: C asks a power of two's shift or mask of a computation of an integer's class, which a
        // destination of one and a divisor that is an integer constant have, and no other update.)
        const __mmask64 update_divisor = integral & right_constant & _mm512_test_epi8_mask(classes, _mm512_set1_epi8(BUSTER_NODE_CONTEXTS_CLASS_QUOTIENT));
        const __mmask64 divisor = (is_divide & right_constant) | update_divisor;
        _mm512_storeu_si512(state->window_c_reasons + x, class_kinds);
        // An update: C asks its plan (node_machine_update_plan) of a record of a number's class that is not read for its
        // address (node_machine_classify's case). No update's record is read for its address: its value is no lvalue
        // and no struct's, and each operator that takes its first operand's address asks for one. And a record the
        // kernel didn't place has no class's byte. The masks take what the plan takes, each of its terms from a byte:
        // - a destination that is no atomic's. That it is an lvalue is the checker's, which takes no update of anything
        //   else. Its class and its other facts are the update's record's own, a number's with no _BitInt's and no
        //   complex fact (`free` and `numeric`): an update's type is its destination's value's. A bit-field's record is
        //   a value the columns don't hold whole, which lists the expression, and a vector's element has no template of
        //   its own, which refuses it, whatever the update's is. A pointer's is by its type's fact, `left_pointer`'s:
        //   an array's and a function's are no update's;
        // - a destination that is a name's is no register variable's, by its use's flag (a name's leaf has its object,
        //   which the unit knows: the flags' byte's comment);
        // - a second operand whose own type is of a number's class, unless the update is `++`, `--` or a pointer's,
        //   whose second operand C converts to long. T's conversions of the two, which the plan reads, are then of such
        //   a class too, with none of the facts the plan excludes: the usual arithmetic conversions of two such types,
        //   or an integer promotion's (a shift's), as the binary operators' masks take them above; and a pointer's
        //   value's type is its own without its qualifiers. (Of those facts in the second operand's own type: a record
        //   with a _BitInt's or the complex one has no template, and no more has an atomic lvalue's read, which refuses
        //   the expression; the checker takes no atomic value that is no lvalue as an operand; and an aggregate's class
        //   is no number's.)
        // Its shape is the front's: a destination, and a second operand right before a compound's record. A pointer's
        // stride and, in a unit with a type in a space of its own, the destination's type's space are a record at a
        // time (below). A mask only takes: an update the masks don't take lists its expression, and what C's plan makes
        // of it is the reference's to say.
        const __mmask64 update_asked = free & is_update & numeric & ~update_divisor;
        const __mmask64 increment = _mm512_test_epi8_mask(classes, _mm512_set1_epi8(BUSTER_NODE_CONTEXTS_CLASS_INCREMENT));
        const __mmask64 left_register = _mm512_test_epi8_mask(left_flags, _mm512_set1_epi8((char)BUSTER_NODE_CONTEXTS_REGISTER));
        const __mmask64 left_named = _mm512_cmpeq_epi8_mask(left_codes, _mm512_set1_epi8(CHECK_OP_VALUE));
        const __mmask64 update_masked = update_asked & ~left_atomic & ~(left_named & left_register) & (increment | left_pointer | right_scalar);
        // A pointer's sum, a subscript and a pointer's update: its stride is its type's row's, a record at a time, here
        // where the step's other bytes are masks. An update's pointer's type is its destination's
        // (node_machine_update_plan's `stored`), and T's conversion of it, which the update's record holds as a sum's
        // does its first operand's, is that type without its qualifiers: the same element's.
        const __mmask64 sums = (is_sum | is_subscript) & free & placed & ~vector & (~is_subscript | placed_value | numeric);
        uint64_t pointed = (sums & left_pointer & ~right_pointer) | (update_masked & left_pointer);
        for (uint64_t pending = pointed; pending != 0; pending &= pending - 1)
        {
            const uint32_t lane = (uint32_t)_tzcnt_u64(pending);
            pointed &= ~((uint64_t)(node_contexts_stride(state, x + lane, &asked) ? 0 : 1) << lane);
            strides += 1;
        }
        // The records whose operands' own classes don't decide: a sum or a binary operator of a number's class with an
        // operand whose class is no number's, and no pointer on either side (two pointers' difference and an integer
        // plus a pointer have no template by either reading). C's rule on T's two conversions, a record at a time.
        uint64_t asked_binary = 0;
        for (uint64_t pending = free & placed & numeric & ~both_scalar & ((is_sum & ~either_pointer) | is_binary); pending != 0; pending &= pending - 1)
        {
            const uint32_t lane = (uint32_t)_tzcnt_u64(pending);
            asked_binary |= (uint64_t)(node_contexts_converted(state, x + lane) ? 1 : 0) << lane;
            converted += 1;
        }
        const __mmask64 made_pointer = _cvtu64_mask64(pointed);
        const __mmask64 made_binary = own_binary | _cvtu64_mask64(asked_binary);
        // The destination's type's space (check_lower_segment_override), in a unit that has one at all: a record at a
        // time too. None of the corpora's units has.
        uint64_t spaced = 0;
        if (__builtin_expect(extended, 0))
        {
            for (uint64_t pending = update_masked; pending != 0; pending &= pending - 1)
            {
                const uint32_t lane = (uint32_t)_tzcnt_u64(pending);
                const TypeId stored = state->facts.records[x + lane - front->window_left[x + lane]].type;
                spaced |= (uint64_t)(check_lower_segment_override(state->facts.machine->types, stored) != 0 ? 1 : 0) << lane;
            }
        }
        const __mmask64 made_update = _cvtu64_mask64(update_masked & ~spaced & (~(uint64_t)left_pointer | pointed));
        updates += (uint64_t)_mm_popcnt_u64(update_asked);
        updates_taken += (uint64_t)_mm_popcnt_u64(made_update);
        branched_count += (uint64_t)_mm_popcnt_u64(placed & made_branches);
        branched_roots += (uint64_t)_mm_popcnt_u64(placed & made_branches & is_condition);
        // A comma under a wrapper or a conditional's arm: whether that record's type is void's, a record at a time
        // (most commas are a statement's, under its cast to void, and a comma is one record in some hundreds).
        uint64_t voided = 0;
        for (uint64_t pending = made_comma & (near_up | far_up) & (consumer_wrap | (consumer_conditional & ~first_place)); pending != 0; pending &= pending - 1)
        {
            const uint32_t lane = (uint32_t)_tzcnt_u64(pending);
            const uint32_t consumer = x + lane + front->window_ups[x + lane];
            voided |= (uint64_t)(state->window_c_classes[consumer] == BUSTER_TYPED_CLASS_VOID ? 1 : 0) << lane;
        }
        // (A comma the masks don't take has no answer anyone reads: its expression is listed or refused.)
        const __mmask64 discarded = (is_comma & consumer_comma & first_place) | _cvtu64_mask64(voided);
        // A record's context: an address's by the mask, and a discarded comma's a byte each (no such comma is read for
        // its address).
        _mm512_storeu_si512(state->window_c_contexts + x, _mm512_maskz_mov_epi8(address, _mm512_set1_epi8(BUSTER_CHECK_LOWER_TEMPLATE_ADDRESS)));
        for (uint64_t pending = discarded; pending != 0; pending &= pending - 1)
        {
            state->window_c_contexts[x + (uint32_t)_tzcnt_u64(pending)] = BUSTER_CHECK_LOWER_TEMPLATE_DISCARD;
        }
        // (A record the kernel didn't place is in no expression decided here. It has no class's byte, so it is no
        // comma the masks take and no conditional, and it may be an `&&` or an `||` with the value's mask, which the
        // count and the step below leave out.)
        valued_count += (uint64_t)_mm_popcnt_u64(placed & made_valued);
        comma_count += (uint64_t)_mm_popcnt_u64(made_comma);
        const __mmask64 listed = placed & (is_uncased | (is_wrap & union_type) | divisor | wide | odd_leaf | (is_call & ~converted_call) | (called & ~is_value) |
                                           (update_asked & ~made_update) | unbranched | (is_conditional & ~open_conditional) | uncommaed);
        // A template's number is four bits: each bit's records, one mask a bit. A comma's is a wrapper's.
        const __mmask64 passed = made_pass | made_comma;
        const __mmask64 bit0 = made_constant | made_global | made_dereference | made_unary | made_binary | made_store_local | made_store;
        const __mmask64 bit1 = passed | made_dereference | made_truth | made_binary | made_store_global | made_store;
        const __mmask64 bit2 = made_name | passed | made_dereference | made_pointer | made_store_local | made_store_global | made_store;
        const __mmask64 bit3 = made_member | made_unary | made_truth | made_binary | made_pointer | made_store_local | made_store_global | made_store;
        const __m512i own_templates = _mm512_or_si512(_mm512_or_si512(_mm512_maskz_mov_epi8(bit0, _mm512_set1_epi8(1)), _mm512_maskz_mov_epi8(bit1, _mm512_set1_epi8(2))),
                                                      _mm512_or_si512(_mm512_maskz_mov_epi8(bit2, _mm512_set1_epi8(4)), _mm512_maskz_mov_epi8(bit3, _mm512_set1_epi8(8))));
        // A call's template's number and an update's are past those four bits. No other mask has a call's record, and
        // a pointer's update's, which `made_pointer` has too, takes its own number here.
        const __m512i called_templates = _mm512_mask_mov_epi8(own_templates, made_call, _mm512_set1_epi8(BUSTER_NODE_MACHINE_TEMPLATE_CALL));
        const __m512i updated_templates = _mm512_mask_mov_epi8(called_templates, made_update, _mm512_set1_epi8(BUSTER_NODE_MACHINE_TEMPLATE_UPDATE));
        const __m512i branched_templates = _mm512_mask_mov_epi8(updated_templates, made_branches, _mm512_set1_epi8(NODE_MACHINE_TEMPLATE_BRANCHES));
        const __m512i templates = _mm512_mask_mov_epi8(branched_templates, made_valued, _mm512_set1_epi8(NODE_MACHINE_TEMPLATE_BRANCH));
        // Its key, which its operands' rows are read by: its template's number, and for an update its destination's two
        // bits: one that is no name's is evaluated for its address, and a pointer's second operand is made a long. A
        // conditional's, a comma's and a discarded comma's are keys of their own.
        const __m512i placed_keys = _mm512_mask_add_epi8(templates, made_update & ~left_named, templates, _mm512_set1_epi8(BUSTER_NODE_CONTEXTS_KEY_ADDRESS));
        const __m512i update_keys = _mm512_mask_add_epi8(placed_keys, made_update & left_pointer, placed_keys, _mm512_set1_epi8(BUSTER_NODE_CONTEXTS_KEY_POINTER));
        // (The three keys of their own are made beside the templates, from the masks alone, and take their records'
        // places in one step.)
        const __m512i comma_keys = _mm512_mask_mov_epi8(_mm512_set1_epi8(BUSTER_NODE_CONTEXTS_KEY_COMMA), discarded, _mm512_set1_epi8(BUSTER_NODE_CONTEXTS_KEY_COMMA_DISCARD));
        const __m512i own_keys = _mm512_mask_mov_epi8(comma_keys, made_conditional, _mm512_set1_epi8(BUSTER_NODE_CONTEXTS_KEY_CONDITIONAL));
        const __m512i keys = _mm512_mask_mov_epi8(update_keys, made_comma | made_conditional, own_keys);
        // What its consumer's key gives it: whether it is evaluated, and its target's source.
        const __m512i near_keys = _mm512_maskz_permutex2var_epi8(near_up, keys, at_up, next_keys);
        const __m512i consumer_keys = _mm512_mask_loadu_epi8(near_keys, far_up, far_bytes[BUSTER_NODE_CONTEXTS_FAR_TEMPLATE]);
        const __m512i rows = _mm512_permutexvar_epi8(_mm512_or_si512(_mm512_slli_epi16(consumer_keys, 1), places), _mm512_load_si512(tables->rows));
        const __mmask64 evaluates = _mm512_test_epi8_mask(rows, _mm512_set1_epi8((char)BUSTER_NODE_CONTEXTS_EVALUATES)) & ~direct;
        // An operand of `&x` has no target, and no more has a record evaluated as branches.
        const __m512i sources = _mm512_maskz_mov_epi8(~(addressed | branched), _mm512_and_si512(rows, _mm512_set1_epi8(BUSTER_NODE_CONTEXTS_SOURCE)));
        _mm512_storeu_si512(state->window_c_templates + x, templates);
        _mm512_storeu_si512(state->window_c_keys + x, keys);
        _mm512_storeu_si512(state->window_c_sources + x, sources);
        // An operator's value's terms that are a record at a time (one record in some hundreds): a conditional of one
        // operand twice (`a ?: b`, which C's rule leaves to the tasks), and the conversion to its target, the type its
        // consumer's row names, where the tasks put it into its arms (node_contexts_sinks). Either lists its
        // expression. A root's target is its expression's part's to give, and to ask.
        uint64_t sunk = 0;
        for (uint64_t pending = placed & made_valued; pending != 0; pending &= pending - 1)
        {
            const uint32_t lane = (uint32_t)_tzcnt_u64(pending);
            const uint32_t r = x + lane;
            // (A record nothing takes yet has no distance and no source: its own line is read for nothing.)
            const uint32_t consumer = front->ups[r] != 0 ? r + front->window_ups[r] : r;
            const uint32_t source = state->window_c_sources[r];
            const NodeFactsRecord* line = state->facts.records + consumer;
            const TypeId target = source == BUSTER_NODE_CONTEXTS_SOURCE_GIVEN ? line->converted[front->places[r] != 0 ? 1 : 0] :
                                  source == BUSTER_NODE_CONTEXTS_SOURCE_NODE  ? line->type :
                                  source == BUSTER_NODE_CONTEXTS_SOURCE_LONG  ? BUSTER_TYPE_LONG :
                                  source == BUSTER_NODE_CONTEXTS_SOURCE_OWN   ? (TypeId)state->facts.records[r].spare : 0;
            // (To void, a comma's first operand's target, nothing is put into the arms: no target reads as none. And
            // a logical operator's two operands are two records.)
            const bool twice = front->window_left[r] == front->window_right[r];
            sunk |= (uint64_t)(twice || node_contexts_sinks(state, front, r, target) ? 1 : 0) << lane;
        }
        sunk_count += (uint64_t)_mm_popcnt_u64(sunk);
        // The records it takes out with it where it isn't evaluated: its subtree's, the front's sizes'. And those a
        // record of a block after took out.
        uint64_t dead = dead_low <= x ? ~UINT64_C(0) : dead_low < x + 64 ? ~UINT64_C(0) << (dead_low - x) : 0;
        for (uint64_t pending = placed & ~evaluates & ~is_value; pending != 0; pending &= pending - 1)
        {
            const uint32_t lane = (uint32_t)_tzcnt_u64(pending);
            const uint32_t size = front->sizes[x + lane];
            const uint32_t low = x + lane + 1 - (size < x + lane + 1 ? size : x + lane + 1);
            const uint64_t below = (UINT64_C(1) << lane) - 1;
            dead |= low <= x ? below : below & (~UINT64_C(0) << (low - x));
            dead_low = low < x && low < dead_low ? low : dead_low;
        }
        const __mmask64 alive = placed & evaluates & ~dead;
        uint64_t* masks = state->window_c_masks + BUSTER_NODE_CONTEXTS_MASKS * (uint64_t)(block - 1);
        masks[BUSTER_NODE_CONTEXTS_MASK_ALIVE] = alive;
        masks[BUSTER_NODE_CONTEXTS_MASK_NONE] = alive & _mm512_testn_epi8_mask(templates, templates);
        masks[BUSTER_NODE_CONTEXTS_MASK_LISTED] = listed | sunk;
        masks[BUSTER_NODE_CONTEXTS_MASK_DIVISOR] = placed & divisor;
        masks[BUSTER_NODE_CONTEXTS_MASK_WIDE] = placed & wide;
        next_codes = codes;
        next_classes = classes;
        next_keys = keys;
        carry_branched = generate & 1;
    }
    counts->strides += strides;
    counts->asked += asked;
    counts->converted += converted;
    counts->far += far_count;
    counts->updates += updates;
    counts->updates_taken += updates_taken;
    counts->branched += branched_count;
    counts->branched_roots += branched_roots;
    counts->valued += valued_count;
    counts->sunk += sunk_count;
    counts->commas += comma_count;
}

// The targets of a chunk's `count` records, 16 a step: the type each evaluated record's source names. T's conversion of
// the operand and the consumer's own type are both in the consumer's record's line: one gather of the lines, at the
// one's place or the other's.
static __attribute__((noinline)) void node_contexts_window_targets(struct NodeFactsReader* state, const NodeFront* front, uint32_t count)
{
    const uint8_t* const records = (const uint8_t*)state->facts.records;
    const __m512i lanes = _mm512_set_epi32(15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);
    for (uint32_t x = 0; x < count; x += 16)
    {
        const uint64_t alive_word = state->window_c_masks[BUSTER_NODE_CONTEXTS_MASKS * (uint64_t)(x / 64) + BUSTER_NODE_CONTEXTS_MASK_ALIVE];
        const __mmask16 alive = (__mmask16)(alive_word >> (x & 63));
        const __m512i sources = _mm512_cvtepu8_epi32(_mm_loadu_si128((const __m128i*)(const void*)(state->window_c_sources + x)));
        const __m512i ups = _mm512_cvtepu16_epi32(_mm256_loadu_si256((const __m256i*)(const void*)(front->window_ups + x)));
        const __m512i places = _mm512_cvtepu8_epi32(_mm_loadu_si128((const __m128i*)(const void*)(front->places + x)));
        const __mmask16 given = _mm512_mask_cmpeq_epi32_mask(alive, sources, _mm512_set1_epi32(BUSTER_NODE_CONTEXTS_SOURCE_GIVEN));
        const __mmask16 node = _mm512_mask_cmpeq_epi32_mask(alive, sources, _mm512_set1_epi32(BUSTER_NODE_CONTEXTS_SOURCE_NODE));
        const __mmask16 index = _mm512_mask_cmpeq_epi32_mask(alive, sources, _mm512_set1_epi32(BUSTER_NODE_CONTEXTS_SOURCE_LONG));
        // The consumer's line, and in it its conversion of this operand (its first two operands' alone: the rows give
        // a third none) or its own type.
        const __m512i consumers = _mm512_add_epi32(_mm512_add_epi32(_mm512_set1_epi32((int32_t)x), lanes), ups);
        const __m512i converted = _mm512_add_epi32(_mm512_set1_epi32((int32_t)offsetof(NodeFactsRecord, converted)),
                                                   _mm512_slli_epi32(_mm512_min_epu32(places, _mm512_set1_epi32(1)), 2));
        // A call's argument's is in its own record's line, its last quadword.
        const __mmask16 own = _mm512_mask_cmpeq_epi32_mask(alive, sources, _mm512_set1_epi32(BUSTER_NODE_CONTEXTS_SOURCE_OWN));
        const __m512i lines = _mm512_mask_mov_epi32(consumers, own, _mm512_add_epi32(_mm512_set1_epi32((int32_t)x), lanes));
        __m512i fields = _mm512_mask_mov_epi32(converted, node, _mm512_set1_epi32((int32_t)offsetof(NodeFactsRecord, type)));
        fields = _mm512_mask_mov_epi32(fields, own, _mm512_set1_epi32((int32_t)offsetof(NodeFactsRecord, spare)));
        const __m512i offsets = _mm512_add_epi32(_mm512_slli_epi32(lines, 6), fields);
        const __m512i gathered = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), given | node | own, offsets, records, 1);
        const __mmask16 voided = _mm512_mask_cmpeq_epi32_mask(alive, sources, _mm512_set1_epi32(BUSTER_NODE_CONTEXTS_SOURCE_VOID));
        const __m512i indexed = _mm512_mask_mov_epi32(gathered, index, _mm512_set1_epi32((int32_t)BUSTER_TYPE_LONG));
        const __m512i targets = _mm512_mask_mov_epi32(indexed, voided, _mm512_set1_epi32((int32_t)BUSTER_TYPE_VOID));
        _mm512_storeu_si512(state->window_c_targets + x, targets);
    }
}

// Whether any of a block's mask `mask`'s bits is set for the `size` records from the window's record `first`. A range of
// 64 records or fewer is in two words at most, read as one with no branch on where it lies; a longer one a word at a
// time.
static inline __attribute__((always_inline)) bool node_contexts_window_any(const uint64_t* masks, uint32_t mask, uint32_t first, uint32_t size)
{
    const uint64_t* words = masks + BUSTER_NODE_CONTEXTS_MASKS * (uint64_t)(first / 64) + mask;
    const uint32_t lane = first & 63;
    const uint64_t low = words[0] >> lane;
    // The next word's bits come in above this one's: by two shifts, as a shift by 64 is none.
    const uint64_t high = words[BUSTER_NODE_CONTEXTS_MASKS] << 1 << (63 - lane);
    const uint32_t short_size = size < 64 ? size : 64;
    const uint64_t taken = short_size == 64 ? ~UINT64_C(0) : (UINT64_C(1) << short_size) - 1;
    uint64_t any = (low | high) & taken;
    if (__builtin_expect(size > 64, 0))
    {
        uint32_t at = first + 64;
        const uint32_t end = first + size;
        while (at < end)
        {
            const uint32_t from = at & 63;
            const uint32_t room = 64 - from;
            const uint32_t take = end - at < room ? end - at : room;
            const uint64_t bits = take == 64 ? ~UINT64_C(0) : ((UINT64_C(1) << take) - 1) << from;
            any |= masks[BUSTER_NODE_CONTEXTS_MASKS * (uint64_t)(at / 64) + mask] & bits;
            at += take;
        }
    }
    return any != 0;
}

// Why the window's record `x` lists its expression (BUSTER_NODE_CONTEXTS_REASON_*), or none: by its operator's class,
// then by its block's masks. A `!` lists for nothing but its value's width or an `&&` or an `||` it is over or under
// (the loop's `unbranched`). The shadow's count.
static uint8_t node_contexts_window_reason(const struct NodeFactsReader* state, uint32_t x)
{
    const uint64_t* masks = state->window_c_masks + BUSTER_NODE_CONTEXTS_MASKS * (uint64_t)(x / 64);
    const uint64_t bit = UINT64_C(1) << (x & 63);
    const uint8_t class = state->window_c_reasons[x];
    uint8_t reason = BUSTER_NODE_CONTEXTS_REASON_NONE;
    if ((masks[BUSTER_NODE_CONTEXTS_MASK_LISTED] & bit) != 0)
    {
        reason = class == BUSTER_NODE_CONTEXTS_CLASS_CALL                     ? BUSTER_NODE_CONTEXTS_REASON_CALL :
                 class == BUSTER_NODE_CONTEXTS_CLASS_LOGICAL                  ? BUSTER_NODE_CONTEXTS_REASON_LOGICAL :
                 class == BUSTER_NODE_CONTEXTS_CLASS_CONDITIONAL              ? BUSTER_NODE_CONTEXTS_REASON_LOGICAL :
                 class == BUSTER_NODE_CONTEXTS_CLASS_UPDATE                   ? BUSTER_NODE_CONTEXTS_REASON_UPDATE :
                 class == BUSTER_NODE_CONTEXTS_CLASS_COMMA                    ? BUSTER_NODE_CONTEXTS_REASON_COMMA :
                 (masks[BUSTER_NODE_CONTEXTS_MASK_DIVISOR] & bit) != 0        ? BUSTER_NODE_CONTEXTS_REASON_DIVISION :
                 (masks[BUSTER_NODE_CONTEXTS_MASK_WIDE] & bit) != 0           ? BUSTER_NODE_CONTEXTS_REASON_WIDE :
                 class == BUSTER_NODE_CONTEXTS_CLASS_TRUTH                    ? BUSTER_NODE_CONTEXTS_REASON_LOGICAL :
                                                                                BUSTER_NODE_CONTEXTS_REASON_OPERATOR;
    }
    return reason;
}

// The reference's answers and the window's for the batch's expression `e`, decided here: whether its verdict or any of
// its records' answers differs. C on T's columns gave the first (node_facts_window_expression kept them by the front's
// record): the records it evaluated, and of each its context, its target and its template.
static bool node_contexts_window_compare(const struct NodeFactsReader* state, const NodeFront* front, uint32_t e, bool refused, uint64_t* record_differences)
{
    const uint32_t record = state->records[e];
    const uint32_t operand = state->operands[e];
    const uint32_t size = front->sizes[operand];
    const uint32_t first = operand + 1 - size;
    const uint32_t count = state->counts[e];
    const bool reference_refused = state->c_ran[e] == BUSTER_NODE_CONTEXTS_RAN_REFUSED;
    uint64_t wrong = 0;
    for (uint32_t r = 0; !refused && !reference_refused && r < count; r += 1)
    {
        const uint32_t x = r < size ? first + r : record;
        const bool alive = (state->window_c_masks[BUSTER_NODE_CONTEXTS_MASKS * (uint64_t)(x / 64) + BUSTER_NODE_CONTEXTS_MASK_ALIVE] >> (x & 63) & 1) != 0;
        const bool evaluated = (state->place_c_states[x] & BUSTER_NODE_MACHINE_EVALUATED) != 0;
        const bool same = alive == evaluated && (!alive || (state->window_c_templates[x] == state->place_c_templates[x] &&
                                                            state->window_c_contexts[x] == state->place_c_contexts[x] &&
                                                            state->window_c_targets[x] == state->place_c_targets[x]));
        wrong += !same;
    }
    *record_differences += wrong;
    return refused != reference_refused || wrong != 0;
}

// An expression's part, for each of the batch's expressions the kernel placed and the reference ran on: it is decided
// here unless one of its records is listed; a decided one is refused where an evaluated record has no template; and its
// root's target is the one it is given.
static __attribute__((noinline)) void node_contexts_window_part(struct NodeFactsReader* state, const NodeFront* front)
{
    for (uint32_t e = 0; e < state->expression_count; e += 1)
    {
        const bool run = state->placed[e] != 0 && state->c_ran[e] != BUSTER_NODE_CONTEXTS_RAN_NONE;
        uint8_t verdict = 0;
        if (run)
        {
            const uint32_t record = state->records[e];
            const uint32_t operand = state->operands[e];
            const uint32_t size = front->sizes[operand];
            const uint32_t first = operand + 1 - size;
            const bool rooted = state->counts[e] > size;
            // Its root's record, where it has one of its own (a control's), may be apart from its operand's: its bit.
            const uint64_t* root_masks = state->window_c_masks + BUSTER_NODE_CONTEXTS_MASKS * (uint64_t)(record / 64);
            const uint64_t root_bit = (uint64_t)(rooted ? 1 : 0) << (record & 63);
            const bool listed = node_contexts_window_any(state->window_c_masks, BUSTER_NODE_CONTEXTS_MASK_LISTED, first, size) |
                                ((root_masks[BUSTER_NODE_CONTEXTS_MASK_LISTED] & root_bit) != 0);
            const bool refused = node_contexts_window_any(state->window_c_masks, BUSTER_NODE_CONTEXTS_MASK_NONE, first, size) |
                                 ((root_masks[BUSTER_NODE_CONTEXTS_MASK_NONE] & root_bit) != 0);
            // Its root's target: the one its consumer gives it (an initialization's object's type), where the loop
            // gave a root none.
            state->window_c_targets[rooted ? record : operand] = state->c_targets[e];
            // A root that is an operator's value, an initialization's: C's rule on its target (node_contexts_sinks),
            // which the loop asks of a record with a consumer. (A root of its own record is a control's or a
            // wrapper's, no such operator.)
            // It lists the expression as the loop's does: by the record's bit, which the shadow's count of the
            // reasons reads.
            const bool sunk = !rooted && state->window_c_templates[operand] == NODE_MACHINE_TEMPLATE_BRANCH && node_contexts_sinks(state, front, operand, state->c_targets[e]);
            if (__builtin_expect(sunk, 0))
            {
                state->window_c_masks[BUSTER_NODE_CONTEXTS_MASKS * (uint64_t)(operand / 64) + BUSTER_NODE_CONTEXTS_MASK_LISTED] |= UINT64_C(1) << (operand & 63);
            }
            verdict = (uint8_t)(listed || sunk ? BUSTER_NODE_CONTEXTS_LISTED : refused ? BUSTER_NODE_CONTEXTS_REFUSED : BUSTER_NODE_CONTEXTS_TAKEN);
        }
        state->c_verdicts[e] = verdict;
    }
}

// C on the window for the chunk the kernel has just made in place: the loop and the targets over its records, then each
// of the batch's expressions' part. Each decided expression is then compared with the reference, and each listed one
// counted by its first record's reason. The shadow runs the three sweeps a second time over the same chunk, for their
// ticks with their code and their constants at hand: they give the same answers.
static void node_contexts_window_chunk(struct NodeFactsReader* state, const NodeFront* front)
{
    NodeFrontTypes* types = state->types;
    uint64_t* counts = types->facts_counts;
    const uint32_t count = front->count;
    NodeWindowCounter* const counter = node_window_counting != NULL && node_window_counting->family == NODE_WINDOW_CONTEXTS ? node_window_counting : NULL;
    uint64_t first_sample[4];
    uint64_t second_sample[4];
    const uint64_t t0 = __rdtsc();
    first_sample[0] = node_window_stamp(counter);
    NodeContextsLoopCounts loop_counts = { 0 };
    node_contexts_window_loop(state, front, count, &loop_counts);
    state->c_updates_now = loop_counts.updates_taken;
    state->c_branches_now = loop_counts.branched - loop_counts.branched_roots + loop_counts.valued;
    const uint64_t t1 = __rdtsc();
    first_sample[1] = node_window_stamp(counter);
    node_contexts_window_targets(state, front, count);
    const uint64_t t2 = __rdtsc();
    first_sample[2] = node_window_stamp(counter);
    node_contexts_window_part(state, front);
    const uint64_t t3 = __rdtsc();
    first_sample[3] = node_window_stamp(counter);
    second_sample[0] = first_sample[3];
    // The shadow's own: the second run, the counts, and each decided expression against the reference.
    NodeContextsLoopCounts again = { 0 };
    node_contexts_window_loop(state, front, count, &again);
    const uint64_t t4 = __rdtsc();
    second_sample[1] = node_window_stamp(counter);
    node_contexts_window_targets(state, front, count);
    const uint64_t t5 = __rdtsc();
    second_sample[2] = node_window_stamp(counter);
    node_contexts_window_part(state, front);
    const uint64_t t6 = __rdtsc();
    second_sample[3] = node_window_stamp(counter);
    if (counter != NULL)
    {
        node_window_collect(counter, 0, first_sample, 3, count);
        node_window_collect(counter, 1, second_sample, 3, count);
    }
    uint64_t decided_records = 0;
    for (uint32_t e = 0; e < state->expression_count; e += 1)
    {
        const uint8_t verdict = state->c_verdicts[e];
        if (verdict != 0)
        {
            const uint32_t record = state->records[e];
            const uint32_t operand = state->operands[e];
            const uint32_t size = front->sizes[operand];
            const uint32_t first = operand + 1 - size;
            counts[NODE_FACTS_COUNT_CONTEXTS_EXPRESSIONS] += 1;
            counts[NODE_FACTS_COUNT_CONTEXTS_RECORDS] += state->counts[e];
            if (verdict == BUSTER_NODE_CONTEXTS_LISTED)
            {
                // Its first record's reason, its root's last.
                uint8_t reason = BUSTER_NODE_CONTEXTS_REASON_NONE;
                for (uint32_t r = 0; reason == BUSTER_NODE_CONTEXTS_REASON_NONE && r < state->counts[e]; r += 1)
                {
                    reason = node_contexts_window_reason(state, r < size ? first + r : record);
                }
                types->contexts_listed[reason] += 1;
            }
            else
            {
                uint64_t record_differences = 0;
                const bool differs = node_contexts_window_compare(state, front, e, verdict == BUSTER_NODE_CONTEXTS_REFUSED, &record_differences);
                decided_records += state->counts[e];
                counts[NODE_FACTS_COUNT_CONTEXTS_DECIDED] += 1;
                counts[NODE_FACTS_COUNT_CONTEXTS_REFUSED] += verdict == BUSTER_NODE_CONTEXTS_REFUSED;
                counts[NODE_FACTS_COUNT_CONTEXTS_DIFFERENCES] += differs;
                counts[NODE_FACTS_COUNT_CONTEXTS_RECORD_DIFFERENCES] += record_differences;
                counts[NODE_FACTS_COUNT_CONTEXTS_REFERENCE_TICKS] += state->c_ticks[e];
                if (differs && types->contexts_shown < BUSTER_NODE_FRONT_LISTED)
                {
                    types->contexts_tokens[types->contexts_shown] = front->tokens[record];
                    types->contexts_shown += 1;
                }
            }
        }
    }
    counts[NODE_FACTS_COUNT_CONTEXTS_DECIDED_RECORDS] += decided_records;
    counts[NODE_FACTS_COUNT_CONTEXTS_SWEPT] += count;
    counts[NODE_FACTS_COUNT_CONTEXTS_STRIDES] += loop_counts.strides;
    counts[NODE_FACTS_COUNT_CONTEXTS_STRIDES_ASKED] += loop_counts.asked;
    counts[NODE_FACTS_COUNT_CONTEXTS_CONVERTED] += loop_counts.converted;
    counts[NODE_FACTS_COUNT_CONTEXTS_FAR] += loop_counts.far;
    counts[NODE_FACTS_COUNT_CONTEXTS_UPDATES] += loop_counts.updates;
    counts[NODE_FACTS_COUNT_CONTEXTS_UPDATES_TAKEN] += loop_counts.updates_taken;
    counts[NODE_FACTS_COUNT_CONTEXTS_BRANCHED] += loop_counts.branched;
    counts[NODE_FACTS_COUNT_CONTEXTS_BRANCHED_ROOTS] += loop_counts.branched_roots;
    counts[NODE_FACTS_COUNT_CONTEXTS_VALUED] += loop_counts.valued;
    counts[NODE_FACTS_COUNT_CONTEXTS_SUNK] += loop_counts.sunk;
    counts[NODE_FACTS_COUNT_CONTEXTS_COMMAS] += loop_counts.commas;
    counts[NODE_FACTS_COUNT_CONTEXTS_LOOP_TICKS] += t1 - t0;
    counts[NODE_FACTS_COUNT_CONTEXTS_TARGET_TICKS] += t2 - t1;
    counts[NODE_FACTS_COUNT_CONTEXTS_PART_TICKS] += t3 - t2;
    counts[NODE_FACTS_COUNT_CONTEXTS_LATER_LOOP_TICKS] += t4 - t3;
    counts[NODE_FACTS_COUNT_CONTEXTS_LATER_TARGET_TICKS] += t5 - t4;
    counts[NODE_FACTS_COUNT_CONTEXTS_LATER_PART_TICKS] += t6 - t5;
}

#endif
