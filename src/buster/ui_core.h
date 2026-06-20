#pragma once

#include <buster/base.h>
#include <buster/arena.h>
#include <buster/rendering.h>

// Raddebugger-inspired immediate-mode UI core.  The public API keeps the
// existing Buster entry points alive while exposing a UI_Box based model.

typedef enum UI_MouseButtonKind
{
    UI_MouseButtonKind_Left,
    UI_MouseButtonKind_Middle,
    UI_MouseButtonKind_Right,
    UI_MouseButtonKind_COUNT,
} UI_MouseButtonKind;

typedef enum UI_EventKind
{
    UI_EventKind_Null,
    UI_EventKind_Press,
    UI_EventKind_Release,
    UI_EventKind_Text,
    UI_EventKind_MouseMove,
    UI_EventKind_Scroll,
    UI_EventKind_COUNT,
} UI_EventKind;

typedef struct UI_EventNode UI_EventNode;
typedef struct UI_Event UI_Event;
struct UI_Event
{
    UI_EventNode* node;
    UI_EventKind kind;
    WmKey key;
    u8 modifiers;
    String8 string;
    float2 pos;
    float2 delta;
};

struct UI_EventNode
{
    UI_EventNode* next;
    UI_EventNode* prev;
    UI_Event v;
};

typedef struct UI_EventList UI_EventList;
struct UI_EventList
{
    UI_EventNode* first;
    UI_EventNode* last;
    u64 count;
};

typedef struct UI_Key UI_Key;
struct UI_Key
{
    u64 value;
};

typedef enum UI_SizeKind
{
    UI_SizeKind_Null,
    UI_SizeKind_Pixels,
    UI_SizeKind_TextContent,
    UI_SizeKind_ParentPct,
    UI_SizeKind_ChildrenSum,
} UI_SizeKind;

// Compatibility names used by the previous ui_core.
#define UI_SIZE_PIXEL_COUNT UI_SizeKind_Pixels
#define UI_SIZE_PERCENTAGE  UI_SizeKind_ParentPct
#define UI_SIZE_BY_CHILDREN UI_SizeKind_ChildrenSum
#define UI_SIZE_KIND_COUNT  ((UI_SizeKind)(UI_SizeKind_ChildrenSum + 1))

typedef struct UI_Size UI_Size;
struct UI_Size
{
    UI_SizeKind kind;
    f32 value;
    f32 strictness;
};

typedef enum UI_TextAlign
{
    UI_TextAlign_Left,
    UI_TextAlign_Center,
    UI_TextAlign_Right,
    UI_TextAlign_COUNT,
} UI_TextAlign;

typedef u64 UI_BoxFlags;
#define UI_BoxFlag_MouseClickable      (UI_BoxFlags)(1ull << 0)
#define UI_BoxFlag_KeyboardClickable   (UI_BoxFlags)(1ull << 1)
#define UI_BoxFlag_Disabled            (UI_BoxFlags)(1ull << 2)

#define UI_BoxFlag_FloatingX           (UI_BoxFlags)(1ull << 3)
#define UI_BoxFlag_FloatingY           (UI_BoxFlags)(1ull << 4)
#define UI_BoxFlag_FixedWidth          (UI_BoxFlags)(1ull << 5)
#define UI_BoxFlag_FixedHeight         (UI_BoxFlags)(1ull << 6)
#define UI_BoxFlag_AllowOverflowX      (UI_BoxFlags)(1ull << 7)
#define UI_BoxFlag_AllowOverflowY      (UI_BoxFlags)(1ull << 8)

#define UI_BoxFlag_DrawBackground      (UI_BoxFlags)(1ull << 9)
#define UI_BoxFlag_DrawBorder          (UI_BoxFlags)(1ull << 10)
#define UI_BoxFlag_DrawText            (UI_BoxFlags)(1ull << 11)
#define UI_BoxFlag_DrawHotEffects      (UI_BoxFlags)(1ull << 12)
#define UI_BoxFlag_DrawActiveEffects   (UI_BoxFlags)(1ull << 13)
#define UI_BoxFlag_DrawSideTop         (UI_BoxFlags)(1ull << 14)
#define UI_BoxFlag_DrawSideBottom      (UI_BoxFlags)(1ull << 15)
#define UI_BoxFlag_DrawSideLeft        (UI_BoxFlags)(1ull << 16)
#define UI_BoxFlag_DrawSideRight       (UI_BoxFlags)(1ull << 17)
#define UI_BoxFlag_DrawDropShadow      (UI_BoxFlags)(1ull << 18)
#define UI_BoxFlag_Clip                (UI_BoxFlags)(1ull << 19)
#define UI_BoxFlag_Debug               (UI_BoxFlags)(1ull << 63)

#define UI_BoxFlag_Clickable           (UI_BoxFlag_MouseClickable | UI_BoxFlag_KeyboardClickable)
#define UI_BoxFlag_Floating            (UI_BoxFlag_FloatingX | UI_BoxFlag_FloatingY)
#define UI_BoxFlag_FixedSize           (UI_BoxFlag_FixedWidth | UI_BoxFlag_FixedHeight)
#define UI_BoxFlag_AllowOverflow       (UI_BoxFlag_AllowOverflowX | UI_BoxFlag_AllowOverflowY)
#define UI_BoxFlag_DrawSides           (UI_BoxFlag_DrawSideTop | UI_BoxFlag_DrawSideBottom | UI_BoxFlag_DrawSideLeft | UI_BoxFlag_DrawSideRight)

// Compatibility bitfield accepted by ui_widget_make*.
typedef union UI_WidgetFlags UI_WidgetFlags;
union UI_WidgetFlags
{
    struct
    {
        u64 disabled:1;
        u64 mouse_clickable:1;
        u64 keyboard_pressable:1;
        u64 draw_text:1;
        u64 draw_background:1;
        u64 overflow_x:1;
        u64 overflow_y:1;
        u64 floating_x:1;
        u64 floating_y:1;
        u64 draw_border:1;
        u64 draw_hot_effects:1;
        u64 draw_active_effects:1;
        u64 reserved:52;
    };
    u64 v;
};
BUSTER_CT_CHECK(sizeof(UI_WidgetFlags) == sizeof(u64));

typedef struct UI_Box UI_Box;
struct UI_Box
{
    // persistent hash links
    UI_Box* hash_next;
    UI_Box* hash_prev;

    // per-build tree links
    UI_Box* first;
    UI_Box* last;
    UI_Box* next;
    UI_Box* prev;
    UI_Box* parent;
    u64 child_count;

    // per-build equipment
    UI_Key key;
    UI_BoxFlags flags;
    String8 string;
    UI_TextAlign text_align;
    float2 fixed_position;
    float2 fixed_size;
    float2 min_size;
    UI_Size pref_size[(u64)AXIS2_COUNT];
    Axis2 child_layout_axis;
    float4 background_color;
    float4 text_color;
    float4 border_color;
    f32 font_size;
    f32 text_padding;
    f32 corner_radii[(u64)CORNER_COUNT];

    // per-build artifacts
    F32Interval2 rect;
    float2 position_delta;

    // persistent interaction/animation data
    u64 first_touched_build_index;
    u64 last_touched_build_index;
    f32 hot_t;
    f32 active_t;
    float2 view_off;
    float2 view_bounds;
};

typedef UI_Box UI_Widget;

typedef struct UI_BoxRec UI_BoxRec;
struct UI_BoxRec
{
    UI_Box* next;
    s32 push_count;
    s32 pop_count;
};

typedef struct UI_BoxHashSlot UI_BoxHashSlot;
struct UI_BoxHashSlot
{
    UI_Box* first;
    UI_Box* last;
};

typedef u32 UI_SignalFlags;
enum
{
    UI_SignalFlag_LeftPressed   = (1u << 0),
    UI_SignalFlag_LeftReleased  = (1u << 1),
    UI_SignalFlag_LeftClicked   = (1u << 2),
    UI_SignalFlag_Hovering      = (1u << 3),
    UI_SignalFlag_MouseOver     = (1u << 4),
    UI_SignalFlag_KeyboardPressed = (1u << 5),
    UI_SignalFlag_Pressed       = UI_SignalFlag_LeftPressed | UI_SignalFlag_KeyboardPressed,
    UI_SignalFlag_Released      = UI_SignalFlag_LeftReleased,
    UI_SignalFlag_Clicked       = UI_SignalFlag_LeftClicked | UI_SignalFlag_KeyboardPressed,
};

typedef struct UI_Signal UI_Signal;
struct UI_Signal
{
    UI_Box* box;
    UI_SignalFlags f;
    u32 clicked_left:1;
    u32 pressed_left:1;
    u32 released_left:1;
    u32 hovering:1;
    u32 mouse_over:1;
    u32 reserved:27;
};

#define ui_pressed(s)        !!((s).f & UI_SignalFlag_Pressed)
#define ui_clicked(s)        !!((s).f & UI_SignalFlag_Clicked)
#define ui_released(s)       !!((s).f & UI_SignalFlag_Released)
#define ui_hovering(s)       !!((s).f & UI_SignalFlag_Hovering)
#define ui_mouse_over(s)     !!((s).f & UI_SignalFlag_MouseOver)

#define UI_STACK_CAPACITY (64)

typedef struct UI_StateStacks UI_StateStacks;
struct UI_StateStacks
{
    UI_Box* parent[UI_STACK_CAPACITY]; u32 parent_length; u8 parent_auto_pop;
    Axis2 child_layout_axis[UI_STACK_CAPACITY]; u32 child_layout_axis_length; u8 child_layout_axis_auto_pop;
    f32 fixed_x[UI_STACK_CAPACITY]; u32 fixed_x_length; u8 fixed_x_auto_pop;
    f32 fixed_y[UI_STACK_CAPACITY]; u32 fixed_y_length; u8 fixed_y_auto_pop;
    f32 fixed_width[UI_STACK_CAPACITY]; u32 fixed_width_length; u8 fixed_width_auto_pop;
    f32 fixed_height[UI_STACK_CAPACITY]; u32 fixed_height_length; u8 fixed_height_auto_pop;
    UI_Size pref_width[UI_STACK_CAPACITY]; u32 pref_width_length; u8 pref_width_auto_pop;
    UI_Size pref_height[UI_STACK_CAPACITY]; u32 pref_height_length; u8 pref_height_auto_pop;
    f32 min_width[UI_STACK_CAPACITY]; u32 min_width_length; u8 min_width_auto_pop;
    f32 min_height[UI_STACK_CAPACITY]; u32 min_height_length; u8 min_height_auto_pop;
    UI_BoxFlags flags[UI_STACK_CAPACITY]; u32 flags_length; u8 flags_auto_pop;
    float4 background_color[UI_STACK_CAPACITY]; u32 background_color_length; u8 background_color_auto_pop;
    float4 text_color[UI_STACK_CAPACITY]; u32 text_color_length; u8 text_color_auto_pop;
    float4 border_color[UI_STACK_CAPACITY]; u32 border_color_length; u8 border_color_auto_pop;
    f32 font_size[UI_STACK_CAPACITY]; u32 font_size_length; u8 font_size_auto_pop;
    f32 text_padding[UI_STACK_CAPACITY]; u32 text_padding_length; u8 text_padding_auto_pop;
    UI_TextAlign text_alignment[UI_STACK_CAPACITY]; u32 text_alignment_length; u8 text_alignment_auto_pop;
};

typedef struct UI_StateStackNulls UI_StateStackNulls;
struct UI_StateStackNulls
{
    UI_Box* parent;
    Axis2 child_layout_axis;
    f32 fixed_x;
    f32 fixed_y;
    f32 fixed_width;
    f32 fixed_height;
    UI_Size pref_width;
    UI_Size pref_height;
    f32 min_width;
    f32 min_height;
    UI_BoxFlags flags;
    float4 background_color;
    float4 text_color;
    float4 border_color;
    f32 font_size;
    f32 text_padding;
    UI_TextAlign text_alignment;
};

typedef struct UI_State UI_State;
struct UI_State
{
    Arena* arena;
    Arena* build_arenas[2];
    RenderingHandle* rendering;
    RenderingWindowHandle* rendering_window;
    WmHandle* windowing;
    WmWindowHandle* window;
    UI_EventList events;
    u64 build_index;
    f64 frame_time;

    UI_Box* root;
    UI_Box* first_free_box;
    u64 box_table_size;
    UI_BoxHashSlot* box_table;

    UI_Key hot_box_key;
    UI_Key active_box_key[(u64)UI_MouseButtonKind_COUNT];
    float2 mouse;
    u64 is_animating:1;
    u64 reserved:63;

    UI_StateStacks stacks;
    UI_StateStackNulls stack_nulls;
};

BUSTER_V_DECL UI_State* ui_state;

// Stack accessors (RAD style)
BUSTER_F_DECL UI_Box* ui_top_parent(void);
BUSTER_F_DECL Axis2 ui_top_child_layout_axis(void);
BUSTER_F_DECL f32 ui_top_fixed_x(void);
BUSTER_F_DECL f32 ui_top_fixed_y(void);
BUSTER_F_DECL f32 ui_top_fixed_width(void);
BUSTER_F_DECL f32 ui_top_fixed_height(void);
BUSTER_F_DECL UI_Size ui_top_pref_width(void);
BUSTER_F_DECL UI_Size ui_top_pref_height(void);
BUSTER_F_DECL f32 ui_top_min_width(void);
BUSTER_F_DECL f32 ui_top_min_height(void);
BUSTER_F_DECL UI_BoxFlags ui_top_flags(void);
BUSTER_F_DECL float4 ui_top_background_color(void);
BUSTER_F_DECL float4 ui_top_text_color(void);
BUSTER_F_DECL float4 ui_top_border_color(void);
BUSTER_F_DECL f32 ui_top_font_size(void);
BUSTER_F_DECL f32 ui_top_text_padding(void);
BUSTER_F_DECL UI_TextAlign ui_top_text_alignment(void);

BUSTER_F_DECL UI_Box* ui_push_parent(UI_Box* v);
BUSTER_F_DECL Axis2 ui_push_child_layout_axis(Axis2 v);
BUSTER_F_DECL f32 ui_push_fixed_x(f32 v);
BUSTER_F_DECL f32 ui_push_fixed_y(f32 v);
BUSTER_F_DECL f32 ui_push_fixed_width(f32 v);
BUSTER_F_DECL f32 ui_push_fixed_height(f32 v);
BUSTER_F_DECL UI_Size ui_push_pref_width(UI_Size v);
BUSTER_F_DECL UI_Size ui_push_pref_height(UI_Size v);
BUSTER_F_DECL f32 ui_push_min_width(f32 v);
BUSTER_F_DECL f32 ui_push_min_height(f32 v);
BUSTER_F_DECL UI_BoxFlags ui_push_flags(UI_BoxFlags v);
BUSTER_F_DECL float4 ui_push_background_color(float4 v);
BUSTER_F_DECL float4 ui_push_text_color(float4 v);
BUSTER_F_DECL float4 ui_push_border_color(float4 v);
BUSTER_F_DECL f32 ui_push_font_size(f32 v);
BUSTER_F_DECL f32 ui_push_text_padding(f32 v);
BUSTER_F_DECL UI_TextAlign ui_push_text_alignment(UI_TextAlign v);

BUSTER_F_DECL UI_Box* ui_pop_parent(void);
BUSTER_F_DECL Axis2 ui_pop_child_layout_axis(void);
BUSTER_F_DECL f32 ui_pop_fixed_x(void);
BUSTER_F_DECL f32 ui_pop_fixed_y(void);
BUSTER_F_DECL f32 ui_pop_fixed_width(void);
BUSTER_F_DECL f32 ui_pop_fixed_height(void);
BUSTER_F_DECL UI_Size ui_pop_pref_width(void);
BUSTER_F_DECL UI_Size ui_pop_pref_height(void);
BUSTER_F_DECL f32 ui_pop_min_width(void);
BUSTER_F_DECL f32 ui_pop_min_height(void);
BUSTER_F_DECL UI_BoxFlags ui_pop_flags(void);
BUSTER_F_DECL float4 ui_pop_background_color(void);
BUSTER_F_DECL float4 ui_pop_text_color(void);
BUSTER_F_DECL float4 ui_pop_border_color(void);
BUSTER_F_DECL f32 ui_pop_font_size(void);
BUSTER_F_DECL f32 ui_pop_text_padding(void);
BUSTER_F_DECL UI_TextAlign ui_pop_text_alignment(void);

BUSTER_F_DECL UI_Box* ui_set_next_parent(UI_Box* v);
BUSTER_F_DECL Axis2 ui_set_next_child_layout_axis(Axis2 v);
BUSTER_F_DECL f32 ui_set_next_fixed_x(f32 v);
BUSTER_F_DECL f32 ui_set_next_fixed_y(f32 v);
BUSTER_F_DECL f32 ui_set_next_fixed_width(f32 v);
BUSTER_F_DECL f32 ui_set_next_fixed_height(f32 v);
BUSTER_F_DECL UI_Size ui_set_next_pref_width(UI_Size v);
BUSTER_F_DECL UI_Size ui_set_next_pref_height(UI_Size v);
BUSTER_F_DECL f32 ui_set_next_min_width(f32 v);
BUSTER_F_DECL f32 ui_set_next_min_height(f32 v);
BUSTER_F_DECL UI_BoxFlags ui_set_next_flags(UI_BoxFlags v);
BUSTER_F_DECL float4 ui_set_next_background_color(float4 v);
BUSTER_F_DECL float4 ui_set_next_text_color(float4 v);
BUSTER_F_DECL float4 ui_set_next_border_color(float4 v);
BUSTER_F_DECL f32 ui_set_next_font_size(f32 v);
BUSTER_F_DECL f32 ui_set_next_text_padding(f32 v);
BUSTER_F_DECL UI_TextAlign ui_set_next_text_alignment(UI_TextAlign v);

// Compatibility stack macro names from the old ui_core.
#define ui_push(field_name, value) ui_push_##field_name(value)
#define ui_pop(field_name) ui_pop_##field_name()
#define ui_top(field_name) ui_top_##field_name()
#define ui_push_next_only(field_name, value) ui_set_next_##field_name(value)

// Core API
BUSTER_F_DECL UI_State* ui_state_allocate(RenderingHandle* rendering, RenderingWindowHandle* window);
BUSTER_F_DECL void ui_state_deinitialize(UI_State* state);
BUSTER_F_DECL void ui_state_select(UI_State* state);
BUSTER_F_DECL UI_State* ui_state_get(void);
BUSTER_F_DECL Arena* ui_build_arena(void);
BUSTER_F_DECL UI_Box* ui_root_from_state(UI_State* state);
BUSTER_F_DECL u8 ui_build_begin(WmHandle* windowing, WmWindowHandle* window, f64 frame_time, WmEventList event_queue);
BUSTER_F_DECL void ui_build_end(void);
BUSTER_F_DECL void ui_draw(void);

// UI event queue. ui_build_begin converts WmEventList input into this list.
BUSTER_F_DECL UI_EventNode* ui_event_list_push(Arena* arena, UI_EventList* list, UI_Event* event);
BUSTER_F_DECL void ui_eat_event_node(UI_EventList* list, UI_EventNode* node);
BUSTER_F_DECL bool ui_next_event(UI_Event** event);
BUSTER_F_DECL void ui_eat_event(UI_Event* event);

// Keys, sizes, boxes, interaction
BUSTER_F_DECL UI_Key ui_key_zero(void);
BUSTER_F_DECL UI_Key ui_key_make(u64 value);
BUSTER_F_DECL UI_Key ui_key_from_string(UI_Key seed, String8 string);
BUSTER_F_DECL bool ui_key_match(UI_Key a, UI_Key b);
BUSTER_F_DECL UI_Size ui_size(UI_SizeKind kind, f32 value, f32 strictness);
BUSTER_F_DECL UI_Size ui_pixels(u32 width, f32 strictness);
BUSTER_F_DECL UI_Size ui_percentage(f32 percentage, f32 strictness);
BUSTER_F_DECL UI_Size ui_em(f32 value, f32 strictness);
BUSTER_F_DECL UI_Size ui_text_dim(f32 padding, f32 strictness);
BUSTER_F_DECL UI_Size ui_children_sum(f32 strictness);
#define ui_px(value, strictness) ui_size(UI_SizeKind_Pixels, (f32)(value), (strictness))
#define ui_pct(value, strictness) ui_percentage((value), (strictness))
BUSTER_F_DECL UI_Box* ui_box_from_key(UI_Key key);
BUSTER_F_DECL UI_Box* ui_build_box_from_key(UI_BoxFlags flags, UI_Key key);
BUSTER_F_DECL UI_Box* ui_build_box_from_string(UI_BoxFlags flags, String8 string);
BUSTER_F_DECL UI_Box* ui_build_box_from_stringf(UI_BoxFlags flags, String8 format, ...);
BUSTER_F_DECL UI_BoxRec ui_box_rec_df(UI_Box* box, UI_Box* root, u64 sibling_offset, u64 child_offset);
BUSTER_F_DECL UI_Signal ui_signal_from_box(UI_Box* box);

#define ui_box_rec_df_pre(box, root) ui_box_rec_df((box), (root), BUSTER_OFFSET_OF(UI_Box, next), BUSTER_OFFSET_OF(UI_Box, first))
#define ui_box_rec_df_post(box, root) ui_box_rec_df((box), (root), BUSTER_OFFSET_OF(UI_Box, prev), BUSTER_OFFSET_OF(UI_Box, last))

// Compatibility construction API.
BUSTER_F_DECL UI_BoxFlags ui_box_flags_from_widget_flags(UI_WidgetFlags flags);
BUSTER_F_DECL UI_Signal ui_signal_from_widget(UI_Widget* widget);
BUSTER_F_DECL UI_Widget* ui_widget_make(UI_WidgetFlags flags, String8 string);
BUSTER_F_DECL UI_Widget* ui_widget_make_format(UI_WidgetFlags flags, String8 format, ...);

// Macro-loop conveniences in RAD style.
#define UI_Parent(v) for (u8 ui_once = (ui_push_parent(v), 1); ui_once; ui_once = (ui_pop_parent(), 0))
#define UI_ChildLayoutAxis(v) for (u8 ui_once = (ui_push_child_layout_axis(v), 1); ui_once; ui_once = (ui_pop_child_layout_axis(), 0))
#define UI_PrefWidth(v) for (u8 ui_once = (ui_push_pref_width(v), 1); ui_once; ui_once = (ui_pop_pref_width(), 0))
#define UI_PrefHeight(v) for (u8 ui_once = (ui_push_pref_height(v), 1); ui_once; ui_once = (ui_pop_pref_height(), 0))
#define UI_BackgroundColor(v) for (u8 ui_once = (ui_push_background_color(v), 1); ui_once; ui_once = (ui_pop_background_color(), 0))
#define UI_TextColor(v) for (u8 ui_once = (ui_push_text_color(v), 1); ui_once; ui_once = (ui_pop_text_color(), 0))
#define UI_FontSize(v) for (u8 ui_once = (ui_push_font_size(v), 1); ui_once; ui_once = (ui_pop_font_size(), 0))
#define UI_Flags(v) for (u8 ui_once = (ui_push_flags(v), 1); ui_once; ui_once = (ui_pop_flags(), 0))
#define UI_FlagsAdd(v) for (u8 ui_once = (ui_push_flags(ui_top_flags() | (v)), 1); ui_once; ui_once = (ui_pop_flags(), 0))
#define UI_WidthFill UI_PrefWidth(ui_percentage(1.0f, 0.0f))
#define UI_HeightFill UI_PrefHeight(ui_percentage(1.0f, 0.0f))
