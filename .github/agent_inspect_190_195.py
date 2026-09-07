from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / ".agent" / "inspect-190-195.txt"
OUT.parent.mkdir(parents=True, exist_ok=True)

sections = []

def add_context(path_name, needles, radius=140):
    path = ROOT / path_name
    lines = path.read_text(encoding="utf-8").splitlines()
    seen = []
    for needle in needles:
        hits = [i for i, line in enumerate(lines) if needle in line]
        sections.append(f"\n===== {path_name}: needle {needle!r}, hits {[i + 1 for i in hits]} =====\n")
        for hit in hits:
            lo = max(0, hit - radius)
            hi = min(len(lines), hit + radius + 1)
            key = (path_name, lo, hi)
            if key in seen:
                continue
            seen.append(key)
            for i in range(lo, hi):
                sections.append(f"{i + 1:6d}: {lines[i]}\n")

add_context(
    "src/buster/lib/compiler/codegen/codegen.c",
    [
        "codegen_canonical_a64_i128_to_float",
        "codegen_canonical_a64_float_to_i128",
        "source_integer128",
        "target_integer128",
        "IR_OPCODE_ATOMIC_COMPARE_EXCHANGE",
        "CMPXCHG16B",
    ],
    radius=180,
)

# Locate tests and registration points that already cover adjacent behavior.
terms = [
    "__int128",
    "atomic_compare_exchange",
    "CMPXCHG16B",
    "assembly_x86_metadata",
    "vaddps",
    "bts $",
]
for path in sorted((ROOT / "src").rglob("*")):
    if not path.is_file() or path.suffix.lower() not in {".c", ".h", ".asm", ".s"}:
        continue
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except UnicodeDecodeError:
        continue
    hits = []
    for i, line in enumerate(lines):
        if any(term in line for term in terms):
            hits.append(i)
    if not hits:
        continue
    sections.append(f"\n===== TEST/RELATED HITS {path.relative_to(ROOT)} =====\n")
    emitted = set()
    for hit in hits[:80]:
        lo = max(0, hit - 5)
        hi = min(len(lines), hit + 8)
        for i in range(lo, hi):
            if i not in emitted:
                sections.append(f"{i + 1:6d}: {lines[i]}\n")
                emitted.add(i)
        sections.append("------\n")

OUT.write_text("".join(sections), encoding="utf-8")
print(OUT)
