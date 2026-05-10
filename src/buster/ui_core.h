#pragma once

#include <buster/base.h>
#include <buster/arena.h>
#include <buster/rendering.h>

// Windowing event types
typedef enum WindowingEventType
{
    WINDOWING_EVENT_TYPE_MOUSE_BUTTON,
    WINDOWING_EVENT_TYPE_CURSOR_POSITION,
    WINDOWING_EVENT_TYPE_CURSOR_ENTER,
    WINDOWING_EVENT_TYPE_WINDOW_FOCUS,
    WINDOWING_EVENT_TYPE_WINDOW_POSITION,
    WINDOWING_EVENT_TYPE_WINDOW_CLOSE,
    WINDOWING_EVENT_TYPE_COUNT,
} WindowingEventType;

typedef enum WindowingEventMouseButtonKind
{
    WINDOWING_EVENT_MOUSE_BUTTON_1 = 0,
    WINDOWING_EVENT_MOUSE_BUTTON_2 = 1,
    WINDOWING_EVENT_MOUSE_BUTTON_3 = 2,
    WINDOWING_EVENT_MOUSE_BUTTON_4 = 3,
    WINDOWING_EVENT_MOUSE_BUTTON_5 = 4,
    WINDOWING_EVENT_MOUSE_BUTTON_6 = 5,
    WINDOWING_EVENT_MOUSE_BUTTON_7 = 6,
    WINDOWING_EVENT_MOUSE_BUTTON_8 = 7,
    WINDOWING_EVENT_MOUSE_COUNT,
    WINDOWING_EVENT_MOUSE_LEFT = 0,
    WINDOWING_EVENT_MOUSE_RIGHT = 1,
    WINDOWING_EVENT_MOUSE_MIDDLE = 2,
} WindowingEventMouseButtonKind;

typedef enum WindowingEventMouseButtonAction
{
    WINDOWING_EVENT_MOUSE_RELAX = 0,
    WINDOWING_EVENT_MOUSE_RELEASE = 1,
    WINDOWING_EVENT_MOUSE_PRESS = 2,
    WINDOWING_EVENT_MOUSE_REPEAT = 3,
    WINDOWING_EVENT_MOUSE_ACTION_COUNT,
} WindowingEventMouseButtonAction;

typedef struct WindowingEventMouseButtonEvent WindowingEventMouseButtonEvent;
struct WindowingEventMouseButtonEvent
{
    WindowingEventMouseButtonAction action;
    u8 mod_shift:1;
    u8 mod_control:1;
    u8 mod_alt:1;
    u8 mod_super:1;
    u8 mod_caps_lock:1;
    u8 mod_num_lock:1;
    u8 reserved:2;
};

typedef struct WindowingEventMouseButton WindowingEventMouseButton;
struct WindowingEventMouseButton 
{
    WindowingEventMouseButtonKind button;
    WindowingEventMouseButtonEvent event;
};

typedef struct WindowingEventDescriptor WindowingEventDescriptor;
struct WindowingEventDescriptor
{
    u32 index:24;
    WindowingEventType type:8;
};
BUSTER_CT_CHECK(sizeof(WindowingEventDescriptor) == 4);

typedef struct WindowingEventCursorPosition WindowingEventCursorPosition;
struct WindowingEventCursorPosition
{
    f64 x;
    f64 y;
};

typedef struct WindowingEventWindowPosition WindowingEventWindowPosition;
struct WindowingEventWindowPosition
{
    u32 x;
    u32 y;
};

#define UI_EVENT_QUEUE_CAPACITY (256)

typedef struct WindowingEventQueue WindowingEventQueue;
struct WindowingEventQueue
{
    WindowingEventDescriptor descriptors[UI_EVENT_QUEUE_CAPACITY];
    u32 descriptor_count;
    WindowingEventMouseButton mouse_buttons[UI_EVENT_QUEUE_CAPACITY];
    u32 mouse_button_count;
    WindowingEventCursorPosition cursor_positions[UI_EVENT_QUEUE_CAPACITY];
    u32 cursor_position_count;
    WindowingEventWindowPosition window_positions[UI_EVENT_QUEUE_CAPACITY];
    u32 window_position_count;
};

// UI types
enum UI_SizeKind
{
    UI_SIZE_PIXEL_COUNT,
    UI_SIZE_PERCENTAGE,
    UI_SIZE_BY_CHILDREN,
    UI_SIZE_KIND_COUNT,
};

typedef u8 UI_SizeKind;

typedef struct UI_Size UI_Size;
struct UI_Size
{
    f32 value;
    f32 strictness;
    UI_SizeKind kind;
    u8 reserved[3];
};
BUSTER_CT_CHECK(sizeof(UI_Size) == 12);

typedef struct UI_Key UI_Key;
struct UI_Key
{
    u64 value;
};

typedef struct UI_MousePosition UI_MousePosition;
struct UI_MousePosition
{
    f64 x;
    f64 y;
};

enum UI_WidgetFlagEnum
{
    UI_WIDGET_FLAG_DISABLED                      = 1 << 0,
    UI_WIDGET_FLAG_MOUSE_CLICKABLE               = 1 << 1,
    UI_WIDGET_FLAG_KEYBOARD_PRESSABLE            = 1 << 2,
    UI_WIDGET_FLAG_DRAW_TEXT                     = 1 << 3,
    UI_WIDGET_FLAG_DRAW_BACKGROUND               = 1 << 4,
    UI_WIDGET_FLAG_OVERFLOW_X                    = 1 << 5,
    UI_WIDGET_FLAG_OVERFLOW_Y                    = 1 << 6,
    UI_WIDGET_FLAG_FLOATING_X                    = 1 << 7,
    UI_WIDGET_FLAG_FLOATING_Y                    = 1 << 8,
};

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
        u64 reserved:55;
    };
    u64 v;
};
BUSTER_CT_CHECK(sizeof(UI_WidgetFlags) == sizeof(u64));

typedef struct UI_Widget UI_Widget;
struct UI_Widget
{
    String8 text;

    UI_Widget* hash_previous;
    UI_Widget* hash_next;

    UI_Widget* first;
    UI_Widget* last;
    UI_Widget* next;
    UI_Widget* previous;
    UI_Widget* parent;
    u64 child_count;

    UI_Key key;

    // Input parameters
    UI_Size pref_size[(u64)AXIS2_COUNT];
    Axis2 child_layout_axis;
    u8 reserved[4];
    UI_WidgetFlags flags;

    // Data known after size determination happens
    float2 computed_size;
    float2 computed_relative_position;

    // Data known after layout computation happens
    F32Interval2 relative_rect;
    F32Interval2 rect;
    float2 relative_corner_delta[(u64)CORNER_COUNT];

    // Persistent data across frames
    u64 last_build_touched;
    float2 view_offset;
    float4 background_colors[4];
    float4 text_color;
};

typedef struct UI_WidgetSlot UI_WidgetSlot;
struct UI_WidgetSlot
{
    UI_Widget* first;
    UI_Widget* last;
};

#define UI_STACK_CAPACITY (64)

typedef struct UI_StateStackAutoPops UI_StateStackAutoPops;
struct UI_StateStackAutoPops 
{
    u64 parent:1;
    u64 pref_width:1;
    u64 pref_height:1;
    u64 child_layout_axis:1;
    u64 text_color:1;
    u64 background_color:1;
    u64 font_size:1;
    u64 reserved:57;
};
BUSTER_CT_CHECK(sizeof(UI_StateStackAutoPops) % sizeof(u64) == 0);

typedef struct UI_StateStackNulls UI_StateStackNulls;
struct UI_StateStackNulls
{
    float4 text_color;
    float4 background_color;
    UI_Widget* parent;
    UI_Size pref_width;
    UI_Size pref_height;
    Axis2 child_layout_axis;
    f32 font_size;
    u8 reserved[8];
};

typedef struct UI_StateStacks UI_StateStacks;
struct UI_StateStacks
{
    UI_Widget* parent[UI_STACK_CAPACITY];
    u32 parent_length;
    UI_Size pref_width[UI_STACK_CAPACITY];
    u32 pref_width_length;
    UI_Size pref_height[UI_STACK_CAPACITY];
    u32 pref_height_length;
    Axis2 child_layout_axis[UI_STACK_CAPACITY];
    u32 child_layout_axis_length;
    float4 text_color[UI_STACK_CAPACITY];
    u32 text_color_length;
    u8 reserved[12];
    float4 background_color[UI_STACK_CAPACITY];
    u32 background_color_length;
    f32 font_size[UI_STACK_CAPACITY];
    u32 font_size_length;
    u8 reserved2[8];
};

typedef struct UI_State UI_State;
struct UI_State
{
    Arena* arena;
    Arena* build_arenas[2];
    RenderingHandle* rendering;
    RenderingWindowHandle* rendering_window;
    OsWindowHandle* window;
    u64 build_count;
    f64 frame_time;
    UI_Widget* root;
    UI_MousePosition mouse_position;
    struct
    {
        UI_WidgetSlot* pointer;
        u64 length;
    } widget_table;
    UI_Widget* free_widget_list;
    u64 free_widget_count;
    WindowingEventMouseButtonEvent mouse_button_events[WINDOWING_EVENT_MOUSE_COUNT];
    u64 focused:1;
    u64 reserved:63;

    UI_StateStacks stacks;
    UI_StateStackNulls stack_nulls;
    UI_StateStackAutoPops stack_autopops;
    u8 reserved2[8];
};

enum UI_SignalFlag
{
    UI_SIGNAL_FLAG_CLICKED_LEFT = (1 << 0),
    UI_SIGNAL_FLAG_COUNT,
};
typedef u32 UI_SignalFlags;

typedef struct UI_Signal UI_Signal;
struct UI_Signal
{
    UI_Widget* widget;
    union
    {
        UI_SignalFlags flags;
        struct
        {
            u32 clicked_left:1;
            u32 reserved:31;
        };
    };
    u8 reserved2[4];
};

// Stack manipulation macros
#define ui_stack_autopop_set(field_name, value) ui_state->stack_autopops.field_name = (value)

#define ui_stack_push_impl(field_name, value, auto_pop_value) do { \
    BUSTER_CHECK(ui_state->stacks.field_name ## _length < UI_STACK_CAPACITY); \
    ui_state->stacks.field_name[ui_state->stacks.field_name ## _length] = (value); \
    ui_state->stacks.field_name ## _length += 1; \
    ui_stack_autopop_set(field_name, auto_pop_value); \
} while (0)

#define ui_push(field_name, value) ui_stack_push_impl(field_name, value, 0)
#define ui_push_next_only(field_name, value) ui_stack_push_impl(field_name, value, 1)

#define ui_pop(field_name) ( \
    BUSTER_CHECK(ui_state->stacks.field_name ## _length > 0), \
    ui_state->stacks.field_name ## _length -= 1, \
    ui_state->stacks.field_name[ui_state->stacks.field_name ## _length] \
)

#define ui_top(field_name) ( \
    ui_state->stacks.field_name ## _length \
        ? ui_state->stacks.field_name[ui_state->stacks.field_name ## _length - 1] \
        : ui_state->stack_nulls.field_name \
)

BUSTER_V_DECL UI_State* ui_state;

// Function declarations
BUSTER_F_DECL UI_State* ui_state_allocate(RenderingHandle* rendering, RenderingWindowHandle* window);
BUSTER_F_DECL void ui_state_deinitialize(UI_State* state);
BUSTER_F_DECL void ui_state_select(UI_State* state);
BUSTER_F_DECL u8 ui_build_begin(OsWindowingHandle* windowing, OsWindowHandle* window, f64 frame_time, OsWindowingEventList* event_queue);
BUSTER_F_DECL void ui_build_end(void);
BUSTER_F_DECL void ui_draw(void);
BUSTER_F_DECL UI_Signal ui_signal_from_widget(UI_Widget* widget);
BUSTER_F_DECL UI_State* ui_state_get(void);

BUSTER_F_DECL UI_Widget* ui_widget_make(UI_WidgetFlags flags, String8 string);
BUSTER_F_DECL UI_Widget* ui_widget_make_format(UI_WidgetFlags flags, String8 format, ...);
BUSTER_F_DECL UI_Size ui_pixels(u32 width, f32 strictness);
BUSTER_F_DECL UI_Size ui_percentage(f32 percentage, f32 strictness);
BUSTER_F_DECL UI_Size ui_em(f32 value, f32 strictness);
