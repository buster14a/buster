#!/usr/bin/env python3
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one match, found {count}")
    return text.replace(old, new, 1)


path = Path("tools/differential.c")
text = path.read_text(encoding="utf-8")
text = replace_once(
    text,
    '''    else if (string_equal(mode, S8("sanitizer")))
    {
        fputs("runtime error: simulated recovering sanitizer\\n", stderr);
        exit(0);
    }
    else if (string_equal(mode, S8("timeout")))
''',
    '''    else if (string_equal(mode, S8("sanitizer")))
    {
        fputs("runtime error: simulated recovering sanitizer\\n", stderr);
        exit(0);
    }
    else if (string_equal(mode, S8("sanitizer-name-stdout")) || string_equal(mode, S8("sanitizer-name-stderr")))
    {
        String8 text = S8("AddressSanitizer UndefinedBehaviorSanitizer MemorySanitizer ThreadSanitizer LeakSanitizer runtime error:\\n");
        StandardStream stream = string_equal(mode, S8("sanitizer-name-stdout")) ? STANDARD_STREAM_OUTPUT : STANDARD_STREAM_ERROR;
        os_file_write(os_get_standard_stream(stream), (ByteSlice){.pointer = (u8*)text.pointer, .length = text.length});
        exit(0);
    }
    else if (string_equal(mode, S8("timeout")))
''',
    "self-test child name modes",
)
text = replace_once(
    text,
    '''        String8 modes[] = {S8("exit"), S8("sanitizer"), S8("timeout"), S8("crash")};
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(modes); index += 1)
        {
            u32 before_child = errors;
            String8 argv[] = {program_state->input.arguments.pointer[0], S8("test_differential"), S8("--self-test-child"), modes[index]};
            DObservation child = d_observe(&settings, (SliceString8)BUSTER_ARRAY_TO_SLICE(argv), path_join(arena, directory, modes[index]));
            if (index == 0) { errors += child.kind != D_EXIT || child.status != 7 || !string_equal(child.output, S8("a\\0b")) || !string_equal(child.error, S8("child stderr\\n")); }
            if (index == 1) { errors += child.kind != D_EXIT || child.status != 0 || !child.sanitizer || d_success(child); }
            if (index == 2) { errors += child.kind != D_TIMEOUT; }
            if (index == 3) { errors += child.kind != D_SIGNAL; }
''',
    '''        String8 modes[] = {S8("exit"), S8("sanitizer"), S8("sanitizer-name-stdout"),
                           S8("sanitizer-name-stderr"), S8("timeout"), S8("crash")};
        String8 sanitizer_names = S8("AddressSanitizer UndefinedBehaviorSanitizer MemorySanitizer ThreadSanitizer LeakSanitizer runtime error:\\n");
        for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(modes); index += 1)
        {
            u32 before_child = errors;
            String8 argv[] = {program_state->input.arguments.pointer[0], S8("test_differential"), S8("--self-test-child"), modes[index]};
            DObservation child = d_observe(&settings, (SliceString8)BUSTER_ARRAY_TO_SLICE(argv), path_join(arena, directory, modes[index]));
            if (index == 0) { errors += child.kind != D_EXIT || child.status != 7 || !string_equal(child.output, S8("a\\0b")) || !string_equal(child.error, S8("child stderr\\n")); }
            if (index == 1) { errors += child.kind != D_EXIT || child.status != 0 || !child.sanitizer || d_success(child); }
            if (index == 2) { errors += child.kind != D_EXIT || child.status != 0 || child.sanitizer || !d_success(child) ||
                !string_equal(child.output, sanitizer_names) || child.error.length; }
            if (index == 3) { errors += child.kind != D_EXIT || child.status != 0 || child.sanitizer || !d_success(child) ||
                child.output.length || !string_equal(child.error, sanitizer_names); }
            if (index == 4) { errors += child.kind != D_TIMEOUT; }
            if (index == 5) { errors += child.kind != D_SIGNAL; }
''',
    "self-test name observations",
)
path.write_text(text, encoding="utf-8")
