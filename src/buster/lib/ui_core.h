#pragma once

#include <buster/lib/base.h>
#include <buster/lib/arena.h>
#include <buster/lib/rendering.h>

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
    UI_EventKind_FileDrop,
    UI_EventKind_COUNT,
} UI_EventKind;

typedef struct UI_EventNode UI_EventNode;
typedef struct UI_Event UI_Event;
struct UI_Event
{
    UI_EventKind kind;
    WmKey key;
    u8 modifiers;
    String8 string;
    SliceString8 paths;
    float2 pos;
    float2 delta;
    // Filled by the UI event router. Pointer/scroll/drop ownership comes from
    // the previous completed tree; a zero key with owner_assigned set means
    // the event is quarantined because no completed hit tree exists. Active
    // capture and focus transitions are applied in event-list order before
    // box signals report their owned events.
    u64 owner_key;
    u8 owner_assigned;
    // Set by the chronological router for transitions that signals must
    // report without replaying global state in box/build order.
    u8 route_flags;
    u8 reserved_owner[6];
};

typedef u8 UI_EventRouteFlags;
enum
{
    UI_EventRouteFlag_None = 0,
    UI_EventRouteFlag_FocusChanged = (1u << 0),
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

typedef struct UI_EventIterator UI_EventIterator;
struct UI_EventIterator
{
    UI_EventList list;
    UI_EventNode* current;
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
#define UI_SIZE_PERCENTAGE UI_SizeKind_ParentPct
#define UI_SIZE_BY_CHILDREN UI_SizeKind_ChildrenSum
#define UI_SIZE_KIND_COUNT ((UI_SizeKind)(UI_SizeKind_ChildrenSum + 1))

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

// Compatibility bitfield accepted by ui_box_make*.
typedef u64 UI_BoxFlags;

#define UI_BoxFlag_MouseClickable (UI_BoxFlags)(1ull << 0)
#define UI_BoxFlag_KeyboardClickable (UI_BoxFlags)(1ull << 1)
#define UI_BoxFlag_DropSite (UI_BoxFlags)(1ull << 2)
#define UI_BoxFlag_ClickToFocus (UI_BoxFlags)(1ull << 3)
#define UI_BoxFlag_Scroll (UI_BoxFlags)(1ull << 4)
#define UI_BoxFlag_ViewScrollX (UI_BoxFlags)(1ull << 5)
#define UI_BoxFlag_ViewScrollY (UI_BoxFlags)(1ull << 6)
#define UI_BoxFlag_ViewClampX (UI_BoxFlags)(1ull << 7)
#define UI_BoxFlag_ViewClampY (UI_BoxFlags)(1ull << 8)
#define UI_BoxFlag_FocusHot (UI_BoxFlags)(1ull << 9)
#define UI_BoxFlag_FocusActive (UI_BoxFlags)(1ull << 10)
// These suppress the corresponding focus mode even when the matching focus
// flag is present. They are independent of UI_BoxFlag_Disabled.
#define UI_BoxFlag_FocusHotDisabled (UI_BoxFlags)(1ull << 11)
#define UI_BoxFlag_FocusActiveDisabled (UI_BoxFlags)(1ull << 12)
#define UI_BoxFlag_DefaultFocusNavX (UI_BoxFlags)(1ull << 13)
#define UI_BoxFlag_DefaultFocusNavY (UI_BoxFlags)(1ull << 14)
#define UI_BoxFlag_DefaultFocusEdit (UI_BoxFlags)(1ull << 15)
#define UI_BoxFlag_FocusNavSkip (UI_BoxFlags)(1ull << 16)
#define UI_BoxFlag_DisableTruncatedHover (UI_BoxFlags)(1ull << 17)
#define UI_BoxFlag_Disabled (UI_BoxFlags)(1ull << 18)

//- rjf: layout
#define UI_BoxFlag_FloatingX (UI_BoxFlags)(1ull << 19)
#define UI_BoxFlag_FloatingY (UI_BoxFlags)(1ull << 20)
#define UI_BoxFlag_FixedWidth (UI_BoxFlags)(1ull << 21)
#define UI_BoxFlag_FixedHeight (UI_BoxFlags)(1ull << 22)
#define UI_BoxFlag_AllowOverflowX (UI_BoxFlags)(1ull << 23)
#define UI_BoxFlag_AllowOverflowY (UI_BoxFlags)(1ull << 24)
#define UI_BoxFlag_SkipViewOffX (UI_BoxFlags)(1ull << 25)
#define UI_BoxFlag_SkipViewOffY (UI_BoxFlags)(1ull << 26)

//- rjf: appearance / animation
#define UI_BoxFlag_DrawDropShadow (UI_BoxFlags)(1ull << 27)
#define UI_BoxFlag_DrawBackgroundBlur (UI_BoxFlags)(1ull << 28)
#define UI_BoxFlag_DrawBackground (UI_BoxFlags)(1ull << 29)
#define UI_BoxFlag_DrawBorder (UI_BoxFlags)(1ull << 30)
#define UI_BoxFlag_DrawSideTop (UI_BoxFlags)(1ull << 31)
#define UI_BoxFlag_DrawSideBottom (UI_BoxFlags)(1ull << 32)
#define UI_BoxFlag_DrawSideLeft (UI_BoxFlags)(1ull << 33)
#define UI_BoxFlag_DrawSideRight (UI_BoxFlags)(1ull << 34)
#define UI_BoxFlag_DrawText (UI_BoxFlags)(1ull << 35)
#define UI_BoxFlag_DrawTextFastpathCodepoint (UI_BoxFlags)(1ull << 36)
#define UI_BoxFlag_DrawTextWeak (UI_BoxFlags)(1ull << 37)
#define UI_BoxFlag_DrawHotEffects (UI_BoxFlags)(1ull << 38)
#define UI_BoxFlag_DrawActiveEffects (UI_BoxFlags)(1ull << 39)
#define UI_BoxFlag_DrawOverlay (UI_BoxFlags)(1ull << 40)
#define UI_BoxFlag_DrawBucket (UI_BoxFlags)(1ull << 41)
#define UI_BoxFlag_DrawFadeTop (UI_BoxFlags)(1ull << 42)
#define UI_BoxFlag_DrawFadeBottom (UI_BoxFlags)(1ull << 43)
#define UI_BoxFlag_DrawFadeLeft (UI_BoxFlags)(1ull << 44)
#define UI_BoxFlag_DrawFadeRight (UI_BoxFlags)(1ull << 45)
#define UI_BoxFlag_Clip (UI_BoxFlags)(1ull << 46)
#define UI_BoxFlag_AnimatePosX (UI_BoxFlags)(1ull << 47)
#define UI_BoxFlag_AnimatePosY (UI_BoxFlags)(1ull << 48)
#define UI_BoxFlag_DisableTextTrunc (UI_BoxFlags)(1ull << 49)
#define UI_BoxFlag_DisableIDString (UI_BoxFlags)(1ull << 50)
#define UI_BoxFlag_DisableFocusBorder (UI_BoxFlags)(1ull << 51)
#define UI_BoxFlag_DisableFocusOverlay (UI_BoxFlags)(1ull << 52)
#define UI_BoxFlag_HasDisplayString (UI_BoxFlags)(1ull << 53)
#define UI_BoxFlag_HasFuzzyMatchRanges (UI_BoxFlags)(1ull << 54)
#define UI_BoxFlag_RoundChildrenByParent (UI_BoxFlags)(1ull << 55)
#define UI_BoxFlag_SquishAnchored (UI_BoxFlags)(1ull << 56)

//- rjf: debug
#define UI_BoxFlag_Debug (UI_BoxFlags)(1ull << 63)

//- rjf: bundles
#define UI_BoxFlag_Clickable (UI_BoxFlag_MouseClickable | UI_BoxFlag_KeyboardClickable)
#define UI_BoxFlag_DefaultFocusNav (UI_BoxFlag_DefaultFocusNavX | UI_BoxFlag_DefaultFocusNavY | UI_BoxFlag_DefaultFocusEdit)
#define UI_BoxFlag_Floating (UI_BoxFlag_FloatingX | UI_BoxFlag_FloatingY)
#define UI_BoxFlag_FixedSize (UI_BoxFlag_FixedWidth | UI_BoxFlag_FixedHeight)
#define UI_BoxFlag_AllowOverflow (UI_BoxFlag_AllowOverflowX | UI_BoxFlag_AllowOverflowY)
#define UI_BoxFlag_AnimatePos (UI_BoxFlag_AnimatePosX | UI_BoxFlag_AnimatePosY)
#define UI_BoxFlag_ViewScroll (UI_BoxFlag_ViewScrollX | UI_BoxFlag_ViewScrollY)
#define UI_BoxFlag_ViewClamp (UI_BoxFlag_ViewClampX | UI_BoxFlag_ViewClampY)
#define UI_BoxFlag_DisableFocusEffects (UI_BoxFlag_DisableFocusBorder | UI_BoxFlag_DisableFocusOverlay)

#define UI_BOX_FLAG_COUNT (57)
#define UI_BoxFlag_AllContiguous (UI_BoxFlags)((1ull << UI_BOX_FLAG_COUNT) - 1ull)
#define UI_BoxFlag_All (UI_BoxFlags)(UI_BoxFlag_AllContiguous | UI_BoxFlag_Debug)

BUSTER_CT_CHECK((UI_BoxFlag_All & UI_BoxFlag_AllContiguous) == UI_BoxFlag_AllContiguous);
BUSTER_CT_CHECK((UI_BoxFlag_All & UI_BoxFlag_Debug) == UI_BoxFlag_Debug);
BUSTER_CT_CHECK((UI_BoxFlag_All & ((UI_BoxFlags)0x3full << 57)) == 0);

typedef enum UI_BoxFlagSupportKind
{
    UI_BoxFlagSupport_Interaction,
    UI_BoxFlagSupport_Layout,
    UI_BoxFlagSupport_Appearance,
    UI_BoxFlagSupport_Debug,
} UI_BoxFlagSupportKind;

typedef struct UI_BoxFlagInfo UI_BoxFlagInfo;
struct UI_BoxFlagInfo
{
    UI_BoxFlags flag;
    String8 name;
    UI_BoxFlagSupportKind kind;
    bool implemented;
    bool renderer_dependency;
};

typedef u32 UI_BoxRendererDependencyFlags;
enum
{
    UI_BoxRendererDependency_None = 0,
    UI_BoxRendererDependency_BackgroundBlur = (1u << 0),
    UI_BoxRendererDependency_BucketSubmission = (1u << 1),
    UI_BoxRendererDependency_CornerRadii = (1u << 2),
    UI_BoxRendererDependency_TextScissor = (1u << 3),
};

typedef u32 UI_BoxStateFlags;
enum
{
    UI_BoxState_Visible = (1u << 0),
    UI_BoxState_Hot = (1u << 1),
    UI_BoxState_Active = (1u << 2),
    UI_BoxState_FocusHot = (1u << 3),
    UI_BoxState_FocusActive = (1u << 4),
    UI_BoxState_FocusEdit = (1u << 5),
    UI_BoxState_TextTruncated = (1u << 6),
    UI_BoxState_Clipped = (1u << 7),
    UI_BoxState_Scrollable = (1u << 8),
    UI_BoxState_RendererDependency = (1u << 9),
    UI_BoxState_TextFastpath = (1u << 10),
    UI_BoxState_Overlay = (1u << 11),
    UI_BoxState_Fade = (1u << 12),
    UI_BoxState_Tooltip = (1u << 13),
};

typedef struct UI_FuzzyMatchRange UI_FuzzyMatchRange;
struct UI_FuzzyMatchRange
{
    u64 first;
    u64 one_past_last;
};

// union UI_BoxFlags
// {
//     struct
//     {
//         // Interaction
//         mouse_clickable,
//         keyboard_pressable,
//         drop_site,
//         click_to_focus,
//         scroll,
//         view_scroll_x,
//         view_scroll_y,
//         view_clamp_x,
//         view_clamp_y,
//         focus_hot,
//         focus_active,
//         focus_hot_disabled,
//         focus_active_disabled,
//         default_focus_nav_x,
//         default_focus_nav_y,
//         default_focus_edit,
//         focus_nav_skip,
//         disable_truncated_hover,
//         disabled,
//
//         // Layout
//         floating_x,
//         floating_y,
//         fixed_width,
//         fixed_height,
//         allow_overflow_x,
//         allow_overflow_y,
//         skip_view_off_x,
//         skip_view_off_y,
//
//         // Appearance / animation
//         draw_drop_shadow,
//         draw_background_blur,
//         draw_background,
//         draw_border,
//         draw_side_top,
//         draw_side_bottom,
//         draw_side_left,
//         draw_side_right,
//         draw_text,
//         draw_text_fastpath_codepoint,
//         draw_text_weak,
//         draw_hot_effects,
//         draw_active_effects,
//         draw_overlay,
//         draw_bucket,
//         draw_fade_top,
//         draw_fade_bottom,
//         draw_fade_left,
//         draw_fade_right,
//         clip,
//         animation_pos_x,
//         animation_pos_y,
//         disable_text_trunc,
//         disable_id_string,
//         disable_focus_border,
//         disable_focus_overlay,
//         has_display_string,
//         has_fuzzy_match_ranges,
//         round_children_by_parent,
//         squish_anchored,
//         debug,
//     };
//     u64 v;
// };
BUSTER_CT_CHECK(sizeof(UI_BoxFlags) == sizeof(u64));

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
    String8 raw_string;
    String8 string;
    String8 display_string;
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
    u32 fastpath_codepoint;
    f32 corner_radii[(u64)CORNER_COUNT];

    // per-build artifacts
    F32Interval2 rect;
    F32Interval2 clip_rect;
    float2 position_delta;
    u64 build_order;
    u64 draw_order;
    u32 draw_pass;
    u32 reserved_draw_order;
    // UTF-8 byte offset of the visible text prefix. text_visible_columns is
    // the corresponding display-column count after reserving an ellipsis.
    u64 text_visible_length;
    u64 text_visible_columns;
    F32Interval2 tooltip_rect;
    // Tooltip text is wrapped/truncated into the current build arena and is
    // valid through the current UI draw, just like display_string.
    String8 tooltip_string;
    u64 tooltip_line_count;
    UI_BoxRendererDependencyFlags renderer_dependency_flags;
    UI_BoxStateFlags state_flags;
    UI_FuzzyMatchRange* fuzzy_match_ranges;
    u64 fuzzy_match_range_count;
    bool visible;
    bool text_truncated;
    bool tooltip_visible;
    bool tooltip_text_truncated;
    bool has_previous_rect;
    u8 reserved_artifacts[2];

    // persistent interaction/animation data
    u64 first_touched_build_index;
    u64 last_touched_build_index;
    f32 hot_t;
    f32 active_t;
    float2 view_off;
    float2 view_bounds;
};

typedef UI_Box UI_Box;

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

typedef enum UI_DrawCommandKind
{
    UI_DrawCommandKind_Rect,
    UI_DrawCommandKind_Text,
    UI_DrawCommandKind_COUNT,
} UI_DrawCommandKind;

// UI-owned audit/draw commands produced by ui_draw. They are not a second
// renderer command stream: box pointers refer to persistent UI boxes, text
// pointers borrow the completed build arena, and remain valid until that arena
// is reused by a later build. The state capacity is a pre-counted upper bound
// and every emitted command must fit; command loss is an invariant failure,
// never a valid result.
typedef struct UI_DrawCommand UI_DrawCommand;
struct UI_DrawCommand
{
    UI_DrawCommandKind kind;
    UI_Box* box;
    F32Interval2 rect;
    F32Interval2 clip_rect;
    float4 colors[4];
    float4 corner_radii;
    String8 text;
};

typedef u32 UI_SignalFlags;
enum
{
    UI_SignalFlag_LeftPressed = (1u << 0),
    UI_SignalFlag_LeftReleased = (1u << 1),
    UI_SignalFlag_LeftClicked = (1u << 2),
    UI_SignalFlag_Hovering = (1u << 3),
    UI_SignalFlag_MouseOver = (1u << 4),
    UI_SignalFlag_KeyboardPressed = (1u << 5),
    UI_SignalFlag_Dragging = (1u << 6),
    UI_SignalFlag_Scrolled = (1u << 7),
    UI_SignalFlag_Dropped = (1u << 8),
    UI_SignalFlag_FocusChanged = (1u << 9),
    UI_SignalFlag_Focused = (1u << 10),
    UI_SignalFlag_Pressed = UI_SignalFlag_LeftPressed | UI_SignalFlag_KeyboardPressed,
    UI_SignalFlag_Released = UI_SignalFlag_LeftReleased,
    UI_SignalFlag_Clicked = UI_SignalFlag_LeftClicked | UI_SignalFlag_KeyboardPressed,
};

typedef struct UI_Signal UI_Signal;
struct UI_Signal
{
    UI_Box* box;
    UI_SignalFlags f;
    WmKey key;
    u8 modifiers;
    u8 reserved_key[2];
    float2 scroll_delta;
    u32 clicked_left : 1;
    u32 pressed_left : 1;
    u32 released_left : 1;
    u32 hovering : 1;
    u32 mouse_over : 1;
    u32 dragging : 1;
    u32 scrolled : 1;
    u32 dropped : 1;
    u32 focus_changed : 1;
    u32 focused : 1;
    u32 reserved : 22;
    // File-drop paths are valid for the duration of the current build and
    // are copied into the build arena by ui_signal_from_box.
    SliceString8 drop_paths;
};

#define ui_pressed(s) !!((s).f & UI_SignalFlag_Pressed)
#define ui_clicked(s) !!((s).f & UI_SignalFlag_Clicked)
#define ui_released(s) !!((s).f & UI_SignalFlag_Released)
#define ui_hovering(s) !!((s).f & UI_SignalFlag_Hovering)
#define ui_mouse_over(s) !!((s).f & UI_SignalFlag_MouseOver)
#define ui_dragging(s) !!((s).f & UI_SignalFlag_Dragging)
#define ui_scrolled(s) !!((s).f & UI_SignalFlag_Scrolled)
#define ui_dropped(s) !!((s).f & UI_SignalFlag_Dropped)
#define ui_focus_changed(s) !!((s).f & UI_SignalFlag_FocusChanged)
#define ui_focused(s) !!((s).f & UI_SignalFlag_Focused)

#define UI_STACK_CAPACITY (64)

typedef struct UI_StateStacks UI_StateStacks;
struct UI_StateStacks
{
    UI_Box* parent[UI_STACK_CAPACITY];
    u32 parent_length;
    u8 parent_auto_pop;
    Axis2 child_layout_axis[UI_STACK_CAPACITY];
    u32 child_layout_axis_length;
    u8 child_layout_axis_auto_pop;
    f32 fixed_x[UI_STACK_CAPACITY];
    u32 fixed_x_length;
    u8 fixed_x_auto_pop;
    f32 fixed_y[UI_STACK_CAPACITY];
    u32 fixed_y_length;
    u8 fixed_y_auto_pop;
    f32 fixed_width[UI_STACK_CAPACITY];
    u32 fixed_width_length;
    u8 fixed_width_auto_pop;
    f32 fixed_height[UI_STACK_CAPACITY];
    u32 fixed_height_length;
    u8 fixed_height_auto_pop;
    UI_Size pref_width[UI_STACK_CAPACITY];
    u32 pref_width_length;
    u8 pref_width_auto_pop;
    UI_Size pref_height[UI_STACK_CAPACITY];
    u32 pref_height_length;
    u8 pref_height_auto_pop;
    f32 min_width[UI_STACK_CAPACITY];
    u32 min_width_length;
    u8 min_width_auto_pop;
    f32 min_height[UI_STACK_CAPACITY];
    u32 min_height_length;
    u8 min_height_auto_pop;
    UI_BoxFlags flags[UI_STACK_CAPACITY];
    u32 flags_length;
    u8 flags_auto_pop;
    float4 background_color[UI_STACK_CAPACITY];
    u32 background_color_length;
    u8 background_color_auto_pop;
    float4 text_color[UI_STACK_CAPACITY];
    u32 text_color_length;
    u8 text_color_auto_pop;
    float4 border_color[UI_STACK_CAPACITY];
    u32 border_color_length;
    u8 border_color_auto_pop;
    f32 font_size[UI_STACK_CAPACITY];
    u32 font_size_length;
    u8 font_size_auto_pop;
    f32 text_padding[UI_STACK_CAPACITY];
    u32 text_padding_length;
    u8 text_padding_auto_pop;
    UI_TextAlign text_alignment[UI_STACK_CAPACITY];
    u32 text_alignment_length;
    u8 text_alignment_auto_pop;
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
    // The completed tree from the preceding build.  The event router uses
    // this tree for keyboard navigation and pointer ownership before the new
    // tree has been constructed.
    UI_Box* previous_root;
    UI_Box* first_free_box;
    u64 box_table_size;
    UI_BoxHashSlot* box_table;
    UI_DrawCommand* draw_commands;
    u64 draw_command_count;
    u64 draw_command_capacity;
    bool draw_commands_complete;
    u8 reserved_draw_commands[7];
    UI_Box* draw_command_box;
    // Pointer/scroll/drop ownership is routed from the previous completed
    // tree. It is false on the first build, so current partial build order
    // cannot claim an event.
    bool pointer_targets_assigned;
    u8 reserved_pointer_targets[7];

    UI_Key hot_box_key;
    UI_Key active_box_key[(u64)UI_MouseButtonKind_COUNT];
    UI_Key focus_hot_key;
    UI_Key focus_active_key;
    UI_Key focus_edit_key;
    // Keyboard navigation is resolved by the pre-build event router against
    // the previous completed tree.  A changed target is published to that
    // target's signal in the following build, and is discarded after that
    // delivery window.
    UI_Key focus_changed_key;
    u64 focus_changed_delivery_build_index;
    float2 mouse;
    // Pointer position from the preceding completed frame; event routing
    // advances from this position in event-list order.
    float2 previous_mouse;
    u64 next_build_order;
    u64 next_draw_order;
    u64 is_animating : 1;
    u64 focus_changed : 1;
    u64 focus_changed_pending : 1;
    u64 reserved : 61;

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
BUSTER_F_DECL UI_EventList ui_event_list_from_wm_events(Arena* arena, WmWindowHandle* window, WmEventList event_queue);
BUSTER_F_DECL void ui_build_begin(WmHandle* windowing, WmWindowHandle* window, f64 frame_time_milliseconds, UI_EventList events);
BUSTER_F_DECL void ui_build_end(void);
BUSTER_F_DECL void ui_draw(void);

// UI event queue. Window-system events are translated before ui_build_begin.
BUSTER_F_DECL UI_EventNode* ui_event_list_push(Arena* arena, UI_EventList* list, UI_Event* event);
BUSTER_F_DECL void ui_eat_event_node(UI_EventList* list, UI_EventNode* node);
BUSTER_F_DECL UI_Event* ui_next_event(UI_EventIterator* iterator);
BUSTER_F_DECL void ui_eat_event(UI_Event* event);

// Keys, sizes, boxes, interaction
BUSTER_F_DECL UI_Key ui_key_zero(void);
BUSTER_F_DECL UI_Key ui_key_make(u64 value);
BUSTER_F_DECL UI_Key ui_key_from_string(UI_Key seed, String8 string);
BUSTER_F_DECL bool ui_key_match(UI_Key a, UI_Key b);
BUSTER_F_DECL UI_BoxFlagInfo ui_box_flag_info(u32 bit_index);
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
BUSTER_F_DECL void ui_box_set_display_string(UI_Box* box, String8 string);
BUSTER_F_DECL void ui_box_set_fastpath_codepoint(UI_Box* box, u32 codepoint);
// Fuzzy-match ranges are UTF-8 byte ranges in the displayed string. The
// ranges are copied into the current build arena and may be frame-borrowed.
BUSTER_F_DECL void ui_box_set_fuzzy_match_ranges(UI_Box* box, UI_FuzzyMatchRange* ranges, u64 count);
BUSTER_F_DECL void ui_box_set_corner_radii(UI_Box* box, f32 radius);
BUSTER_F_DECL bool ui_box_is_visible(UI_Box* box);
BUSTER_F_DECL bool ui_box_is_focused(UI_Box* box);
BUSTER_F_DECL bool ui_box_has_tooltip(UI_Box* box);
BUSTER_F_DECL void ui_box_scroll_by(UI_Box* box, float2 delta);
BUSTER_F_DECL float2 ui_box_scroll_offset(UI_Box* box);
BUSTER_F_DECL UI_BoxRec ui_box_rec_df(UI_Box* box, UI_Box* root, u64 sibling_offset, u64 child_offset);
BUSTER_F_DECL UI_Signal ui_signal_from_box(UI_Box* box);

#define ui_box_rec_df_pre(box, root) ui_box_rec_df((box), (root), BUSTER_OFFSET_OF(UI_Box, next), BUSTER_OFFSET_OF(UI_Box, first))
#define ui_box_rec_df_post(box, root) ui_box_rec_df((box), (root), BUSTER_OFFSET_OF(UI_Box, prev), BUSTER_OFFSET_OF(UI_Box, last))

// Compatibility construction API.
BUSTER_F_DECL UI_BoxFlags ui_box_flags_from_box_flags(UI_BoxFlags flags);
BUSTER_F_DECL UI_Signal ui_signal_from_box(UI_Box* box);
BUSTER_F_DECL UI_Box* ui_box_make(UI_BoxFlags flags, String8 string);
BUSTER_F_DECL UI_Box* ui_box_make_format(UI_BoxFlags flags, String8 format, ...);

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
#define UI_MinWidth(v) for (u8 ui_once = (ui_push_min_width(v), 1); ui_once; ui_once = (ui_pop_min_width(), 0))
#define UI_MinHeight(v) for (u8 ui_once = (ui_push_min_height(v), 1); ui_once; ui_once = (ui_pop_min_height(), 0))
#define UI_TextPadding(v) for (u8 ui_once = (ui_push_text_padding(v), 1); ui_once; ui_once = (ui_pop_text_padding(), 0))
#define UI_TextAlign(v) for (u8 ui_once = (ui_push_text_alignment(v), 1); ui_once; ui_once = (ui_pop_text_alignment(), 0))
#define UI_BorderColor(v) for (u8 ui_once = (ui_push_border_color(v), 1); ui_once; ui_once = (ui_pop_border_color(), 0))
#define UI_WidthFill UI_PrefWidth(ui_percentage(1.0f, 0.0f))
#define UI_HeightFill UI_PrefHeight(ui_percentage(1.0f, 0.0f))
