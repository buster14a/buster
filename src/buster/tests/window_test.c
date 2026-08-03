#include <buster/tests/window_test.h>

#include <buster/lib/string.h>
#include <buster/lib/ui_core.h>
#include <buster/lib/window.h>
#if defined(_WIN32)
#include <buster/lib/system_headers.h>
#ifndef VK_OEM_102
#define VK_OEM_102 0xE2
#endif
#endif

BUSTER_TEST_F_DECL UnitTestResult window_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};
#if BUSTER_LINUX
    BUSTER_TEST(arguments, wm_x11_utf8_result_length(-1, 64) == 0);
    BUSTER_TEST(arguments, wm_x11_utf8_result_length(0, 64) == 0);
    BUSTER_TEST(arguments, wm_x11_utf8_result_length(3, 64) == 3);
    BUSTER_TEST(arguments, wm_x11_utf8_result_length(64, 64) == 0);
#endif
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
