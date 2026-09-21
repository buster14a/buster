#!/usr/bin/env python3
from pathlib import Path
import runpy

path = Path(__file__).with_name("apply_929_patch.py")
text = path.read_text(encoding="utf-8")

old_import = """tests = replace_once(
    tests,
    "import native_retirement_performance_binding as binding\\n",
    "import native_retirement_performance_binding as binding\\n"
    "import native_retirement_performance_schema as RETIREMENT_SCHEMA\\n",
    "test schema import",
)
"""
new_import = """tests = replace_once(
    tests,
    "SPEC.loader.exec_module(binding)\\n\\n\\nclass BindingTests",
    "SPEC.loader.exec_module(binding)\\n"
    "RETIREMENT_SCHEMA = binding.RETIREMENT_SCHEMA\\n\\n\\nclass BindingTests",
    "test schema import",
)
"""
if text.count(old_import) != 1:
    raise RuntimeError("staged patch driver has an unexpected test-import edit")
text = text.replace(old_import, new_import, 1)

old_cleanup = 'for relative in ("tools/apply_929_patch.py", ".github/workflows/apply-929-patch.yml"):\\n'
new_cleanup = (
    'for relative in ("tools/apply_929_patch.py", ".github/workflows/apply-929-patch.yml", '
    '"tools/run_929_patch.py", "apply-929-error.txt"):\\n'
)
if text.count(old_cleanup) != 1:
    raise RuntimeError("staged patch driver has an unexpected cleanup edit")
text = text.replace(old_cleanup, new_cleanup, 1)

path.write_text(text, encoding="utf-8")
runpy.run_path(str(path), run_name="__main__")
