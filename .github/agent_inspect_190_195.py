from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT_DIR = ROOT / ".agent"
OUT_DIR.mkdir(parents=True, exist_ok=True)


def context(path_name, needles, radius):
    path = ROOT / path_name
    lines = path.read_text(encoding="utf-8").splitlines()
    chunks = []
    emitted_ranges = set()
    for needle in needles:
        hits = [i for i, line in enumerate(lines) if needle in line]
        chunks.append(f"\n===== {path_name}: {needle!r}, hits {[i + 1 for i in hits]} =====\n")
        for hit in hits:
            lo = max(0, hit - radius)
            hi = min(len(lines), hit + radius + 1)
            key = (lo, hi)
            if key in emitted_ranges:
                continue
            emitted_ranges.add(key)
            for i in range(lo, hi):
                chunks.append(f"{i + 1:6d}: {lines[i]}\n")
    return "".join(chunks)

codegen = "src/buster/lib/compiler/codegen/codegen.c"
(OUT_DIR / "codegen-casts.txt").write_text(
    context(
        codegen,
        [
            "codegen_canonical_a64_i128_to_float",
            "codegen_canonical_a64_float_to_i128",
            "source_integer128",
            "target_integer128",
        ],
        230,
    ),
    encoding="utf-8",
)
(OUT_DIR / "codegen-atomics.txt").write_text(
    context(
        codegen,
        [
            "IR_OPCODE_ATOMIC_COMPARE_EXCHANGE",
            "CMPXCHG16B",
            "cmpxchg16b",
            "ATOMIC_COMPARE_EXCHANGE",
        ],
        260,
    ),
    encoding="utf-8",
)
(OUT_DIR / "assembly-metadata.txt").write_text(
    context(
        "src/buster/lib/compiler/assembly/x86_64_metadata.c",
        [
            "buster_x86_metadata_emit_tuple_scale",
            "scalar_memory_width_rex_w",
            "apx_rex2_unary_imul_qword",
            "if (apx_evex)",
            "vvvv_index",
        ],
        100,
    ),
    encoding="utf-8",
)
(OUT_DIR / "assembly-parser.txt").write_text(
    context(
        "src/buster/lib/compiler/assembly/assembly.c",
        [
            "assembly_x86_metadata_suffix_alias",
            "mnemonic_suffix_base",
            "suffix_alias_selected",
        ],
        100,
    ),
    encoding="utf-8",
)

# Locate nearby tests and registration/build points, retaining their file paths.
terms = [
    "__int128",
    "atomic_compare_exchange",
    "CMPXCHG16B",
    "assembly_x86_metadata",
    "vaddps",
    "bts $",
    "_Atomic",
]
chunks = []
for root_name in ["src", "tests"]:
    root = ROOT / root_name
    if not root.exists():
        continue
    for path in sorted(root.rglob("*")):
        if not path.is_file() or path.suffix.lower() not in {".c", ".h", ".asm", ".s", ".txt", ".cmake", ".py"}:
            continue
        try:
            lines = path.read_text(encoding="utf-8").splitlines()
        except UnicodeDecodeError:
            continue
        hits = [i for i, line in enumerate(lines) if any(term in line for term in terms)]
        if not hits:
            continue
        chunks.append(f"\n===== {path.relative_to(ROOT)}: hits {[i + 1 for i in hits]} =====\n")
        emitted = set()
        for hit in hits[:120]:
            lo = max(0, hit - 10)
            hi = min(len(lines), hit + 16)
            for i in range(lo, hi):
                if i not in emitted:
                    chunks.append(f"{i + 1:6d}: {lines[i]}\n")
                    emitted.add(i)
            chunks.append("------\n")
(OUT_DIR / "related-tests.txt").write_text("".join(chunks), encoding="utf-8")

# Capture repository instructions and CI/build entry points.
build_chunks = []
for path_name in ["AGENTS.md", "README.md", ".github/workflows/ci.yml", ".github/workflows/build.yml"]:
    path = ROOT / path_name
    if path.exists():
        build_chunks.append(f"\n===== {path_name} =====\n")
        for i, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            build_chunks.append(f"{i:6d}: {line}\n")
for path in sorted((ROOT / ".github" / "workflows").glob("*.yml")):
    if path.name == "agent-inspect-190-195.yml":
        continue
    if f"===== {path.relative_to(ROOT)} =====" in "".join(build_chunks):
        continue
    build_chunks.append(f"\n===== {path.relative_to(ROOT)} =====\n")
    for i, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        build_chunks.append(f"{i:6d}: {line}\n")
(OUT_DIR / "build-and-ci.txt").write_text("".join(build_chunks), encoding="utf-8")

print("wrote focused issue 190-195 inspection files")
