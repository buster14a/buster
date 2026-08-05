#include <buster/tests/window_test.h>
#if BUSTER_INCLUDE_TESTS

#include <buster/lib/string.h>
#include <buster/lib/ui_core.h>
#include <buster/lib/window.h>
#if defined(_WIN32)
#include <buster/lib/system_headers.h>
#ifndef VK_OEM_102
#define VK_OEM_102 0xE2
#endif
#endif

UnitTestResult window_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
    u64 max_drop_paths = wm_native_file_drop_max_path_count();
    u64 max_drop_bytes = wm_native_file_drop_max_path_bytes();
    BUSTER_TEST(arguments, max_drop_paths != 0 && max_drop_bytes != 0);
    BUSTER_TEST(arguments, wm_native_file_drop_budget_allows(max_drop_paths, max_drop_bytes));
    BUSTER_TEST(arguments, !wm_native_file_drop_budget_allows(max_drop_paths + 1, 0));
    BUSTER_TEST(arguments, !wm_native_file_drop_budget_allows(0, max_drop_bytes + 1));
    BUSTER_TEST(arguments, wm_native_file_drop_array_size_allowed(max_drop_paths, sizeof(String8)));
    BUSTER_TEST(arguments, !wm_native_file_drop_array_size_allowed(UINT64_MAX, sizeof(String8)));
#if BUSTER_LINUX
    BUSTER_TEST(arguments, wm_x11_utf8_result_length(-1, 64) == 0);
    BUSTER_TEST(arguments, wm_x11_utf8_result_length(0, 64) == 0);
    BUSTER_TEST(arguments, wm_x11_utf8_result_length(3, 64) == 3);
    BUSTER_TEST(arguments, wm_x11_utf8_result_length(64, 64) == 0);
    u32 xim_attempt = 0;
    for (u32 retry = 0; retry < 32; retry += 1)
    {
        xim_attempt = wm_xim_next_input_style_attempt(xim_attempt);
    }
    BUSTER_TEST(arguments, xim_attempt == 9);
    BUSTER_TEST(arguments, wm_xim_next_input_style_attempt(xim_attempt) == xim_attempt);

    bool valid_uri = false;
    String8 decoded_uri = wm_x11_decode_file_uri(arguments->arena, S8("file:///tmp/%E2%82%AC"), &valid_uri);
    BUSTER_TEST(arguments, valid_uri);
    BUSTER_STRING_TEST(arguments, decoded_uri, S8("/tmp/\xe2\x82\xac"));
    valid_uri = false;
    decoded_uri = wm_x11_decode_file_uri(arguments->arena, S8("FiLe:///tmp/mixed"), &valid_uri);
    BUSTER_TEST(arguments, valid_uri);
    BUSTER_STRING_TEST(arguments, decoded_uri, S8("/tmp/mixed"));

    String8 uri_list = S8("# comment\r\nfile:///tmp/first%20file\r\nfile://localhost/tmp/\xc3\xa9\nfile://remote/tmp/rejected\nfile:///tmp/first%20file\r\nhttp://localhost/tmp/rejected\r\nfile:///tmp/bad%ZZ\r\n");
    SliceString8 parsed_paths = wm_x11_parse_uri_list(arguments->arena, uri_list);
    BUSTER_TEST(arguments, parsed_paths.length == 3);
    if (parsed_paths.length == 3)
    {
        BUSTER_STRING_TEST(arguments, parsed_paths.pointer[0], S8("/tmp/first file"));
        BUSTER_STRING_TEST(arguments, parsed_paths.pointer[1], S8("/tmp/\xc3\xa9"));
        BUSTER_STRING_TEST(arguments, parsed_paths.pointer[2], S8("/tmp/first file"));
    }

    bool malformed_uri = true;
    String8 malformed_path = wm_x11_decode_file_uri(arguments->arena, S8("file://remote/tmp/file"), &malformed_uri);
    BUSTER_TEST(arguments, !malformed_uri && malformed_path.length == 0);
    malformed_uri = true;
    malformed_path = wm_x11_decode_file_uri(arguments->arena, S8("filex/tmp/file"), &malformed_uri);
    BUSTER_TEST(arguments, !malformed_uri && malformed_path.length == 0);
    malformed_uri = true;
    malformed_path = wm_x11_decode_file_uri(arguments->arena, S8("file///tmp/file"), &malformed_uri);
    BUSTER_TEST(arguments, !malformed_uri && malformed_path.length == 0);
    malformed_uri = true;
    malformed_path = wm_x11_decode_file_uri(arguments->arena, S8("file/tmp/file"), &malformed_uri);
    BUSTER_TEST(arguments, !malformed_uri && malformed_path.length == 0);
    malformed_uri = true;
    malformed_path = wm_x11_decode_file_uri(arguments->arena, S8("file:///tmp/%00"), &malformed_uri);
    BUSTER_TEST(arguments, !malformed_uri && malformed_path.length == 0);
    BUSTER_TEST(arguments, wm_x11_parse_uri_list(arguments->arena, S8("")).length == 0);

    BUSTER_TEST(arguments, wm_x11_xdnd_version_supported(3));
    BUSTER_TEST(arguments, wm_x11_xdnd_version_supported(5));
    BUSTER_TEST(arguments, !wm_x11_xdnd_version_supported(2));
    BUSTER_TEST(arguments, !wm_x11_xdnd_version_supported(6));
    BUSTER_TEST(arguments, wm_x11_xdnd_transaction_matches(17, 29, 17, 29));
    BUSTER_TEST(arguments, !wm_x11_xdnd_transaction_matches(17, 29, 18, 29));
    BUSTER_TEST(arguments, !wm_x11_xdnd_transaction_matches(17, 29, 17, 30));

    u32 source_watch_bit = wm_x11_xdnd_source_watch_event_mask(0);
    u32 source_mask = wm_x11_xdnd_source_watch_event_mask(0x24);
    BUSTER_TEST(arguments, source_watch_bit != 0);
    BUSTER_TEST(arguments, source_mask == (source_watch_bit | 0x24));
    BUSTER_TEST(arguments, wm_x11_xdnd_source_restore_event_mask(source_mask) == source_mask);
    BUSTER_TEST(arguments, wm_x11_xdnd_source_destroyed(true, 17, 17));
    BUSTER_TEST(arguments, !wm_x11_xdnd_source_destroyed(false, 17, 17));
    BUSTER_TEST(arguments, !wm_x11_xdnd_source_destroyed(true, 17, 18));

    u64 max_transfer_bytes = wm_x11_xdnd_max_transfer_bytes();
    BUSTER_TEST(arguments, max_transfer_bytes != 0);
    BUSTER_TEST(arguments, wm_x11_xdnd_transfer_length_allowed(0, max_transfer_bytes));
    BUSTER_TEST(arguments, wm_x11_xdnd_transfer_length_allowed(max_transfer_bytes - 1, 1));
    BUSTER_TEST(arguments, !wm_x11_xdnd_transfer_length_allowed(max_transfer_bytes, 1));
    BUSTER_TEST(arguments, !wm_x11_xdnd_transfer_length_allowed(max_transfer_bytes - 1, 2));
    BUSTER_TEST(arguments, !wm_x11_xdnd_transfer_length_allowed(max_transfer_bytes + 1, 0));

    Arena* hostile_lines_arena = arena_create((ArenaCreation){0});
    if (hostile_lines_arena)
    {
        u64 hostile_length = max_transfer_bytes;
        char8* hostile_lines = arena_allocate(hostile_lines_arena, char8, hostile_length);
        for (u64 index = 0; index < hostile_length; index += 2)
        {
            hostile_lines[index] = 'x';
            hostile_lines[index + 1] = '\n';
        }
        u64 position_before_parse = hostile_lines_arena->position;
        SliceString8 hostile_paths = wm_x11_parse_uri_list(hostile_lines_arena, (String8){.pointer = hostile_lines, .length = hostile_length});
        BUSTER_TEST(arguments, hostile_paths.length == 0);
        BUSTER_TEST(arguments, hostile_lines_arena->position == position_before_parse);
        arena_destroy(hostile_lines_arena, 1);
    }
    else
    {
        BUSTER_TEST(arguments, false);
    }

    String8 exact_uri_line = S8("file:///x\n");
    if (max_drop_paths <= UINT64_MAX / exact_uri_line.length)
    {
        u64 exact_input_length = max_drop_paths * exact_uri_line.length;
        Arena* exact_boundary_arena = arena_create((ArenaCreation){0});
        if (exact_boundary_arena)
        {
            char8* exact_input = arena_allocate(exact_boundary_arena, char8, exact_input_length);
            for (u64 line_index = 0; line_index < max_drop_paths; line_index += 1)
            {
                u64 line_start = line_index * exact_uri_line.length;
                for (u64 character_index = 0; character_index < exact_uri_line.length; character_index += 1)
                {
                    exact_input[line_start + character_index] = exact_uri_line.pointer[character_index];
                }
            }
            SliceString8 exact_paths = wm_x11_parse_uri_list(exact_boundary_arena, (String8){.pointer = exact_input, .length = exact_input_length});
            BUSTER_TEST(arguments, exact_paths.length == max_drop_paths);
            if (exact_paths.length == max_drop_paths)
            {
                BUSTER_STRING_TEST(arguments, exact_paths.pointer[0], S8("/x"));
                BUSTER_STRING_TEST(arguments, exact_paths.pointer[exact_paths.length - 1], S8("/x"));
            }
            arena_destroy(exact_boundary_arena, 1);
        }
        else
        {
            BUSTER_TEST(arguments, false);
        }
    }
    else
    {
        BUSTER_TEST(arguments, false);
    }

    WmOffset x11_position = wm_x11_drop_position_from_root(-10, -20, 5, 30);
    BUSTER_TEST(arguments, x11_position.x == -15 && x11_position.y == -50);
#endif

    WmAppleFileUrlPath apple_values[] = {
        {.path = S8("/tmp/first"), .is_file_url = true},
        {.path = S8("https://example.test/not-a-file"), .is_file_url = false},
        {.path = S8("/tmp/\xc3\xa9"), .is_file_url = true},
        {.path = S8("/tmp/first"), .is_file_url = true},
        {.path = S8("\xff"), .is_file_url = true},
        {.path = S8(""), .is_file_url = true},
    };
    SliceString8 apple_paths = wm_apple_file_paths_from_values(arguments->arena,
                                                               (SliceWmAppleFileUrlPath){.pointer = apple_values, .length = BUSTER_ARRAY_LENGTH(apple_values)});
    BUSTER_TEST(arguments, apple_paths.length == 3);
    if (apple_paths.length == 3)
    {
        BUSTER_STRING_TEST(arguments, apple_paths.pointer[0], S8("/tmp/first"));
        BUSTER_STRING_TEST(arguments, apple_paths.pointer[1], S8("/tmp/\xc3\xa9"));
        BUSTER_STRING_TEST(arguments, apple_paths.pointer[2], S8("/tmp/first"));
    }
    SliceString8 too_many_apple_paths = wm_apple_file_paths_from_values(arguments->arena,
                                                                         (SliceWmAppleFileUrlPath){.pointer = 0, .length = max_drop_paths + 1});
    BUSTER_TEST(arguments, too_many_apple_paths.length == 0);
    WmOffset apple_position = wm_apple_drop_position_from_content_point(17.75, 25.0, 100.0);
    BUSTER_TEST(arguments, apple_position.x == 17 && apple_position.y == 75);
    apple_position = wm_apple_drop_position_from_content_point(-4.0, 120.0, 100.0);
    BUSTER_TEST(arguments, apple_position.x == -4 && apple_position.y == -20);

#if defined(_WIN32)
    struct WmWin32KeyTestCase
    {
        u32 virtual_key;
        WmKey expected;
    } key_cases[] = {
        {'A', WM_KEY_A},
        {'Z', WM_KEY_Z},
        {'0', WM_KEY_0},
        {'9', WM_KEY_9},
        {VK_F1, WM_KEY_F1},
        {VK_F12, WM_KEY_F12},
        {VK_F24, WM_KEY_F24},
        {VK_OEM_MINUS, WM_KEY_MINUS},
        {VK_OEM_PLUS, WM_KEY_EQUAL},
        {VK_OEM_3, WM_KEY_BACKTICK},
        {VK_OEM_4, WM_KEY_LEFT_BRACKET},
        {VK_OEM_6, WM_KEY_RIGHT_BRACKET},
        {VK_OEM_5, WM_KEY_BACKWARD_SLASH},
        {VK_OEM_102, WM_KEY_BACKWARD_SLASH},
        {VK_OEM_1, WM_KEY_SEMICOLON},
        {VK_OEM_7, WM_KEY_SINGLE_QUOTE},
        {VK_OEM_COMMA, WM_KEY_COMMA},
        {VK_OEM_PERIOD, WM_KEY_DOT},
        {VK_OEM_2, WM_KEY_FORWARD_SLASH},
        {VK_ESCAPE, WM_KEY_ESC},
        {VK_BACK, WM_KEY_BACKSPACE},
        {VK_TAB, WM_KEY_TAB},
        {VK_RETURN, WM_KEY_RETURN},
        {VK_SPACE, WM_KEY_SPACE},
        {VK_APPS, WM_KEY_MENU},
        {VK_CAPITAL, WM_KEY_CAPS_LOCK},
        {VK_SCROLL, WM_KEY_SCROLL_LOCK},
        {VK_INSERT, WM_KEY_INSERT},
        {VK_PAUSE, WM_KEY_PAUSE},
        {VK_HOME, WM_KEY_HOME},
        {VK_END, WM_KEY_END},
        {VK_PRIOR, WM_KEY_PAGE_UP},
        {VK_NEXT, WM_KEY_PAGE_DOWN},
        {VK_DELETE, WM_KEY_DELETE},
        {VK_UP, WM_KEY_UP},
        {VK_DOWN, WM_KEY_DOWN},
        {VK_LEFT, WM_KEY_LEFT},
        {VK_RIGHT, WM_KEY_RIGHT},
        {VK_LCONTROL, WM_KEY_CONTROL},
        {VK_RCONTROL, WM_KEY_CONTROL},
        {VK_LSHIFT, WM_KEY_SHIFT},
        {VK_RSHIFT, WM_KEY_SHIFT},
        {VK_LMENU, WM_KEY_ALT},
        {VK_RMENU, WM_KEY_ALT},
        {VK_NUMLOCK, WM_KEY_NUM_LOCK},
        {VK_DIVIDE, WM_KEY_NUM_SLASH},
        {VK_MULTIPLY, WM_KEY_NUM_STAR},
        {VK_SUBTRACT, WM_KEY_NUM_MINUS},
        {VK_ADD, WM_KEY_NUM_PLUS},
        {VK_DECIMAL, WM_KEY_NUM_DOT},
        {VK_NUMPAD0, WM_KEY_NUM_0},
        {VK_NUMPAD9, WM_KEY_NUM_9},
    };
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(key_cases); i += 1)
    {
        BUSTER_TEST(arguments, wm_win32_keyboard_key_for_test(key_cases[i].virtual_key) == key_cases[i].expected);
    }

    char16 ordinary_units[] = {'x'};
    WmEventList ordinary_events = wm_win32_text_events_for_test(arguments->arena,
                                                                (String16){.pointer = ordinary_units, .length = BUSTER_ARRAY_LENGTH(ordinary_units)}, false);
    BUSTER_TEST(arguments, ordinary_events.count == 1);
    if (ordinary_events.count == 1)
    {
        BUSTER_STRING_TEST(arguments, ordinary_events.first->text, S8("x"));
    }

    char16 pair_units[] = {(char16)0xd83du, (char16)0xde00u};
    WmEventList pair_events = wm_win32_text_events_for_test(arguments->arena,
                                                            (String16){.pointer = pair_units, .length = BUSTER_ARRAY_LENGTH(pair_units)}, false);
    BUSTER_TEST(arguments, pair_events.count == 1);
    if (pair_events.count == 1)
    {
        BUSTER_STRING_TEST(arguments, pair_events.first->text, S8("\xf0\x9f\x98\x80"));
    }

    char16 orphan_low_units[] = {(char16)0xdc00u};
    WmEventList orphan_low_events = wm_win32_text_events_for_test(arguments->arena,
                                                                  (String16){.pointer = orphan_low_units, .length = BUSTER_ARRAY_LENGTH(orphan_low_units)}, false);
    BUSTER_TEST(arguments, orphan_low_events.count == 1);
    if (orphan_low_events.count == 1)
    {
        BUSTER_STRING_TEST(arguments, orphan_low_events.first->text, S8("\xef\xbf\xbd"));
    }

    char16 interrupted_units[] = {(char16)0xd800u, 'x'};
    WmEventList interrupted_events = wm_win32_text_events_for_test(arguments->arena,
                                                                   (String16){.pointer = interrupted_units, .length = BUSTER_ARRAY_LENGTH(interrupted_units)}, false);
    BUSTER_TEST(arguments, interrupted_events.count == 2);
    if (interrupted_events.count == 2)
    {
        BUSTER_STRING_TEST(arguments, interrupted_events.first->text, S8("\xef\xbf\xbd"));
        BUSTER_STRING_TEST(arguments, interrupted_events.first->next->text, S8("x"));
    }

    char16 pending_units[] = {(char16)0xd800u};
    WmEventList pending_events = wm_win32_text_events_for_test(arguments->arena,
                                                               (String16){.pointer = pending_units, .length = BUSTER_ARRAY_LENGTH(pending_units)}, true);
    BUSTER_TEST(arguments, pending_events.count == 1);
    if (pending_events.count == 1)
    {
        BUSTER_STRING_TEST(arguments, pending_events.first->text, S8("\xef\xbf\xbd"));
    }
#endif
    Arena* source_arena = arena_create((ArenaCreation){0});
    if (source_arena)
    {
        String8 source_path_values[] = {
            S8("C:/workspace/first.bbb"),
            S8("C:/workspace/second.bbb"),
        };
        String8* source_paths = arena_allocate(source_arena, String8, BUSTER_ARRAY_LENGTH(source_path_values));
        for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(source_path_values); i += 1)
        {
            source_paths[i] = string_duplicate_arena(source_arena, source_path_values[i], false);
        }

        WmWindowHandle* window = (WmWindowHandle*)(uintptr_t)1;
        WmEvent event = {
            .window = window,
            .kind = WM_EVENT_FILE_DROP,
            .position = (WmOffset){.x = -17, .y = 29},
            .paths = (SliceString8){.pointer = source_paths, .length = BUSTER_ARRAY_LENGTH(source_path_values)},
        };
        WmEventList event_queue = {
            .first = &event,
            .last = &event,
            .count = 1,
        };

        UI_EventList ui_events = ui_event_list_from_wm_events(arguments->arena, window, event_queue);
        UI_Event* ui_event = ui_events.first ? &ui_events.first->v : 0;
        BUSTER_TEST(arguments, ui_event != 0);
        if (ui_event)
        {
            BUSTER_TEST(arguments, ui_event->kind == UI_EventKind_FileDrop);
            BUSTER_TEST(arguments, float2_element(ui_event->pos, 0) == -17.0f && float2_element(ui_event->pos, 1) == 29.0f);
            BUSTER_TEST(arguments, ui_event->paths.length == 2);
            if (ui_event->paths.length == 2)
            {
                BUSTER_STRING_TEST(arguments, ui_event->paths.pointer[0], source_path_values[0]);
                BUSTER_STRING_TEST(arguments, ui_event->paths.pointer[1], source_path_values[1]);
                BUSTER_TEST(arguments, ui_event->paths.pointer != source_paths);
                BUSTER_TEST(arguments, ui_event->paths.pointer[0].pointer != source_paths[0].pointer);
                BUSTER_TEST(arguments, ui_event->paths.pointer[1].pointer != source_paths[1].pointer);
            }
        }

        arena_destroy(source_arena, 1);
        if (ui_event && ui_event->paths.length == 2)
        {
            BUSTER_STRING_TEST(arguments, ui_event->paths.pointer[0], S8("C:/workspace/first.bbb"));
            BUSTER_STRING_TEST(arguments, ui_event->paths.pointer[1], S8("C:/workspace/second.bbb"));
        }
    }
    else
    {
        BUSTER_TEST(arguments, false);
    }
    return result;
}
#endif
