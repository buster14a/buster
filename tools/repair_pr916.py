#!/usr/bin/env python3
from pathlib import Path
import sys

root = Path(sys.argv[1]).resolve()
workflow_path = root / ".github/workflows/ci.yml"
workflow = workflow_path.read_text(encoding="utf-8")

old_differential = (
    '          "$BUSTER_CI_PYTHON" tools/ci_native_observation.py run             '
    '--root "$BUSTER_CI_OBSERVATION_ROOT" --phase differential_corpus --sequence 80 --             '
    '"$driver" test_differential --ide build/Release/ide               '
    '--out "$RUNNER_TEMP/buster-ci/differential" --sanitize-oracle --jobs 4\n'
)
new_differential = (
    '          "$BUSTER_CI_PYTHON" tools/ci_native_observation.py run '
    '--root "$BUSTER_CI_OBSERVATION_ROOT" --phase differential_corpus --sequence 80 -- '
    '"$driver" test_differential --ide build/Release/ide '
    '--out "$RUNNER_TEMP/buster-ci/differential" --sanitize-oracle --jobs 4\n'
)
if workflow.count(old_differential) != 1:
    raise SystemExit("unexpected differential command population")
workflow = workflow.replace(old_differential, new_differential, 1)

start = workflow.index("      - name: Pack native logs\n")
end = workflow.index("      - name: Retain native logs\n", start)
expr_open = "$" + "{{"
expr_close = "}}"
pack = """      - name: Pack native logs
        id: pack
        if: EXPR_OPEN !cancelled() && steps.checkout.outcome == 'success' EXPR_CLOSE
        shell: bash
        env:
          BUSTER_CI_NATIVE_COMPLETE: EXPR_OPEN matrix.platform == 'windows' && steps.modes_windows.outcome == 'success' && '1' || matrix.platform == 'unix' && steps.modes.outcome == 'success' && steps.differential.outcome == 'success' && '1' || '0' EXPR_CLOSE
        run: if [[ -n "${BUSTER_CI_OBSERVATION_ROOT:-}" ]]; then python_command="${BUSTER_CI_PYTHON:-python3}"; if ! command -v "$python_command" >/dev/null 2>&1; then python_command=python3; fi; mkdir -p "$RUNNER_TEMP/native-ci-upload"; pack_status=0; "$python_command" tools/ci_native_observation.py run --root "$BUSTER_CI_OBSERVATION_ROOT" --phase evidence_packing --sequence 85 -- "$python_command" tools/ci_pack_evidence.py --source "$RUNNER_TEMP/buster-ci" --output "$RUNNER_TEMP/native-ci-upload" || pack_status=$?; required=(calibration_cpu calibration_filesystem bootstrap_driver configuration producer_build mode_payload evidence_packing upload_handoff); if [[ 'EXPR_OPEN matrix.platform EXPR_CLOSE' == unix ]]; then required+=(differential_driver_refresh differential_preparation differential_producer_check differential_corpus); fi; expect_complete="$BUSTER_CI_NATIVE_COMPLETE"; if (( pack_status != 0 )); then expect_complete=0; fi; finalize=("$python_command" tools/ci_native_observation.py finalize --root "$BUSTER_CI_OBSERVATION_ROOT" --artifact-directory "$RUNNER_TEMP/native-ci-upload" --expect-complete "$expect_complete"); for phase in "${required[@]}"; do finalize+=(--required-phase "$phase"); done; finalize_status=0; "${finalize[@]}" || finalize_status=$?; if (( pack_status != 0 )); then exit "$pack_status"; fi; exit "$finalize_status"; elif command -v "${BUSTER_CI_PYTHON:-}" >/dev/null 2>&1; then "$BUSTER_CI_PYTHON" tools/ci_pack_evidence.py --source "$RUNNER_TEMP/buster-ci" --output "$RUNNER_TEMP/native-ci-upload"; else python3 tools/ci_pack_evidence.py --source "$RUNNER_TEMP/buster-ci" --output "$RUNNER_TEMP/native-ci-upload"; fi

""".replace("EXPR_OPEN", expr_open).replace("EXPR_CLOSE", expr_close)
workflow = workflow[:start] + pack + workflow[end:]

def expr(text: str) -> str:
    return expr_open + " " + text + " " + expr_close

def replace_in_block(text: str, start_marker: str, end_marker: str, old: str, new: str) -> str:
    block_start = text.index(start_marker)
    block_end = text.index(end_marker, block_start)
    block = text[block_start:block_end]
    if block.count(old) != 1:
        raise SystemExit(f"unexpected condition population in {start_marker.strip()}: {old.strip()}")
    block = block.replace(old, new, 1)
    return text[:block_start] + block + text[block_end:]

packed_old = "        if: " + expr("always() && steps.pack.outcome == 'success'") + "\n"
packed_new = "        if: " + expr("!cancelled() && steps.pack.outcome == 'success'") + "\n"
if workflow.count(packed_old) != 1:
    raise SystemExit("unexpected packed-upload condition population")
workflow = workflow.replace(packed_old, packed_new, 1)

shared_failure_old = (
    "        if: " +
    expr("always() && steps.checkout.outcome == 'success' && steps.pack.outcome != 'success'") +
    "\n"
)
record_failure_new = (
    "        if: " +
    expr("!cancelled() && steps.checkout.outcome == 'success' && steps.pack.outcome != 'success'") +
    "\n"
)
unpacked_new = "        if: " + expr("!cancelled() && steps.pack.outcome != 'success'") + "\n"
workflow = replace_in_block(
    workflow,
    "      - name: Record native packaging failure\n",
    "      - name: Retain unpacked native logs\n",
    shared_failure_old,
    record_failure_new,
)
workflow = replace_in_block(
    workflow,
    "      - name: Retain unpacked native logs\n",
    "\n  mobile:\n",
    shared_failure_old,
    unpacked_new,
)
workflow_path.write_text(workflow, encoding="utf-8")

test_path = root / "tools/ci_native_observation_test.py"
test = test_path.read_text(encoding="utf-8")
old = (
    '            "if: ' +
    expr("always() && steps.checkout.outcome == 'success' && steps.native_observation.outcome == 'success'") +
    '",\n'
)
new = (
    '            "if: ' +
    expr("!cancelled() && steps.checkout.outcome == 'success'") +
    '",\n'
)
if test.count(old) != 1:
    raise SystemExit("unexpected focused workflow-condition assertion population")
test_path.write_text(test.replace(old, new, 1), encoding="utf-8")
