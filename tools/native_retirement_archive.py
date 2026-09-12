#!/usr/bin/env python3
"""Create and independently verify durable native-retirement release assets."""
import argparse
import copy
import csv
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import stat
import sys
import tarfile
import zipfile


MANIFEST_NAME = "archive-manifest.json"
CHECKSUM_NAME = "SHA256SUMS"
CHUNK = 1024 * 1024


def digest(path):
    value = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(CHUNK), b""):
            value.update(block)
    return value.hexdigest()


def checked(path, size, sha256):
    path = Path(path)
    if not path.is_file():
        raise ValueError(f"missing archive asset: {path.name}")
    if path.stat().st_size != size:
        raise ValueError(f"archive size mismatch: {path.name}")
    if digest(path) != sha256:
        raise ValueError(f"archive digest mismatch: {path.name}")
    return path


def record(path, role):
    path = Path(path)
    return {"name": path.name, "role": role, "size": path.stat().st_size, "sha256": digest(path)}


def load_json(path):
    with Path(path).open(encoding="utf-8") as stream:
        value = json.load(stream)
    if not isinstance(value, dict):
        raise ValueError(f"expected a JSON object: {path}")
    return value


def safe_name(name):
    path = PurePosixPath(name)
    return bool(name) and not path.is_absolute() and ".." not in path.parts


def validate_tar(path):
    names = set()
    with tarfile.open(path, "r:gz") as bundle:
        for member in bundle:
            if not safe_name(member.name) or member.name in names:
                raise ValueError(f"unsafe or duplicate source member: {member.name}")
            if not (member.isdir() or member.isfile()):
                raise ValueError(f"unsupported source member: {member.name}")
            names.add(member.name)
            if member.isfile():
                source = bundle.extractfile(member)
                for _block in iter(lambda: source.read(CHUNK), b""):
                    pass
    if not names:
        raise ValueError(f"empty source archive: {path}")


def write_checksums(output, assets):
    paths = [output / item["name"] for item in assets] + [output / MANIFEST_NAME]
    text = "".join(f"{digest(path)}  {path.name}\n" for path in sorted(paths))
    (output / CHECKSUM_NAME).write_text(text, encoding="utf-8")


def prepare(contract_path, source, output):
    contract = load_json(contract_path)
    source = Path(source)
    output = Path(output)
    output.mkdir(parents=True, exist_ok=False)
    manifest = copy.deepcopy(contract)
    assets = []

    census = manifest["artifacts"]["census"]
    census_source = checked(source / census["archive_name"], census["size"], census["sha256"])
    census_target = output / census_source.name
    shutil.copyfile(census_source, census_target)
    assets.append(record(census_target, "census-actions-archive"))
    census["release_assets"] = [census_target.name]

    strict = manifest["artifacts"]["strict"]
    strict_source = checked(source / strict["archive_name"], strict["size"], strict["sha256"])
    strict["release_assets"] = []
    with strict_source.open("rb") as stream:
        index = 0
        while True:
            data = stream.read(strict["part_size"])
            if not data:
                break
            part = output / f"{strict_source.name}.part-{index:02d}"
            part.write_bytes(data)
            assets.append(record(part, "strict-actions-archive-part"))
            strict["release_assets"].append(part.name)
            index += 1
    if index < 2:
        raise ValueError("strict archive was not split")

    for name, source_info in manifest["sources"].items():
        archive = source / source_info["asset"]
        validate_tar(archive)
        target = output / archive.name
        shutil.copyfile(archive, target)
        source_info.update(record(target, f"{name}-source"))
        source_info.pop("name")
        assets.append(record(target, f"{name}-source"))

    manifest["published_assets"] = sorted(assets, key=lambda item: item["name"])
    (output / MANIFEST_NAME).write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    write_checksums(output, manifest["published_assets"])
    print(json.dumps({"assets": len(assets) + 2, "manifest_sha256": digest(output / MANIFEST_NAME)}))


def checksum_table(path):
    result = {}
    for line in Path(path).read_text(encoding="utf-8").splitlines():
        sha256, separator, name = line.partition("  ")
        if not separator or len(sha256) != 64 or Path(name).name != name or name in result:
            raise ValueError("malformed SHA256SUMS")
        result[name] = sha256
    return result


def verify_assets(directory, roles):
    directory = Path(directory)
    manifest = load_json(directory / MANIFEST_NAME)
    listed = {item["name"]: item for item in manifest["published_assets"]}
    expected = {name: item for name, item in listed.items() if item["role"] in roles}
    for name, item in expected.items():
        checked(directory / name, item["size"], item["sha256"])
    checksums = checksum_table(directory / CHECKSUM_NAME)
    required = set(expected) | {MANIFEST_NAME}
    if not required <= set(checksums):
        raise ValueError("SHA256SUMS omits a required asset")
    for name in required:
        if digest(directory / name) != checksums[name]:
            raise ValueError(f"published checksum mismatch: {name}")
    return manifest


def extract_zip(path, output):
    output = Path(output)
    output.mkdir(parents=True, exist_ok=False)
    names = set()
    with zipfile.ZipFile(path) as bundle:
        for info in bundle.infolist():
            if not safe_name(info.filename) or info.filename in names:
                raise ValueError(f"unsafe or duplicate ZIP member: {info.filename}")
            mode = info.external_attr >> 16
            if stat.S_ISLNK(mode):
                raise ValueError(f"refusing ZIP symbolic link: {info.filename}")
            names.add(info.filename)
            target = output.joinpath(*PurePosixPath(info.filename).parts)
            if info.is_dir():
                target.mkdir(parents=True, exist_ok=True)
            else:
                target.parent.mkdir(parents=True, exist_ok=True)
                with bundle.open(info) as source, target.open("wb") as destination:
                    shutil.copyfileobj(source, destination, CHUNK)
                if mode:
                    target.chmod(stat.S_IMODE(mode))
    if not names:
        raise ValueError(f"empty ZIP archive: {path}")


def verify_census(directory, output, cleanup):
    directory = Path(directory)
    manifest = verify_assets(directory, {"census-actions-archive", "validation-source", "candidate-source", "direct_oracle-source"})
    census = manifest["artifacts"]["census"]
    archive = checked(directory / census["release_assets"][0], census["size"], census["sha256"])
    extract_zip(archive, output)
    if cleanup:
        archive.unlink()
    print(json.dumps({"census_archive_verified": True, "sha256": census["sha256"], "size": census["size"]}))


def restore_census_inputs(evidence, source):
    evidence = Path(evidence)
    source = Path(source)
    shards = sorted(path for path in evidence.glob("census-integrated-[0-9]*") if path.is_dir())
    if len(shards) != 4:
        raise ValueError("expected four extracted census shards")
    restored = []
    for shard in shards:
        with (shard / "inputs.tsv").open(encoding="utf-8") as stream:
            rows = list(csv.DictReader(stream, delimiter="\t"))
        for row in rows:
            relative = PurePosixPath(row["path"])
            if not safe_name(row["path"]):
                raise ValueError(f"unsafe census input path: {row['path']}")
            destination = shard / "inputs" / Path(*relative.parts)
            if not destination.exists():
                original = source / Path(*relative.parts)
                if not original.is_file():
                    raise ValueError(f"source snapshot cannot restore census input: {row['path']}")
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(original, destination)
                restored.append(f"{shard.name}/{row['path']}")
    if restored != [f"census-integrated-{index}/tests/.gitignore" for index in range(4)]:
        raise ValueError(f"unexpected census archive omissions: {restored}")
    print(json.dumps({"restored_tracked_inputs": restored}))


def strict_selected_files(archive, output):
    wanted = {
        "strict-binary/ide": "archived-ide",
        "strict-binary/source.txt": "source.txt",
        "strict-binary/sha256.txt": "binary-sha256.txt",
        "strict-differential/summary.txt": "recorded-strict-summary.txt",
        "result.json": "inner-result.json",
        "summary.md": "inner-summary.md",
    }
    found = {}
    names = set()
    with tarfile.open(archive, "r:gz") as bundle:
        for member in bundle:
            if not safe_name(member.name) or member.name in names:
                raise ValueError(f"unsafe or duplicate strict member: {member.name}")
            if not (member.isdir() or member.isfile()):
                raise ValueError(f"unsupported strict member: {member.name}")
            names.add(member.name)
            if member.isfile():
                source = bundle.extractfile(member)
                suffix = member.name.split("/", 1)[-1]
                destination = output / wanted[suffix] if suffix in wanted else None
                stream = destination.open("wb") if destination else None
                value = hashlib.sha256()
                try:
                    for block in iter(lambda: source.read(CHUNK), b""):
                        value.update(block)
                        if stream:
                            stream.write(block)
                finally:
                    if stream:
                        stream.close()
                if destination:
                    found[suffix] = (destination, value.hexdigest())
    if set(found) != set(wanted):
        raise ValueError(f"strict archive is missing selected files: {sorted(set(wanted) - set(found))}")
    return found


def verify_strict(directory, output, cleanup):
    directory = Path(directory)
    output = Path(output)
    output.mkdir(parents=True, exist_ok=False)
    manifest = verify_assets(directory, {"strict-actions-archive-part", "candidate-source"})
    strict = manifest["artifacts"]["strict"]
    reconstructed = output / strict["archive_name"]
    with reconstructed.open("wb") as destination:
        for name in strict["release_assets"]:
            with (directory / name).open("rb") as source:
                shutil.copyfileobj(source, destination, CHUNK)
    checked(reconstructed, strict["size"], strict["sha256"])
    outer = output / "outer"
    extract_zip(reconstructed, outer)
    packed = list(outer.rglob("native-ci-logs.tar.gz"))
    results = list(outer.rglob("result.json"))
    summaries = list(outer.rglob("summary.md"))
    if len(packed) != 1 or len(results) != 1 or len(summaries) != 1:
        raise ValueError("strict Actions archive does not contain exactly one packed bundle and summaries")
    selected = strict_selected_files(packed[0], output)
    if selected["result.json"][0].read_bytes() != results[0].read_bytes():
        raise ValueError("strict result.json copy differs from packed evidence")
    if selected["summary.md"][0].read_bytes() != summaries[0].read_bytes():
        raise ValueError("strict summary.md copy differs from packed evidence")
    result = load_json(results[0])
    required = {"strict_build", "strict_self_test", "strict_execute"}
    if result.get("success") is not True or set(result.get("required_steps", ())) != required:
        raise ValueError("recorded strict CI verdict is not successful and complete")
    for name in required:
        if result.get("steps", {}).get(name, {}).get("outcome") != "success":
            raise ValueError(f"recorded strict step did not pass: {name}")
    expected_source = manifest["sources"]["candidate"]
    source_lines = selected["strict-binary/source.txt"][0].read_text(encoding="utf-8").splitlines()
    if source_lines != [expected_source["commit"], expected_source["tree"]]:
        raise ValueError("strict compiler source identity mismatch")
    binary = selected["strict-binary/ide"][0]
    binary.chmod(0o555)
    binary_sha256 = digest(binary)
    strict_binary_sha256 = manifest["identities"].get("strict_candidate_binary_sha256")
    if strict_binary_sha256 is not None and binary_sha256 != strict_binary_sha256:
        raise ValueError("strict compiler binary identity mismatch")
    recorded_binary_sha256 = selected["strict-binary/sha256.txt"][0].read_text(encoding="utf-8").split()
    if not recorded_binary_sha256 or recorded_binary_sha256[0] != binary_sha256:
        raise ValueError("strict recorded binary digest mismatch")
    recorded = selected["strict-differential/summary.txt"][0].read_text(encoding="utf-8").strip()
    if recorded != "version=1 failures=0 io_failed=0 configurations=432":
        raise ValueError("recorded strict differential summary is not a pass")
    preflight = {
        "archive_sha256": strict["sha256"],
        "archive_size": strict["size"],
        "binary_sha256": binary_sha256,
        "recorded_summary": recorded,
        "recorded_ci_success": True,
    }
    (output / "strict-preflight.json").write_text(json.dumps(preflight, indent=2) + "\n", encoding="utf-8")
    if cleanup:
        for name in strict["release_assets"]:
            (directory / name).unlink()
        reconstructed.unlink()
        shutil.rmtree(outer)
    print(json.dumps(preflight))


def tree_digest(directory):
    value = hashlib.sha256()
    files = sorted(path for path in Path(directory).rglob("*") if path.is_file())
    for path in files:
        relative = path.relative_to(directory).as_posix().encode()
        value.update(len(relative).to_bytes(8, "little"))
        value.update(relative)
        value.update(bytes.fromhex(digest(path)))
    return value.hexdigest(), len(files)


def census_receipt(manifest_path, report_path, recorded, replayed, archived_reference, rebuilt_reference, run_id, output):
    manifest = load_json(manifest_path)
    report = load_json(report_path)
    identities = manifest["identities"]
    binaries = report.get("binaries_sha256", {})
    if report.get("rows_validated") != identities["census_rows"]:
        raise ValueError("replayed census row count mismatch")
    if binaries.get("candidate-ide.exe") != identities["candidate_binary_sha256"]:
        raise ValueError("replayed census candidate binary mismatch")
    if binaries.get("baseline-ide.exe") != identities["direct_oracle_binary_sha256"]:
        raise ValueError("replayed census direct binary mismatch")
    archived_sha = digest(archived_reference)
    rebuilt_sha = digest(rebuilt_reference)
    if archived_sha != identities["direct_oracle_binary_sha256"] or rebuilt_sha != archived_sha:
        raise ValueError("clean direct-oracle rebuild is not byte-identical to the archive")
    recorded_sha, recorded_files = tree_digest(recorded)
    replayed_sha, replayed_files = tree_digest(replayed)
    if (recorded_sha, recorded_files) != (replayed_sha, replayed_files):
        raise ValueError("replayed census join differs from the recorded join")
    receipt = {
        "schema": "buster-native-retirement-census-replay-v1",
        "success": True,
        "release": manifest["release"],
        "github_run_id": str(run_id),
        "rows_validated": report["rows_validated"],
        "inputs_validated": len(report.get("inputs_sha256", {})),
        "joined_files": replayed_files,
        "joined_tree_sha256": replayed_sha,
        "candidate_binary_sha256": binaries["candidate-ide.exe"],
        "archived_direct_oracle_sha256": archived_sha,
        "rebuilt_direct_oracle_sha256": rebuilt_sha,
        "validator_report_sha256": digest(report_path),
    }
    Path(output).write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(receipt))


def strict_receipt(manifest_path, preflight_path, replay, run_id, output):
    manifest = load_json(manifest_path)
    preflight = load_json(preflight_path)
    summary = (Path(replay) / "summary.txt").read_text(encoding="utf-8").strip()
    if summary != "version=1 failures=0 io_failed=0 configurations=432":
        raise ValueError("replayed strict differential summary is not a pass")
    cases = len(list(Path(replay).glob("*/result.txt")))
    configurations = cases * 432
    if configurations != manifest["identities"]["strict_configurations"]:
        raise ValueError("replayed strict configuration count mismatch")
    receipt = {
        "schema": "buster-native-retirement-strict-replay-v1",
        "success": True,
        "release": manifest["release"],
        "github_run_id": str(run_id),
        "archive_sha256": preflight["archive_sha256"],
        "archive_size": preflight["archive_size"],
        "candidate_binary_sha256": preflight["binary_sha256"],
        "recorded_summary": preflight["recorded_summary"],
        "replayed_summary": summary,
        "cases": cases,
        "configurations": configurations,
    }
    Path(output).write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(receipt))


def parser():
    result = argparse.ArgumentParser(description=__doc__)
    commands = result.add_subparsers(dest="command", required=True)
    make = commands.add_parser("prepare")
    make.add_argument("--contract", required=True, type=Path)
    make.add_argument("--source", required=True, type=Path)
    make.add_argument("--output", required=True, type=Path)
    for name in ("verify-census", "verify-strict"):
        check = commands.add_parser(name)
        check.add_argument("--assets", required=True, type=Path)
        check.add_argument("--output", required=True, type=Path)
        check.add_argument("--cleanup", action="store_true")
    restore = commands.add_parser("restore-census-inputs")
    restore.add_argument("--evidence", required=True, type=Path)
    restore.add_argument("--source", required=True, type=Path)
    census = commands.add_parser("census-receipt")
    census.add_argument("--manifest", required=True, type=Path)
    census.add_argument("--report", required=True, type=Path)
    census.add_argument("--recorded", required=True, type=Path)
    census.add_argument("--replayed", required=True, type=Path)
    census.add_argument("--archived-reference", required=True, type=Path)
    census.add_argument("--rebuilt-reference", required=True, type=Path)
    census.add_argument("--run-id", required=True)
    census.add_argument("--output", required=True, type=Path)
    strict = commands.add_parser("strict-receipt")
    strict.add_argument("--manifest", required=True, type=Path)
    strict.add_argument("--preflight", required=True, type=Path)
    strict.add_argument("--replay", required=True, type=Path)
    strict.add_argument("--run-id", required=True)
    strict.add_argument("--output", required=True, type=Path)
    return result


def main(argv=None):
    arguments = parser().parse_args(argv)
    status = 1
    try:
        if arguments.command == "prepare":
            prepare(arguments.contract, arguments.source, arguments.output)
        elif arguments.command == "verify-census":
            verify_census(arguments.assets, arguments.output, arguments.cleanup)
        elif arguments.command == "verify-strict":
            verify_strict(arguments.assets, arguments.output, arguments.cleanup)
        elif arguments.command == "restore-census-inputs":
            restore_census_inputs(arguments.evidence, arguments.source)
        elif arguments.command == "census-receipt":
            census_receipt(arguments.manifest, arguments.report, arguments.recorded, arguments.replayed,
                           arguments.archived_reference, arguments.rebuilt_reference, arguments.run_id, arguments.output)
        elif arguments.command == "strict-receipt":
            strict_receipt(arguments.manifest, arguments.preflight, arguments.replay, arguments.run_id, arguments.output)
        status = 0
    except (OSError, ValueError, KeyError, TypeError, tarfile.TarError, zipfile.BadZipFile, json.JSONDecodeError) as error:
        print(f"native-retirement archive failure: {error}", file=sys.stderr)
    return status


if __name__ == "__main__":
    sys.exit(main())
