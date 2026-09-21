#!/usr/bin/env python3
from pathlib import Path
import runpy

path = Path(__file__).with_name("apply_929_patch.py")
text = path.read_text(encoding="utf-8")

start_marker = 'path_test = "tools/native_retirement_performance_binding_test.py"\n'
end_marker = "old_report_start = "
start = text.find(start_marker)
end = text.find(end_marker, start + 1)
if start < 0 or end < 0:
    raise RuntimeError("staged patch driver has an unexpected fixture section")
replacement = """path_test = "tools/native_retirement_performance_binding_test.py"
tests = read(path_test)
tests = replace_once(
    tests,
    "SPEC.loader.exec_module(binding)\\n\\n\\nclass BindingTests",
    "SPEC.loader.exec_module(binding)\\n"
    "RETIREMENT_SCHEMA = binding.RETIREMENT_SCHEMA\\n\\n\\nclass BindingTests",
    "test schema import",
)
"""
text = text[:start] + replacement + text[end:]

old_cleanup = 'for relative in ("tools/apply_929_patch.py", ".github/workflows/apply-929-patch.yml"):\n'
new_cleanup = (
    'for relative in ("tools/apply_929_patch.py", ".github/workflows/apply-929-patch.yml", '
    '"tools/run_929_patch.py", "apply-929-error.txt"):\n'
)
if text.count(old_cleanup) != 1:
    raise RuntimeError("staged patch driver has an unexpected cleanup edit")
text = text.replace(old_cleanup, new_cleanup, 1)

path.write_text(text, encoding="utf-8")
runpy.run_path(str(path), run_name="__main__")
