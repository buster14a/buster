#!/usr/bin/env python3
from pathlib import Path
import sys

root = Path(sys.argv[1])


def replace_once(text, old, new, name):
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{name} changed unexpectedly: found {count}")
    return text.replace(old, new)


guide = root / "docs/agents/benchmarking.md"
text = guide.read_text()
text = replace_once(
    text,
    """  `python3 tools/native_retirement_performance_binding.py <record.json>
  --evidence-root <bundle> --repository-root <immutable-checkout>`; the
""",
    """  `python3 tools/native_retirement_performance_binding.py <record.json>
  --evidence-root <bundle> --repository-root <immutable-checkout>
  --trusted-execution-receipt-sha256 <independently-obtained-sha256>`; the
""",
    "primary benchmarking command",
)
text = replace_once(
    text,
    """  relations. This validation does not itself accept a performance result.
""",
    """  relations. The trusted execution-receipt digest must come out of band
  from the authenticated admitted control service for the exact job and attempt;
  never derive it from the result bundle being validated. This validation does
  not itself accept a performance result.
""",
    "primary benchmarking trust boundary",
)
guide.write_text(text)

tests = root / "tools/native_retirement_performance_binding_test.py"
text = tests.read_text()
start = text.index("    def _build_small_evidence_fixture")
end = text.index("    def test_bounded_validate_evidence_path_replays_sealed_workflow", start)
fixture = text[start:end]

replacements = [
    (
        """        The production support declaration remains the 77,184-object-row
        census; the support-output function is patched only in this test so
        the workflow/seal/replay composition can execute on one canonical
        performance row and 120 streamed #615 records.  The independent
""",
        """        The production support declaration remains the 77,184-object-row
        census; the support-output function is patched only in this test so
        the workflow/seal/replay composition can execute on one object row,
        one native link row, and 240 streamed #615 records.  The independent
""",
        "bounded fixture description",
    ),
    (
        """        row = copy.deepcopy(rows_record[\"rows\"][0])
        row[\"row\"] = 0
        row[\"eligibility\"] = {
            \"compiler_wall_time\": True, \"compiler_peak_rss\": True,
            \"generated_code_bytes\": True, \"generated_runtime\": True,
            \"runtime_oracle\": \"independent-native-executable-oracle\",
            \"code_section\": \"deterministic-code-section\",
        }
        rows_record[\"rows\"] = [row]
""",
        """        object_row = copy.deepcopy(rows_record[\"rows\"][0])
        object_row[\"row\"] = 0
        object_row[\"eligibility\"] = {
            \"compiler_wall_time\": True, \"compiler_peak_rss\": True,
            \"generated_code_bytes\": True, \"generated_runtime\": False,
            \"runtime_oracle\": \"not-applicable\",
            \"code_section\": \"deterministic-code-section\",
        }
        runtime_row = copy.deepcopy(object_row)
        runtime_row[\"row\"] = 1
        runtime_row[\"identity\"][\"artifact_stage\"] = \"link\"
        runtime_row[\"eligibility\"].update({
            \"generated_runtime\": True,
            \"runtime_oracle\": \"independent-native-executable-oracle\",
        })
        rows_record[\"rows\"] = [object_row, runtime_row]
""",
        "bounded fixture rows",
    ),
    (
        """        admission[\"records\"] = [copy.deepcopy(admission[\"records\"][0])]
        admission[\"records\"][0].update({\"row\": 0, \"census_row\": 0,
                                         \"identity\": parsed[0][\"identity\"],
                                         \"artifact_stage\": \"object\"})
""",
        """        admission_template = copy.deepcopy(admission[\"records\"][0])
        admission[\"records\"] = []
        for performance_row in parsed:
            item = copy.deepcopy(admission_template)
            stage = performance_row[\"identity\"][\"artifact_stage\"]
            item.update({\"row\": performance_row[\"row\"], \"census_row\": 0,
                         \"identity\": performance_row[\"identity\"],
                         \"artifact_stage\": stage,
                         \"artifact_kind\": (\"object\" if stage == \"object\"
                                           else \"linked-executable\")})
            admission[\"records\"].append(item)
""",
        "bounded fixture admission",
    ),
    (
        """        oracle[\"records\"] = [copy.deepcopy(oracle[\"records\"][0])]
        oracle[\"records\"][0].update({
            \"row\": 0, \"runtime_oracle_status\": \"passed-native\",
            \"runtime_exit_code\": 0, \"native_runtime\": True,
        })
""",
        """        oracle_template = copy.deepcopy(oracle[\"records\"][0])
        oracle[\"records\"] = []
        for performance_row in parsed:
            runtime = performance_row[\"metrics\"][\"generated_runtime\"]
            item = copy.deepcopy(oracle_template)
            item.update({
                \"row\": performance_row[\"row\"],
                \"runtime_oracle_status\": (\"passed-native\" if runtime
                                            else \"not-applicable\"),
                \"runtime_exit_code\": 0 if runtime else -1,
                \"native_runtime\": runtime,
            })
            oracle[\"records\"].append(item)
""",
        "bounded fixture oracle",
    ),
    (
        """        # Stream one complete canonical row over both rounds and all 60 pairs.
        measurement_lines = []
        for round_number in range(2):
            for pair in range(60):
                value = {
                    \"record_id\": f\"row-0/round-{round_number}/pair-{pair}\",
                    \"row\": 0, \"round\": round_number, \"pair\": pair,
                    \"measurements\": {
                        metric: {\"baseline\": (1 if metric in (\"generated_code_bytes\", \"compiler_peak_rss\") else 1.0),
                                 \"candidate\": (1 if metric in (\"generated_code_bytes\", \"compiler_peak_rss\") else 1.0)}
                        for metric in binding.METRICS
                    },
                }
                measurement_lines.append(json_data(value))
""",
        """        # Stream both canonical rows over both rounds and all 60 pairs.
        measurement_lines = []
        for performance_row in parsed:
            for round_number in range(2):
                for pair in range(60):
                    value = {
                        \"record_id\": (f\"row-{performance_row['row']}/\"
                                      f\"round-{round_number}/pair-{pair}\"),
                        \"row\": performance_row[\"row\"], \"round\": round_number,
                        \"pair\": pair,
                        \"measurements\": {
                            metric: {
                                \"baseline\": (1 if metric in (\"generated_code_bytes\",
                                                              \"compiler_peak_rss\") else 1.0),
                                \"candidate\": (1 if metric in (\"generated_code_bytes\",
                                                               \"compiler_peak_rss\") else 1.0),
                            }
                            for metric in binding.METRICS
                            if performance_row[\"metrics\"][metric]
                        },
                    }
                    measurement_lines.append(json_data(value))
""",
        "bounded fixture measurements",
    ),
    (
        """        for member in family[\"members\"]:
            metric_name = member.split(\"/\", 1)[0]
            is_cell = \"/cell/\" in member
            index = cell_index[member] if is_cell else bootstrap_index[member]
            limit = binding.CELL_THRESHOLDS[metric_name] if is_cell \\
                else binding.AGGREGATE_THRESHOLDS[metric_name]
            series_lines.append(
                f\"member={member} metric={binding.STATISTICAL_METRICS.index(metric_name)} \"
                f\"kind={1 if is_cell else 0} family={index} cells=1 pairs=60 \"
                f\"resamples={0 if is_cell else 100000} limit={limit}\\n\")
            series_lines.extend(\"ratio=1.0\\n\" for _ in range(120))
            series_lines.append(\"end\\n\")
""",
        """        for member in family[\"members\"]:
            metric_name = member.split(\"/\", 1)[0]
            is_cell = \"/cell/\" in member
            index = cell_index[member] if is_cell else bootstrap_index[member]
            limit = binding.CELL_THRESHOLDS[metric_name] if is_cell \\
                else binding.AGGREGATE_THRESHOLDS[metric_name]
            if is_cell:
                selected_rows = [int(member.rsplit(\"=\", 1)[1])]
            elif member.endswith(\"/aggregate\"):
                selected_rows = [item[\"row\"] for item in parsed
                                 if item[\"metrics\"][metric_name]]
            else:
                dimension, selected = member.split(\"/slice/\", 1)[1].split(\"=\", 1)
                selected_rows = [item[\"row\"] for item in parsed
                                 if item[\"metrics\"][metric_name]
                                 and str(item[\"identity\"][dimension]) == selected]
            series_lines.append(
                f\"member={member} metric={binding.STATISTICAL_METRICS.index(metric_name)} \"
                f\"kind={1 if is_cell else 0} family={index} \"
                f\"cells={len(selected_rows)} pairs=60 \"
                f\"resamples={0 if is_cell else 100000} limit={limit}\\n\")
            series_lines.extend(\"ratio=1.0\\n\" for _ in range(120 * len(selected_rows)))
            series_lines.append(\"end\\n\")
""",
        "bounded fixture statistics series",
    ),
    (
        """            for round_number in range(2):
                for pair in range(60):
                    connection.execute(
                        \"INSERT INTO samples VALUES (0, ?, ?, 'generated_code_bytes', '1', '1')\",
                        (round_number, pair))
""",
        """            for performance_row in parsed:
                for round_number in range(2):
                    for pair in range(60):
                        connection.execute(
                            \"INSERT INTO samples VALUES (?, ?, ?, 'generated_code_bytes', '1', '1')\",
                            (performance_row[\"row\"], round_number, pair))
""",
        "bounded fixture code samples",
    ),
]

for old, new, name in replacements:
    fixture = replace_once(fixture, old, new, name)

for old, new, expected in (
    ('\"required_row_count\": 1', '\"required_row_count\": 2', 2),
    ('\"object_row_count\": 1, \"sample_row_count\": 1',
     '\"object_row_count\": 1, \"sample_row_count\": 2', 1),
    ('\"required_records\": 120', '\"required_records\": 240', 1),
    ('\"records\": 120', '\"records\": 240', 2),
):
    count = fixture.count(old)
    if count != expected:
        raise SystemExit(f"bounded fixture count {old!r} changed: found {count}")
    fixture = fixture.replace(old, new)

text = text[:start] + fixture + text[end:]
tests.write_text(text)
