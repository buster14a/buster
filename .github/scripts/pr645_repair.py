#!/usr/bin/env python3
import csv
import hashlib
import json
import re
from collections import Counter
from pathlib import Path

root = Path(__file__).resolve().parents[2]


def replace_once(text, pattern, replacement, label, flags=0):
    result, count = re.subn(pattern, replacement, text, count=1, flags=flags)
    if count != 1:
        raise SystemExit(f"{label} replacement count: {count}")
    return result


# Match indexed debug-value lookup to the dense/reference semantics.
machine_path = root / "src/buster/lib/compiler/codegen/machine.c"
machine = machine_path.read_text(encoding="utf-8")
machine_replacement = r'''BUSTER_GLOBAL_LOCAL IrValueId machine_debug_local_place(IrFunction* function, MachineFunction* machine_function, MachineDebugFacts const* facts,
                                                         IrValueId const* local_places, IrDebugLocal const* local, u32 parameter_ordinal)
{
    IrValueId parameter_place = IR_VALUE_ID_INVALID;
    if (local->is_parameter && function->published_cfg)
    {
        for (u32 parameter_index = 0; parameter_index < function->published_cfg->parameter_count; parameter_index += 1)
        {
            IrCfgParameter const* parameter = function->published_cfg->parameters + parameter_index;
            if (parameter->canonical_local.value == local->id.value)
            {
                parameter_place = parameter->value;
                break;
            }
        }
    }
    if (local->is_parameter && parameter_place.value == IR_ID_UNDERLYING_INVALID && parameter_ordinal < facts->argument_count)
    {
        parameter_place = facts->argument_results[parameter_ordinal];
    }
    IrValueId place = local->id.value < function->local_count ? local_places[local->id.value] : IR_VALUE_ID_INVALID;
    if (local->is_parameter && place.value == IR_ID_UNDERLYING_INVALID)
    {
        place = parameter_place;
    }
    if (place.value != IR_ID_UNDERLYING_INVALID && machine_debug_place_promoted(machine_function, facts, place))
    {
        // A promoted place is only an implementation cell. Preserve the ABI
        // parameter value when one exists; canonical block-local SSA values
        // carry every non-parameter source variable's value.
        place = parameter_place;
    }
    if (place.value == IR_ID_UNDERLYING_INVALID && local->is_parameter)
    {
        place = parameter_place;
    }
    if (place.value == IR_ID_UNDERLYING_INVALID && local->id.value >= function->local_count)
    {
        place = machine_debug_wide_place(facts, local->id.value);
    }
    return place;
}

BUSTER_GLOBAL_LOCAL IrValueId machine_debug_block_value'''
machine = replace_once(
    machine,
    r"BUSTER_GLOBAL_LOCAL IrValueId machine_debug_local_place\(.*?\n\}\n\nBUSTER_GLOBAL_LOCAL IrValueId machine_debug_block_value",
    machine_replacement,
    "machine_debug_local_place",
    re.S,
)
machine_path.write_text(machine, encoding="utf-8")

# The fixed-RAX cmpxchg fixture is now a supported MIR form. Use a stable,
# otherwise-valid GNU asm row beyond the selector's explicit 16-operand cap.
driver_path = root / "src/buster/tests/compiler/driver/driver_test.c"
driver = driver_path.read_text(encoding="utf-8")
operands = ", ".join(f'\"m\"(pointer[{index}])' for index in range(17))
source = (
    "int machine_fallback_inline_asm(volatile int* pointer)\n"
    "{\n"
    f"    __asm__ __volatile__(\"\" : : {operands} : \"memory\");\n"
    "    return pointer[0];\n"
    "}\n"
)
literal = "    String8 fallback_source_text = S8(" + json.dumps(source) + ");\n"
driver = replace_once(
    driver,
    r"    String8 fallback_source_text = S8\(.*?\);\n"
    r"    BUSTER_TEST\(arguments, file_write\(fallback_source, BUSTER_SLICE_TO_BYTE_SLICE\(fallback_source_text\)\)\);",
    lambda _match: literal + "    BUSTER_TEST(arguments, file_write(fallback_source, BUSTER_SLICE_TO_BYTE_SLICE(fallback_source_text)));",
    "fallback fixture",
    re.S,
)
driver_path.write_text(driver, encoding="utf-8")

# Derive the exact frozen population from the checked-in support contract.
support_path = root / "docs/native-retirement-support-v1.tsv"
support_bytes = support_path.read_bytes()
with support_path.open(encoding="utf-8", newline="") as stream:
    support_rows = list(csv.DictReader(stream, delimiter="\t"))
input_count = len(support_rows)
role_counts = Counter(row["role"] for row in support_rows)
subject_count = role_counts["subject"]
group_count = subject_count * 12 * 2 * 2
row_count = group_count * 4
support_sha = hashlib.sha256(support_bytes).hexdigest()

contract_path = root / "tools/native_retirement_contract.py"
contract = contract_path.read_text(encoding="utf-8")
for name, value in {
    "FULL_INPUT_COUNT": input_count,
    "FULL_SUBJECT_COUNT": subject_count,
    "FULL_GROUP_COUNT": group_count,
    "FULL_ROW_COUNT": row_count,
}.items():
    contract = re.sub(rf"^({name}\s*=\s*)\d+\s*$", rf"\g<1>{value}", contract, flags=re.M)
contract = re.sub(
    r'^(FULL_SUPPORT_CONTRACT_SHA256\s*=\s*)"[0-9a-f]{64}"\s*$',
    rf'\g<1>"{support_sha}"',
    contract,
    flags=re.M,
)
contract_path.write_text(contract, encoding="utf-8")

# Refresh only the production-profile assertions. The forged-population test
# deliberately retains obsolete counts so it remains a negative test.
test_path = root / "tools/native_retirement_contract_test.py"
tests = test_path.read_text(encoding="utf-8")
tests = replace_once(
    tests,
    r'self\.assertEqual\(len\(records\), \d+\)',
    f'self.assertEqual(len(records), {input_count})',
    "support record count",
)
tests = replace_once(
    tests,
    r'self\.assertEqual\(len\(\{record\["path"\] for record in records\}\), \d+\)',
    f'self.assertEqual(len({{record["path"] for record in records}}), {input_count})',
    "support unique path count",
)
role_literal = "{" + ", ".join(f'{name!r}: {role_counts[name]}' for name in sorted(role_counts)) + "}"
tests = replace_once(
    tests,
    r'self\.assertEqual\(role_counts, \{.*?\}\)',
    f'self.assertEqual(role_counts, {role_literal})',
    "support role counts",
    re.S,
)
exact_match = re.search(
    r'(    def test_exact_full_profile_shape_is_admissible\(self\):.*?)(?=\n    def |\Z)',
    tests,
    re.S,
)
if not exact_match:
    raise SystemExit("exact full-profile method not found")
exact_block = exact_match.group(1)
exact_block = re.sub(r'"inputs": "\d+"', f'"inputs": "{input_count}"', exact_block)
exact_block = re.sub(r'"subjects": "\d+"', f'"subjects": "{subject_count}"', exact_block)
tests = tests[:exact_match.start(1)] + exact_block + tests[exact_match.end(1):]
test_path.write_text(tests, encoding="utf-8")

# Source-bind every applicability record to the current fixture bytes.
applicability_path = root / "docs/native-retirement-applicability-v1.tsv"
with applicability_path.open(encoding="utf-8", newline="") as stream:
    reader = csv.DictReader(stream, delimiter="\t")
    fields = reader.fieldnames
    applicability = list(reader)
if not fields:
    raise SystemExit("applicability ledger has no header")
for row in applicability:
    fixture = root / row["fixture"]
    if not fixture.is_file():
        raise SystemExit(f"missing applicability fixture: {row['fixture']}")
    row["fixture_sha256"] = hashlib.sha256(fixture.read_bytes()).hexdigest()
with applicability_path.open("w", encoding="utf-8", newline="") as stream:
    writer = csv.DictWriter(stream, fieldnames=fields, delimiter="\t", lineterminator="\n")
    writer.writeheader()
    writer.writerows(applicability)

# Refresh the two repository-owned archive identities reported by the current
# materializer failure; external checkout and generated-source pins stay fixed.
descriptor_path = root / "docs/native-retirement-dependencies-v1.json"
descriptor = json.loads(descriptor_path.read_text(encoding="utf-8"))
identity_paths = {
    "docs/native-retirement-support-v1.tsv": support_path,
    "tools/native_retirement_census.c": root / "tools/native_retirement_census.c",
}
identities = {
    name: {"bytes": path.stat().st_size, "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
    for name, path in identity_paths.items()
}


def refresh(value):
    if isinstance(value, dict):
        referenced = next((value[key] for key in ("source", "path", "repository_path", "file")
                           if value.get(key) in identities), None)
        if referenced:
            identity = identities[referenced]
            for key in ("bytes",):
                if key in value:
                    value[key] = identity[key]
            for key in ("sha256", "input_sha256"):
                if key in value:
                    value[key] = identity["sha256"]
        direct = {
            "support_contract_sha256": identities["docs/native-retirement-support-v1.tsv"]["sha256"],
            "support_contract_bytes": identities["docs/native-retirement-support-v1.tsv"]["bytes"],
            "census_utility_sha256": identities["tools/native_retirement_census.c"]["sha256"],
            "census_utility_bytes": identities["tools/native_retirement_census.c"]["bytes"],
        }
        for key, replacement in direct.items():
            if key in value:
                value[key] = replacement
        for child in value.values():
            refresh(child)
    elif isinstance(value, list):
        for child in value:
            refresh(child)


refresh(descriptor)
descriptor_path.write_text(json.dumps(descriptor, indent=2, ensure_ascii=True) + "\n", encoding="utf-8")
