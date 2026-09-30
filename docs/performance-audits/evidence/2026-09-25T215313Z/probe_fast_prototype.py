#!/usr/bin/env python3
"""Disposable counters for machine_fast_close_live_ranges (slot caller). Never commit."""
import sys
path = sys.argv[1]
src = open(path).read()
def rep(old, new, count=1):
    # Replace the first occurrence only; it must precede the test-only sweep reference.
    global src
    n = src.count(old)
    limit = src.find("machine_fast_close_live_ranges_sweep_reference(Arena* arena")
    if n < 1 or (n > 1 and src.find(old) > limit): sys.exit(f"anchor count {n}: {old[:90]!r}")
    src = src.replace(old, new, 1)
PROBE = r'''
#include <stdio.h>
#include <stdlib.h>
typedef struct FastProbe FastProbe;
struct FastProbe
{
    unsigned long long calls, slot_calls, blocks, slots, plane_words, worklist_visits, bits_total, bits_fixed, bits_colorable,
        bits_untouched, slots_fixed, slots_colorable, slots_untouched, max_slots, max_blocks, max_bits;
    u8 const* fixed;
    u32 const* touched_starts;
};
static FastProbe fast_probe;
__attribute__((destructor)) static void fast_probe_print(void)
{
    if (getenv("BUSTER_PROBE"))
    {
        FastProbe* p = &fast_probe;
        fprintf(stderr, "FASTPROBE calls=%llu slot_calls=%llu blocks=%llu slots=%llu plane_words=%llu worklist_visits=%llu bits_total=%llu bits_fixed=%llu bits_colorable=%llu bits_untouched=%llu slots_fixed=%llu slots_colorable=%llu slots_untouched=%llu max_slots=%llu max_blocks=%llu max_bits=%llu\n",
            p->calls, p->slot_calls, p->blocks, p->slots, p->plane_words, p->worklist_visits, p->bits_total, p->bits_fixed, p->bits_colorable,
            p->bits_untouched, p->slots_fixed, p->slots_colorable, p->slots_untouched, p->max_slots, p->max_blocks, p->max_bits);
    }
}
'''
rep("BUSTER_GLOBAL_LOCAL void machine_fast_close_live_ranges(Arena* arena, MachineFunction const* function, MachineFastPrepass const* prepass, u64 const* reads,\n",
    PROBE + "BUSTER_GLOBAL_LOCAL void machine_fast_close_live_ranges(Arena* arena, MachineFunction const* function, MachineFastPrepass const* prepass, u64 const* reads,\n")
rep("""    u32 block_count = function->block_count;
    u64 plane = (u64)block_count * words;
    u64* live_in = arena_allocate(arena, u64, plane ? plane : 1);""", """    u32 block_count = function->block_count;
    u64 plane = (u64)block_count * words;
    unsigned long long probe_bits_before = fast_probe.bits_total;
    fast_probe.calls += 1; fast_probe.blocks += block_count; fast_probe.plane_words += plane;
    if (block_count > fast_probe.max_blocks) fast_probe.max_blocks = block_count;
    u64* live_in = arena_allocate(arena, u64, plane ? plane : 1);""")
rep("""        u32 block_index = worklist[--work_count];
        queued[block_index] = 0;
        u64 const* block_reads = reads + (u64)block_index * words;""", """        u32 block_index = worklist[--work_count];
        queued[block_index] = 0;
        fast_probe.worklist_visits += 1;
        u64 const* block_reads = reads + (u64)block_index * words;""")
def count_bit(var):
    return f"""                u32 object = 64u * word + trailing_zeroes_u64({var});
                fast_probe.bits_total += 1;
                if (fast_probe.fixed) {{ if (fast_probe.touched_starts[object] == UINT32_MAX) fast_probe.bits_untouched += 1; else if (fast_probe.fixed[object]) fast_probe.bits_fixed += 1; else fast_probe.bits_colorable += 1; }}"""
rep("""                u32 object = 64u * word + trailing_zeroes_u64(entering);""", count_bit("entering"))
rep("""                u32 object = 64u * word + trailing_zeroes_u64(leaving);""", count_bit("leaving"))
# end of function: max bits per call
rep("""                ends[object] = BUSTER_MAX(ends[object], last);
            }
        }
    }
}
""", """                ends[object] = BUSTER_MAX(ends[object], last);
            }
        }
    }
    if (fast_probe.bits_total - probe_bits_before > fast_probe.max_bits) fast_probe.max_bits = fast_probe.bits_total - probe_bits_before;
}
""")
# prototype: the call lives in machine_fast_close_slot_ranges over dense objects
rep("""        machine_fast_close_live_ranges(arena, function, prepass, reads, writes, words, starts, ends);
        for (u32 closed = 0; closed < object_count; closed += 1)""", """        fast_probe.slot_calls += 1; fast_probe.slots += slot_count; fast_probe.slots_colorable += object_count;
        if (slot_count > fast_probe.max_slots) fast_probe.max_slots = slot_count;
        machine_fast_close_live_ranges(arena, function, prepass, reads, writes, words, starts, ends);
        for (u32 closed = 0; closed < object_count; closed += 1)""")
open(path, "w").write(src)
print("fast probe applied")
