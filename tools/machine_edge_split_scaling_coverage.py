"""Count actual splitter traversals with Clang source coverage (#296).

Build the instrumented probe using docs/agents/benchmarking.md first. This
driver checks 56 geometric cells and preserves raw LLVM profiles, counted
source regions and output records. It makes no timing claim.
"""

import argparse
import hashlib
import json
import os
import pathlib
import platform
import subprocess


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()
    root = pathlib.Path(__file__).resolve().parents[1]
    binary = args.binary.resolve()
    out = args.output.resolve()
    # A fresh attempt prevents mixing profiles and identities from another run.
    out.mkdir(parents=True, exist_ok=False)
    source = root / "src/buster/lib/compiler/codegen/machine.c"
    lines = source.read_text().splitlines()
    start = next(i for i, line in enumerate(lines) if line.startswith(
        "bool machine_function_split_parameter_edges_with_canonical_map("))
    stop = next(i for i, line in enumerate(lines[start + 1:], start + 1)
                if line.startswith("bool machine_function_split_parameter_edges("))
    needles = {
        "count_edges": "split_count += machine_function_parameter_edge_needs_split",
        "index_edges": "MachineEdge const* edge = function->edges + edge_index;",
        "prefix_blocks": "outgoing_offsets[old_block + 1u] += outgoing_offsets[old_block];",
        "scatter_edges": "u32 source = function->edges[edge_index].source_block;",
        "number_edges": "u32 edge_index = outgoing_edges[cursor];",
        "map_edges": "u32 destination = function->edges[edge_index].destination_block;",
        "reset_edges": "split_targets[edge->destination_block] = UINT32_MAX;",
        "publish_edges": "MachineEdge edge = function->edges[edge_index];",
        "copy_rows": "MachineInstruction instruction = function->instructions[old_instruction];",
    }
    points = {}
    for key, needle in needles.items():
        matches = [(i + 1, line.index(needle) + 1) for i, line in enumerate(lines)
                   if needle in line and start < i < stop]
        if not matches:
            raise ValueError(f"coverage point missing: {key}")
        # First occurrence identifies the histogram or block-numbering walk.
        # Later identical statements belong to the separately counted walks.
        points[key] = matches[0]

    rows = []
    for target in ("x64", "a64"):
        for shape in ("chain", "fanout", "duplicates", "no-split"):
            for count in (0, 1, 2, 17, 257, 4097, 16384):
                stem = f"{target}-{shape}-{count}"
                raw = out / f"{stem}.profraw"
                prof = out / f"{stem}.profdata"
                env = dict(os.environ, LLVM_PROFILE_FILE=str(raw))
                process = subprocess.run(
                    [str(binary), str(count), "1", shape, target], env=env,
                    capture_output=True, text=True, check=True)
                subprocess.run(["llvm-profdata", "merge", "-sparse", str(raw),
                                "-o", str(prof)], check=True)
                export = json.loads(subprocess.check_output([
                    "llvm-cov", "export", str(binary), f"-instr-profile={prof}",
                    str(source)]))["data"][0]
                function = next(f for f in export["functions"] if f["name"] ==
                                "machine_function_split_parameter_edges_with_canonical_map")
                counts = {}
                used = []
                for key, point in points.items():
                    # LLVM regions: start/end line+column, execution count,
                    # file ID, expanded-file ID, kind. Kind 0 is executable.
                    containing = [r for r in function["regions"] if
                                  r[7] == 0 and r[5] == 0 and
                                  (r[0], r[1]) <= point < (r[2], r[3])]
                    region = min(containing, key=lambda r: (r[2] - r[0], r[3] - r[1]))
                    counts[key] = region[4]
                    if region not in used:
                        used.append(region)
                helpers = {f["name"].split(":")[-1]: f["count"] for
                           f in export["functions"] if "parameter_edge_" in f["name"]}
                counts["needs_split"] = helpers["machine_function_parameter_edge_needs_split"]
                counts["remaps"] = helpers["machine_function_parameter_edge_target"]
                edges = max(count - 1, 0) * (3 if shape == "duplicates" else 1)
                split = shape != "no-split" and count > 1
                expected = {key: edges if split else 0 for key in counts}
                remaps = count if shape in ("fanout", "duplicates") else 2 * (count - 1)
                expected.update(
                    count_edges=edges, prefix_blocks=count if split else 0,
                    copy_rows=count if split else 0,
                    needs_split=edges * (2 if split else 1),
                    remaps=remaps if split else 0)
                if counts != expected:
                    raise ValueError(f"{stem}: observed {counts}, expected {expected}")
                rows.append({
                    "target": target, "shape": shape, "blocks": count, "edges": edges,
                    "counts": counts, "stdout": process.stdout.strip(),
                    "coverage_regions": used, "profraw_sha256": digest(raw),
                    "profdata_sha256": digest(prof),
                })

    record = {
        "schema": "buster-parameter-edge-scaling-diagnostic-v1",
        "revision": subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=root, text=True).strip(),
        "platform": platform.platform(),
        "llvm_cov_version": subprocess.check_output(
            ["llvm-cov", "--version"], text=True).strip(),
        "source_sha256": digest(source),
        "probe_sha256": digest(root / "tools/machine_edge_split_scaling.c"),
        "collector_sha256": digest(pathlib.Path(__file__).resolve()),
        "binary_sha256": digest(binary), "points": points, "rows": rows,
    }
    (out / "counts.json").write_text(json.dumps(record, indent=2) + "\n")
    print(f"PASS {len(rows)} geometric cells: observed traversal counts match linear expectations")


if __name__ == "__main__":
    main()
