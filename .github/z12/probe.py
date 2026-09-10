"""Temporary diagnostic only; never used as a timing compiler."""
from pathlib import Path
import sys
p=Path(sys.argv[1])/'src/buster/lib/compiler/codegen/register_allocator_fast.c'
s=p.read_text()
s='#include <stdio.h>\n'+s
s=s.replace('struct MachineFastState\n{','struct MachineFastState\n{\n    u64 z12_census[4][65][3];',1)
needle='#if BUSTER_INCLUDE_TESTS\nu64 machine_fast_owner_match_mask_test'
assert s.count(needle)==1
s=s.replace(needle,'''static u64 z12_query(MachineFastState* state, u32 site, u32 const* owner, u64 active, u32 value)
{
    u64 matches = machine_fast_owner_match_mask(owner, active, value);
    u32 population = mask64_count(active);
    u32 prefix = 0;
    for (u64 remaining = active; remaining; remaining &= remaining - 1u)
    {
        prefix += 1u;
        if ((remaining & (0u - remaining)) & matches)
        {
            break;
        }
    }
    state->z12_census[site][population][0] += 1u;
    state->z12_census[site][population][1] += matches != 0;
    state->z12_census[site][population][2] += prefix;
    return matches;
}

static void z12_report(MachineFastState const* state)
{
    for (u32 site = 0; site < 4; site += 1)
    {
        for (u32 population = 0; population <= 64; population += 1)
        {
            u64 const* row = state->z12_census[site][population];
            if (row[0])
            {
                fprintf(stderr, "Z12_OWNER,%u,%u,%llu,%llu,%llu\\n", site, population,
                        (unsigned long long)row[0], (unsigned long long)row[1], (unsigned long long)row[2]);
            }
        }
    }
}

#if BUSTER_INCLUDE_TESTS
u64 machine_fast_owner_match_mask_test''')
for site,(args,state) in enumerate([
('contract_owner, contract_held, resident','state'),
('contract_owner, pending, resident','state'),
('edge_owner, edge_dirty, value','&state'),
('entry_owner, entry_held, split_value','&state')]):
    old=f'machine_fast_owner_match_mask({args})'
    assert s.count(old)==1,old
    s=s.replace(old,f'z12_query({state}, {site}, {args})')
for marker in [
'                if (!info)\n                {\n                    return placement;',
'        if (edge_copy_temporary_size > UINT32_MAX - running)\n        {\n            return placement;']:
    assert s.count(marker)==1,marker
    s=s.replace(marker,marker.replace('return placement;','z12_report(&state);\n'+('                    ' if '!info' in marker else '            ')+'return placement;'))
marker='''            placement.valid = true;
        }
    }

    return placement;
}'''
assert s.count(marker)==1
s=s.replace(marker,marker.replace('        }\n    }','        }\n        z12_report(&state);\n    }'))
p.write_text(s)
