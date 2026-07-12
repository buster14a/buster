#define BUSTER_USE_GRAPHICS 1

#include <buster/base.h>
#include <buster/entry_point.h>
#include <buster/ui_core.h>
#include <buster/rendering.h>
#include <buster/window.h>
#include <buster/font_provider.h>
#include <buster/time.h>
#include <buster/ui_builder.h>
#include <buster/arena.h>
#include <buster/compiler/frontend/buster/parser.h>
#include <buster/integer.h>
#include <buster/string.h>

#if BUSTER_UNITY_BUILD
#include <buster/arena.c>
#include <buster/integer.c>
#include <buster/os.c>
#include <buster/string.c>
#if BUSTER_INCLUDE_TESTS
#include <buster/test.c>
#endif
#include <buster/entry_point.c>
#include <buster/target.c>
#include <buster/simd.c>
#include <buster/file.c>
#include <buster/truetype.c>
#include <buster/font_provider.c>
#include <buster/window.c>
#include <buster/rendering.c>
#include <buster/ui_core.c>
#include <buster/ui_builder.c>
#include <buster/time.c>
#include <buster/float.c>
#include <buster/compiler/frontend/buster/parser.c>
#include <buster/hash.c>
#endif

typedef struct IdePanel IdePanel;
struct IdePanel
{
    IdePanel* first;
    IdePanel* last;
    IdePanel* previous;
    IdePanel* next;
    IdePanel* parent;
    f32 parent_percentage;
    Axis2 split_axis;
};

typedef struct IdeWindow IdeWindow;
struct IdeWindow
{
    WmWindowHandle* wm;
    RenderingWindowHandle* render;
    IdeWindow* previous;
    IdeWindow* next;
    IdePanel* root_panel;
    UI_State* ui;
    f32 dpi;
    f32 font_size;
    u32 font_height;
    u8 reserved[4];
};

typedef struct IdeProgram IdeProgram;
struct IdeProgram
{
    ProgramState state;
    IdeWindow* first_window;
    IdeWindow* last_window;
    WmHandle* windowing;
    RenderingHandle* rendering;
    bool test;
    bool bench;
    u8 reserved[6];
    TimeDataType last_frame_timestamp;
};

BUSTER_GLOBAL_LOCAL IdeProgram ide_state = {0};

BUSTER_V_IMPL ProgramState* program_state = &ide_state.state;

#define IDE_BASE_DPI (96.0f)
#define IDE_BASE_FONT_SIZE (24.0f)

BUSTER_GLOBAL_LOCAL f32 ide_font_size_from_dpi(f32 dpi)
{
    if (dpi <= 0.0f)
    {
        dpi = IDE_BASE_DPI;
    }

    return BUSTER_CLAMP(6.0f, IDE_BASE_FONT_SIZE * (dpi / IDE_BASE_DPI), 72.0f);
}

BUSTER_GLOBAL_LOCAL void ide_window_queue_font_update(IdeWindow* window, f32 dpi)
{
    f32 font_size = ide_font_size_from_dpi(dpi);
    u32 font_height = (u32)(font_size + 0.5f);
    if (font_height == 0)
    {
        font_height = 1;
    }

    String8 font_path = font_file_get_path(ide_state.state.arena, FONT_INDEX_MONO);
    FontTextureAtlas font = rendering_font_create(ide_state.state.arena, ide_state.rendering, (FontTextureAtlasCreate) {
            .font_path = font_path,
            .text_height = font_height,
            });
    rendering_queue_font_update(ide_state.rendering, window->render, RENDER_FONT_TYPE_MONOSPACE, font);

    window->dpi = dpi;
    window->font_size = font_size;
    window->font_height = font_height;
}

BUSTER_GLOBAL_LOCAL void ide_window_update_font_for_dpi(IdeWindow* window)
{
    if (window && window->wm && window->render)
    {
        f32 dpi = wm_window_get_dpi(ide_state.windowing, window->wm);
        f32 font_size = ide_font_size_from_dpi(dpi);
        u32 font_height = (u32)(font_size + 0.5f);
        if (dpi != window->dpi || font_height != window->font_height)
        {
            rendering_window_rect_texture_update_begin(window->render);
            ide_window_queue_font_update(window, dpi);
            rendering_window_rect_texture_update_end(ide_state.rendering, window->render);
        }
    }
}

#if BUSTER_FUZZ
BUSTER_EXPORT s32 buster_fuzz(const u8* pointer, size_t size)
{
    BUSTER_UNUSED(pointer);
    BUSTER_UNUSED(size);
    return 0;
}
#else
ProcessResult process_arguments(void)
{
    ProcessResult result = PROCESS_RESULT_SUCCESS;

    SliceString8 arguments = program_state->input.arguments;
    // SliceString8 environment = program_state->input.environment;

    // StringOsListIterator arg_it = string_os_list_iterator_initialize(argv);
    //
    // string_os_list_iterator_next(&arg_it);

    for (u64 i = 1; i < arguments.length; i += 1)
    {
        String8 arg = arguments.pointer[i];
        if (string_equal(arg, S8("test")))
        {
            ide_state.test = true;
        }
        else if (string_equal(arg, S8("bench")))
        {
            ide_state.bench = true;
        }
        else
        {
            ProcessResult r = buster_argument_process(i);
            if (r != PROCESS_RESULT_SUCCESS)
            {
                string_print(S8("Failed to process argument {S8}\n"), arg);
                result = r;
                break;
            }
        }
    }

    return result;
}

BUSTER_GLOBAL_LOCAL void ui_top_bar(void)
{
    ui_push(pref_height, ui_em(1, 1));
    {
        ui_push(child_layout_axis, AXIS2_X);
        UI_Box* top_bar = ui_box_make((UI_BoxFlags) {0}, S8("top_bar"));
        ui_push(parent, top_bar);
        {
            if (ui_button(S8("Button 123")).clicked_left)
            {
                string_print(S8("Button pressed\n"));
            }
            ui_button(S8("Button 2"));
            ui_button(S8("Button 3"));
        }
        BUSTER_UNUSED(ui_pop(parent));
        BUSTER_UNUSED(ui_pop(child_layout_axis));
    }
    BUSTER_UNUSED(ui_pop(pref_height));
}

typedef struct UI_Node UI_Node;
struct UI_Node
{
    String8 name;
    String8 type;
    String8 value;
    String8 name_space;
    String8 function;
};

BUSTER_GLOBAL_LOCAL void ui_node(UI_Node node)
{
    UI_BoxFlags flags = UI_BoxFlag_DrawBackground | UI_BoxFlag_DrawText;
    UI_Box* node_widget = ui_box_make_format(flags, S8("{S8} : {S8} = {S8}##{S8}{S8}"), node.name, node.type, node.value, node.function, node.name_space);
    BUSTER_UNUSED(node_widget);
}

BUSTER_GLOBAL_LOCAL u64 frame_depth = 0;

bool frame(void)
{
    frame_depth += 1;

    TimeDataType frame_end = timestamp_take();

    WmEventList event_list = {0};
    if (frame_depth == 1)
    {
        event_list = wm_poll_events(ide_state.state.arena, ide_state.windowing);
    }

    f64 frame_ms = (f64)timestamp_ns_between(ide_state.last_frame_timestamp, frame_end) / (1000 * 1000);
    ide_state.last_frame_timestamp = frame_end;

    for (WmEvent* event = event_list.first; event; event = event->next)
    {
        switch (event->kind)
        {
            break; case WM_EVENT_WINDOW_CLOSE:
            {
                for (IdeWindow* window = ide_state.first_window; window; window = window->next)
                {
                    if (window->wm == event->window)
                    {
                        if (window->previous)
                        {
                            window->previous->next = window->next;
                        }

                        if (window->next)
                        {
                            window->next->previous = window->previous;
                        }

                        if (ide_state.first_window == window)
                        {
                            ide_state.first_window = window->next;
                        }

                        if (ide_state.last_window == window)
                        {
                            ide_state.last_window = window->previous;
                        }

                        ui_state_deinitialize(window->ui);
                        window->ui = 0;
                        rendering_window_deinitialize(ide_state.rendering, window->render);
                        window->render = 0;

                        break;
                    }
                }
            }
            break; case WM_EVENT_TEXT_INPUT:
            {
                string_print(S8("User wrote \"{S8}\"\n"), event->text);
            }
            break; default:
            {
            }
            break; case WM_EVENT_COUNT: BUSTER_UNREACHABLE();
        }
    }

#if BUSTER_ANDROID
    {
        // While backgrounded/locked the native window (and its Vulkan surface)
        // is gone: skip rendering instead of crashing. On resume, rebuild the
        // surface/swapchain for the new native window before drawing again.
        static bool was_paused = false;
        if (!wm_window_is_visible(ide_state.windowing))
        {
            was_paused = true;
            frame_depth -= 1;
            return false;
        }
        if (was_paused)
        {
            was_paused = false;
            for (IdeWindow* w = ide_state.first_window; w; w = w->next)
            {
                if (w->render)
                {
                    rendering_window_surface_recreate(ide_state.rendering, ide_state.windowing, w->render, w->wm);
                }
            }
        }
    }
#endif

    IdeWindow* window = ide_state.first_window;
    while (window)
    {
        IdeWindow* next = window->next;

        RenderingWindowHandle* render_window = window->render;
        rendering_window_frame_begin(ide_state.rendering, render_window);
        ide_window_update_font_for_dpi(window);

        ui_state_select(window->ui);

        TemporalArena ui_events_scratch = scratch_begin(0, 0);
        UI_EventList ui_events = ui_event_list_from_wm_events(ui_events_scratch.arena, window->wm, event_list);
        ui_build_begin(ide_state.windowing, window->wm, frame_ms, ui_events);

        ui_push(font_size, window->font_size);

        ui_top_bar();
        ui_push(child_layout_axis, AXIS2_X);
        UI_Box* workspace_widget = ui_box_make_format((UI_BoxFlags) {0}, S8("workspace{u64}"), window->wm);
        ui_push(parent, workspace_widget);
        {
            // Node visualizer
            ui_push(child_layout_axis, AXIS2_Y);
            UI_Box* node_visualizer_widget = ui_box_make_format(UI_BoxFlag_DrawBackground, S8("node_visualizer{u64}"), window->wm);

            ui_push(parent, node_visualizer_widget);
            {
                ui_node((UI_Node) {
                    .name = S8("a"),
                    .type = S8("s32"),
                    .value = S8("1"),
                    .name_space = S8("foo"),
                    .function = S8("main"),
                });
                ui_node((UI_Node) {
                    .name = S8("b"),
                    .type = S8("s32"),
                    .value = S8("2"),
                    .name_space = S8("foo"),
                    .function = S8("main"),
                });
            }
            BUSTER_UNUSED(ui_pop(parent));
            BUSTER_UNUSED(ui_pop(child_layout_axis));

            // Side-panel stub
            ui_button(S8("Options"));
        }
        BUSTER_UNUSED(ui_pop(parent));
        BUSTER_UNUSED(ui_pop(child_layout_axis));

        ui_build_end();

        ui_draw();

        BUSTER_UNUSED(ui_pop(font_size));

        rendering_window_frame_end(ide_state.rendering, render_window);
        scratch_end(ui_events_scratch);

        window = next;
    }

    frame_depth -= 1;

    bool result = !ide_state.first_window;
    return result;
}

void async_user_tick(void)
{
}

#define BUSTER_OPERAND_COUNT (4)

enum MachineOperandId
{
    MACHINE_OPERAND_NONE,
    MACHINE_OPERAND_VIRTUAL_REGISTER,
    MACHINE_OPERAND_PHYSICAL_REGISTER,
    MACHINE_OPERAND_IMMEDIATE,
    MACHINE_OPERAND_MEMORY,
    MACHINE_OPERAND_COUNT,
};
typedef u8 MachineOperandId;

enum MachineInstructionId
{
    MACHINE_INSTRUCTION_RETURN,
    MACHINE_INSTRUCTION_MOVE_08_REG_IMM,
    MACHINE_INSTRUCTION_MOVE_16_REG_IMM,
    MACHINE_INSTRUCTION_MOVE_32_REG_IMM,
    MACHINE_INSTRUCTION_MOVE_64_REG_IMM,
    MACHINE_INSTRUCTION_ZERO_08_GPR,
    MACHINE_INSTRUCTION_ZERO_16_GPR,
    MACHINE_INSTRUCTION_ZERO_32_GPR,
    MACHINE_INSTRUCTION_ZERO_64_GPR,
    MACHINE_INSTRUCTION_COPY_08,
    MACHINE_INSTRUCTION_COPY_16,
    MACHINE_INSTRUCTION_COPY_32,
    MACHINE_INSTRUCTION_COPY_64,
    MACHINE_INSTRUCTION_RET_08,
    MACHINE_INSTRUCTION_RET_16,
    MACHINE_INSTRUCTION_RET_32,
    MACHINE_INSTRUCTION_RET_64,
    MACHINE_INSTRUCTION_LOAD_08,
    MACHINE_INSTRUCTION_LOAD_16,
    MACHINE_INSTRUCTION_LOAD_32,
    MACHINE_INSTRUCTION_LOAD_64,
    MACHINE_INSTRUCTION_STORE_08,
    MACHINE_INSTRUCTION_STORE_16,
    MACHINE_INSTRUCTION_STORE_32,
    MACHINE_INSTRUCTION_STORE_64,
    MACHINE_INSTRUCTION_COUNT,
};

typedef u64 MachineInstructionId;

enum MachineSize
{
    MACHINE_SIZE_ONE = 0,
    MACHINE_SIZE_TWO = 1,
    MACHINE_SIZE_FOUR = 2,
    MACHINE_SIZE_EIGHT = 3,
    MACHINE_SIZE_SIXTEEN = 4,
    MACHINE_SIZE_THIRTY_TWO = 5,
    MACHINE_SIZE_SIXTY_FOUR = 6
};
typedef u8 MachineSize;

BUSTER_GLOBAL_LOCAL u32 machine_size_to_int(MachineSize size)
{
    return (u32)1 << (u32)size;
}

typedef struct MachineOperandFlags MachineOperandFlags;
struct MachineOperandFlags
{
    u8 def:1;
    u8 use:1;
    u8 implicit:1;
    u8 reserved:5;
};

enum RegisterBase
{
    REGISTER_BASE_BASE_POINTER,
    REGISTER_BASE_COUNT,
};
typedef u8 RegisterBase;

typedef struct OperandMemory OperandMemory;
struct OperandMemory
{
    s32 offset;
    RegisterBase base;
    u8 reserved[3];
};

typedef union OperandValue OperandValue;
union OperandValue
{
    u64 integer;
    u64 index;
    OperandMemory memory;
};

BUSTER_CT_CHECK(sizeof(OperandValue) == sizeof(u64));

typedef struct MachineInstruction MachineInstruction;
struct MachineInstruction
{
    OperandValue operand_values[BUSTER_OPERAND_COUNT];
    MachineInstructionId id;
    MachineOperandId operand_ids[BUSTER_OPERAND_COUNT];
    MachineOperandFlags operand_flags[BUSTER_OPERAND_COUNT];
    u8 reserved[16];
};

typedef struct SliceMachineInstruction SliceMachineInstruction;
struct SliceMachineInstruction
{
    MachineInstruction* pointer;
    u64 length;
};

BUSTER_CT_CHECK(sizeof(MachineInstruction) == 64);

typedef enum PhysicalRegisterX8664
{
    PHYSICAL_REGISTER_X86_64_RAX = 0,
    PHYSICAL_REGISTER_X86_64_RCX = 1,
    PHYSICAL_REGISTER_X86_64_RDX = 2,
    PHYSICAL_REGISTER_X86_64_RBX = 3,
    PHYSICAL_REGISTER_X86_64_RSP = 4,
    PHYSICAL_REGISTER_X86_64_RBP = 5,
    PHYSICAL_REGISTER_X86_64_RSI = 6,
    PHYSICAL_REGISTER_X86_64_RDI = 7,
    PHYSICAL_REGISTER_X86_64_R8 = 8,
    PHYSICAL_REGISTER_X86_64_R9 = 9,
    PHYSICAL_REGISTER_X86_64_R10 = 10,
    PHYSICAL_REGISTER_X86_64_R11 = 11,
    PHYSICAL_REGISTER_X86_64_R12 = 12,
    PHYSICAL_REGISTER_X86_64_R13 = 13,
    PHYSICAL_REGISTER_X86_64_R14 = 14,
    PHYSICAL_REGISTER_X86_64_R15 = 15,

    PHYSICAL_REGISTER_X86_64_ZMM0,
    PHYSICAL_REGISTER_X86_64_ZMM1,
    PHYSICAL_REGISTER_X86_64_ZMM2,
    PHYSICAL_REGISTER_X86_64_ZMM3,
    PHYSICAL_REGISTER_X86_64_ZMM4,
    PHYSICAL_REGISTER_X86_64_ZMM5,
    PHYSICAL_REGISTER_X86_64_ZMM6,
    PHYSICAL_REGISTER_X86_64_ZMM7,
    PHYSICAL_REGISTER_X86_64_ZMM8,
    PHYSICAL_REGISTER_X86_64_ZMM9,
    PHYSICAL_REGISTER_X86_64_ZMM10,
    PHYSICAL_REGISTER_X86_64_ZMM11,
    PHYSICAL_REGISTER_X86_64_ZMM12,
    PHYSICAL_REGISTER_X86_64_ZMM13,
    PHYSICAL_REGISTER_X86_64_ZMM14,
    PHYSICAL_REGISTER_X86_64_ZMM15,
    PHYSICAL_REGISTER_X86_64_ZMM16,
    PHYSICAL_REGISTER_X86_64_ZMM17,
    PHYSICAL_REGISTER_X86_64_ZMM18,
    PHYSICAL_REGISTER_X86_64_ZMM19,
    PHYSICAL_REGISTER_X86_64_ZMM20,
    PHYSICAL_REGISTER_X86_64_ZMM21,
    PHYSICAL_REGISTER_X86_64_ZMM22,
    PHYSICAL_REGISTER_X86_64_ZMM23,
    PHYSICAL_REGISTER_X86_64_ZMM24,
    PHYSICAL_REGISTER_X86_64_ZMM25,
    PHYSICAL_REGISTER_X86_64_ZMM26,
    PHYSICAL_REGISTER_X86_64_ZMM27,
    PHYSICAL_REGISTER_X86_64_ZMM28,
    PHYSICAL_REGISTER_X86_64_ZMM29,
    PHYSICAL_REGISTER_X86_64_ZMM30,
    PHYSICAL_REGISTER_X86_64_ZMM31,

    PHYSICAL_REGISTER_X86_64_K0,
    PHYSICAL_REGISTER_X86_64_K1,
    PHYSICAL_REGISTER_X86_64_K2,
    PHYSICAL_REGISTER_X86_64_K3,
    PHYSICAL_REGISTER_X86_64_K4,
    PHYSICAL_REGISTER_X86_64_K5,
    PHYSICAL_REGISTER_X86_64_K6,
    PHYSICAL_REGISTER_X86_64_K7,
} PhysicalRegisterX8664;

enum RegisterClassX86_64
{
    REGISTER_CLASS_GPR,
    REGISTER_CLASS_GPR8,
    REGISTER_CLASS_XMM,
    REGISTER_CLASS_XMM32,
    REGISTER_CLASS_YMM,
    REGISTER_CLASS_YMM32,
    REGISTER_CLASS_ZMM,
    REGISTER_CLASS_MASK,
    REGISTER_CLASS_MASK_NO_ZERO,
};
typedef u8 RegisterClassX86_64;

typedef struct VirtualRegister VirtualRegister;
struct VirtualRegister
{
    s32 offset;
    RegisterClassX86_64 register_class;
    u8 physical;
    MachineSize size;
    u8 reserved[1];
};

typedef struct SliceVirtualRegister SliceVirtualRegister;
struct SliceVirtualRegister
{
    VirtualRegister* pointer;
    u64 length;
};

BUSTER_GLOBAL_LOCAL u8 physical_not_assigned = UINT8_MAX;

typedef struct ISelArena ISelArena;
struct ISelArena
{
    Arena* arena;
    u64 original_position;
};

typedef struct FunctionISel FunctionISel;
struct FunctionISel
{
    ISelArena virtual_registers;
    ISelArena instructions;
};

BUSTER_GLOBAL_LOCAL MachineInstruction* isel_function_allocate_instruction(FunctionISel* isel, u64 count)
{
    MachineInstruction* result = arena_allocate(isel->instructions.arena, MachineInstruction, count);
    return result;
}

BUSTER_GLOBAL_LOCAL MachineInstruction* isel_allocate_instruction(ISelArena* isel_arena, u64 count)
{
    MachineInstruction* result = arena_allocate(isel_arena->arena, MachineInstruction, count);
    return result;
}

BUSTER_GLOBAL_LOCAL OperandValue new_virtual_register(FunctionISel* isel, RegisterClassX86_64 register_class, MachineSize size)
{
    u64 index = (isel->virtual_registers.arena->position - arena_minimum_position) / sizeof(VirtualRegister);
    VirtualRegister* virtual_register = arena_allocate(isel->virtual_registers.arena, VirtualRegister, 1);
    *virtual_register = (VirtualRegister){
        .register_class = register_class,
        .physical = physical_not_assigned,
        .size = size,
    };
    return (OperandValue){ .index = index };
}

BUSTER_GLOBAL_LOCAL void instruction_new_virtual_register(FunctionISel* isel, MachineInstruction* i, RegisterClassX86_64 register_class, MachineSize size, u8 index)
{
    OperandValue virtual_register = new_virtual_register(isel, register_class, size);
    i->operand_values[index] = virtual_register;
    i->operand_ids[index] = MACHINE_OPERAND_VIRTUAL_REGISTER;
    i->operand_flags[index] = (MachineOperandFlags){ .def = 1 };
}

BUSTER_GLOBAL_LOCAL MachineInstruction mov_imm(FunctionISel* isel, u64 immediate, MachineSize size)
{
    BUSTER_CHECK(size <= MACHINE_SIZE_EIGHT);
    MachineInstruction i = {0};

    instruction_new_virtual_register(isel, &i, REGISTER_CLASS_GPR, size, 0);
    
    bool is_zero = immediate == 0;

    if (!is_zero)
    {
        i.operand_values[1] = (OperandValue){ .integer = immediate };
        i.operand_ids[1] = MACHINE_OPERAND_IMMEDIATE;
    }

    i.id = (MachineInstructionId)((u64)size + (u64)(is_zero ? MACHINE_INSTRUCTION_ZERO_08_GPR : MACHINE_INSTRUCTION_MOVE_08_REG_IMM));

    return i;
}

BUSTER_GLOBAL_LOCAL MachineInstruction copy(FunctionISel* isel, PhysicalRegisterX8664 physical_register, u64 virtual_register, MachineSize size)
{
    BUSTER_UNUSED(isel);

    MachineInstruction i = {0};
    
    i.operand_values[0] = (OperandValue){ .index = (u64)physical_register };
    i.operand_ids[0] = MACHINE_OPERAND_PHYSICAL_REGISTER;
    i.operand_flags[0] = (MachineOperandFlags){ .def = 1 };

    i.operand_values[1] = (OperandValue){ .index = virtual_register };
    i.operand_ids[1] = MACHINE_OPERAND_VIRTUAL_REGISTER;
    i.operand_flags[1] = (MachineOperandFlags){ .use = 1 };

    i.id = (MachineInstructionId)((u64)size + (u64)(MACHINE_INSTRUCTION_COPY_08));

    return i;
}

BUSTER_GLOBAL_LOCAL MachineInstruction ret(FunctionISel* isel, PhysicalRegisterX8664 physical_register, MachineSize size)
{
    BUSTER_UNUSED(isel);

    MachineInstruction i = {0};
    
    i.operand_values[0] = (OperandValue){ .index = (u64)physical_register };
    i.operand_ids[0] = MACHINE_OPERAND_PHYSICAL_REGISTER;
    i.operand_flags[0] = (MachineOperandFlags){ .use = 1, .implicit = 1 };

    i.id = (MachineInstructionId)((u64)size + (u64)(MACHINE_INSTRUCTION_RET_08));

    return i;
}

BUSTER_GLOBAL_LOCAL MachineInstruction consume_spill(PhysicalRegisterX8664 physical_register, s32 offset, MachineSize size)
{
    MachineInstruction i = {0};

    i.operand_values[0] = (OperandValue){ .index = (u64)physical_register };
    i.operand_ids[0] = MACHINE_OPERAND_PHYSICAL_REGISTER;
    i.operand_flags[0] = (MachineOperandFlags){ .def = 1 };

    i.operand_values[1] = (OperandValue){ .memory = { .offset = offset, .base = REGISTER_BASE_BASE_POINTER } };
    i.operand_ids[1] = MACHINE_OPERAND_MEMORY;
    i.operand_flags[1] = (MachineOperandFlags){ .use = 1 };

    i.id = (MachineInstructionId)((u64)MACHINE_INSTRUCTION_LOAD_08 + (u64)size);

    return i;
}

BUSTER_GLOBAL_LOCAL MachineInstruction produce_spill(s32 offset, PhysicalRegisterX8664 physical_register, MachineSize size)
{
    MachineInstruction i = {0};

    i.operand_values[0] = (OperandValue){ .memory = { .offset = offset, .base = REGISTER_BASE_BASE_POINTER } };
    i.operand_ids[0] = MACHINE_OPERAND_MEMORY;
    i.operand_flags[0] = (MachineOperandFlags){ .def = 1 };

    i.operand_values[1] = (OperandValue){ .index = (u64)physical_register };
    i.operand_ids[1] = MACHINE_OPERAND_PHYSICAL_REGISTER;
    i.operand_flags[1] = (MachineOperandFlags){ .use = 1 };

    i.id = (MachineInstructionId)((u64)MACHINE_INSTRUCTION_STORE_08 + (u64)size);

    return i;
}

typedef int MainFunction(void);

BUSTER_GLOBAL_LOCAL ProcessResult run_graphical_app(void)
{
    ProcessResult result = PROCESS_RESULT_SUCCESS;

    WmHandle* windowing = ide_state.windowing = wm_initialize();
    if (windowing)
    {
        Arena* arena = program_state->arena;
        RenderingHandle* r = ide_state.rendering = rendering_initialize(arena);
        if (r)
        {
            ide_state.first_window = ide_state.last_window = arena_allocate(arena, IdeWindow, 1);
            WmWindowHandle* wm_window = wm_window_create(windowing, (WmWindowCreate) {
                    .name = S8("Ide"),
                    .size = {
                    .width = 1600,
                    .height= 900,
                    },
                    });
            ide_state.first_window->wm = wm_window;

            if (wm_window)
            {
                RenderingWindowHandle* render_window = ide_state.first_window->render = rendering_window_initialize(arena, windowing, r, wm_window);

                if (render_window)
                {
                    ide_state.first_window->ui = ui_state_allocate(r, render_window);
                    ide_state.first_window->root_panel = arena_allocate(ide_state.state.arena, IdePanel, 1);
                    ide_state.first_window->root_panel->parent_percentage = 1.0f;
                    ide_state.first_window->root_panel->split_axis = AXIS2_X;

                    rendering_window_rect_texture_update_begin(ide_state.first_window->render);

                    f32 dpi = wm_window_get_dpi(windowing, wm_window);
                    TextureIndex white_texture = white_texture_create(ide_state.state.arena, ide_state.rendering);

                    rendering_window_queue_rect_texture_update(ide_state.rendering, ide_state.first_window->render, RECT_TEXTURE_SLOT_WHITE, white_texture);
                    ide_window_queue_font_update(ide_state.first_window, dpi);

                    rendering_window_rect_texture_update_end(ide_state.rendering, ide_state.first_window->render);

                    ide_state.last_frame_timestamp = timestamp_take();

                    bool test = ide_state.test && !program_flag_get(PROGRAM_FLAG_TEST_PERSIST);
                    u64 loop_times = test ? (u64)3 : UINT64_MAX;
                    for (u64 i = 0; i < loop_times && ide_state.first_window; i += 1)
                    {
                        bool quit = update();
                        if (quit)
                        {
                            break;
                        }
                    }

                    if (test)
                    {
#if BUSTER_IOS
                        // The iOS worker thread calls exit() right after this
                        // returns, so the OS reclaims all GPU/window resources.
                        // Skip the explicit teardown: in a headless simulator the
                        // last presented drawable's command buffer never completes
                        // (nothing drives a subsequent vsync), so
                        // rendering_window_deinitialize's waitUntilCompleted would
                        // block forever and the BUSTER_IOS_RESULT marker would
                        // never be printed.
#else
                        for (IdeWindow* window = ide_state.first_window; window; window = window->next)
                        {
                            ui_state_deinitialize(window->ui);
                            window->ui = 0;
                            rendering_window_deinitialize(ide_state.rendering, window->render);
                            window->render = 0;
                        }
#endif
                    }

                    // TODO: OS deinitialization
                }
                else
                {
                    string_print(S8("Failed to create render window\n"));
                    result = PROCESS_RESULT_FAILED;
                }
            }
            else
            {
                string_print(S8("Failed to create window\n"));
                result = PROCESS_RESULT_FAILED;
            }

            rendering_deinitialize(r);
        }
        else
        {
            string_print(S8("Failed to initialize rendering\n"));
            result = PROCESS_RESULT_FAILED;
        }

        wm_deinitialize(windowing);
    }
    else
    {
        string_print(S8("Failed to initialize windowing\n"));
        result = PROCESS_RESULT_FAILED;
    }

    return result;
}

// Deliberately independent of the windowing/rendering path `test` drives via
// run_graphical_app(): bench must run headless on a plain CI runner with no
// display server, and BUSTER_INCLUDE_TESTS off must not disable it either.
BUSTER_GLOBAL_LOCAL ProcessResult run_benchmarks(void)
{
    Arena* arena = arena_create((ArenaCreation){0});

    ParserBenchResult parse_result = parser_parse_bench(arena, 200);
    string_print(S8("BENCH parse_all_tests iterations={u64} files={u64} min_ns={u64} median_ns={u64}\n"),
            parse_result.iterations, parse_result.file_count, parse_result.min_ns, parse_result.median_ns);

#if BUSTER_INSTRUMENT
    string_print(S8("BENCH_PHASE tokenize min_ns={u64} median_ns={u64}\n"),
            parse_result.tokenize_min_ns, parse_result.tokenize_median_ns);
    string_print(S8("BENCH_PHASE parse min_ns={u64} median_ns={u64}\n"),
            parse_result.parse_min_ns, parse_result.parse_median_ns);
    for (u64 i = 0; i < parse_result.file_count; i += 1)
    {
        ParserBenchFileResult file_result = parse_result.files[i];
        string_print(S8("BENCH_FILE path={S8} min_ns={u64} median_ns={u64}\n"),
                file_result.path, file_result.min_ns, file_result.median_ns);
    }
#endif

    arena_destroy(arena, 1);
    return PROCESS_RESULT_SUCCESS;
}

BUSTER_GLOBAL_LOCAL ProcessResult run_app(void)
{
#if BUSTER_INCLUDE_TESTS
    if (ide_state.test)
    {
        Arena* arena = arena_create((ArenaCreation){0});
        UnitTestArguments arguments = { arena, &default_show };

        u64 position = arena->position;
        BatchTestResult batch_test_result = library_tests(&arguments);
        arena->position = position;

        if (program_flag_get(PROGRAM_FLAG_CI))
        {
            ProcessResult app_test_result = run_graphical_app();
            consume_external_tests(&batch_test_result, app_test_result);
        }

        position = arena->position;
        ProcessResult result = batch_test_report(&arguments, batch_test_result) ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
        arena->position = position;

        arena_destroy(arena, 1);
        return result;
    }
#endif

    return run_graphical_app();
}

ProcessResult entry_point(void)
{
    if (ide_state.bench)
    {
        return run_benchmarks();
    }

    return run_app();
}
#endif
