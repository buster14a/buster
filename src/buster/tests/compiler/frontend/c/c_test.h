#pragma once

#include <buster/tests/test.h>
#include <buster/lib/compiler/frontend/c/c.h>
#include <buster/lib/compiler/ir/ir.h>
#include <buster/lib/file.h>
#include <buster/lib/string.h>

#if BUSTER_INCLUDE_TESTS
BUSTER_F_DECL CType* c_type_from_id(CParseResult* parse, CTypeId id);
BUSTER_F_DECL CEntityId c_parse_lookup_entity(CParseResult* result, CScopeId scope, String8 name);
BUSTER_F_DECL CEntityId c_parse_lookup_typedef_name_fallback(CParseResult* result, String8 name);
BUSTER_F_DECL u64 c_parse_name_hash(u32 symbol, String8 name);
BUSTER_F_DECL u32 c_parse_name_symbol(CParseResult* result, String8 name);
BUSTER_F_DECL bool c_test_translate_plain_run_paths_agree(String8 source);
BUSTER_F_DECL bool c_test_pp_class_masks_agree(Arena* arena, String8 source);
BUSTER_F_DECL u64 c_test_ir_initializer_slot_count(IrType* type);
BUSTER_F_DECL CEntityId c_test_ir_constant_entity_at(CParseResult* parse, CPreprocessResult preprocess,
                                                      CEntityId* token_entities, u32 token_index);
BUSTER_F_DECL bool c_test_ir_constant_entity_index_equivalent(CParseResult* parse, CPreprocessResult preprocess,
                                                               CEntityId* token_entities, u32 token_count);
BUSTER_F_DECL bool c_test_ir_constant_entity_index_lifetime(CParseResult* parse, CPreprocessResult preprocess,
                                                             CEntityId* token_entities, u32 token_count);
BUSTER_F_DECL bool c_test_lex_compact_tables_ready(void);
BUSTER_F_DECL bool c_test_decode_quoted(Arena* arena, String8 spelling, u8 delimiter, ByteSlice* bytes_out);
BUSTER_F_DECL bool c_test_decode_quoted_paths_agree(Arena* arena, String8 spelling, u8 delimiter, bool* accepted_out);
BUSTER_F_DECL bool c_test_string_literal_range_paths_agree(Arena* arena, CPreprocessResult preprocess, u32 start, u32 end, bool* accepted_out);
BUSTER_F_DECL u64 c_test_lex_punctuator_nfa_mismatches(void);
BUSTER_F_DECL bool c_test_type_parse_rollback_after_growth(Arena* arena, bool* grew_out, bool* restored_pointer_out,
                                                           bool* old_tag_restored_out, bool* grown_tag_preserved_out);
BUSTER_F_DECL UnitTestResult c_frontend_tests(UnitTestArguments* arguments);
#endif
