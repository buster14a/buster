#!/usr/bin/env python3
"""Port Zig's test/c_abi suite into tests/c_abi_*.c.

Reads a Zig checkout's test/c_abi/{cfuncs.c,main.zig} and emits:

  tests/c_abi_cfuncs.c         upstream cfuncs.c with the hosted include block
                               replaced by c_abi.h and capability gates
                               injected (see tests/c_abi.h for the gate list)
  tests/c_abi_main_generated.c the machine-regular middle of main.zig (the
                               @Vector matrix and the struct-shape families)
                               translated to C

The handwritten halves of the Zig side live in tests/c_abi_main.c and are not
touched here. Rerun after syncing the Zig checkout:

  tools/port_zig_c_abi.py --zig ~/dev/zig

The translator understands only the constructs those regions actually use and
fails loudly on anything new, so an upstream sync that grows the grammar shows
up as a hard error here rather than as a silent mistranslation.
"""

import argparse
import os
import re
import sys

SCALAR_TYPES = {
    "u8": "uint8_t",
    "i8": "int8_t",
    "u16": "uint16_t",
    "i16": "int16_t",
    "u32": "uint32_t",
    "i32": "int32_t",
    "u64": "uint64_t",
    "i64": "int64_t",
    "u128": "unsigned __int128",
    "i128": "__int128",
    "f32": "float",
    "f64": "double",
    "usize": "size_t",
    "bool": "bool",
    "c_int": "int",
    "c_uint": "unsigned int",
    "c_ulong": "unsigned long",
    "c_longdouble": "long double",
}

VECTOR_ELEMENT_SIZES = {"u8": 1, "u16": 2, "u32": 4, "u64": 8, "f32": 4, "f64": 8}


def fail(message):
    raise SystemExit("port_zig_c_abi: " + message)


def vector_gate_conditions(element, lanes):
    # ZIG_NO_VECTORS strips the whole matrix: a cross-compiler pairing needs
    # it while the small/wide vector ABI divergences recorded in tests/c_abi.h
    # stay open.
    conditions = ["ZIG_NO_VECTORS"]
    total = lanes * VECTOR_ELEMENT_SIZES[element]
    if total < 4:
        conditions.append("ZIG_NO_TINY_VECTORS")
    if lanes & (lanes - 1):
        conditions.append("ZIG_NO_NON_POW2_VECTORS")
    if total > 64:
        conditions.append("ZIG_NO_WIDE_VECTORS")
    return conditions


def gate_open(conditions):
    return "#if " + " && ".join("!defined(" + c + ")" for c in conditions)


# --- cfuncs.c adaptation ----------------------------------------------------


def find_line(lines, predicate, start=0, what=""):
    for index in range(start, len(lines)):
        if predicate(lines[index]):
            return index
    fail("cfuncs.c anchor not found: " + what)


def balanced_endif(lines, start):
    """Index of the line holding the #endif that closes the #if at start."""
    depth = 0
    for index in range(start, len(lines)):
        stripped = lines[index].strip()
        if stripped.startswith(("#if", "#ifdef", "#ifndef")):
            depth += 1
        elif stripped.startswith("#endif"):
            depth -= 1
            if depth == 0:
                return index
    fail("unbalanced #if at cfuncs.c line %d" % (start + 1))


def close_of_function(lines, header_index):
    for index in range(header_index, len(lines)):
        if lines[index] == "}":
            return index
    fail("no close for function at cfuncs.c line %d" % (header_index + 1))


def preceding_if_stack(lines, index):
    """Walk back over the contiguous run of #if lines above index."""
    start = index
    while start > 0:
        stripped = lines[start - 1].strip()
        if stripped.startswith(("#if", "#ifdef", "#ifndef")) or not stripped:
            start -= 1
            if not stripped:
                # A blank line inside the run is fine; stop only when the run
                # above it is not conditional.
                above = lines[start - 1].strip() if start else ""
                if not above.startswith(("#if", "#ifdef", "#ifndef")):
                    return start + 1
            continue
        break
    return start


def adapt_cfuncs(upstream_lines):
    lines = list(upstream_lines)
    inserts = []  # (index, before_or_after, text) with index into `lines`

    def insert_before(index, text):
        inserts.append((index, 0, text))

    def insert_after(index, text):
        inserts.append((index, 1, text))

    # The long double family: prototypes through c_test_longdouble.
    start = find_line(lines, lambda l: l.startswith("long double zig_ret_longdouble"), 0, "longdouble prototypes")
    header = find_line(lines, lambda l: l.startswith("void c_test_longdouble"), start, "c_test_longdouble")
    insert_before(start, "#ifndef ZIG_NO_LONG_DOUBLE")
    insert_after(close_of_function(lines, header), "#endif // ZIG_NO_LONG_DOUBLE")

    # The bool vector region: the arch #if stack around the ext_vector_type
    # typedefs.
    first_bool = find_line(lines, lambda l: l.startswith("typedef bool Vector_2_bool"), 0, "bool vectors")
    stack_start = preceding_if_stack(lines, first_bool)
    insert_before(stack_start, "#ifndef ZIG_NO_BOOL_VECTORS")
    insert_after(balanced_endif(lines, stack_start), "#endif // ZIG_NO_BOOL_VECTORS")

    # Per-shape gates over the integer/float vector matrix.
    vector_typedef = re.compile(
        r"typedef (?:uint8_t|uint16_t|uint32_t|uint64_t|float|double|size_t) "
        r"Vector_(\d+)_(u8|u16|u32|u64|f32|f64) ")
    section_starts = [i for i, line in enumerate(lines) if vector_typedef.match(line)]
    if len(section_starts) != len(
            [n for sizes in ((18,), (16,), (14,), (12,), (14,), (12,)) for n in sizes]) and len(section_starts) < 80:
        fail("unexpected vector section count %d" % len(section_starts))
    struct_region = find_line(lines, lambda l: l.startswith("struct Struct_u8 {"), 0, "struct families")
    for position, start in enumerate(section_starts):
        match = vector_typedef.match(lines[start])
        lanes, element = int(match.group(1)), match.group(2)
        conditions = vector_gate_conditions(element, lanes)
        if not conditions:
            continue
        end = section_starts[position + 1] if position + 1 < len(section_starts) else struct_region
        # Trim trailing blank lines out of the guarded region.
        while end > start and not lines[end - 1].strip():
            end -= 1
        insert_before(start, gate_open(conditions))
        insert_after(end - 1, "#endif")

    # The f16 pair: the ZIG_NO_RAW_F16 block plus the unconditional struct.
    raw_f16 = find_line(lines, lambda l: l.strip() == "#ifndef ZIG_NO_RAW_F16", 0, "raw f16")
    f16_struct = find_line(lines, lambda l: l.startswith("f16_struct c_f16_struct"), raw_f16, "c_f16_struct")
    insert_before(raw_f16, "#ifndef ZIG_NO_F16")
    insert_after(close_of_function(lines, f16_struct), "#endif // ZIG_NO_F16")

    # The f80 block keeps its upstream arch condition and gains the gate.
    f80_if = find_line(lines, lambda l: l.startswith("#if") and "_MSC_VER" in l and "__x86_64__" in l, 0, "f80 arch #if")
    if not lines[f80_if + 1].startswith("typedef long double f80"):
        fail("f80 arch #if does not open the f80 block")
    insert_before(f80_if, "#ifndef ZIG_NO_F80")
    insert_after(balanced_endif(lines, f80_if), "#endif // ZIG_NO_F80")

    # Explicit calling-convention attributes.
    explicit = find_line(lines, lambda l: "c_explict_win64" in l, 0, "c_explict_win64")
    explicit_if = explicit
    while not lines[explicit_if].strip().startswith("#ifdef"):
        explicit_if -= 1
    insert_before(explicit_if, "#ifndef ZIG_NO_CC_ATTRIBUTES")
    insert_after(balanced_endif(lines, explicit_if), "#endif // ZIG_NO_CC_ATTRIBUTES")

    preserve = find_line(lines, lambda l: "c_preserve_none" in l, 0, "c_preserve_none")
    preserve_if = preserve
    while not lines[preserve_if].strip().startswith("#if "):
        preserve_if -= 1
    insert_before(preserve_if, "#ifndef ZIG_NO_CC_ATTRIBUTES")
    insert_after(balanced_endif(lines, preserve_if), "#endif // ZIG_NO_CC_ATTRIBUTES")

    # The big packed struct family constructs its value with runtime 128-bit
    # shifts; every one of its pieces already sits in its own ZIG_NO_I128
    # region, which gains the ZIG_NO_INT128_SHIFTS gate on top (tests/c_abi.h
    # explains the AArch64 shift gap it covers).
    shift_regions = set()
    for index, line in enumerate(lines):
        if "big_packed_struct" not in line:
            continue
        region = index
        while region >= 0 and lines[region].strip() != "#ifndef ZIG_NO_I128":
            region -= 1
        if region < 0:
            fail("big_packed_struct outside a ZIG_NO_I128 region at cfuncs.c line %d" % (index + 1))
        shift_regions.add(region)
    for region in shift_regions:
        insert_before(region, "#ifndef ZIG_NO_INT128_SHIFTS")
        insert_after(balanced_endif(lines, region), "#endif // ZIG_NO_INT128_SHIFTS")

    # Apply insertions from the bottom up so indices stay valid.
    for index, after, text in sorted(inserts, key=lambda entry: (entry[0], entry[1]), reverse=True):
        lines.insert(index + after, text)

    # Replace the hosted include block. Upstream opens with six includes; keep
    # everything from the first non-include, non-blank line on.
    body_start = 0
    while lines[body_start].startswith("#include") or not lines[body_start].strip():
        body_start += 1
    header = [
        "// The C side of the Zig test/c_abi suite, adapted for this repository:",
        "// generated by tools/port_zig_c_abi.py from ziglang/zig test/c_abi/cfuncs.c",
        "// (hosted includes replaced by c_abi.h, capability gates injected). Edit",
        "// the generator, not this file. See tests/c_abi.h for the pairing and",
        "// gate contract.",
        "#include \"c_abi.h\"",
        "",
    ]
    return header + lines[body_start:]


# --- main.zig translation ---------------------------------------------------


def map_scalar(zig_type):
    if zig_type in SCALAR_TYPES:
        return SCALAR_TYPES[zig_type]
    fail("unmapped zig scalar type: " + zig_type)


def map_type(zig_type):
    zig_type = zig_type.strip()
    vector = re.fullmatch(r"@Vector\((\d+), (\w+)\)", zig_type)
    if vector:
        return "Vector_%s_%s" % (vector.group(1), vector.group(2))
    if zig_type == "*anyopaque" or zig_type == "?*anyopaque":
        return "void*"
    return map_scalar(zig_type)


SKIP_DROPPED = (
    "zig_backend", ".hexagon", "loongarch", "isMIPS", "isPowerPC", "isRiscv",
    "isRISCV", ".s390x", "isWasm", "isArm()", "abi.float",
)


def map_skip_condition(condition):
    """One zig skip condition -> C condition text, or None when impossible on
    the targets this port runs on (x86-64 linux/windows, aarch64 macos)."""
    condition = condition.strip()
    if "isAARCH64()" in condition:
        return "defined(__aarch64__)"
    if condition == "builtin.cpu.arch == .x86 and builtin.os.tag == .windows":
        return None
    if condition == "builtin.cpu.arch == .x86":
        return None
    if condition == "!have_i128":
        return "defined(ZIG_NO_I128)"
    for marker in SKIP_DROPPED:
        if marker in condition:
            return None
    if condition == "builtin.os.tag == .windows":
        return "defined(_WIN32)"
    fail("unmapped skip condition: " + condition)


def sanitize_test_name(title):
    name = re.sub(r"[^A-Za-z0-9]+", "_", title).strip("_").lower()
    return "test_" + name


class Translator:
    def __init__(self, lines):
        self.lines = lines
        self.index = 0
        self.output = []
        self.runner = []  # (guard_conditions, function_name) in order
        self.extern_types = {}  # c function name -> (return_c_type, [param_c_types])
        self.type_names = set()
        self.test_names = set()
        self.emitted_vector_typedefs = set()
        self.sentinels = {}  # (type name, field) -> (sentinel value text, count)
        self.open_gate = None  # conditions list currently open in output

    # -- gate handling per vector shape --------------------------------------

    def map_type(self, zig_type):
        zig_type = zig_type.strip()
        if zig_type in self.type_names:
            return zig_type
        return map_type(zig_type)

    def gate_for_name(self, name):
        match = re.search(r"vector_(\d+)_(u8|u16|u32|u64|f32|f64)", name)
        if not match:
            return []
        return vector_gate_conditions(match.group(2), int(match.group(1)))

    def ensure_gate(self, conditions):
        if self.open_gate == conditions:
            return
        self.close_gate()
        if conditions:
            self.output.append(gate_open(conditions))
            self.open_gate = conditions

    def close_gate(self):
        if self.open_gate:
            self.output.append("#endif")
            self.open_gate = None

    def ensure_vector_typedef(self, c_type):
        match = re.fullmatch(r"Vector_(\d+)_(u8|u16|u32|u64|f32|f64)", c_type)
        if not match or c_type in self.emitted_vector_typedefs:
            return
        lanes, element = match.group(1), match.group(2)
        element_c = map_scalar(element)
        self.output.append("typedef %s %s __attribute__((vector_size(%s * sizeof(%s))));" % (element_c, c_type, lanes, element_c))
        self.emitted_vector_typedefs.add(c_type)

    # -- expression/initializer translation ----------------------------------

    def translate_expression(self, text):
        # `.{` anonymous initializers inside an already-typed context.
        return text.replace(".{", "{")

    def apply_sentinels(self, c_type, literal):
        for (type_name, field), (sentinel, count) in self.sentinels.items():
            if type_name != c_type:
                continue
            pattern = re.compile(r"\.%s = \{([^}]*)\}" % re.escape(field))
            match = pattern.search(literal)
            if not match:
                fail("sentinel field .%s missing in literal for %s" % (field, c_type))
            elements = match.group(1).strip()
            if elements.count(",") + (1 if elements else 0) != count:
                fail("sentinel literal for %s.%s does not have %d elements" % (c_type, field, count))
            filled = (elements + ", " if elements else "") + sentinel
            literal = literal[:match.start(1)] + filled + literal[match.end(1):]
        return literal

    def translate_call_arguments(self, call_name, arguments_text):
        """Type the top-level `.{...}` literals positionally."""
        signature = self.extern_types.get(call_name)
        arguments = split_top_level(arguments_text)
        translated = []
        for position, argument in enumerate(arguments):
            argument = argument.strip()
            if argument.startswith(".{"):
                if not signature or position >= len(signature[1]):
                    fail("no parameter type for literal argument %d of %s" % (position, call_name))
                c_type = signature[1][position]
                literal = self.apply_sentinels(c_type, self.translate_expression(argument[1:]))
                translated.append("(%s)%s" % (c_type, literal))
            else:
                translated.append(self.translate_expression(argument))
        return ", ".join(translated)

    # -- block parsers --------------------------------------------------------

    def read_balanced(self, first_index, closing="}"):
        """Lines of a block opened on first_index whose close is a lone `}`
        (or `};` for declarations) at the same indentation as the opener."""
        indent = len(self.lines[first_index]) - len(self.lines[first_index].lstrip())
        closer = " " * indent + closing
        for index in range(first_index + 1, len(self.lines)):
            if self.lines[index].rstrip() == closer:
                return self.lines[first_index:index + 1], index
        fail("unterminated block at main.zig line %d" % (first_index + 1))

    def collect_statement(self, body, start):
        """One logical statement possibly spanning lines (until balanced
        parentheses/braces and a trailing semicolon)."""
        text = body[start]
        index = start
        while True:
            depth = text.count("(") - text.count(")") + text.count("{") - text.count("}")
            if depth == 0 and text.rstrip().endswith(";"):
                return text, index
            index += 1
            if index >= len(body):
                fail("unterminated statement: " + body[start].strip())
            text += "\n" + body[index]

    def translate_type_declaration(self, name, body_lines, keyword="struct"):
        cursor = [0]

        def parse_fields(closing):
            fields = []
            while cursor[0] < len(body_lines):
                line = body_lines[cursor[0]].strip()
                cursor[0] += 1
                if not line:
                    continue
                if line == closing:
                    return fields
                if line == "_: void = {},":
                    # A zero-size Zig field; the C layout has no counterpart.
                    continue
                inline_nested = re.fullmatch(r"(\w+): extern (struct|union) \{ (.+) \},", line)
                if inline_nested:
                    sub = []
                    for sub_field in split_top_level(inline_nested.group(3)):
                        match = re.fullmatch(r"(\w+): (\w+)", sub_field.strip())
                        if not match:
                            fail("unparsed inline nested field in %s: %s" % (name, sub_field))
                        sub.append("%s %s;" % (map_scalar(match.group(2)), match.group(1)))
                    fields.append("%s { %s } %s;" % (inline_nested.group(2), " ".join(sub), inline_nested.group(1)))
                    continue
                nested = re.fullmatch(r"(\w+): extern (struct|union) \{", line)
                if nested:
                    sub = parse_fields("},")
                    fields.append("%s { %s } %s;" % (nested.group(2), " ".join(sub), nested.group(1)))
                    continue
                match = re.fullmatch(r"(\w+): \[(\d+)(:[0-9a-fA-Fx.]+)?\](\w+),", line)
                if match:
                    field, count, sentinel, element = match.groups()
                    slots = int(count) + (1 if sentinel else 0)
                    if sentinel:
                        # Zig writes the sentinel into the extra slot on every
                        # construction; typed literals replay that below.
                        self.sentinels[(name, field)] = (sentinel[1:], int(count))
                    fields.append("%s %s[%d];" % (map_scalar(element), field, slots))
                    continue
                match = re.fullmatch(r"(\w+): (\w+) align\((\d+)\),", line)
                if match:
                    fields.append("alignas(%s) %s %s;" % (match.group(3), map_scalar(match.group(2)), match.group(1)))
                    continue
                match = re.fullmatch(r"(\w+): ([\w*?@ ()]+?)(?: = .+)?,", line)
                if match:
                    fields.append("%s %s;" % (self.map_type(match.group(2)), match.group(1)))
                    continue
                fail("unparsed field in %s: %s" % (name, line))
            if closing is not None:
                fail("unterminated field list in %s" % name)
            return fields

        fields = parse_fields(None)
        return "typedef %s { %s } %s;" % (keyword, " ".join(fields), name)

    def translate_export_function(self, header, body):
        name, parameters_text, return_type = split_function_header(header, "export fn ", " {")
        parameters = []
        blank = 0
        for parameter in split_top_level(parameters_text):
            parameter = parameter.strip()
            if not parameter:
                continue
            parameter_name, _, zig_type = parameter.partition(": ")
            c_type = self.map_type(zig_type)
            self.ensure_vector_typedef(c_type)
            if parameter_name == "_":
                parameter_name = "ignored%d" % blank
                blank += 1
            parameters.append("%s %s" % (c_type, parameter_name))
        c_return = "void" if return_type == "void" else self.map_type(return_type)
        self.ensure_vector_typedef(c_return)
        self.output.append("%s %s(%s) {" % (c_return, name, ", ".join(parameters) or "void"))
        self.translate_body(body, c_return, inside_test=None)
        self.output.append("}")

    def translate_body(self, body, return_type, inside_test):
        index = 0
        while index < len(body):
            raw = body[index]
            line = raw.strip()
            index += 1
            if not line or line.startswith("//"):
                continue
            if line.startswith("if (") and line.endswith("return error.SkipZigTest;"):
                fail("skip line reached translate_body: " + line)
            if line == "_ = &sentinel_index;":
                continue
            match = re.fullmatch(r"var sentinel_index: usize = (\d+);", line)
            if match:
                self.output.append("    volatile size_t sentinel_index = %s;" % match.group(1))
                continue
            match = re.fullmatch(r"expect\((.+)\) catch @panic\(\".*\"\);", line)
            if match:
                self.output.append("    assert_or_panic(%s);" % self.translate_expression(match.group(1)))
                continue
            match = re.fullmatch(r"try expect\((.+)\);", line)
            if match:
                self.output.append("    assert_or_panic(%s);" % self.translate_expression(match.group(1)))
                continue
            if line.startswith("return .{") or line == "return .{":
                statement, index_new = self.collect_statement(body, index - 1)
                index = index_new + 1
                literal = statement.strip()[len("return "):-1]
                literal = self.apply_sentinels(return_type, self.translate_expression(literal[1:]))
                self.output.append("    return (%s)%s;" % (return_type, reindent(literal)))
                continue
            match = re.fullmatch(r"const (\w+) = (c_\w+)\(\);", line)
            if match:
                variable, call = match.groups()
                signature = self.extern_types.get(call)
                if not signature:
                    fail("call of undeclared %s" % call)
                self.output.append("    %s %s = %s();" % (signature[0], variable, call))
                continue
            call_match = re.match(r"(c_\w+)\(", line)
            if call_match:
                statement, index_new = self.collect_statement(body, index - 1)
                index = index_new + 1
                statement = statement.strip()
                name = call_match.group(1)
                arguments_text = statement[len(name) + 1:-2]
                self.output.append("    %s(%s);" % (name, self.translate_call_arguments(name, arguments_text)))
                continue
            fail("unparsed statement: " + line)

    def translate_test(self, title, body):
        name = sanitize_test_name(title)
        if name in self.test_names:
            suffix = 2
            while "%s_%d" % (name, suffix) in self.test_names:
                suffix += 1
            name = "%s_%d" % (name, suffix)
        self.test_names.add(name)
        skip_conditions = []
        remaining = []
        for line in body:
            stripped = line.strip()
            match = re.fullmatch(r"if \((.+)\) return error\.SkipZigTest;", stripped)
            if match:
                mapped = map_skip_condition(match.group(1))
                if mapped:
                    skip_conditions.append(mapped)
                continue
            remaining.append(line)
        self.output.append("static void %s(void) {" % name)
        self.output.append("    c_abi_current_test = \"%s\";" % title.replace("\"", "\\\""))
        guard = " || ".join(dict.fromkeys(skip_conditions))
        if guard:
            self.output.append("#if !(%s)" % guard)
        self.translate_body(remaining, "void", inside_test=name)
        if guard:
            self.output.append("#endif")
        self.output.append("}")
        self.runner.append((list(self.open_gate) if self.open_gate else [], name))

    # -- top-level driver -----------------------------------------------------

    def run(self):
        while self.index < len(self.lines):
            line = self.lines[self.index]
            stripped = line.strip()
            if not stripped or stripped.startswith("//"):
                self.index += 1
                continue
            if line.startswith("const ") and (line.endswith("= extern struct {") or line.endswith("= extern union {")):
                name = line.split()[1]
                keyword = "union" if line.endswith("= extern union {") else "struct"
                body, close = self.read_balanced(self.index, "};")
                self.type_names.add(name)
                self.ensure_gate(self.gate_for_name(name.lower()))
                self.output.append(self.translate_type_declaration(name, body[1:-1], keyword))
                self.index = close + 1
                continue
            if line.startswith("export fn "):
                header_index = self.index
                header = line
                while not header.rstrip().endswith("{"):
                    self.index += 1
                    header += " " + self.lines[self.index].strip()
                body, close = self.read_balanced(header_index)
                # Re-split: body[0] may span several physical lines of header.
                body_lines = self.lines[header_index:close]
                offset = self.index - header_index + 1
                match = re.match(r"export fn (\w+)", header)
                self.ensure_gate(self.gate_for_name(match.group(1)))
                self.translate_export_function(header.rstrip(), body_lines[offset:])
                self.index = close + 1
                continue
            if line.startswith("extern fn "):
                name, parameters_text, return_type = split_function_header(stripped, "extern fn ", ";")
                parameter_types = [self.map_type(p) for p in split_top_level(parameters_text) if p.strip()]
                c_return = "void" if return_type == "void" else self.map_type(return_type)
                self.extern_types[name] = (c_return, parameter_types)
                self.ensure_gate(self.gate_for_name(name))
                for c_type in parameter_types + [c_return]:
                    self.ensure_vector_typedef(c_type)
                self.output.append("%s %s(%s);" % (c_return, name, ", ".join(parameter_types) or "void"))
                self.index += 1
                continue
            if line.startswith("test \""):
                title = line[len("test \""):line.rindex("\"")]
                body, close = self.read_balanced(self.index)
                self.ensure_gate(self.gate_for_name(sanitize_test_name(title)))
                self.translate_test(title, body[1:-1])
                self.index = close + 1
                continue
            fail("unrecognized construct at main.zig line %d: %s" % (self.index + 1, stripped))
        self.close_gate()


def split_function_header(header, prefix, suffix):
    """(name, parameters_text, return_type) from `<prefix>name(params) ret<suffix>`,
    with balanced-paren parameter extraction so @Vector(N, T) survives."""
    if not header.startswith(prefix) or not header.endswith(suffix):
        fail("unparsed function header: " + header)
    text = header[len(prefix):len(header) - len(suffix)]
    open_index = text.index("(")
    name = text[:open_index]
    depth = 0
    for index in range(open_index, len(text)):
        if text[index] == "(":
            depth += 1
        elif text[index] == ")":
            depth -= 1
            if depth == 0:
                return name, text[open_index + 1:index], text[index + 1:].strip()
    fail("unbalanced parameters in: " + header)


def split_top_level(text):
    """Split on commas not nested in (), {}, or []."""
    parts = []
    depth = 0
    current = []
    for character in text:
        if character in "({[":
            depth += 1
        elif character in ")}]":
            depth -= 1
        if character == "," and depth == 0:
            parts.append("".join(current))
            current = []
        else:
            current.append(character)
    if current and "".join(current).strip():
        parts.append("".join(current))
    return parts


def reindent(literal):
    lines = literal.split("\n")
    if len(lines) == 1:
        return literal
    result = [lines[0]]
    for line in lines[1:]:
        result.append("    " + line.strip())
    return "\n".join(result)


def translate_main(upstream_lines):
    start = None
    end = None
    for index, line in enumerate(upstream_lines):
        if start is None and line.startswith("export fn zig_ret_vector_1_u8()"):
            start = index
        if line.startswith("const Struct_i32_i32 = extern struct {"):
            end = index
            break
    if start is None or end is None:
        fail("main.zig translation range anchors not found")
    translator = Translator(upstream_lines[start:end])
    translator.run()

    header = [
        "// The generated middle of the Zig side of the test/c_abi suite: the",
        "// @Vector matrix and the struct-shape families, translated from",
        "// ziglang/zig test/c_abi/main.zig by tools/port_zig_c_abi.py. Edit the",
        "// generator, not this file. The upstream zig_ symbol names are kept so",
        "// the pair links against c_abi_cfuncs.c and stays diffable upstream.",
        "// See tests/c_abi.h for the gate contract.",
        "#include \"c_abi.h\"",
        "",
        "static void assert_or_panic(bool ok) {",
        "    if (!ok) {",
        "        zig_panic();",
        "    }",
        "}",
        "",
    ]
    body = header + translator.output + ["", "void c_abi_run_generated_tests(void) {"]
    open_gate = None
    for conditions, name in translator.runner:
        if conditions != open_gate:
            if open_gate:
                body.append("#endif")
            if conditions:
                body.append(gate_open(conditions))
            open_gate = conditions or None
        body.append("    %s();" % name)
    if open_gate:
        body.append("#endif")
    body.append("}")
    return body


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--zig", default=os.path.expanduser("~/dev/zig"), help="zig checkout root")
    parser.add_argument("--out", default="tests", help="output directory")
    arguments = parser.parse_args()

    def read(path):
        with open(path, "r", encoding="utf-8") as handle:
            return handle.read().split("\n")

    source = os.path.join(arguments.zig, "test", "c_abi")
    cfuncs = adapt_cfuncs(read(os.path.join(source, "cfuncs.c")))
    generated = translate_main(read(os.path.join(source, "main.zig")))

    def write(name, lines):
        path = os.path.join(arguments.out, name)
        with open(path, "w", encoding="utf-8") as handle:
            handle.write("\n".join(lines).rstrip("\n") + "\n")
        print("wrote %s (%d lines)" % (path, len(lines)))

    write("c_abi_cfuncs.c", cfuncs)
    write("c_abi_main_generated.c", generated)


if __name__ == "__main__":
    main()
