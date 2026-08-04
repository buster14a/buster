#pragma once

#include <buster/lib/ui_core.h>

typedef struct UI_WidgetResult UI_WidgetResult;
struct UI_WidgetResult
{
    UI_Box* box;
    UI_Signal signal;
    bool changed;
    bool value;
    f32 value_f32;
};

typedef struct UI_TextEditState UI_TextEditState;
struct UI_TextEditState
{
    UI_Key key;
    u64 cursor;
    u64 mark;
    bool selecting;
    u8 reserved[7];
};

typedef struct UI_TextEditResult UI_TextEditResult;
struct UI_TextEditResult
{
    UI_WidgetResult widget;
    String8 value;
    u64 cursor;
    u64 mark;
    bool active;
    bool changed;
};

BUSTER_F_DECL UI_Box* ui_label(String8 string);
BUSTER_F_DECL UI_Box* ui_spacer(UI_Size width, UI_Size height);
BUSTER_F_DECL UI_Box* ui_separator(Axis2 axis);
BUSTER_F_DECL UI_Signal ui_button(String8 string);
BUSTER_F_DECL UI_WidgetResult ui_checkbox(String8 string, bool checked);
BUSTER_F_DECL UI_WidgetResult ui_radio(String8 string, bool selected);
BUSTER_F_DECL UI_WidgetResult ui_slider(String8 string, f32 value, f32 minimum, f32 maximum);
BUSTER_F_DECL UI_TextEditResult ui_text_edit(UI_TextEditState* state, String8 label, String8* value, u64 capacity);

BUSTER_F_DECL UI_Box* ui_scroll_region_begin(String8 string);
BUSTER_F_DECL void ui_scroll_region_end(void);
BUSTER_F_DECL UI_Signal ui_list_row(String8 string, bool selected);
BUSTER_F_DECL UI_Signal ui_tree_row(String8 string, u32 depth, bool selected, bool expanded);
BUSTER_F_DECL UI_WidgetResult ui_tab(String8 string, bool selected);
BUSTER_F_DECL UI_Box* ui_menu_begin(String8 string);
BUSTER_F_DECL void ui_menu_end(void);
BUSTER_F_DECL UI_WidgetResult ui_menu_item(String8 string, bool enabled);
BUSTER_F_DECL UI_Box* ui_popup_begin(String8 string);
BUSTER_F_DECL void ui_popup_end(void);
BUSTER_F_DECL UI_WidgetResult ui_popup_item(String8 string, bool enabled);
