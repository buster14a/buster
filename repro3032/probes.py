from pathlib import Path
import sys

mode = sys.argv[1]
root = Path(__file__).resolve().parent
owner = Path("src/buster/compiler")
source = owner / "interpreter_link.c"
test = owner / "interpreter_link_test.c"
text = source.read_text()

def insert(before, extra):
    global text
    if text.count(before) != 1:
        raise RuntimeError(before)
    text = text.replace(before, extra + before)

insert("static const InterpreterLinkExternal* interpreter_link_external(", "static uint64_t probe_candidates, probe_externals, probe_build, probe_refs, probe_visits, probe_scratch;\n")
insert("        if (externals[i].input == input", "        probe_externals += 1;\n")
insert("    const uint64_t before = arena != 0 ? arena->length : 0;", "    probe_candidates = probe_externals = probe_build = probe_refs = probe_visits = probe_scratch = 0;\n")
insert("static InterpreterLinkResult interpreter_link_materialize(", (root / "observe.c").read_text() + "\n")
if mode == "base":
    insert("                        const uint8_t candidate_binding =", "                        probe_candidates += 1;\n")
else:
    exec((root / "head-counters.py").read_text())
insert("                const InterpreterLinkUnit* selected = &image->units[selected_input];", '                fprintf(stderr, "WIN %u %u %u %u\\n", input, symbol, selected_input, selected_symbol);\n')
start = "        result = interpreter_link_resolve(&image,"
insert(start, "        const uint64_t setup_external = probe_externals;\n")
end = text.index(";", text.index(start))
text = text[:end+1] + '\n        fprintf(stderr, "COUNT units=%u ext=%u candidates=%llu resolver_external=%llu setup_external=%llu build_equal=%llu ref_equal=%llu build_visits=%llu scratch=%llu\\n", link_count, external_count, (unsigned long long)probe_candidates, (unsigned long long)(probe_externals - setup_external), (unsigned long long)setup_external, (unsigned long long)probe_build, (unsigned long long)probe_refs, (unsigned long long)probe_visits, (unsigned long long)probe_scratch);' + text[end+1:]
insert("        *output = image;", "        probe_map(&image, links, externals, external_count);\n")
marker = "    return result;\n}\n\n#endif"
insert(marker, '    fprintf(stderr, "DIAG %u %u %u %u %u\\n", result.status, result.input, result.symbol, result.atom, result.fixup);\n')
source.write_text(text)

text = test.read_text()
insert("static int run_interpreter_link_tests(", (root / "stress.c").read_text() + "\n")
insert("        printf(\"interpreter-link:", "        interpreter_link_test_stress(&tally, 8);\n        interpreter_link_test_stress(&tally, 32);\n        interpreter_link_test_stress(&tally, 128);\n")
test.write_text(text)
