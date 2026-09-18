from pathlib import Path

path = Path("src/buster/tests/compiler/link/link_test.c")
text = path.read_text()
replacements = {
    "link_test_single_comdat(arena, target, BUSTER_ARRAY_TO_SLICE(":
        "link_test_single_comdat(arena, target, (ByteSlice)BUSTER_ARRAY_TO_SLICE(",
    "link_test_comdat_object(arena, target, BUSTER_ARRAY_TO_SLICE(":
        "link_test_comdat_object(arena, target, (ByteSlice)BUSTER_ARRAY_TO_SLICE(",
}
for old, new in replacements.items():
    if old not in text:
        raise RuntimeError(f"missing expected test expression: {old}")
    text = text.replace(old, new)
path.write_text(text)
