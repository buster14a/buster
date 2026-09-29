/* Research-only allocation dataflow checker. No production allocator helpers.
 * ra_check solves must-value facts; ra_check_certificate checks an explicit
 * inductive certificate. The frozen source/CFG and ABI entry contract are input.
 * All supported resources are full-width GPRs or actual eight-byte frame ranges.
 * The source verifier's reference/dominance/CFG guarantee is trusted. CALL64
 * assumes the independently supplied ABI mask is complete, the call returns
 * normally and it cannot modify private spill bytes. Unmodeled calls are excluded
 * by the real-MIR adapter. Callee-save/return observations must be explicit rows.
 */
#ifndef BUSTER_RESEARCH_RA_CHECKER_H
#define BUSTER_RESEARCH_RA_CHECKER_H

#include <stdint.h>

#define RA_CHECK_VERSION 1u
#define RA_CHECK_LIMIT 64u
#define RA_CHECK_MAX_ROWS 4096u
#define RA_CHECK_MAX_EDITS 4096u
#define RA_CHECK_MAX_EDGES 256u
#define RA_CHECK_NONE 255u

typedef enum RaCheckStatus
{
    RA_CHECK_VALID,
    RA_CHECK_INVALID,
    RA_CHECK_NOT_PROVEN,
    RA_CHECK_UNCOVERED,
    RA_CHECK_CERTIFICATE_REJECTED
} RaCheckStatus;

typedef enum RaCheckOpcode
{
    RA_CHECK_CONST64,
    RA_CHECK_COPY64,
    RA_CHECK_ADD64,
    RA_CHECK_SUB64,
    RA_CHECK_AND64,
    RA_CHECK_OR64,
    RA_CHECK_XOR64,
    RA_CHECK_IMUL64,
    RA_CHECK_NEG64,
    RA_CHECK_EARLY64,
    RA_CHECK_CALL64,
    RA_CHECK_OBSERVE64,
    RA_CHECK_UNSUPPORTED
} RaCheckOpcode;

typedef enum RaCheckLocationKind
{
    RA_CHECK_GPR,
    RA_CHECK_STACK
} RaCheckLocationKind;

typedef struct RaCheckLocation
{
    uint32_t kind;
    uint32_t index; /* GPR resource id, or actual frame byte offset. */
    uint32_t width_bits;
} RaCheckLocation;

typedef struct RaCheckRow
{
    uint32_t opcode;
    uint32_t width_bits;
    uint64_t payload; /* CONST literal or immutable source operation payload. */
    uint64_t call_clobber_gprs; /* Independently supplied ABI contract. */
    uint8_t definition; /* NONE is permitted only for COPY, OBSERVE or CALL. */
    uint8_t use_count;
    uint8_t uses[3];
    uint8_t fixed_definition; /* GPR resource id, or NONE. */
    uint8_t fixed_uses[3];
    uint8_t tied_use; /* Definition location must equal this use's location. */
    uint8_t early_clobber;
} RaCheckRow;

typedef struct RaCheckAllocatedRow
{
    uint32_t opcode;
    uint32_t width_bits;
    uint64_t payload;
    uint8_t definition_location;
    uint8_t use_locations[3];
    uint32_t first_before;
    uint32_t before_count;
    uint32_t first_after;
    uint32_t after_count;
} RaCheckAllocatedRow;

typedef enum RaCheckEditKind
{
    RA_CHECK_MOVE64,
    RA_CHECK_SPILL64,
    RA_CHECK_RELOAD64,
    RA_CHECK_REMATERIALIZE64
} RaCheckEditKind;

typedef enum RaCheckEditPhase
{
    RA_CHECK_BEFORE,
    RA_CHECK_AFTER,
    RA_CHECK_EDGE
} RaCheckEditPhase;

typedef struct RaCheckEdit
{
    uint32_t kind;
    uint32_t phase;
    uint32_t owner; /* Original row index, or edge index for EDGE. */
    uint32_t width_bits;
    uint8_t source_location; /* NONE for rematerialization. */
    uint8_t destination_location;
    uint32_t recipe_row; /* Original CONST64 row for rematerialization. */
    uint64_t payload; /* Must equal recipe literal for rematerialization. */
} RaCheckEdit;

typedef struct RaCheckBlock
{
    uint32_t first_row;
    uint32_t row_count;
    uint32_t parameter_count;
    const uint8_t *parameters;
} RaCheckBlock;

typedef struct RaCheckEdge
{
    uint32_t source_block;
    uint32_t destination_block;
    uint32_t argument_count;
    const uint8_t *arguments; /* Parallel source symbols for destination params. */
    uint32_t first_edit;
    uint32_t edit_count;
} RaCheckEdge;

typedef struct RaCheckBinding
{
    uint8_t symbol;
    uint8_t location;
} RaCheckBinding;

typedef struct RaCheckProgram
{
    uint32_t version;
    uint32_t symbol_count;
    uint32_t location_count;
    const RaCheckLocation *locations;
    uint32_t row_count;
    const RaCheckRow *rows;
    uint32_t allocated_row_count;
    const RaCheckAllocatedRow *allocated_rows;
    uint32_t edit_count;
    const RaCheckEdit *edits;
    uint32_t block_count;
    const RaCheckBlock *blocks;
    uint32_t edge_count;
    const RaCheckEdge *edges;
    uint32_t entry_block;
    uint32_t binding_count;
    const RaCheckBinding *bindings;
    uint32_t frame_size;
} RaCheckProgram;

typedef struct RaCheckState
{
    uint64_t facts[RA_CHECK_LIMIT]; /* Location -> must-equal symbolic names. */
    uint64_t available_symbols; /* Original definitions reached on every path. */
} RaCheckState;

typedef struct RaCheckCertificate
{
    uint32_t present;
    uint32_t version;
    uint32_t state_count; /* Exactly program block_count; unreachable states zero. */
    const RaCheckState *block_entries;
} RaCheckCertificate;

typedef struct RaCheckWorkspace
{
    RaCheckState entries[RA_CHECK_LIMIT];
    RaCheckState exits[RA_CHECK_LIMIT];
    uint8_t reachable[RA_CHECK_LIMIT];
    uint8_t edit_seen[RA_CHECK_MAX_EDITS];
} RaCheckWorkspace;

typedef struct RaCheckResult
{
    RaCheckStatus status;
    const char *reason;
    uint32_t block;
    uint32_t row;
    uint64_t transfer_rows;
    uint64_t transfer_edits;
    uint64_t edge_transfers;
    uint64_t sweeps;
} RaCheckResult;

RaCheckResult ra_check(const RaCheckProgram *program, RaCheckWorkspace *workspace);
RaCheckResult ra_check_certificate(const RaCheckProgram *program, RaCheckWorkspace *workspace, const RaCheckCertificate *certificate);

#endif
