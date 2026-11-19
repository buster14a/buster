insert("            const SymbolId symbol = unit->bindings[i].symbol;", "            probe_visits += 1;\n")
insert("    const uint64_t mask = capacity - 1;", "    probe_scratch = capacity * sizeof(*names);\n")
before = "while (names[slot].symbol != 0 && !interpreter_link_same_name(link, symbol, links[names[slot].input], names[slot].symbol))"
after = "while (names[slot].symbol != 0 && (++probe_candidates, !interpreter_link_same_name(link, symbol, links[names[slot].input], names[slot].symbol)))"
assert text.count(before) == 1
text = text.replace(before, after)
boundary = "        unit->binding_count = 0;\n    }\n"
assert text.count(boundary) == 1
text = text.replace(boundary, boundary + "    probe_build = probe_candidates;\n")
insert("    if (scratch.arena != 0)\n", "    probe_refs = probe_candidates - probe_build;\n")
