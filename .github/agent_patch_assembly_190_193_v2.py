from pathlib import Path
import subprocess
import sys

root = Path(sys.argv[1]).resolve()
subprocess.check_call([sys.executable, str(Path(__file__).with_name("agent_patch_assembly_190_193.py")), str(root)])

path = root / "src/buster/lib/compiler/assembly/x86_64_metadata.c"
text = path.read_text(encoding="utf-8")
old = "            address_ready = buster_x86_metadata_emit_address(memory, form, plan->pattern, force_disp32, &address);\n"
new = "            address_ready = buster_x86_metadata_emit_address(memory, form, plan->pattern,\n                                                               query.attributes.broadcast_elements != 0, force_disp32, &address);\n"
if text.count(old) != 1:
    raise RuntimeError(f"prepared address call: expected one match, found {text.count(old)}")
path.write_text(text.replace(old, new, 1), encoding="utf-8")
print("patched prepared broadcast address path")
