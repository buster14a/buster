#pragma once

#include <buster/lib/compiler/codegen/machine.h>

// Private scheduler publication seam, shared with source-location regressions.
// Copy marks into arena, remap rows below row_count, and stably order the copy.
// Rows at or beyond row_count (including terminal marks) retain their keys.
// Input arrays are borrowed and never modified. scratch_arena may equal arena;
// temporary merge storage is released before returning the retained copy.
BUSTER_F_DECL MachineLineMark* machine_schedule_remap_line_marks(Arena* arena, Arena* scratch_arena,
                                                                 MachineLineMark const* marks, u32 mark_count,
                                                                 u32 const* new_rows, u32 row_count);
