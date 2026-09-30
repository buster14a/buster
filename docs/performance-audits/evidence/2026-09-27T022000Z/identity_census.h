#pragma once

// DISPOSABLE identity census (never committed to production). Counts identity
// work -- hashes, hashed bytes, string compares, compared bytes, interner
// probes -- keyed by compiler phase, work kind and the two innermost return
// addresses of the counted routine. Enabled only with -DBUSTER_IDENTITY_CENSUS=1.
#include <buster/lib/base.h>

#ifndef BUSTER_IDENTITY_CENSUS
#define BUSTER_IDENTITY_CENSUS 0
#endif

#if BUSTER_IDENTITY_CENSUS
typedef enum IdentityCensusPhase
{
    IDENTITY_CENSUS_PHASE_OTHER,
    IDENTITY_CENSUS_PHASE_PREPROCESS,
    IDENTITY_CENSUS_PHASE_SYNTAX,
    IDENTITY_CENSUS_PHASE_SEMANTIC,
    IDENTITY_CENSUS_PHASE_LOWER,
    IDENTITY_CENSUS_PHASE_IR_PREPARE,
    IDENTITY_CENSUS_PHASE_CODEGEN,
    IDENTITY_CENSUS_PHASE_OBJECT,
    IDENTITY_CENSUS_PHASE_WRITE,
    IDENTITY_CENSUS_PHASE_LINK,
    IDENTITY_CENSUS_PHASE_COUNT,
} IdentityCensusPhase;

typedef enum IdentityCensusKind
{
    IDENTITY_CENSUS_STRING_EQUAL,       // calls, bytes compared, aux = equal results
    IDENTITY_CENSUS_HASH64,             // calls, bytes hashed
    IDENTITY_CENSUS_FNV,                // c_macro_name_hash: calls, bytes hashed
    IDENTITY_CENSUS_SYMBOL_INTERN,      // calls, key bytes, aux = slots probed
    IDENTITY_CENSUS_SYMBOL_MIDDLE,      // middle compares, middle bytes
    IDENTITY_CENSUS_SYMBOL_INSERT,      // new ids
    IDENTITY_CENSUS_OBJECT_NAME_HASH,   // object_symbol_name_hash
    IDENTITY_CENSUS_LINK_NAME_HASH,     // link-side name hashes
    IDENTITY_CENSUS_PROBE,              // table probe steps, aux = table tag
    IDENTITY_CENSUS_MAP_ROW,            // translation-map rows written/read
    IDENTITY_CENSUS_STRUCTURAL,         // structural comparison steps
    IDENTITY_CENSUS_CONSTRUCT,          // canonical/duplicate objects constructed
    IDENTITY_CENSUS_KIND_COUNT,
} IdentityCensusKind;

BUSTER_F_DECL void identity_census_phase_set(IdentityCensusPhase phase);
BUSTER_F_DECL void identity_census_record(IdentityCensusKind kind, void* site0, void* site1, u64 bytes, u64 aux);
BUSTER_F_DECL void identity_census_record_tag(IdentityCensusKind kind, u32 tag, u64 bytes, u64 aux);

#define IDENTITY_CENSUS_NOINLINE __attribute__((noinline))
#define IDENTITY_CENSUS_SITE0 __builtin_return_address(0)
#define IDENTITY_CENSUS_SITE1 __builtin_return_address(1)
#define IDENTITY_CENSUS_PHASE(phase) identity_census_phase_set(IDENTITY_CENSUS_PHASE_##phase)
#define IDENTITY_CENSUS_RECORD(kind, bytes, aux) identity_census_record(IDENTITY_CENSUS_##kind, IDENTITY_CENSUS_SITE0, IDENTITY_CENSUS_SITE1, (bytes), (aux))
#define IDENTITY_CENSUS_TAG(kind, tag, bytes, aux) identity_census_record_tag(IDENTITY_CENSUS_##kind, (tag), (bytes), (aux))
#else
#define IDENTITY_CENSUS_NOINLINE
#define IDENTITY_CENSUS_PHASE(phase) ((void)0)
#define IDENTITY_CENSUS_RECORD(kind, bytes, aux) ((void)0)
#define IDENTITY_CENSUS_TAG(kind, tag, bytes, aux) ((void)0)
#endif
