#!/usr/bin/env python3
"""Extract the pinned production cleanup into a bounded C model; no builds here."""
import hashlib
import pathlib
import sys

PIN = "8a02397d698438caab077b05c55c6b01c8ecdb66a365d37ad4235b12a63eb874"
root = pathlib.Path(sys.argv[1])
out = pathlib.Path(sys.argv[2])
source = (root / "src/buster/lib/compiler/ir/ir_promote.c").read_bytes()
if hashlib.sha256(source).hexdigest() != PIN:
    raise SystemExit("Pinned ir_promote.c identity mismatch; refusing extraction")
text = source.decode()
out.mkdir(parents=True, exist_ok=True)


def function(name):
    # Find the declaration explicitly: names can also occur in orientation comments.
    import re
    match = re.search(r"BUSTER_GLOBAL_LOCAL (?:u32|void) " + name + r"\(", text)
    if match is None:
        raise SystemExit("Missing source function " + name)
    start = match.start()
    body = text.index("{", start)
    depth = 1
    end = body + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


def once(data, old, new):
    if data.count(old) != 1:
        raise SystemExit("Source anchor is not unique: " + old[:80])
    return data.replace(old, new, 1)

base = function("ir_promote_compact")
root_function = function("ir_promote_root").replace("ir_promote_root", "source_root")
(out / "source_root.inc").write_text(root_function + "\n")
candidate = once(base, "    bool changed = count != old_value_count;", """    u32 witness_count = count - old_value_count;
    IrPromoteWitness* witnesses = witness_count ? arena_allocate(arena, IrPromoteWitness, witness_count) : 0;
    bool first_sweep = true;
    bool changed = count != old_value_count;""")
candidate = once(candidate, "                bool trivial = parameter->value.value >= old_value_count;", """                bool trivial = parameter->value.value >= old_value_count;
                IrPromoteWitness* witness = trivial ? witnesses + parameter->value.value - old_value_count : 0;
                if (witness)
                {
                    if (!first_sweep && witness->first != IR_PROMOTE_NONE)
                    {
                        u32 first = ir_promote_root(replacements, witness->first);
                        u32 second = ir_promote_root(replacements, witness->second);
                        if (first != second && first != parameter->value.value && second != parameter->value.value)
                        {
                            trivial = false;
                        }
                    }
                    if (trivial)
                    {
                        witness->first = IR_PROMOTE_NONE;
                    }
                }""")
candidate = once(candidate, "                        trivial = same == IR_PROMOTE_NONE || value == same;", """                        if (same != IR_PROMOTE_NONE && value != same)
                        {
                            witness->first = same;
                            witness->second = value;
                        }
                        trivial = same == IR_PROMOTE_NONE || value == same;""")
candidate = once(candidate, "        }\n    }\n    ir_rewrite_compact", "        }\n        first_sweep = false;\n    }\n    ir_rewrite_compact")
(out / "candidate_uninstrumented.inc").write_text(candidate + "\n")

for name in ("baseline", "candidate", "stale", "self"):
    data = base if name == "baseline" else candidate
    if name == "stale":
        data = data.replace("ir_promote_root(replacements, witness->first)", "witness->first")
        data = data.replace("ir_promote_root(replacements, witness->second)", "witness->second")
    if name == "self":
        data = once(data, "first != second && first != parameter->value.value && second != parameter->value.value", "first != second")
    data = once(data, "void ir_promote_compact(", "void " + name + "_compact(")
    data = data.replace("ir_promote_root(replacements,", "probe_root(arena, replacements,")
    data = once(data, "                if (trivial && same != IR_PROMOTE_NONE)", """                probe_decision(arena, replacements, parameter, old_value_count,
                               trivial && same != IR_PROMOTE_NONE, same);
                if (trivial && same != IR_PROMOTE_NONE)""")
    data = once(data, "                    statistics->removed_parameters += 1;", """                    statistics->removed_parameters += 1;
                    probe_removed(arena, parameter->value.value, same, statistics->parameter_sweeps);""")
    if name != "baseline":
        data = once(data, "                    if (!first_sweep && witness->first != IR_PROMOTE_NONE)\n                    {", """                    if (!first_sweep && witness->first != IR_PROMOTE_NONE)
                    {
                        arena->checks += 1;""")
        data = once(data, "                            trivial = false;", "                            arena->hits += 1;\n                            trivial = false;")
        data = once(data, "                        witness->first = IR_PROMOTE_NONE;", "                        arena->stores += 1;\n                        witness->first = IR_PROMOTE_NONE;")
        data = once(data, "                            witness->first = same;", "                            arena->stores += 2;\n                            witness->first = same;")
    (out / (name + ".inc")).write_text(data + "\n")
print("SOURCE_SHA256=" + PIN)
for path in sorted(out.glob("*.inc")):
    print(path.name + " SHA256=" + hashlib.sha256(path.read_bytes()).hexdigest())
