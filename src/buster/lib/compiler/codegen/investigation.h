#pragma once

// Optional native lowering investigation for one function. Capture holds only
// canonical IDs, resolved source anchors and MIR emission byte intervals;
// it never owns frontend state. investigation_record is called after retained
// encoding, investigation_bind_object binds it to a serialized ELF object,
// and investigation_read/command consume the bounded versioned sidecar.
#include <buster/lib/compiler/codegen/machine.h>
#include <buster/lib/compiler/object/object.h>
#include <buster/lib/hash.h>

#define INVESTIGATION_SCHEMA_VERSION 1u
#define INVESTIGATION_ROW_LIMIT 8192u
#define INVESTIGATION_MARK_LIMIT 8192u
#define INVESTIGATION_TEXT_LIMIT 4096u
#define INVESTIGATION_CAPTURE_BYTE_LIMIT BUSTER_MB(4)
#define INVESTIGATION_FUNCTION_BYTE_LIMIT BUSTER_MB(1)
#define INVESTIGATION_ARTIFACT_BYTE_LIMIT BUSTER_MB(64)

typedef struct InvestigationRow InvestigationRow;
struct InvestigationRow
{
    u32 offset;
    u32 end;
    u32 opcode;
};

typedef struct InvestigationMark InvestigationMark;
struct InvestigationMark
{
    String8 path;
    IrSourceRange range;
    IrSourcePosition position;
    IrSourcePosition original_position;
    String8 original_path;
    u32 row;
    u32 instruction;
    u32 opcode;
};

typedef struct InvestigationCapture InvestigationCapture;
struct InvestigationCapture
{
    String8 function_name;
    String8 revision;
    String8 configuration;
    String8 input_path;
    String8 artifact_path;
    String8 section_name;
    String8 target;
    String8 diagnostic;
    InvestigationRow* rows;
    InvestigationMark* marks;
    ByteSlice code;
    u64 file_offset;
    u64 section_offset;
    u64 capture_ns;
    u32 function;
    u32 code_base;
    u32 row_count;
    u32 mark_count;
    u32 allocator;
    u32 cpu;
    u32 os;
    u32 features;
    bool found;
    bool transformed;
    char8 input_sha256[SHA256_HEX_CAPACITY];
    char8 translation_sha256[SHA256_HEX_CAPACITY];
    char8 artifact_sha256[SHA256_HEX_CAPACITY];
};

BUSTER_F_DECL String8 investigation_compiler_revision(void);
BUSTER_F_DECL void investigation_digest(ByteSlice bytes, char8 result[SHA256_HEX_CAPACITY]);
BUSTER_F_DECL void investigation_record(Arena* arena, InvestigationCapture* capture, IrProgram* program, IrFunction* ir_function,
                                       MachineFunction const* machine, u32 const* row_offsets, u8 const* bytes, u32 byte_count, u32 code_base);
BUSTER_F_DECL bool investigation_bind_object(Arena* arena, InvestigationCapture* capture, ObjectFile const* object, ByteSlice artifact);
BUSTER_F_DECL ByteSlice investigation_serialize(Arena* arena, InvestigationCapture const* capture);
BUSTER_F_DECL InvestigationCapture investigation_read(Arena* arena, ByteSlice bytes);
BUSTER_F_DECL bool investigation_matches(InvestigationCapture const* capture, ByteSlice artifact, String8 expected_revision);
BUSTER_F_DECL ProcessResult investigation_command(Arena* arena, SliceString8 arguments);
