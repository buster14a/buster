#pragma once

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
#include <buster/assertion.c>
#include <buster/arguments.c>
#if BUSTER_INCLUDE_TESTS
#include <buster/test.c>
#endif
#include <buster/memory.c>
#include <buster/entry_point.c>
#include <buster/target.c>
#if defined(__x86_64__)
#include <buster/x86_64.c>
#endif
#include <buster/ui_core.c>
#include <buster/ui_builder.c>
#include <buster/window.c>
#include <buster/rendering.c>
#include <buster/file.c>
#include <buster/font_provider.c>
#include <buster/time.c>
#include <buster/float.c>
#include <buster/compiler/frontend/buster/parser.c>
#include <buster/compiler/ir/ir.c>
#include <buster/simd.c>
#include <buster/integer.c>
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
    OsWindowHandle* os;
    RenderingWindowHandle* render;
    IdeWindow* previous;
    IdeWindow* next;
    IdePanel* root_panel;
    UI_State* ui;
};

typedef struct IdeProgram IdeProgram;
struct IdeProgram
{
    ProgramState state;
    IdeWindow* first_window;
    IdeWindow* last_window;
    OsWindowingHandle* windowing;
    RenderingHandle* rendering;
    OsWindowingEventList event_list;
    bool test;
    u8 reserved[7];
    TimeDataType last_frame_timestamp;
};

BUSTER_GLOBAL_LOCAL IdeProgram ide_state = {0};

ProgramState* program_state = &ide_state.state;

#if BUSTER_FUZZING
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

    StringOsList argv = program_state->input.argv;
    // StringOsList envp = program_state->input.envp;

    StringOsListIterator arg_it = string_os_list_iterator_initialize(argv);

    string_os_list_iterator_next(&arg_it);

    for (StringOs arg = string_os_list_iterator_next(&arg_it); arg.pointer; arg = string_os_list_iterator_next(&arg_it))
    {
        BUSTER_TRAP();
        // if (!string_os_equal(arg, SOs("test")))
        // {
        //     let r = buster_argument_process(argv, envp, i, arg);
        //     if (r != ProcessResult::Success)
        //     {
        //         string8_print(S8("Failed to process argument {SOs}\n"), arg);
        //         result = r;
        //         break;
        //     }
        // }
        // else
        // {
        //     ide_state.test = true;
        // }
    }

    return result;
}

BUSTER_GLOBAL_LOCAL void ui_top_bar(void)
{
    ui_push(pref_height, ui_em(1, 1));
    {
        ui_push(child_layout_axis, AXIS2_X);
        UI_Widget* top_bar = ui_widget_make((UI_WidgetFlags) {0}, S8("top_bar"));
        ui_push(parent, top_bar);
        {
            if (ui_button(S8("Button s")).clicked_left)
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
    UI_Widget* node_widget = ui_widget_make_format((UI_WidgetFlags) {
        .draw_background = 1,
        .draw_text = 1,
    }, S8("{S8} : {S8} = {S8}##{S8}{S8}"), node.name, node.type, node.value, node.function, node.name_space);
    BUSTER_UNUSED(node_widget);
}

BUSTER_GLOBAL_LOCAL void app_update(void)
{
    TimeDataType frame_end = timestamp_take();
    ide_state.event_list = os_windowing_poll_events(ide_state.state.arena, ide_state.windowing);
    f64 frame_ms = (f64)timestamp_ns_between(ide_state.last_frame_timestamp, frame_end) / (1000 * 1000);
    ide_state.last_frame_timestamp = frame_end;

    for (OsWindowingEvent* os_event = ide_state.event_list.first; os_event; os_event = os_event->next)
    {
        switch (os_event->kind)
        {
            case OS_WINDOWING_EVENT_WINDOW_CLOSE:
            {
                for (IdeWindow* window = ide_state.first_window; window; window = window->next)
                {
                    if (window->os == os_event->window)
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
            } break;
            break; case OS_WINDOWING_EVENT_COUNT: BUSTER_UNREACHABLE();
        }
    }

    IdeWindow* window = ide_state.first_window;
    while (window)
    {
        IdeWindow* next = window->next;

        RenderingWindowHandle* render_window = window->render;
        rendering_window_frame_begin(ide_state.rendering, render_window);

        ui_state_select(window->ui);

        ui_build_begin(ide_state.windowing, window->os, frame_ms, &ide_state.event_list);

        ui_push(font_size, 24);

        ui_top_bar();
        ui_push(child_layout_axis, AXIS2_X);
        UI_Widget* workspace_widget = ui_widget_make_format((UI_WidgetFlags) {0}, S8("workspace{u64}"), window->os);
        ui_push(parent, workspace_widget);
        {
            // Node visualizer
            ui_push(child_layout_axis, AXIS2_Y);
            UI_Widget* node_visualizer_widget = ui_widget_make_format((UI_WidgetFlags) {
                .draw_background = 1,
            }, S8("node_visualizer{u64}"), window->os);

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

        window = next;
    }
}

BUSTER_GLOBAL_LOCAL void window_refresh_callback(OsWindowHandle* window, void* context)
{
    BUSTER_UNUSED(window);
    BUSTER_UNUSED(context);
    app_update();
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

enum RegisterClass
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
typedef u8 RegisterClass;

typedef struct VirtualRegister VirtualRegister;
struct VirtualRegister
{
    s32 offset;
    RegisterClass register_class;
    u8 physical;
    MachineSize size;
    u8 reserved[1];
};

// BUSTER_GLOBAL_LOCAL constexpr u8 physical_not_assigned = UINT8_MAX;

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

// BUSTER_GLOBAL_LOCAL MachineInstruction* allocate_instruction(FunctionIsel* isel, u64 count)
// {
//     let result = arena_allocate(isel->instructions.arena, MachineInstruction, count);
//     return result;
// }
//
// BUSTER_GLOBAL_LOCAL MachineInstruction* allocate_instruction(ISelArena* isel_arena, u64 count)
// {
//     let result = arena_allocate(isel_arena->arena, MachineInstruction, count);
//     return result;
// }
//
// BUSTER_GLOBAL_LOCAL OperandValue new_virtual_register(FunctionIsel* isel, RegisterClass register_class, MachineSize size)
// {
//     let index = (isel->virtual_registers.arena->position - arena_minimum_position) / sizeof(VirtualRegister);
//     let virtual_register = arena_allocate(isel->virtual_registers.arena, VirtualRegister, 1);
//     *virtual_register = {
//         .register_class = register_class,
//         .physical = physical_not_assigned,
//         .size = size,
//     };
//     return { .index = index };
// }
//
// BUSTER_GLOBAL_LOCAL void instruction_new_virtual_register(FunctionIsel* isel, MachineInstruction& i, RegisterClass register_class, MachineSize size, u8 index)
// {
//     let virtual_register = new_virtual_register(isel, register_class, size);
//     i.operand_values[index] = virtual_register;
//     i.operand_ids[index] = MachineOperandId::VirtualRegister;
//     i.operand_flags[index] = { .def = 1 };
// }
//
// BUSTER_GLOBAL_LOCAL MachineInstruction mov_imm(FunctionIsel* isel, u64 immediate, MachineSize size)
// {
//     BUSTER_CHECK(size <= MachineSize::Eight);
//     MachineInstruction i = {};
//
//     instruction_new_virtual_register(isel, i, RegisterClass::GPR, size, 0);
//     
//     bool is_zero = immediate == 0;
//
//     if (!is_zero)
//     {
//         i.operand_values[1] = { .integer = immediate };
//         i.operand_ids[1] = MachineOperandId::Immediate;
//     }
//
//     i.id = (MachineInstructionId)((u64)size + (u64)(is_zero ? MachineInstructionId::Zero08GPR : MachineInstructionId::Move08RegImm));
//
//     return i;
// }
//
// BUSTER_GLOBAL_LOCAL MachineInstruction copy(FunctionIsel* isel, PhysicalRegisterX8664 physical_register, u64 virtual_register, MachineSize size)
// {
//     BUSTER_UNUSED(isel);
//
//     MachineInstruction i = {};
//     
//     i.operand_values[0] = { .index = (u64)physical_register };
//     i.operand_ids[0] = MachineOperandId::PhysicalRegister;
//     i.operand_flags[0] = { .def = 1 };
//
//     i.operand_values[1] = { .index = virtual_register };
//     i.operand_ids[1] = MachineOperandId::VirtualRegister;
//     i.operand_flags[1] = { .use = 1 };
//
//     i.id = (MachineInstructionId)((u64)size + (u64)(MachineInstructionId::Copy08));
//
//     return i;
// }
//
// BUSTER_GLOBAL_LOCAL MachineInstruction ret(FunctionIsel* isel, PhysicalRegisterX8664 physical_register, MachineSize size)
// {
//     BUSTER_UNUSED(isel);
//
//     MachineInstruction i = {};
//     
//     i.operand_values[0] = { .index = (u64)physical_register };
//     i.operand_ids[0] = MachineOperandId::PhysicalRegister;
//     i.operand_flags[0] = { .use = 1, .implicit = 1 };
//
//     i.id = (MachineInstructionId)((u64)size + (u64)(MachineInstructionId::Ret08));
//
//     return i;
// }
//
// BUSTER_GLOBAL_LOCAL MachineInstruction consume_spill(PhysicalRegisterX8664 physical_register, s32 offset, MachineSize size)
// {
//     MachineInstruction i = {};
//
//     i.operand_values[0] = { .index = (u64)physical_register };
//     i.operand_ids[0] = MachineOperandId::PhysicalRegister;
//     i.operand_flags[0] = { .def = 1 };
//
//     i.operand_values[1] = { .memory = { .offset = offset, .base = RegisterBase::BasePointer } };
//     i.operand_ids[1] = MachineOperandId::Memory;
//     i.operand_flags[1] = { .use = 1 };
//
//     i.id = (MachineInstructionId)((u64)MachineInstructionId::Load08 + (u64)size);
//
//     return i;
// }
//
// BUSTER_GLOBAL_LOCAL MachineInstruction produce_spill(s32 offset, PhysicalRegisterX8664 physical_register, MachineSize size)
// {
//     MachineInstruction i = {};
//
//     i.operand_values[0] = { .memory = { .offset = offset, .base = RegisterBase::BasePointer } };
//     i.operand_ids[0] = MachineOperandId::Memory;
//     i.operand_flags[0] = { .def = 1 };
//
//     i.operand_values[1] = { .index = (u64)physical_register };
//     i.operand_ids[1] = MachineOperandId::PhysicalRegister;
//     i.operand_flags[1] = { .use = 1 };
//
//     i.id = (MachineInstructionId)((u64)MachineInstructionId::Store08 + (u64)size);
//
//     return i;
// }
//
// BUSTER_GLOBAL_LOCAL ByteSlice module_lower(IrModule* module)
// {
//     let functions = ir_module_get_functions(module);
//     let instruction_arena = arena_create({});
//     let virtual_register_arena = arena_create({});
//     let emitter_arena = arena_create({ .flags = { .execute = 1 }});
//     let emitter_position = emitter_arena->position;
//
//     for (EACH_SLICE_REF(function, functions))
//     {
//         IrBasicBlock* queue[64];
//         u64 queue_count = 0;
//
//         queue[0] = function.entry_block;
//         queue_count = 1;
//
//         FunctionIsel function_isel = {
//             .virtual_registers = {
//                 .arena = virtual_register_arena,
//                 .original_position = virtual_register_arena->position,
//             },
//             .instructions = {
//                 .arena = instruction_arena,
//                 .original_position = instruction_arena->position,
//             },
//         };
//         FunctionIsel* isel = &function_isel;
//
//         while (queue_count)
//         {
//             let block = queue[queue_count - 1];
//             queue_count -= 1;
//
//             for (IrInstruction* instruction = block->first; instruction; instruction = instruction->next)
//             {
//                 switch (instruction->id)
//                 {
//                     break; case IrInstructionId::Return:
//                     {
//                         let value = instruction->value;
//
//                         switch (value->id)
//                         {
//                             break; case IrValueId::ConstantInteger:
//                             {
//                                 let instructions = allocate_instruction(isel, 3);
//                                 let size = MachineSize::Four;
//                                 instructions[0] = mov_imm(isel, value->constant_integer, size);
//                                 instructions[1] = copy(isel, PhysicalRegisterX8664::RAX, instructions[0].operand_values[0].integer, size);
//                                 instructions[2] = ret(isel, PhysicalRegisterX8664::RAX, size);
//                             }
//                             break; case IrValueId::Count: BUSTER_UNREACHABLE();
//                         }
//                     }
//
//                     break; case IrInstructionId::Count: BUSTER_UNREACHABLE();
//                 }
//             }
//         }
//
//         let isel_instructions = arena_get_slice_at_position(isel->instructions.arena, MachineInstruction, isel->instructions.original_position, isel->instructions.arena->position);
//         let virtual_registers = arena_get_slice_at_position(isel->virtual_registers.arena, VirtualRegister, isel->virtual_registers.original_position, isel->virtual_registers.arena->position);
//
//         s32 frame_offset = 0;
//
//         for (EACH_SLICE_REF(virtual_register, virtual_registers))
//         {
//             let size = machine_size_to_int(virtual_register.size);
//             let alignment = size;
//             virtual_register.offset = -(s32)((u32)align_forward((u32)-frame_offset, alignment) + size);
//             frame_offset = virtual_register.offset;
//         }
//
//         ISelArena ni = {
//             .arena = isel->instructions.arena,
//             .original_position = isel->instructions.arena->position,
//         };
//
//         ISelArena* register_allocation_instructions = &ni;
//
//         PhysicalRegisterX8664 scratch_gpr[] = { PhysicalRegisterX8664::R10, PhysicalRegisterX8664::R11 };
//
//         for (EACH_SLICE_REF(instruction, isel_instructions))
//         {
//             u8 gpr_index = 0;
//
//             for (u64 operand_i = 0; operand_i < operand_count; operand_i += 1)
//             {
//                 let id = instruction.operand_ids[operand_i];
//                 if (id == MachineOperandId::None)
//                 {
//                     break;
//                 }
//
//                 if (id == MachineOperandId::VirtualRegister)
//                 {
//                     let flags = instruction.operand_flags[operand_i];
//                     let virtual_register_index = instruction.operand_values[operand_i].index;
//                     let virtual_register = virtual_registers.pointer[virtual_register_index];
//
//                     if (flags.use)
//                     {
//                         PhysicalRegisterX8664 scratch;
//                         switch (virtual_register.register_class)
//                         {
//                             break; case RegisterClass::GPR: scratch = scratch_gpr[gpr_index++];
//                             break; default: BUSTER_UNREACHABLE();
//                         }
//
//                         *allocate_instruction(register_allocation_instructions, 1) = consume_spill(scratch, virtual_register.offset, virtual_register.size);
//
//                         instruction.operand_values[operand_i] = { .index = (u64)scratch };
//                         instruction.operand_ids[operand_i] = MachineOperandId::PhysicalRegister;
//                     }
//                 }
//             }
//
//             STRUCT(DefScratch)
//             {
//                 u64 virtual_register;
//                 PhysicalRegisterX8664 physical_register;
//                 u8 reserved[4];
//             };
//
//             DefScratch def_scratches[operand_count];
//             u32 def_count = 0;
//
//             for (u64 operand_i = 0; operand_i < operand_count; operand_i += 1)
//             {
//                 let id = instruction.operand_ids[operand_i];
//                 if (id == MachineOperandId::None)
//                 {
//                     break;
//                 }
//
//                 if (id == MachineOperandId::VirtualRegister)
//                 {
//                     let flags = instruction.operand_flags[operand_i];
//                     let virtual_register_index = instruction.operand_values[operand_i].index;
//                     let virtual_register = virtual_registers.pointer[virtual_register_index];
//
//                     if (flags.def)
//                     {
//                         PhysicalRegisterX8664 scratch;
//                         switch (virtual_register.register_class)
//                         {
//                             break; case RegisterClass::GPR: scratch = scratch_gpr[gpr_index++];
//                             break; default: BUSTER_UNREACHABLE();
//                         }
//
//                         def_scratches[def_count++] = {
//                             .virtual_register = virtual_register_index,
//                             .physical_register = scratch,
//                         };
//
//                         instruction.operand_values[operand_i] = { .index = (u64)scratch };
//                         instruction.operand_ids[operand_i] = MachineOperandId::PhysicalRegister;
//                     }
//                 }
//             }
//
//             *allocate_instruction(register_allocation_instructions, 1) = instruction;
//
//             for (u32 def_i = 0; def_i < def_count; def_i += 1)
//             {
//                 let def = def_scratches[def_i];
//                 let virtual_register_index = def.virtual_register;
//                 let virtual_register = &virtual_registers.pointer[virtual_register_index];
//                 *allocate_instruction(register_allocation_instructions, 1) = produce_spill(virtual_register->offset, def.physical_register, virtual_register->size);
//             }
//         }
//
//         let ra_instructions = arena_get_slice_at_position(register_allocation_instructions->arena, MachineInstruction, register_allocation_instructions->original_position, register_allocation_instructions->arena->position);
//
//         for (EACH_SLICE_REF(instruction, ra_instructions))
//         {
//             switch (instruction.id)
//             {
//                 break; case MachineInstructionId::Return: BUSTER_UNREACHABLE();
//                 break; case MachineInstructionId::Move08RegImm: BUSTER_UNREACHABLE();
//                 break; case MachineInstructionId::Move16RegImm: BUSTER_UNREACHABLE();
//                 break; case MachineInstructionId::Move32RegImm: BUSTER_UNREACHABLE();
//                 break; case MachineInstructionId::Move64RegImm: BUSTER_UNREACHABLE();
//                 break; case MachineInstructionId::Zero08GPR: BUSTER_UNREACHABLE();
//                 break; case MachineInstructionId::Zero16GPR: BUSTER_UNREACHABLE();
//                 break; case MachineInstructionId::Zero32GPR:
//                 {
//                     let reg = (PhysicalRegisterX8664)instruction.operand_values[0].index;
//                     bool use_rex = reg >= PhysicalRegisterX8664::R8;
//                     u64 byte_count = 2 + use_rex;
//                     let allocation = arena_allocate(emitter_arena, u8, byte_count);
//                     allocation[0] = 0x45;
//                     allocation[use_rex + 0] = 0x31;
//                     let encoding_reg = (u8)reg & 0b111;
//                     allocation[use_rex + 1] = 0xc0 | (u8)(encoding_reg << 3) | (u8)(encoding_reg << 0);
//                 }
//                 break; case MachineInstructionId::Zero64GPR: BUSTER_UNREACHABLE();
//                 break; case MachineInstructionId::Copy08: BUSTER_UNREACHABLE();
//                 break; case MachineInstructionId::Copy16: BUSTER_UNREACHABLE();
//                 break; case MachineInstructionId::Copy32:
//                 {
//                     BUSTER_CHECK(instruction.operand_ids[0] == MachineOperandId::PhysicalRegister);
//                     BUSTER_CHECK(instruction.operand_ids[1] == MachineOperandId::PhysicalRegister);
//
//                     let destination = (PhysicalRegisterX8664)instruction.operand_values[0].index;
//                     let source = (PhysicalRegisterX8664)instruction.operand_values[1].index;
//
//                     bool is_destination_reg64 = destination >= PhysicalRegisterX8664::R8;
//                     bool is_source_reg64 = source >= PhysicalRegisterX8664::R8;
//                     bool use_rex = is_destination_reg64 || is_source_reg64;
//                     u64 byte_count = 2 + use_rex;
//
//                     let encoding_source = (u8)source & 0b111;
//                     let encoding_destination = (u8)destination & 0b111;
//
//                     let allocation = arena_allocate(emitter_arena, u8, byte_count);
//
//                     if (is_destination_reg64 && is_source_reg64)
//                     {
//                         BUSTER_TRAP();
//                     }
//                     else if (is_source_reg64)
//                     {
//                         allocation[0] = 0x44;
//                         allocation[use_rex + 0] = 0x89;
//                         allocation[use_rex + 1] = (1 << 7) | (1 << 6) | (u8)(encoding_source << 3) | (u8)(encoding_destination << 0);
//                     }
//                     else if (is_destination_reg64)
//                     {
//                         BUSTER_TRAP();
//                     }
//                     else
//                     {
//                         BUSTER_TRAP();
//                     }
//                 }
//                 break; case MachineInstructionId::Copy64: BUSTER_UNREACHABLE();
//                 break; case MachineInstructionId::Ret08: BUSTER_UNREACHABLE();
//                 break; case MachineInstructionId::Ret16: BUSTER_UNREACHABLE();
//                 break; case MachineInstructionId::Ret32:
//                 {
//                     *arena_allocate(emitter_arena, u8, 1) = 0xc3;
//                 }
//                 break; case MachineInstructionId::Ret64: BUSTER_UNREACHABLE();
//                 break; case MachineInstructionId::Load08: BUSTER_UNREACHABLE();
//                 break; case MachineInstructionId::Load16: BUSTER_UNREACHABLE();
//                 break; case MachineInstructionId::Load32:
//                 {
//                     BUSTER_CHECK(instruction.operand_ids[0] == MachineOperandId::PhysicalRegister);
//                     BUSTER_CHECK(instruction.operand_ids[1] == MachineOperandId::Memory);
//                     let destination_reg = (PhysicalRegisterX8664)instruction.operand_values[0].index;
//                     let source = instruction.operand_values[1];
//                     BUSTER_CHECK(source.memory.base == RegisterBase::BasePointer);
//                     bool use_rex = destination_reg >= PhysicalRegisterX8664::R8;
//                     u64 byte_count = 3 + use_rex;
//                     let allocation = arena_allocate(emitter_arena, u8, byte_count);
//
//                     if (source.memory.offset >= INT8_MIN && source.memory.offset <= INT8_MAX)
//                     {
//                         let encoding_reg = (u8)destination_reg & 0b111;
//                         allocation[0] = 0x44;
//                         allocation[use_rex + 0] = 0x8b;
//                         allocation[use_rex + 1] = (1 << 6) | (u8)(encoding_reg << 3) | (u8)PhysicalRegisterX8664::RBP;
//                         allocation[use_rex + 2] = (u8)(s8)source.memory.offset;
//                     }
//                     else
//                     {
//                         BUSTER_TRAP();
//                     }
//                 }
//                 break; case MachineInstructionId::Load64: BUSTER_UNREACHABLE();
//                 break; case MachineInstructionId::Store08: BUSTER_UNREACHABLE();
//                 break; case MachineInstructionId::Store16: BUSTER_UNREACHABLE();
//                 break; case MachineInstructionId::Store32:
//                 {
//                     BUSTER_CHECK(instruction.operand_ids[0] == MachineOperandId::Memory);
//                     BUSTER_CHECK(instruction.operand_ids[1] == MachineOperandId::PhysicalRegister);
//                     let source_reg = (PhysicalRegisterX8664)instruction.operand_values[1].index;
//                     let destination = instruction.operand_values[0];
//                     BUSTER_CHECK(destination.memory.base == RegisterBase::BasePointer);
//
//                     bool use_rex = source_reg >= PhysicalRegisterX8664::R8;
//                     u64 byte_count = 3 + use_rex;
//                     let allocation = arena_allocate(emitter_arena, u8, byte_count);
//
//                     if (destination.memory.offset >= INT8_MIN && destination.memory.offset <= INT8_MAX)
//                     {
//                         let encoding_reg = (u8)source_reg & 0b111;
//                         allocation[0] = 0x44;
//                         allocation[use_rex + 0] = 0x89;
//                         allocation[use_rex + 1] = (1 << 6) | (u8)(encoding_reg << 3) | (u8)PhysicalRegisterX8664::RBP;
//                         allocation[use_rex + 2] = (u8)(s8)destination.memory.offset;
//                     }
//                     else
//                     {
//                         BUSTER_TRAP();
//                     }
//                 }
//                 break; case MachineInstructionId::Store64: BUSTER_UNREACHABLE();
//                 break; case MachineInstructionId::Count: BUSTER_UNREACHABLE();
//             }
//         }
//     }
//
//     let code = arena_get_slice_at_position(emitter_arena, u8, emitter_position, emitter_arena->position);
//     return code;
// }

// typedef int MainFunction();

// BUSTER_GLOBAL_LOCAL void compiler_experiments()
// {
//     let arena = arena_create({});
//     let module = ir_create_mock_module(arena);
//     let code = module_lower(module);
//     let fn = (MainFunction*)code.pointer;
//     BUSTER_CHECK(fn() == 0);
// }

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

        {
            u64 position = arena->position;
            UnitTestArguments arguments = { arena, &default_show };
            BatchTestResult batch_test_result = parser_tests(&arguments);
            result = batch_test_report(&arguments, batch_test_result) ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
            arena->position = position;
        }

        arena_destroy(arena, 1);

    }
#endif
    parser_experiments();
    // compiler_experiments();
    // analysis_experiments();

#if 0
    if (result == ProcessResult::Success)
    {
        let windowing = state.windowing = os_windowing_initialize();
        if (windowing)
        {
            let arena = program_state->arena;
            let r = state.rendering = rendering_initialize(arena);
            if (r)
            {
                state.first_window = state.last_window = arena_allocate(arena, IdeWindow, 1);
                let os_window = os_window_create(windowing, (OsWindowCreate) {
                        .name = SOs("Ide"),
                        .size = {
                        .width = 1600,
                        .height= 900,
                        },
                        .refresh_callback = &window_refresh_callback,
                        });
                state.first_window->os = os_window;

                if (os_window)
                {
                    let render_window = state.first_window->render = rendering_window_initialize(arena, windowing, r, os_window);

                    if (render_window)
                    {
                        state.first_window->ui = ui_state_allocate(r, render_window);
                        state.first_window->root_panel = arena_allocate(state.state.arena, IdePanel, 1);
                        state.first_window->root_panel->parent_percentage = 1.0f;
                        state.first_window->root_panel->split_axis = Axis2::X;

                        rendering_window_rect_texture_update_begin(state.first_window->render);

                        let font_path = font_file_get_path(arena, FontIndex::FONT_INDEX_MONO);
                        u32 monospace_font_height = 24;

                        let white_texture = white_texture_create(state.state.arena, state.rendering);
                        let font = rendering_font_create(state.state.arena, state.rendering, (FontTextureAtlasCreate) {
                                .font_path = font_path,
                                .text_height = monospace_font_height,
                                });

                        rendering_window_queue_rect_texture_update(state.rendering, state.first_window->render, RectTextureSlot::RECT_TEXTURE_SLOT_WHITE, white_texture);
                        rendering_queue_font_update(state.rendering, state.first_window->render, RenderFontType::RENDER_FONT_TYPE_MONOSPACE, font);

                        rendering_window_rect_texture_update_end(state.rendering, state.first_window->render);

                        state.last_frame_timestamp = timestamp_take();

                        bool test = state.test && !flag_get(program_state->input.flags, ProgramFlag::Test_Persist);
                        let loop_times = test ? (u64)3 : UINT64_MAX;
                        for (u64 i = 0; i < loop_times && state.first_window; i += 1)
                        {
                            app_update();
                        }

                        if (test)
                        {
                            for (let window = state.first_window; window; window = window->next)
                            {
                                ui_state_deinitialize(window->ui);
                                window->ui = 0;
                                rendering_window_deinitialize(state.rendering, window->render);
                                window->render = 0;
                            }
                        }

                        // TODO: OS deinitialization
                    }
                    else
                    {
                        string8_print(S8("Failed to create render window\n"));
                        result = ProcessResult::Failed;
                    }
                }
                else
                {
                    string8_print(S8("Failed to create window\n"));
                    result = ProcessResult::Failed;
                }

                rendering_deinitialize(r);
            }
            else
            {
                string8_print(S8("Failed to initialize rendering\n"));
                result = ProcessResult::Failed;
            }

            os_windowing_deinitialize(windowing);
        }
        else
        {
            string8_print(S8("Failed to initialize windowing\n"));
            result = ProcessResult::Failed;
        }
    }
#endif

    return result;
}

ProcessResult entry_point(void)
{
    return run_app();
}
#endif
