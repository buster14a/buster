#!/usr/bin/env python3
"""Apply one disposable, exact-source C representation experiment.

Usage: python3 root_output_prototype.py WORKTREE EVIDENCE_DIRECTORY
Not imported by production or ordinary CI. No performance claim.
"""
from pathlib import Path
import difflib
import hashlib
import re
import sys

root = Path(sys.argv[1]).resolve()
out = Path(sys.argv[2]).resolve()
out.mkdir(parents=True, exist_ok=True)
p = root / 'src/buster/lib/compiler/frontend/c/c_source.c'
raw = p.read_bytes()
assert hashlib.sha1(b'blob ' + str(len(raw)).encode() + b'\0' + raw).hexdigest() == '7d3d9e83e4273e669107819d721c18f15183a1eb'
original = raw.decode()
s = original

def replace(old, new, count=1):
    global s
    assert s.count(old) == count, (old[:150], s.count(old), count)
    s = s.replace(old, new)

anchor = '// Room for `token_count` tokens plus an optional ENABLE marker above the\n'
helper = '''// Experimental root-output sink. The arena belongs only to the completed
// text-line output, never to tasks, child contexts, spelling, or diagnostics.
// No published token/source-map pointer points into this arena.
BUSTER_C_INTERNAL void c_preprocess_output_append(Arena* arena, Arena* flat_arena,
    CPreprocessTokenNode** first, CPreprocessTokenNode** last, CPpToken token, u64* count)
{
    if (flat_arena)
    {
        BUSTER_CHECK(!*first && !*last);
        BUSTER_CHECK(flat_arena->position == arena_minimum_position + *count * sizeof(CPpToken));
        CPpToken* slot = arena_allocate(flat_arena, CPpToken, 1);
        *slot = token;
        *count += 1;
    }
    else
    {
        c_preprocess_output_push(arena, first, last, token, count);
    }
}

'''
replace(anchor, helper + anchor)
replace('CMacroExpansionContext* target, CMacroExpansionTaskStack* tasks)\n', 'CMacroExpansionContext* target, CMacroExpansionTaskStack* tasks, Arena* flat_output_arena)\n')
replace('c_preprocess_output_push(arena, &target->first_output, &target->last_output, pragma, &target->output_count);', 'c_preprocess_output_append(arena, flat_output_arena, &target->first_output, &target->last_output, pragma, &target->output_count);')
replace('CPreprocessResult* result)\n{\n    CMacroExpansionContext* context = arena_allocate(arena, CMacroExpansionContext, 1);', 'CPreprocessResult* result, Arena* flat_output_arena)\n{\n    BUSTER_CHECK(!flat_output_arena || (!*first_output && !*last_output && !*output_count));\n    CMacroExpansionContext* context = arena_allocate(arena, CMacroExpansionContext, 1);')
replace('    CMacroExpansionTask local_tasks[C_MACRO_EXPANSION_LOCAL_TASK_CAPACITY];', '    CMacroExpansionContext* root_context = context;\n    CMacroExpansionTask local_tasks[C_MACRO_EXPANSION_LOCAL_TASK_CAPACITY];')
replace('continuation->invocation, stamps, result, context, &tasks);', 'continuation->invocation, stamps, result, context, &tasks, context == root_context ? flat_output_arena : 0);')
replace('argument_count, token, stamps, result, context, &tasks);', 'argument_count, token, stamps, result, context, &tasks, context == root_context ? flat_output_arena : 0);', 2)
replace('c_preprocess_output_push(arena, &context->first_output, &context->last_output, token, &context->output_count);', 'c_preprocess_output_append(arena, context == root_context ? flat_output_arena : 0, &context->first_output, &context->last_output, token, &context->output_count);', 2)
pattern = re.compile(r'c_preprocess_expand\((.*?)\);', re.S)
for m in reversed(list(pattern.finditer(s))):
    text = m.group(0)
    if 'BUSTER_C_INTERNAL' in text or '\n{' in text:
        continue
    assert len(text) < 700
    value = 'root_output_arena' if 'wrapped_tokens, wrapped_count' in text else '0'
    s = s[:m.start()] + text[:-2] + ', ' + value + ');' + s[m.end():]
assert s.count('expansion_limit, result, 0);') == 1
assert s.count('expansion_limit, &result, 0);') == 3
assert s.count('expansion_limit, &result, root_output_arena);') == 1
replace('CPpStampTable const* stamps, CPreprocessTokenNode* first_line, u64 line_output_count,', 'CPpStampTable const* stamps, CPreprocessTokenNode* first_line, CPpToken const* flat_tokens, u64 line_output_count,')
start = s.index('BUSTER_C_INTERNAL void c_preprocess_process_expanded_line(')
end = s.index('\nBUSTER_C_INTERNAL u32 c_preprocess_tokens_from_nodes', start)
block = s[start:end]
assert block.count('for (CPreprocessTokenNode* node = first_line; node; node = node->next)') == 2
block = block.replace('    for (CPreprocessTokenNode* node = first_line; node; node = node->next)\n    {\n        foreign_length += node->token.foreign ? c_token_length(space->base, node->token.token) : 0;\n    }', '''    CPreprocessTokenNode* node = first_line;
    for (u64 index = 0; index < line_output_count; index += 1)
    {
        CPpToken item = flat_tokens ? flat_tokens[index] : node->token;
        if (node) node = node->next;
        foreign_length += item.foreign ? c_token_length(space->base, item.token) : 0;
    }''')
block = block.replace('    for (CPreprocessTokenNode* node = first_line; node; node = node->next)\n    {\n        if (node->token.token.kind == C_TOKEN_PRAGMA)', '''    node = first_line;
    for (u64 index = 0; index < line_output_count; index += 1)
    {
        CPpToken item = flat_tokens ? flat_tokens[index] : node->token;
        if (node) node = node->next;
        if (item.token.kind == C_TOKEN_PRAGMA)''')
block = block.replace('node->token.stamp', 'item.stamp').replace('node->token.token', 'item.token').replace('        CPpToken item = node->token;\n', '')
s = s[:start] + block + s[end:]
replace('    CPreprocessSourceFrame* source_frame = &root_frame;', '''    // Lazily reserved only for macro-bearing text lines. A failed reserve
    // retains the original linked path. Equal reserve size admits at least
    // every root output that fit in the caller's 24-byte-node arena.
    Arena* root_output_arena = 0;
    bool root_output_reserve_attempted = false;
    CPreprocessSourceFrame* source_frame = &root_frame;''')
replace('\n            CPreprocessTokenNode* first_line = 0;', '''
            if (!root_output_reserve_attempted)
            {
                root_output_reserve_attempted = true;
                root_output_arena = arena_create((ArenaCreation){
                    .reserved_size = arena->reserved_size,
                    .flags = {.no_pool = 1},
                });
            }
            CPreprocessTokenNode* first_line = 0;''')
replace('first_line, line_output_count, &token_stream, &output_count);', 'first_line, root_output_arena ? (CPpToken const*)((u8*)root_output_arena + arena_minimum_position) : 0,\n                                                    line_output_count, &token_stream, &output_count);')
replace('''                c_preprocess_process_expanded_line(pragma_context, space, &map, &stamps, first_line, root_output_arena ? (CPpToken const*)((u8*)root_output_arena + arena_minimum_position) : 0,
                                                    line_output_count, &token_stream, &output_count);
            }
        }
        source_frame->token_index = logical_end;''', '''                c_preprocess_process_expanded_line(pragma_context, space, &map, &stamps, first_line, root_output_arena ? (CPpToken const*)((u8*)root_output_arena + arena_minimum_position) : 0,
                                                    line_output_count, &token_stream, &output_count);
            }
            // All root output readers are finished, including pragma handling
            // and source-stamp recovery. Rewind after failure as well.
            if (root_output_arena) arena_reset_to_start(root_output_arena);
        }
        source_frame->token_index = logical_end;''')
replace('''    return result;
}

BUSTER_C_SHARED bool c_parse_auto_type_word''', '''    if (root_output_arena)
    {
        // Do not retain this transient high-water mark in the arena reuse pool.
        bool destroyed = arena_destroy(root_output_arena, 1);
        BUSTER_CHECK(destroyed);
        (void)destroyed;
    }
    return result;
}

BUSTER_C_SHARED bool c_parse_auto_type_word''')
patch = ''.join(difflib.unified_diff(original.splitlines(True), s.splitlines(True), fromfile='a/src/buster/lib/compiler/frontend/c/c_source.c', tofile='b/src/buster/lib/compiler/frontend/c/c_source.c'))
blob = hashlib.sha1(b'blob ' + str(len(s.encode())).encode() + b'\0' + s.encode()).hexdigest()
assert blob == 'e16652488717bfadd90f50fc0f8b3b8a771bb979', blob
(out / 'root-output.patch').write_text(patch)
(out / 'c_source.flat.c').write_text(s)
(out / 'candidate-blob.txt').write_text(blob + '\n')
p.write_text(s)
