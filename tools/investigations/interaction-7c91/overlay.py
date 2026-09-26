"""Apply a disposable source overlay; never accept drift from the pinned blob.
Compiler experiment and generator are C; this file only materializes the overlay.
"""
from pathlib import Path
import hashlib
import shutil
import sys

root, research = map(Path, sys.argv[1:3])
p = root / 'src/buster/lib/compiler/llvm/bitcode.c'
s = p.read_text()
blob = hashlib.sha1(f'blob {len(s.encode())}\0'.encode() + s.encode()).hexdigest()
if blob != 'bfdcda8bcb96f7150a374c5c9260bda3e074a306':
    raise SystemExit(f'refusing source drift: {blob}')

def replace(old, new):
    global s
    if s.count(old) != 1:
        raise SystemExit(f'anchor count {s.count(old)}: {old[:100]}')
    s = s.replace(old, new, 1)

replace('#include <buster/lib/compiler/llvm/bitcode.h>',
        '#include <buster/lib/compiler/llvm/bitcode.h>\n#include "research_trace.h"')
replace('struct LlvmBcContext\n{', 'struct LlvmBcContext\n{\n#if R7_TRACE\n    R7Counts r7;\n#endif')
a = s.index('static u32 llvm_bc_add_type_record(')
b = s.index('static u32 llvm_bc_integer_type(', a)
f = s[a:b]
old = '        LlvmBcTypeRecord* record = context->types + index;'
if f.count(old) != 1:
    raise SystemExit('ambiguous type-interner anchor')
f = f.replace(old, '        R7_ADD(context, intern_rows, 1);\n' + old)
s = s[:a] + f + s[b:]
replace('            equal &= record->operands[operand] == operands[operand];',
        '            R7_ADD(context, intern_operands, 1);\n            equal &= record->operands[operand] == operands[operand];')
a = s.index('static bool llvm_bc_type_dependencies_ready(')
b = s.index('static bool llvm_bc_add_ir_type(', a)
f = s[a:b]
f = f.replace('            return context->ir_type_ids[type->unqualified_type.value]',
              '            R7_ADD(context, dependency_checks, 1);\n            return context->ir_type_ids[type->unqualified_type.value]', 1)
f = f.replace('        case IR_TYPE_VECTOR:\n            return',
              '        case IR_TYPE_VECTOR:\n            R7_ADD(context, dependency_checks, 1);\n            return', 1)
f = f.replace('            if (type->return_type.value',
              '            R7_ADD(context, dependency_checks, 1);\n            if (type->return_type.value', 1)
f = f.replace('                if (type->parameter_types[parameter_index].value',
              '                R7_ADD(context, dependency_checks, 1);\n                if (type->parameter_types[parameter_index].value', 1)
f = f.replace('                if (type->fields[field_index].type.value',
              '                R7_ADD(context, dependency_checks, 1);\n                if (type->fields[field_index].type.value', 1)
f = f.replace('        case IR_TYPE_ENUM:\n            return',
              '        case IR_TYPE_ENUM:\n            R7_ADD(context, dependency_checks, type->unqualified_type.value != IR_ID_UNDERLYING_INVALID);\n            return', 1)
s = s[:a] + f + s[b:]
replace('static bool llvm_bc_build_types(LlvmBcContext* context)',
        '#include "research_order.inc"\n\nstatic bool llvm_bc_build_types(LlvmBcContext* context)')
a = s.index('    u32 unresolved = count;', s.index('static bool llvm_bc_build_types'))
b = s.index('\n}\n', a)
s = s[:a] + '''#if R7_TRACE
    r7_observe_graph(context);
#endif
    bool success;
#if R7_ORDERED
    R7PlanResult plan = r7_ordered_types(context);
    if (plan.handled)
    {
        success = plan.success;
    }
    else
    {
        R7_ADD(context, fallback, 1);
        success = r7_sweep_types(context);
    }
#else
    success = r7_sweep_types(context);
#endif
    return success;''' + s[b:]
replace('    context.stream.abbreviation_width = 2;', '''    context.stream.abbreviation_width = 2;
#if R7_TRACE
    context.r7.graph = getenv("BUSTER_R7_GRAPH") != NULL;
#endif''')
replace('    return artifact;\n}\n\nLlvmBitcodeArtifact llvm_bitcode_emit(', '''#if R7_TRACE
    fprintf(stderr, "R7_SUMMARY types=%u records=%u bytes=%llu ok=%u error=%u "
            "passes=%llu slots=%llu attempts=%llu resolved=%llu dependencies=%llu "
            "intern_rows=%llu intern_operands=%llu plan_nodes=%llu plan_edges=%llu "
            "delivered=%llu buckets=%llu setup_bytes=%llu fallback=%llu observed_edges=%llu\\n",
            program ? (unsigned)program->types.count : 0u, (unsigned)context.type_count,
            (unsigned long long)artifact.bytes.length, (unsigned)artifact.success, (unsigned)context.error.code,
            (unsigned long long)context.r7.passes, (unsigned long long)context.r7.slots,
            (unsigned long long)context.r7.attempts, (unsigned long long)context.r7.resolved,
            (unsigned long long)context.r7.dependency_checks, (unsigned long long)context.r7.intern_rows,
            (unsigned long long)context.r7.intern_operands, (unsigned long long)context.r7.plan_nodes,
            (unsigned long long)context.r7.plan_edges, (unsigned long long)context.r7.delivered_edges,
            (unsigned long long)context.r7.bucket_nodes, (unsigned long long)context.r7.setup_bytes,
            (unsigned long long)context.r7.fallback, (unsigned long long)context.r7.observed_edges);
#endif
    return artifact;
}

LlvmBitcodeArtifact llvm_bitcode_emit(''')
p.write_text(s)
shutil.copyfile(research / 'trace.h', p.parent / 'research_trace.h')
shutil.copyfile(research / 'order.inc', p.parent / 'research_order.inc')
