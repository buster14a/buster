#pragma once

#include <buster/lib/file.h>
#include <buster/tests/test.h>
#include <buster/lib/compiler/pdb/pdb.h>
#include <buster/lib/string.h>
// The test drives the real CodeView emitter so the split logic is exercised
// against the exact blob the object writers produce.
#include <buster/lib/compiler/codeview/codeview.h>

#if BUSTER_INCLUDE_TESTS
enum
{
    PDB_TEST_BLOCK_SIZE = 4096,
    PDB_TEST_STREAM_INFO = 1,
    PDB_TEST_STREAM_DBI = 3,
    PDB_TEST_STREAM_MODULE = 9,
    PDB_TEST_STREAM_COUNT = 11,
    PDB_TEST_INFO_VERSION_VC70 = 20000404,
    PDB_TEST_DBI_VERSION_V70 = 19990903,
    PDB_TEST_DBI_HEADER_SIZE = 64,
    PDB_TEST_SECTION_CONTRIBUTION_SIZE = 28,
    PDB_TEST_S_LOCAL = 0x113e,
    PDB_TEST_S_GPROC32 = 0x1110,
    PDB_TEST_S_DEFRANGE_FRAMEPOINTER_REL = 0x1142,
    PDB_TEST_STREAM_TPI = 2,
    PDB_TEST_TPI_HEADER_SIZE = 56,
    PDB_TEST_LF_MODIFIER = 0x1001,
    PDB_TEST_LF_POINTER = 0x1002,
    PDB_TEST_T_INT32 = 0x0074,
    PDB_TEST_T_REAL32 = 0x0040,
    PDB_TEST_TYPE_INDEX_BASE = 0x1000,
};

// A hand-built .debug$T blob, sized for the handful of records the merge test
// needs so no growth logic is involved.
typedef struct PdbTestTypeBuffer PdbTestTypeBuffer;
struct PdbTestTypeBuffer
{
    u8 bytes[64];
    u64 count;
};

BUSTER_F_DECL u32 pdb_read_u32(ByteSlice bytes, u64 offset);
BUSTER_F_DECL UnitTestResult pdb_tests(UnitTestArguments* arguments);
#endif
