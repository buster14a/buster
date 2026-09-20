#!/usr/bin/env python3
"""Fail-closed reviewed declarations used by native-retirement rebinding."""
from __future__ import annotations
import ast, hashlib, json, os, re, stat
from pathlib import Path, PurePosixPath

PYTHON_BINDING_PATH = "tools/native_retirement_contract.py"
SDK_MANIFEST_PATH = "docs/native-retirement-sdks-v1.json"
SHA256_RE = re.compile(r"[0-9a-f]{64}\Z")

class RebindError(ValueError): pass

def _fail(message): raise RebindError(message)

def _identity_tuple(info):
    return (info.st_dev, info.st_ino, info.st_mode, info.st_nlink, info.st_size, info.st_mtime_ns, info.st_ctime_ns)

def _absolute_root(value):
    root=Path(os.path.abspath(os.fspath(value)))
    try: info=root.lstat()
    except OSError as error: _fail(f"cannot inspect repository root {root}: {error}")
    if stat.S_ISLNK(info.st_mode) or not stat.S_ISDIR(info.st_mode): _fail(f"repository root must be a real directory: {root}")
    return root

def _read_no_follow(path, field, require_single_link=False):
    path=Path(os.path.abspath(os.fspath(path))); current=Path(path.anchor); descriptor=None
    try:
        parts=path.relative_to(current).parts
        for part in parts[:-1]:
            current/=part; info=current.lstat()
            if stat.S_ISLNK(info.st_mode) or not stat.S_ISDIR(info.st_mode): _fail(f"{field} parent is not a real directory: {current}")
        current/=parts[-1]; info=current.lstat()
        if stat.S_ISLNK(info.st_mode) or not stat.S_ISREG(info.st_mode): _fail(f"{field} is not a real file: {path}")
        if require_single_link and info.st_nlink != 1: _fail(f"{field} is hard-linked: {path}")
        descriptor=os.open(os.fspath(current),os.O_RDONLY|getattr(os,"O_NOFOLLOW",0)); before=os.fstat(descriptor); chunks=bytearray()
        while True:
            block=os.read(descriptor,1024*1024)
            if not block: break
            chunks.extend(block)
        after=os.fstat(descriptor)
        if _identity_tuple(before)!=_identity_tuple(after): _fail(f"{field} changed while reading: {path}")
        return bytes(chunks)
    except FileNotFoundError: _fail(f"missing {field}: {path}")
    except OSError as error: _fail(f"cannot read {field} {path}: {error}")
    finally:
        if descriptor is not None: os.close(descriptor)

def _decode(data, field):
    try: return data.decode("utf-8")
    except UnicodeError as error: _fail(f"{field} is not UTF-8: {error}")

def _literal_assignment(text, name):
    try: tree=ast.parse(text,filename=PYTHON_BINDING_PATH)
    except SyntaxError as error: _fail(f"cannot parse {PYTHON_BINDING_PATH}: {error}")
    matches=[]
    for node in tree.body:
        if isinstance(node,ast.Assign) and any(isinstance(target,ast.Name) and target.id==name for target in node.targets): matches.append(node.value)
    if len(matches)!=1: _fail(f"expected exactly one reviewed Python {name} declaration, found {len(matches)}")
    try: return ast.literal_eval(matches[0])
    except (ValueError,TypeError) as error: _fail(f"Python {name} is not a literal declaration: {error}")

def _validate_external_declarations(policy, contract_text):
    expected_checkouts=_literal_assignment(contract_text,"FULL_EXTERNAL_CHECKOUTS")
    expected_generated=_literal_assignment(contract_text,"FULL_EXTERNAL_GENERATED")
    if not isinstance(expected_checkouts,tuple) or not isinstance(expected_generated,tuple): _fail("independent external dependency declarations must be tuples")
    if policy.get("external_checkouts")!=list(expected_checkouts): _fail("dependency policy external checkout pins differ from the independent contract")
    if policy.get("external_generated")!=list(expected_generated): _fail("dependency policy generated-external pins differ from the independent contract")

def _json_object(pairs):
    result={}
    for key,value in pairs:
        if key in result: _fail(f"SDK manifest repeats JSON key {key!r}")
        result[key]=value
    return result

def _load_sdk_manifest(raw):
    try: value=json.loads(_decode(raw,"SDK manifest"),object_pairs_hook=_json_object)
    except json.JSONDecodeError as error: _fail(f"cannot parse SDK manifest: {error}")
    if not isinstance(value,dict) or tuple(value)!=("version","archives","files") or value.get("version")!=1: _fail("SDK manifest has an unexpected declaration")
    archives=value.get("archives"); files=value.get("files")
    if not isinstance(archives,list) or not isinstance(files,list): _fail("SDK manifest archives/files must be lists")
    by_name={}
    for index,archive in enumerate(archives):
        if not isinstance(archive,dict) or tuple(archive)!=("name","url","sha256","bytes"): _fail(f"SDK manifest archive {index} has an unexpected declaration")
        name=archive["name"]
        if not isinstance(name,str) or not name or name in by_name or SHA256_RE.fullmatch(archive["sha256"]) is None or type(archive["bytes"]) is not int or archive["bytes"]<0: _fail(f"SDK manifest archive {index} is invalid")
        by_name[name]=archive
    seen=set()
    for index,record in enumerate(files):
        if not isinstance(record,dict) or tuple(record)!=("archive","member","source","bytes","sha256"): _fail(f"SDK manifest file {index} has an unexpected declaration")
        source=record["source"]
        if not isinstance(source,str) or not source.startswith("sdk-headers/") or source in seen or record["archive"] not in by_name or not isinstance(record["member"],str) or not record["member"] or type(record["bytes"]) is not int or record["bytes"]<0 or SHA256_RE.fullmatch(record["sha256"]) is None: _fail(f"SDK manifest file {index} is invalid")
        seen.add(source)
    return value,by_name

def _validate_sdk_declarations(policy, sdk_manifest_raw):
    sdk,archives=_load_sdk_manifest(sdk_manifest_raw); projects={record["source"]:record for record in policy["projects"]}; expected=set()
    for record in sdk["files"]:
        source=record["source"]; expected.add(source); dependency=projects.get(source); archive=archives[record["archive"]]
        provenance=f"sdk/{archive['sha256']}/{record['member']}"
        if dependency is None or dependency.get("provenance")!=provenance or dependency.get("bytes")!=record["bytes"] or dependency.get("sha256")!=record["sha256"]: _fail(f"SDK dependency binding mismatch: {source}")
    actual={record["source"] for record in policy["projects"] if record["source"].startswith("sdk-headers/")}
    if actual!=expected: _fail("SDK inventory does not match the dependency policy")

def _source_identity(root, source, field="authenticated dependency source"):
    pure=PurePosixPath(source)
    if pure.is_absolute() or not pure.parts or any(part in ("", ".", "..") for part in pure.parts): _fail(f"{field} is not canonical: {source!r}")
    data=_read_no_follow(Path(root).joinpath(*pure.parts),f"{field} {source}",require_single_link=True)
    return len(data),hashlib.sha256(data).hexdigest()
