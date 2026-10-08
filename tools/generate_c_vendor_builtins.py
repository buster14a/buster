#!/usr/bin/env python3
"""Maintain finite LLVM x86 builtin type admission. Normal builds do not run this tool."""

import argparse
import hashlib
import json
import re
from pathlib import Path


SCALAR_KINDS = {
    "void": "C_TYPE_VOID", "char": "C_TYPE_CHAR",
    "signed char": "C_TYPE_SIGNED_CHAR", "unsigned char": "C_TYPE_UNSIGNED_CHAR",
    "short": "C_TYPE_SHORT", "signed short": "C_TYPE_SHORT",
    "unsigned short": "C_TYPE_UNSIGNED_SHORT", "int": "C_TYPE_INT",
    "signed int": "C_TYPE_INT", "unsigned int": "C_TYPE_UNSIGNED_INT",
    "long int": "C_TYPE_LONG", "unsigned long int": "C_TYPE_UNSIGNED_LONG",
    "long long int": "C_TYPE_LONG_LONG", "signed long long int": "C_TYPE_LONG_LONG",
    "unsigned long long int": "C_TYPE_UNSIGNED_LONG_LONG", "float": "C_TYPE_FLOAT",
    "double": "C_TYPE_DOUBLE", "_Float16": "C_TYPE_FLOAT16", "__bf16": "C_TYPE_BFLOAT16",
    "size_t": "C_TYPE_UNSIGNED_LONG_LONG", "int64_t": "C_TYPE_LONG_LONG",
    "uint64_t": "C_TYPE_UNSIGNED_LONG_LONG", "msuint32_t": "C_TYPE_UNSIGNED_INT",
}
DATA_MODELS = {
    "size_t": "C_VENDOR_DATA_MODEL_SIZE", "int64_t": "C_VENDOR_DATA_MODEL_INT64",
    "uint64_t": "C_VENDOR_DATA_MODEL_UINT64", "msuint32_t": "C_VENDOR_DATA_MODEL_MS_UINT32",
}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def git_blob(content):
    result = hashlib.sha1(b"blob " + str(len(content)).encode("ascii") + b"\0" + content).hexdigest()
    return result


def load_metadata(directory, provenance):
    content = (directory / "c_vendor_builtin_metadata.json").read_bytes()
    require(git_blob(content) == provenance["metadata_git_blob"], "metadata hash differs from provenance")
    data = json.loads(content)
    require(set(data) == {"schema", "types", "signatures", "builtins", "generic"} and data["schema"] == 1,
            "unknown metadata schema")
    require([len(data[key]) for key in ("types", "signatures", "builtins", "generic")] == [114, 868, 2094, 18],
            "unexpected finite closure")
    for kind, lanes, pointer, const, volatile, model in data["types"]:
        require(kind in SCALAR_KINDS.values(), "unknown scalar kind")
        require(type(lanes) is int and 0 <= lanes <= 256 and pointer in (0, 1), "invalid vector/pointer shape")
        require(type(const) is bool and type(volatile) is bool, "invalid qualifier")
        require(model == "C_VENDOR_DATA_MODEL_NONE" or model in DATA_MODELS.values(), "unknown data model")
    for types, count, mask in data["signatures"]:
        require(type(count) is int and 0 <= count <= 7 and len(types) == count + 1, "invalid signature arity")
        require(all(type(index) is int and 0 <= index < 114 for index in types), "invalid type index")
        require(type(mask) is int and 0 <= mask < (1 << count), "invalid constant-argument mask")
    names = []
    for suffix, signature in data["builtins"]:
        require(re.fullmatch(r"[A-Za-z0-9_]+", suffix) is not None, "invalid exact name")
        require(type(signature) is int and 0 <= signature < 868, "invalid signature index")
        names.append(suffix if suffix == "__rdtsc" else "__builtin_ia32_" + suffix)
    require(names == sorted(set(names)) and names.count("__rdtsc") == 1, "names are not unique and sorted")
    generic_names = []
    header = (directory / "c_vendor_builtin.h.in").read_text()
    for row in data["generic"]:
        require(len(row) == 9, "invalid custom metadata row")
        name, operation, category, result, minimum, maximum, type_arguments, vector, same = row
        require(re.fullmatch(r"__builtin_[a-z0-9_]+", name) is not None, "invalid custom name")
        for value in (operation, category, result):
            require(re.fullmatch(r"C_VENDOR_GENERIC_[A-Z_]+", value) is not None and value in header,
                    "unknown custom contract")
        require(type(minimum) is int and type(maximum) is int and 1 <= minimum <= maximum <= 255,
                "invalid custom arity")
        require(type(type_arguments) is int and 0 <= type_arguments < (1 << minimum), "invalid type-argument mask")
        require(type(vector) is bool and type(same) is bool, "invalid custom constraints")
        generic_names.append(name)
    require(generic_names == sorted(set(generic_names)), "custom names are not unique and sorted")
    return data


def split_parameters(text):
    result = []
    start = 0
    depth = 0
    for index, character in enumerate(text):
        if character == "<":
            depth += 1
        elif character == ">":
            depth -= 1
        elif character == "," and depth == 0:
            result.append(text[start:index].strip())
            start = index + 1
        require(depth in (0, 1), "unsupported nested signature grammar")
    if text:
        result.append(text[start:].strip())
    require(depth == 0, "unclosed vector signature")
    return result


def parse_type(text):
    constant = "_Constant" in text
    text = " ".join(text.replace("_Constant", "").split())
    lanes = 0
    if text.startswith("_Vector"):
        match = re.fullmatch(r"_Vector<([0-9]+),\s*(.+)>", text)
        require(match is not None, "unsupported vector type: " + text)
        lanes = int(match[1])
        text = match[2]
    pointer = text.count("*")
    require(pointer in (0, 1) and 0 <= lanes <= 256, "unsupported signature shape")
    words = text.replace("*", " ").split()
    scalar = " ".join(word for word in words if word not in ("const", "volatile"))
    require(scalar in SCALAR_KINDS, "unsupported scalar type: " + scalar)
    # LLVM's V<N>T* modifiers qualify the vector object, not its lanes.
    shape = [SCALAR_KINDS[scalar], lanes, pointer, "const" in words, "volatile" in words,
             DATA_MODELS.get(scalar, "C_VENDOR_DATA_MODEL_NONE")]
    result = (shape, constant)
    return result


def verify_upstream(data, paths):
    expected = {}
    for suffix, index in data["builtins"]:
        name = suffix if suffix == "__rdtsc" else "__builtin_ia32_" + suffix
        types, count, mask = data["signatures"][index]
        expected[name] = ([data["types"][index] for index in types], count, mask)
    observed = set()
    for path in paths:
        table = json.loads(path.read_bytes())
        for record_name in table["!instanceof"]["TargetBuiltin"]:
            record = table[record_name]
            prefix = "__builtin_ia32_" if record.get("RequiredNamePrefix") else ""
            for spelling in record["Spellings"]:
                name = prefix + spelling
                if name in expected:
                    require(name not in observed, "duplicate upstream name: " + name)
                    observed.add(name)
                    result_text, parameters = record["Prototype"].split("(", 1)
                    require(parameters.endswith(")"), "invalid upstream prototype")
                    arguments = split_parameters(parameters[:-1])
                    types = []
                    mask = 0
                    for index, text in enumerate([result_text] + arguments):
                        shape, constant = parse_type(text)
                        require(index != 0 or not constant, "constant return qualifier")
                        types.append(shape)
                        if index and constant:
                            mask |= 1 << (index - 1)
                    require((types, len(arguments), mask) == expected[name], "upstream contract differs: " + name)
    require(observed == set(expected), "upstream JSON is missing admitted records")


def verify_sources(provenance, directory):
    for entry in provenance["sources"] + provenance["headers"]:
        require(git_blob((directory / entry["path"]).read_bytes()) == entry["git_blob"],
                "upstream source hash differs: " + entry["path"])


def generate(data, directory):
    types = []
    for kind, lanes, pointer, const, volatile, model in data["types"]:
        values = f"{kind}, {lanes}, {pointer}, {str(const).lower()}, {str(volatile).lower()}"
        types.append("    {{" + values + "}, " + model + "},\n")
    signatures = []
    for types_ids, count, mask in data["signatures"]:
        signatures.append("    {{" + ", ".join(map(str, types_ids)) + f"}}, {count}, {mask}" + "},\n")
    names = []
    rdtsc = None
    for suffix, signature in data["builtins"]:
        if suffix == "__rdtsc":
            rdtsc = signature
        else:
            names.append(f'    {{S8_INITIALIZER("{suffix}"), {signature}}},\n')
    generic = []
    for name, operation, category, result, minimum, maximum, type_arguments, vector, same in data["generic"]:
        upper = "UINT8_MAX" if maximum == 255 else str(maximum)
        generic.append(f'    {{S8_INITIALIZER("{name}"),\n')
        fields = f"{operation}, {category}, {result}, {minimum}, {upper}, {type_arguments}, {str(vector).lower()}, {str(same).lower()}"
        generic.append("     {" + fields + "}},\n")
    replacements = {
        "@C_TYPE_DEFINITIONS@": "".join(types), "@C_SIGNATURE_DEFINITIONS@": "".join(signatures),
        "@C_BUILTIN_DEFINITIONS@": "".join(names), "@C_GENERIC_DEFINITIONS@": "".join(generic),
        "@RDTSC_SIGNATURE@": str(rdtsc),
    }
    source = (directory / "c_vendor_builtin.c.in").read_text()
    for marker, value in replacements.items():
        require(source.count(marker) == 1, "template marker must occur once: " + marker)
        source = source.replace(marker, value.rstrip("\n"))
    result = {"c_vendor_builtin.c": source.encode("utf-8"),
              "c_vendor_builtin.h": (directory / "c_vendor_builtin.h.in").read_bytes()}
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="compare outputs without writing")
    parser.add_argument("--describe", metavar="NAME", help="print one exact contract without writing")
    parser.add_argument("--output-dir", type=Path, help="override generated source destination")
    parser.add_argument("--verify-upstream-json", action="append", default=[], type=Path, metavar="JSON",
                        help="repeat for LLVM llvm-tblgen --dump-json X86 and X86_64 tables")
    parser.add_argument("--verify-llvm-source", type=Path, metavar="DIRECTORY", help="verify pinned LLVM Git blobs")
    arguments = parser.parse_args()
    directory = Path(__file__).resolve().parent
    provenance = json.loads((directory / "c_vendor_builtin_provenance.json").read_bytes())
    require(provenance["commit"] == "2078da43e25a4623cab2d0d60decddf709aaea28", "unexpected source pin")
    require(git_blob((directory / "licenses/llvm-project.txt").read_bytes()) == provenance["license_git_blob"],
            "upstream license notice differs")
    data = load_metadata(directory, provenance)
    if arguments.verify_upstream_json:
        verify_upstream(data, arguments.verify_upstream_json)
    if arguments.verify_llvm_source:
        verify_sources(provenance, arguments.verify_llvm_source)
    if arguments.describe:
        description = None
        for suffix, index in data["builtins"]:
            name = suffix if suffix == "__rdtsc" else "__builtin_ia32_" + suffix
            if name == arguments.describe:
                type_ids, count, mask = data["signatures"][index]
                description = {"name": name, "parameter_count": count, "constant_arguments": mask,
                               "types": [data["types"][index] for index in type_ids]}
        for row in data["generic"]:
            if row[0] == arguments.describe:
                description = {"custom_contract": row}
        require(description is not None, "unknown exact name: " + arguments.describe)
        print(json.dumps(description, indent=2))
    else:
        destination = arguments.output_dir or directory.parent / "src/buster/lib/compiler/frontend/c"
        for name, content in generate(data, directory).items():
            path = destination / name
            if arguments.check:
                require(path.read_bytes() == content, "generated output differs: " + str(path))
            else:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(content)
        print("vendor metadata: 2094 exact names, 868 signatures, 114 shapes, 18 custom contracts")
    result = 0
    return result


if __name__ == "__main__":
    raise SystemExit(main())
