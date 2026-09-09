// Shared diagnostic ownership and terminal rendering. This layer consumes
// already-created records; it neither parses source nor constructs grammar
// errors. Copies retain all strings and flat notes across arena destruction.
#include <buster/lib/compiler/diagnostic.h>
#include <buster/lib/string.h>

BUSTER_GLOBAL_LOCAL CompilerDiagnosticLocation compiler_diagnostic_location_copy(Arena* arena, CompilerDiagnosticLocation location)
{
    location.path = string_duplicate_arena(arena, location.path, false);
    location.original_path = string_duplicate_arena(arena, location.original_path, false);
    return location;
}

CompilerDiagnostic compiler_diagnostic_copy(Arena* arena, CompilerDiagnostic diagnostic)
{
    diagnostic.code = string_duplicate_arena(arena, diagnostic.code, false);
    diagnostic.symbol = string_duplicate_arena(arena, diagnostic.symbol, false);
    diagnostic.message = string_duplicate_arena(arena, diagnostic.message, false);
    diagnostic.primary = compiler_diagnostic_location_copy(arena, diagnostic.primary);
    if (diagnostic.note_count)
    {
        CompilerDiagnosticNote* notes = arena_allocate(arena, CompilerDiagnosticNote, diagnostic.note_count);
        for (u32 index = 0; index < diagnostic.note_count; index += 1)
        {
            notes[index] = diagnostic.notes[index];
            notes[index].message = string_duplicate_arena(arena, notes[index].message, false);
            notes[index].location = compiler_diagnostic_location_copy(arena, notes[index].location);
        }
        diagnostic.notes = notes;
    }
    if (diagnostic.backend)
    {
        CompilerDiagnosticBackend* backend = arena_allocate(arena, CompilerDiagnosticBackend, 1);
        *backend = *diagnostic.backend;
        backend->target = string_duplicate_arena(arena, backend->target, false);
        backend->allocator = string_duplicate_arena(arena, backend->allocator, false);
        backend->function = string_duplicate_arena(arena, backend->function, false);
        backend->opcode = string_duplicate_arena(arena, backend->opcode, false);
        backend->operation = string_duplicate_arena(arena, backend->operation, false);
        backend->reason = string_duplicate_arena(arena, backend->reason, false);
        backend->referenced_symbol = string_duplicate_arena(arena, backend->referenced_symbol, false);
        diagnostic.backend = backend;
    }
    return diagnostic;
}

BUSTER_GLOBAL_LOCAL String8 compiler_diagnostic_render_line(Arena* arena, CompilerDiagnosticLocation location,
                                                             CompilerDiagnosticSeverity severity, String8 message)
{
    String8 prefix = severity == COMPILER_DIAGNOSTIC_WARNING ? S8("warning: ") : severity == COMPILER_DIAGNOSTIC_NOTE ? S8("note: ") : (String8){0};
    String8 result;
    if (location.path.length && location.position.line)
    {
        result = string_format(arena, S8("{S8}:{u32}:{u32}: {S8}{S8}"), location.path, location.position.line, location.position.column, prefix, message);
    }
    else if (location.path.length)
    {
        result = string_format(arena, S8("{S8}: {S8}{S8}"), location.path, prefix, message);
    }
    else
    {
        result = string_format(arena, S8("{S8}{S8}"), prefix, message);
    }
    return result;
}

String8 compiler_diagnostic_render(Arena* arena, CompilerDiagnostic diagnostic)
{
    String8 primary = compiler_diagnostic_render_line(arena, diagnostic.primary, diagnostic.severity, diagnostic.message);
    String8 result = primary;
    if (diagnostic.note_count)
    {
        // One final join avoids repeatedly copying a growing diagnostic when
        // a producer supplies many secondary locations.
        u64 count = (u64)diagnostic.note_count * 2 + 1;
        String8* parts = arena_allocate(arena, String8, count);
        parts[0] = primary;
        for (u32 index = 0; index < diagnostic.note_count; index += 1)
        {
            parts[(u64)index * 2 + 1] = S8("\n");
            parts[(u64)index * 2 + 2] = compiler_diagnostic_render_line(arena, diagnostic.notes[index].location,
                                                                       COMPILER_DIAGNOSTIC_NOTE, diagnostic.notes[index].message);
        }
        result = string_join_arena(arena, (SliceString8){.pointer = parts, .length = count}, false);
    }
    return result;
}
