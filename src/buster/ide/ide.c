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
#include <buster/compiler/frontend/buster/analysis.h>
#include <buster/compiler/ir/ir.h>
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
#include <buster/compiler/ir/ir.c>
#include <buster/compiler/frontend/buster/analysis.c>
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
    u8 reserved[7];
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
        if (!string_equal(arg, S8("test")))
        {
            ProcessResult r = buster_argument_process(i);
            if (r != PROCESS_RESULT_SUCCESS)
            {
                string_print(S8("Failed to process argument {S8}\n"), arg);
                result = r;
                break;
            }
        }
        else
        {
            ide_state.test = true;
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

typedef struct IrInstructionIterator IrInstructionIterator;
struct IrInstructionIterator
{
    IrModule* module;
    IrBasicBlock* block;
    IrInstructionRef instruction_ref;
};

BUSTER_GLOBAL_LOCAL IrInstructionIterator ir_get_instruction_iterator(IrModule* module, IrBasicBlock* block)
{
    IrInstructionIterator result = {
        .module = module,
        .block = block,
        .instruction_ref = block->first,
    };
    return result;
}

BUSTER_GLOBAL_LOCAL IrInstruction* ir_next_instruction(IrInstructionIterator* iterator)
{
    IrInstructionRef ref = iterator->instruction_ref;
    if (ir_ref_is_valid(ref))
    {
        BUSTER_TODO();
    }
    else
    {
        BUSTER_TODO();
    }
}

typedef struct DefScratch DefScratch;
struct DefScratch
{
    u64 virtual_register;
    PhysicalRegisterX8664 physical_register;
    u8 reserved[4];
};

BUSTER_GLOBAL_LOCAL ByteSlice module_lower(IrModule* module)
{
    SliceIrFunction functions = ir_module_get_functions(module);
    Arena* instruction_arena = arena_create((ArenaCreation){0});
    Arena* virtual_register_arena = arena_create((ArenaCreation){0});
    Arena* emitter_arena = arena_create((ArenaCreation){ .flags = { .execute = 1 }});
    u64 emitter_position = emitter_arena->position;

    for (EACH_SLICE_INT(function_i, functions))
    {
        IrFunction* function = &functions.pointer[function_i];
        IrBasicBlockRef queue[64];
        u64 queue_count = 0;

        queue[0] = function->entry_block;
        queue_count = 1;

        FunctionISel function_isel = {
            .virtual_registers = {
                .arena = virtual_register_arena,
                .original_position = virtual_register_arena->position,
            },
            .instructions = {
                .arena = instruction_arena,
                .original_position = instruction_arena->position,
            },
        };
        FunctionISel* isel = &function_isel;

        while (queue_count)
        {
            IrBasicBlockRef basic_block_ref = queue[queue_count - 1];
            queue_count -= 1;
            IrBasicBlock* basic_block = ir_basic_block_get(module, basic_block_ref);

            IrInstructionIterator iterator = ir_get_instruction_iterator(module, basic_block);
            IrInstruction* instruction;
            while ((instruction = ir_next_instruction(&iterator)))
            {
                switch (instruction->id)
                {
                    break; case IR_INSTRUCTION_RETURN:
                    {
                        IrValue* value = instruction->value;

                        switch (value->id)
                        {
                            break; case IR_VALUE_CONSTANT_INTEGER:
                            {
                                MachineInstruction* instructions = isel_function_allocate_instruction(isel, 3);
                                MachineSize size = MACHINE_SIZE_FOUR;
                                BUSTER_UNUSED(instructions);
                                BUSTER_UNUSED(size);
                                BUSTER_TODO();
                                // instructions[0] = mov_imm(isel, value->constant4.v, size);
                                // instructions[1] = copy(isel, PhysicalRegisterX8664::RAX, instructions[0].operand_values[0].integer, size);
                                // instructions[2] = ret(isel, PhysicalRegisterX8664::RAX, size);
                            }
                            break; case IR_VALUE_COUNT: BUSTER_UNREACHABLE();
                        }
                    }
                    break; case IR_INSTRUCTION_COUNT: BUSTER_UNREACHABLE();
                }
            }
        }

        SliceMachineInstruction isel_instructions = arena_get_slice_at_position(isel->instructions.arena, MachineInstruction, isel->instructions.original_position, isel->instructions.arena->position);
        SliceVirtualRegister virtual_registers = arena_get_slice_at_position(isel->virtual_registers.arena, VirtualRegister, isel->virtual_registers.original_position, isel->virtual_registers.arena->position);

        s32 frame_offset = 0;

        for (EACH_SLICE_INT(virtual_register_i, virtual_registers))
        {
            VirtualRegister* virtual_register = &virtual_registers.pointer[virtual_register_i];
            u32 size = machine_size_to_int(virtual_register->size);
            u32 alignment = size;
            virtual_register->offset = -(s32)((u32)align_forward((u32)-frame_offset, alignment) + size);
            frame_offset = virtual_register->offset;
        }

        ISelArena ni = {
            .arena = isel->instructions.arena,
            .original_position = isel->instructions.arena->position,
        };

        ISelArena* register_allocation_instructions = &ni;

        PhysicalRegisterX8664 scratch_gpr[] = { PHYSICAL_REGISTER_X86_64_R10, PHYSICAL_REGISTER_X86_64_R11 };

        for (EACH_SLICE_INT(instruction_i, isel_instructions))
        {
            MachineInstruction* instruction = &isel_instructions.pointer[instruction_i];
            u8 gpr_index = 0;

            for (u64 operand_i = 0; operand_i < BUSTER_OPERAND_COUNT; operand_i += 1)
            {
                MachineOperandId id = instruction->operand_ids[operand_i];
                if (id == MACHINE_OPERAND_NONE)
                {
                    break;
                }

                if (id == MACHINE_OPERAND_VIRTUAL_REGISTER)
                {
                    MachineOperandFlags flags = instruction->operand_flags[operand_i];
                    u64 virtual_register_index = instruction->operand_values[operand_i].index;
                    VirtualRegister* virtual_register = &virtual_registers.pointer[virtual_register_index];

                    if (flags.use)
                    {
                        PhysicalRegisterX8664 scratch;
                        switch (virtual_register->register_class)
                        {
                            break; case REGISTER_CLASS_GPR: scratch = scratch_gpr[gpr_index++];
                            break; default: BUSTER_UNREACHABLE();
                        }

                        *isel_allocate_instruction(register_allocation_instructions, 1) = consume_spill(scratch, virtual_register->offset, virtual_register->size);

                        instruction->operand_values[operand_i] = (OperandValue){ .index = (u64)scratch };
                        instruction->operand_ids[operand_i] = MACHINE_OPERAND_PHYSICAL_REGISTER;
                    }
                }
            }

            DefScratch def_scratches[BUSTER_OPERAND_COUNT];
            u32 def_count = 0;

            for (u64 operand_i = 0; operand_i < BUSTER_OPERAND_COUNT; operand_i += 1)
            {
                MachineOperandId id = instruction->operand_ids[operand_i];
                if (id == MACHINE_OPERAND_NONE)
                {
                    break;
                }

                if (id == MACHINE_OPERAND_VIRTUAL_REGISTER)
                {
                    MachineOperandFlags flags = instruction->operand_flags[operand_i];
                    u64 virtual_register_index = instruction->operand_values[operand_i].index;
                    VirtualRegister* virtual_register = &virtual_registers.pointer[virtual_register_index];

                    if (flags.def)
                    {
                        PhysicalRegisterX8664 scratch;
                        switch (virtual_register->register_class)
                        {
                            break; case REGISTER_CLASS_GPR: scratch = scratch_gpr[gpr_index++];
                            break; default: BUSTER_UNREACHABLE();
                        }

                        def_scratches[def_count++] = (DefScratch){
                            .virtual_register = virtual_register_index,
                            .physical_register = scratch,
                        };

                        instruction->operand_values[operand_i] = (OperandValue){ .index = (u64)scratch };
                        instruction->operand_ids[operand_i] = MACHINE_OPERAND_PHYSICAL_REGISTER;
                    }
                }
            }

            *isel_allocate_instruction(register_allocation_instructions, 1) = *instruction;

            for (u32 def_i = 0; def_i < def_count; def_i += 1)
            {
                DefScratch def = def_scratches[def_i];
                u64 virtual_register_index = def.virtual_register;
                VirtualRegister* virtual_register = &virtual_registers.pointer[virtual_register_index];
                *isel_allocate_instruction(register_allocation_instructions, 1) = produce_spill(virtual_register->offset, def.physical_register, virtual_register->size);
            }
        }

        SliceMachineInstruction ra_instructions = arena_get_slice_at_position(register_allocation_instructions->arena, MachineInstruction, register_allocation_instructions->original_position, register_allocation_instructions->arena->position);

        for (EACH_SLICE_INT(instruction_i, ra_instructions))
        {
            MachineInstruction* instruction = &ra_instructions.pointer[instruction_i];
            switch (instruction->id)
            {
                break; case MACHINE_INSTRUCTION_RETURN: BUSTER_UNREACHABLE();
                break; case MACHINE_INSTRUCTION_MOVE_08_REG_IMM: BUSTER_UNREACHABLE();
                break; case MACHINE_INSTRUCTION_MOVE_16_REG_IMM: BUSTER_UNREACHABLE();
                break; case MACHINE_INSTRUCTION_MOVE_32_REG_IMM: BUSTER_UNREACHABLE();
                break; case MACHINE_INSTRUCTION_MOVE_64_REG_IMM: BUSTER_UNREACHABLE();
                break; case MACHINE_INSTRUCTION_ZERO_08_GPR: BUSTER_UNREACHABLE();
                break; case MACHINE_INSTRUCTION_ZERO_16_GPR: BUSTER_UNREACHABLE();
                break; case MACHINE_INSTRUCTION_ZERO_32_GPR:
                {
                    PhysicalRegisterX8664 reg = (PhysicalRegisterX8664)instruction->operand_values[0].index;
                    bool use_rex = reg >= PHYSICAL_REGISTER_X86_64_R8;
                    u64 byte_count = 2 + use_rex;
                    u8* allocation = arena_allocate(emitter_arena, u8, byte_count);
                    allocation[0] = 0x45;
                    allocation[use_rex + 0] = 0x31;
                    u8 encoding_reg = (u8)reg & 0x7;
                    allocation[use_rex + 1] = 0xc0 | (u8)(encoding_reg << 3) | (u8)(encoding_reg << 0);
                }
                break; case MACHINE_INSTRUCTION_ZERO_64_GPR: BUSTER_UNREACHABLE();
                break; case MACHINE_INSTRUCTION_COPY_08: BUSTER_UNREACHABLE();
                break; case MACHINE_INSTRUCTION_COPY_16: BUSTER_UNREACHABLE();
                break; case MACHINE_INSTRUCTION_COPY_32:
                {
                    BUSTER_CHECK(instruction->operand_ids[0] == MACHINE_OPERAND_PHYSICAL_REGISTER);
                    BUSTER_CHECK(instruction->operand_ids[1] == MACHINE_OPERAND_PHYSICAL_REGISTER);

                    PhysicalRegisterX8664 destination = (PhysicalRegisterX8664)instruction->operand_values[0].index;
                    PhysicalRegisterX8664 source = (PhysicalRegisterX8664)instruction->operand_values[1].index;

                    bool is_destination_reg64 = destination >= PHYSICAL_REGISTER_X86_64_R8;
                    bool is_source_reg64 = source >= PHYSICAL_REGISTER_X86_64_R8;
                    bool use_rex = is_destination_reg64 || is_source_reg64;
                    u64 byte_count = 2 + use_rex;

                    u8 encoding_source = (u8)source & 0x07;
                    u8 encoding_destination = (u8)destination & 0x07;

                    u8* allocation = arena_allocate(emitter_arena, u8, byte_count);

                    if (is_destination_reg64 && is_source_reg64)
                    {
                        BUSTER_TODO();
                    }
                    else if (is_source_reg64)
                    {
                        allocation[0] = 0x44;
                        allocation[use_rex + 0] = 0x89;
                        allocation[use_rex + 1] = (1 << 7) | (1 << 6) | (u8)(encoding_source << 3) | (u8)(encoding_destination << 0);
                    }
                    else if (is_destination_reg64)
                    {
                        BUSTER_TODO();
                    }
                    else
                    {
                        BUSTER_TODO();
                    }
                }
                break; case MACHINE_INSTRUCTION_COPY_64: BUSTER_UNREACHABLE();
                break; case MACHINE_INSTRUCTION_RET_08: BUSTER_UNREACHABLE();
                break; case MACHINE_INSTRUCTION_RET_16: BUSTER_UNREACHABLE();
                break; case MACHINE_INSTRUCTION_RET_32:
                {
                    *arena_allocate(emitter_arena, u8, 1) = 0xc3;
                }
                break; case MACHINE_INSTRUCTION_RET_64: BUSTER_UNREACHABLE();
                break; case MACHINE_INSTRUCTION_LOAD_08: BUSTER_UNREACHABLE();
                break; case MACHINE_INSTRUCTION_LOAD_16: BUSTER_UNREACHABLE();
                break; case MACHINE_INSTRUCTION_LOAD_32:
                {
                    BUSTER_CHECK(instruction->operand_ids[0] == MACHINE_OPERAND_PHYSICAL_REGISTER);
                    BUSTER_CHECK(instruction->operand_ids[1] == MACHINE_OPERAND_MEMORY);

                    PhysicalRegisterX8664 destination_reg = (PhysicalRegisterX8664)instruction->operand_values[0].index;
                    OperandValue source = instruction->operand_values[1];
                    BUSTER_CHECK(source.memory.base == REGISTER_BASE_BASE_POINTER);
                    bool use_rex = destination_reg >= PHYSICAL_REGISTER_X86_64_R8;
                    u64 byte_count = 3 + use_rex;
                    u8* allocation = arena_allocate(emitter_arena, u8, byte_count);

                    if (source.memory.offset >= INT8_MIN && source.memory.offset <= INT8_MAX)
                    {
                        u8 encoding_reg = (u8)destination_reg & 0x07;
                        allocation[0] = 0x44;
                        allocation[use_rex + 0] = 0x8b;
                        allocation[use_rex + 1] = (1 << 6) | (u8)(encoding_reg << 3) | (u8)PHYSICAL_REGISTER_X86_64_RBP;
                        allocation[use_rex + 2] = (u8)(s8)source.memory.offset;
                    }
                    else
                    {
                        BUSTER_TODO();
                    }
                }
                break; case MACHINE_INSTRUCTION_LOAD_64: BUSTER_UNREACHABLE();
                break; case MACHINE_INSTRUCTION_STORE_08: BUSTER_UNREACHABLE();
                break; case MACHINE_INSTRUCTION_STORE_16: BUSTER_UNREACHABLE();
                break; case MACHINE_INSTRUCTION_STORE_32:
                {
                    BUSTER_CHECK(instruction->operand_ids[0] == MACHINE_OPERAND_MEMORY);
                    BUSTER_CHECK(instruction->operand_ids[1] == MACHINE_OPERAND_PHYSICAL_REGISTER);
                    PhysicalRegisterX8664 source_reg = (PhysicalRegisterX8664)instruction->operand_values[1].index;
                    OperandValue destination = instruction->operand_values[0];
                    BUSTER_CHECK(destination.memory.base == REGISTER_BASE_BASE_POINTER);

                    bool use_rex = source_reg >= PHYSICAL_REGISTER_X86_64_R8;
                    u64 byte_count = 3 + use_rex;
                    u8* allocation = arena_allocate(emitter_arena, u8, byte_count);

                    if (destination.memory.offset >= INT8_MIN && destination.memory.offset <= INT8_MAX)
                    {
                        u8 encoding_reg = (u8)source_reg & 0x07;
                        allocation[0] = 0x44;
                        allocation[use_rex + 0] = 0x89;
                        allocation[use_rex + 1] = (1 << 6) | (u8)(encoding_reg << 3) | (u8)PHYSICAL_REGISTER_X86_64_RBP;
                        allocation[use_rex + 2] = (u8)(s8)destination.memory.offset;
                    }
                    else
                    {
                        BUSTER_TODO();
                    }
                }
                break; case MACHINE_INSTRUCTION_STORE_64: BUSTER_UNREACHABLE();
                break; case MACHINE_INSTRUCTION_COUNT: BUSTER_UNREACHABLE();
            }
        }
    }

    Sliceu8 code = arena_get_slice_at_position(emitter_arena, u8, emitter_position, emitter_arena->position);
    return code;
}

typedef int MainFunction(void);

BUSTER_GLOBAL_LOCAL void compiler_experiments(void)
{
    Arena* arena = arena_create((ArenaCreation){0});
    IrModule* module = ir_create_mock_module(arena);
    ByteSlice code = module_lower(module);
    MainFunction* fn = (MainFunction*)code.pointer;
    BUSTER_CHECK(fn() == 0);
}

BUSTER_GLOBAL_LOCAL ProcessResult run_app(void)
{
    ProcessResult result = PROCESS_RESULT_SUCCESS;

#if BUSTER_INCLUDE_TESTS
    if (ide_state.test)
    {
        Arena* arena = arena_create((ArenaCreation){0});

        {
            u64 position = arena->position;
            UnitTestArguments arguments = { arena, &default_show };
            BatchTestResult batch_test_result = library_tests(&arguments);
            result = batch_test_report(&arguments, batch_test_result) ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
            arena->position = position;
        }

        // {
        //     u64 position = arena->position;
        //     UnitTestArguments arguments = { arena, &default_show };
        //     BatchTestResult batch_test_result = parser_tests(&arguments);
        //     result = batch_test_report(&arguments, batch_test_result) ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
        //     arena->position = position;
        // }

        arena_destroy(arena, 1);
    }
#endif
    parser_experiments();
    // compiler_experiments();
    // analysis_experiments();

    if (result == PROCESS_RESULT_SUCCESS)
    {
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
                            for (IdeWindow* window = ide_state.first_window; window; window = window->next)
                            {
                                ui_state_deinitialize(window->ui);
                                window->ui = 0;
                                rendering_window_deinitialize(ide_state.rendering, window->render);
                                window->render = 0;
                            }
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
    }

    return result;
}

ProcessResult entry_point(void)
{
    return run_app();
}
#endif
