#pragma once

#include <buster/tests/test.h>
#include <buster/lib/window.h>

#if BUSTER_INCLUDE_TESTS
BUSTER_F_DECL UnitTestResult window_tests(UnitTestArguments* arguments);
BUSTER_F_DECL u64 wm_native_file_drop_max_path_count(void);
BUSTER_F_DECL u64 wm_native_file_drop_max_path_bytes(void);
BUSTER_F_DECL bool wm_native_file_drop_budget_allows(u64 path_count, u64 output_bytes);
BUSTER_F_DECL bool wm_native_file_drop_array_size_allowed(u64 count, u64 element_size);

#if defined(_WIN32)
BUSTER_F_DECL WmKey wm_win32_keyboard_key_for_test(u32 virtual_key);
BUSTER_F_DECL WmEventList wm_win32_text_events_for_test(Arena* arena, String16 units, bool flush);
#elif BUSTER_LINUX
BUSTER_F_DECL u64 wm_x11_utf8_result_length(int result, u64 capacity);
BUSTER_F_DECL u32 wm_xim_next_input_style_attempt(u32 current);
BUSTER_F_DECL bool wm_x11_xdnd_version_supported(u8 version);
BUSTER_F_DECL bool wm_x11_xdnd_transaction_matches(u32 active_source, u32 active_target, u32 source, u32 target);
BUSTER_F_DECL u32 wm_x11_xdnd_source_watch_event_mask(u32 previous_mask);
BUSTER_F_DECL u32 wm_x11_xdnd_source_restore_event_mask(u32 saved_mask);
BUSTER_F_DECL bool wm_x11_xdnd_source_destroyed(bool source_destroy_observed, u32 source, u32 destroyed_window);
BUSTER_F_DECL u64 wm_x11_xdnd_max_transfer_bytes(void);
BUSTER_F_DECL bool wm_x11_xdnd_transfer_length_allowed(u64 current_length, u64 incoming_length);
BUSTER_F_DECL WmOffset wm_x11_drop_position_from_root(s32 root_x, s32 root_y, s32 window_root_x, s32 window_root_y);
BUSTER_F_DECL String8 wm_x11_decode_file_uri(Arena* arena, String8 uri, bool* valid_out);
BUSTER_F_DECL SliceString8 wm_x11_parse_uri_list(Arena* arena, String8 uri_list);
#endif

BUSTER_F_DECL SliceString8 wm_apple_file_paths_from_values(Arena* arena, SliceWmAppleFileUrlPath values);
BUSTER_F_DECL WmOffset wm_apple_drop_position_from_content_point(f64 x, f64 y, f64 height);
#endif
