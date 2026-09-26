import subprocess, sys, re
def frame(obj, fn):
    out = subprocess.run(["objdump", "-d", "--no-show-raw-insn", obj], capture_output=True, text=True).stdout
    lines = out.split("\n")
    start = next((i for i, l in enumerate(lines) if l.endswith("<%s>:" % fn)), None)
    if start is None: return None
    total = 0
    for line in lines[start + 1:start + 20000]:
        mm = re.search(r"\tsub\s+\$0x([0-9a-f]+),%rsp", line)
        if mm: total += int(mm.group(1), 16); continue
        if re.search(r"\t(call|j[a-z]+|ret)\b", line) and total: break
    return total
for obj in sys.argv[2:]:
    print(f"{obj}: {frame(obj, sys.argv[1])}")
