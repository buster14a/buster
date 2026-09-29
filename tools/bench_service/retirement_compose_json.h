/* #881-E canonical JSON for the result composer (Linux service code only).
 *
 * Ownership: lane E. Declarations for tools/bench_service/retirement_compose_json.c:
 * a strict, non-recursive JSON reader and the Python-exact canonical writer
 * (json.dumps(sort_keys=True, separators=(",", ":"), ensure_ascii=False)).
 *
 * Entry points:
 *   tp_retirement_compose_execution_context  the validator's _execution_context
 *   tp_retirement_compose_float_repr         Python's repr(float)
 *   tp_retirement_compose_json_parse         the tree the composer queries
 *   tp_retirement_compose_json_canonical     a whole document's canonical bytes
 *   tp_retirement_compose_allocate           a refusing arena allocation
 */
#ifndef BUSTER_BENCH_SERVICE_RETIREMENT_COMPOSE_JSON_H
#define BUSTER_BENCH_SERVICE_RETIREMENT_COMPOSE_JSON_H
#include <buster/lib/arena.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __linux__
/* Python's repr(float): sign, 17 digits, point, exponent and NUL. */
#define TP_RETIREMENT_COMPOSE_REPR_BYTES 40u

/* json.dumps(_execution_context(binding, raw_measurements_sha256),
 * sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode() for a
 * strict JSON binding (duplicate keys, lone surrogates, non-finite numbers
 * and control bytes refused). The bytes are allocated from `arena`, whose
 * reservation is checked before every allocation. */
int tp_retirement_compose_execution_context(unsigned char const* binding, size_t length,
    char const* raw_measurements_sha256, Arena* arena, char** output, size_t* output_length);
/* Python's repr(float) for a finite double, as json.dumps writes it. */
int tp_retirement_compose_float_repr(double value, char output[TP_RETIREMENT_COMPOSE_REPR_BYTES]);

/* The canonical-JSON tree the composer also uses to check the adapter's
 * output structurally. Nodes are stored in document order; an object's
 * members are linked in sorted key order. */
typedef enum TpComposeJsonKind
{
    TP_COMPOSE_JSON_NULL,
    TP_COMPOSE_JSON_FALSE,
    TP_COMPOSE_JSON_TRUE,
    TP_COMPOSE_JSON_INTEGER,
    TP_COMPOSE_JSON_FLOAT,
    TP_COMPOSE_JSON_STRING,
    TP_COMPOSE_JSON_ARRAY,
    TP_COMPOSE_JSON_OBJECT
} TpComposeJsonKind;

typedef struct TpComposeJsonNode
{
    unsigned kind, parent, next, first, last, count;
    /* String bytes (decoded) or integer digits; the member key (decoded). */
    char const* text;
    char const* key;
    uint32_t length, key_length;
    double number;
} TpComposeJsonNode;

typedef struct TpComposeJson
{
    TpComposeJsonNode* nodes;
    unsigned count;
} TpComposeJson;

#define TP_COMPOSE_JSON_NONE 0xffffffffu
int tp_retirement_compose_json_parse(unsigned char const* bytes, size_t length, Arena* arena, TpComposeJson* json);
/* The member of object `node` named `key`, or TP_COMPOSE_JSON_NONE. */
unsigned tp_retirement_compose_json_member(TpComposeJson const* json, unsigned node, char const* key);
/* Whether object `node` has exactly `count` members, named by `keys`. */
int tp_retirement_compose_json_keys(TpComposeJson const* json, unsigned node, char const* const* keys, unsigned count);
/* The canonical bytes of a whole document. */
int tp_retirement_compose_json_canonical(unsigned char const* bytes, size_t length, Arena* arena, char** output,
                                         size_t* output_length);
/* An allocation that refuses (returns NULL) instead of exhausting `arena`. */
void* tp_retirement_compose_allocate(Arena* arena, uint64_t bytes);
#endif
#endif
