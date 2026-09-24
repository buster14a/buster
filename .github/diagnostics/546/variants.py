#!/usr/bin/env python3
"""Pinned, temporary #546 prototypes; never used as a timing harness."""
from pathlib import Path
import hashlib
import json
import sys

root, out = map(Path, sys.argv[1:3])
out.mkdir(parents=True, exist_ok=True)
base = (root / 'src/buster/lib/compiler/ir/ir_cfg.c').read_text()
assert hashlib.sha256(base.encode()).hexdigest() == '6105e0bdc02beaa539e3cd2ff86a0e5d8dd13a707856f891182094ce251dcacb'

def once(text, old, new):
    assert text.count(old) == 1, (old[:80], text.count(old))
    return text.replace(old, new, 1)

common = once(base, '    u32* inverse = arena_allocate(scratch, u32, count);\n', '')
common = once(common, '                inverse[cursor++] = id.value;', '                cursor += 1;')
start = common.index('            for (u32 index = 0; index < count; index += 1)\n            {\n                if (inverse[index] != index)')
end = common.index('            for (u32 value = 0; value < function->value_count;', start)
old_loop = common[start:end]
delayed = once(common, old_loop, '''            u32* inverse = arena_allocate(scratch, u32, count);
            for (u32 old = 0; old < count; old += 1)
            {
                inverse[remap[old].value] = old;
            }
''' + old_loop)
forward = once(common, old_loop, '''            // The published copy owns the original old-to-new map from
            // here onward. Consume the scratch map as cycle state instead of
            // constructing a second, inverse permutation. Completed cycles
            // become identity entries; rows and their source ranges travel
            // together, without changing value/block/local/label identities.
            for (u32 index = 0; index < count; index += 1)
            {
                if (remap[index].value != index)
                {
                    IrInstruction saved = function->instructions[index];
                    IrSourceRange source = function->instruction_canonical_sources ? function->instruction_canonical_sources[index] : (IrSourceRange){0};
                    u32 destination = remap[index].value;
                    while (destination != index)
                    {
                        IrInstruction displaced = function->instructions[destination];
                        function->instructions[destination] = saved;
                        saved = displaced;
                        if (function->instruction_canonical_sources)
                        {
                            IrSourceRange displaced_source = function->instruction_canonical_sources[destination];
                            function->instruction_canonical_sources[destination] = source;
                            source = displaced_source;
                        }
                        u32 next = remap[destination].value;
                        remap[destination].value = destination;
                        destination = next;
                    }
                    function->instructions[index] = saved;
                    if (function->instruction_canonical_sources)
                    {
                        function->instruction_canonical_sources[index] = source;
                    }
                    remap[index].value = index;
                }
            }
''')
forward = once(forward, '                    *definition = remap[definition->value];', '                    *definition = published_remap[definition->value];')
forward = once(forward, '            ir_cfg_remap_extras(scratch, function, remap);', '            ir_cfg_remap_extras(scratch, function, published_remap);')
for name, text in [('baseline', base), ('delayed', delayed), ('forward', forward)]:
    (out / (name + '.c')).write_text(text)
    diagnostic = once(text, '    u32 count = function->instruction_count;\n    IrInstructionId* remap', '''    u64 diagnostic_scratch_start = scratch->position;
    u64 diagnostic_owner_start = arena->position;
    u32 count = function->instruction_count;
    IrInstructionId* remap''')
    diagnostic = once(diagnostic, '''        cfg->instruction_count = count;
    }
    return result;
}''', '''        cfg->instruction_count = count;
    }
    string_print(S8("CFG_546 rows={u32} moved={u32} error={u32} scratch_start={u64} scratch_end={u64} scratch_retained={u64} scratch_committed={u64} owner_start={u64} owner_end={u64} owner_retained={u64} owner_committed={u64} published_bytes={u64}\\n"),
                 count, (u32)moved, (u32)result.error, diagnostic_scratch_start, scratch->position,
                 arena_dirty_position(scratch), scratch->os_position, diagnostic_owner_start, arena->position,
                 arena_dirty_position(arena), arena->os_position, cfg->allocated_bytes);
    return result;
}''')
    (out / (name + '-diagnostic.c')).write_text(diagnostic)
print(json.dumps({p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in out.glob('*.c')}, indent=2))
