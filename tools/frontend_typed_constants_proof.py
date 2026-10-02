#!/usr/bin/env python3
"""Temporary hosted correctness probes for #1570/#1572; never a benchmark."""

import argparse
import hashlib
import json
import re
import shlex
import subprocess
from pathlib import Path


NESTED = '''struct Index { int a; int b; };
struct Object { int values[3]; };
enum { offset = __builtin_offsetof(struct Object, values[__builtin_offsetof(struct Index, b) / sizeof(int)]) };
_Static_assert(offset == 4, "nested offsetof enum");
'''
UNSIGNED_SIZEOF = 'constexpr int value = sizeof(int) - 5;\n'
OFFSET_CONTROLS = '''enum { index = 2 };
struct Index { int a; int b; };
struct Object { int values[50]; };
enum { cast_offset = __builtin_offsetof(struct Object, values[(unsigned char)300]) };
_Static_assert(cast_offset == 176, "typed index truncation");
_Static_assert(__builtin_offsetof(struct Object, values[__builtin_offsetof(struct Index, b) / sizeof(int)]) == 4, "nested assertion");
static unsigned long long global_offset = __builtin_offsetof(struct Object, values[__builtin_offsetof(struct Index, b) / sizeof(int)]);
int main(void) {
    enum { index = 1 };
    enum { local_offset = __builtin_offsetof(struct Object, values[index]) };
    _Static_assert(local_offset == 4, "nearest scope");
    unsigned long long normal_offset = __builtin_offsetof(struct Object, values[__builtin_offsetof(struct Index, b) / sizeof(int)]);
    return global_offset != 4 || normal_offset != 4 || local_offset != 4 || cast_offset != 176;
}
'''
CONSTANT_CONTROLS = '''enum { negative = -1, unsigned_maximum = 0xffffffffU };
constexpr unsigned char octet = (unsigned char)300;
constexpr int signed_negative = negative;
constexpr long long enum_value = unsigned_maximum;
constexpr long long maximum = 9223372036854775807LL;
constexpr long long minimum = -9223372036854775807LL - 1;
_Static_assert(octet == 44 && signed_negative == -1, "conversion and signed enum");
_Static_assert(enum_value == 4294967295LL, "unsigned enum operand");
_Static_assert(maximum == 9223372036854775807LL && minimum == (-9223372036854775807LL - 1), "signed limits");
int main(void) { return octet != 44 || signed_negative != -1 || enum_value != 4294967295LL || maximum != 9223372036854775807LL || minimum != (-9223372036854775807LL - 1); }
'''
# Each row records expected candidate behavior and whether a host compiler is
# an applicable language oracle. Offset overflow refusals are Buster policy.
CASES = [
    ("nested-offsetof-enum", "gnu17", NESTED, True, True),
    ("unsigned-sizeof-to-int", "gnu23", UNSIGNED_SIZEOF, False, True),
    ("offsetof-controls", "gnu17", OFFSET_CONTROLS, True, True),
    ("constexpr-controls", "gnu23", CONSTANT_CONTROLS, True, True),
    ("constexpr-negative-unsigned", "gnu23", "constexpr unsigned int value = -1;\n", False, True),
    ("constexpr-negative-enum-unsigned", "gnu23", "enum { negative = -1 }; constexpr unsigned int value = negative;\n", False, True),
    ("constexpr-uncast-truncation", "gnu23", "constexpr unsigned char value = 300;\n", False, True),
    ("offset-negative-index", "gnu17", "struct S { int a[2]; }; enum { value = __builtin_offsetof(struct S, a[-1]) };\n", False, False),
    ("offset-index-division-zero", "gnu17", "struct S { int a[2]; }; enum { value = __builtin_offsetof(struct S, a[1 / 0]) };\n", False, False),
    ("offset-product-overflow", "gnu17", "struct S { int a[2]; }; enum { value = __builtin_offsetof(struct S, a[4611686018427387904ULL]) };\n", False, False),
    ("offset-addition-overflow", "gnu17", "struct S { char x; char a[2]; }; enum { value = __builtin_offsetof(struct S, a[18446744073709551615ULL]) };\n", False, False),
]
POLICY_CASES = [
    ("signed-overflow", "constexpr int value = 2147483647 + 1;\n"),
    ("negative-left-shift", "constexpr int value = -1 << 1;\n"),
]
TARGETS = ["x86_64-unknown-linux-gnu", "wasm32-unknown-wasi"]
OFFSET_OBSERVATIONS = [
    ("signed-negative", "int a[2];", "-1"),
    ("high-128-bit", "int a[2];", "((unsigned __int128)1 << 64)"),
    ("product-overflow", "int a[2];", "4611686018427387904ULL"),
    ("addition-overflow", "char x; char a[2];", "18446744073709551615ULL"),
]


class Proof:
    def __init__(self, args):
        self.args = args
        self.root = Path(args.source_root).resolve()
        self.ide = Path(args.ide).resolve()
        self.evidence = Path(args.evidence).resolve()
        self.label = args.label or args.phase
        self.output = self.evidence / self.label
        self.output.mkdir(parents=True, exist_ok=True)
        self.records = []
        self.binary_sha = hashlib.sha256(self.ide.read_bytes()).hexdigest()

    def run(self, label, argv, expected=None, timeout=30):
        command = [str(arg) for arg in argv]
        print(f"PROOF_COMMAND {label}: {shlex.join(command)}", flush=True)
        result = subprocess.run(command, cwd=self.root, capture_output=True, text=True, errors="replace", timeout=timeout)
        (self.output / f"{label}.stdout.txt").write_text(result.stdout)
        (self.output / f"{label}.stderr.txt").write_text(result.stderr)
        record = {"label": label, "command": command, "cwd": str(self.root), "exit_code": result.returncode, "expected_exit": expected}
        self.records.append(record)
        print(f"PROOF_RESULT {label}: exit={result.returncode} expected={expected}", flush=True)
        if result.stdout:
            print(result.stdout, end="" if result.stdout.endswith("\n") else "\n", flush=True)
        if result.stderr:
            print(result.stderr, end="" if result.stderr.endswith("\n") else "\n", flush=True)
        if result.returncode < 0 or re.search(r"AddressSanitizer|runtime error:|BUSTER_TODO|assertion.*failed", result.stderr, re.I):
            raise RuntimeError(f"{label}: crash, sanitizer or internal assertion cannot count as rejection")
        if expected is not None and result.returncode != expected:
            raise RuntimeError(f"{label}: expected exit {expected}, observed {result.returncode}")
        if expected == 1 and not result.stderr.strip():
            raise RuntimeError(f"{label}: refusal lacks a diagnostic")
        return result

    def source(self, name, text):
        path = self.output / f"{name}.c"
        path.write_text(text)
        return path

    def identities(self):
        sha = self.run("source-sha", ["git", "rev-parse", "HEAD"], 0).stdout.strip()
        tree = self.run("source-tree", ["git", "rev-parse", "HEAD^{tree}"], 0).stdout.strip()
        if sha != self.args.expected_sha or (self.args.expected_tree and tree != self.args.expected_tree):
            raise RuntimeError("source identity differs from the pinned revision")
        (self.output / "identity.json").write_text(json.dumps({"source_sha": sha, "source_tree": tree, "ide": str(self.ide), "ide_sha256": self.binary_sha, "phase": self.args.phase}, indent=2) + "\n")

    def policy_observations(self, references):
        for name, source in POLICY_CASES:
            path = self.source(name, source)
            for compiler in [str(self.ide)] + references:
                for wrap in [False, True]:
                    tag = f"policy-{name}-{Path(compiler).name}-{'wrapv' if wrap else 'default'}"
                    command = [compiler] + (["cc"] if compiler == str(self.ide) else [])
                    command += ["-std=gnu23", "-fsyntax-only"] + (["-fwrapv"] if wrap else []) + [path]
                    self.run(tag, command)

    def baseline(self):
        self.run("witness-nested-rejected", [self.ide, "cc", "-std=gnu17", "-fsyntax-only", self.source("nested-offsetof-enum", NESTED)], 1)
        self.run("witness-unsigned-wrong-accept", [self.ide, "cc", "-std=gnu23", "-fsyntax-only", self.source("unsigned-sizeof-to-int", UNSIGNED_SIZEOF)], 0)
        self.policy_observations(self.references())
        self.offset_observations()

    def offset_observations(self):
        # A syntax-only acceptance is a pending semantic validation finding;
        # candidate native object emission must still refuse every bad index.
        for name, members, expression in OFFSET_OBSERVATIONS:
            for context in ["static", "runtime"]:
                value = f"__builtin_offsetof(struct S, a[{expression}])"
                source = f"struct S {{ {members} }};\n"
                source += f"static unsigned long long value = {value};\n" if context == "static" else f"unsigned long long f(void) {{ return {value}; }}\n"
                tag = f"observe-offset-{name}-{context}"
                path = self.source(tag, source)
                self.run(f"{tag}-syntax", [self.ide, "cc", "-std=gnu17", "-fsyntax-only", path])
                if self.args.phase == "candidate":
                    self.run(f"{tag}-object", [self.ide, "cc", "-std=gnu17", "-c", path, "-o", self.output / f"{tag}.o"], 1)

    def references(self):
        feature = self.source("reference-constexpr-feature", "constexpr int value = 1; _Static_assert(value == 1, \"feature\");\n")
        constexpr_compilers = []
        for compiler in ["clang", "gcc"]:
            self.run(f"reference-{compiler}-version", [compiler, "--version"], 0)
            result = self.run(f"reference-{compiler}-constexpr-feature", [compiler, "-std=gnu23", "-fsyntax-only", feature])
            if result.returncode == 0:
                constexpr_compilers.append(compiler)
            elif result.returncode != 1 or not re.search(r"constexpr|gnu23|gnu2x", result.stderr):
                raise RuntimeError(f"{compiler}: unexpected reference feature failure")
            else:
                print(f"PROOF_UNAVAILABLE {compiler}: C23 constexpr oracle unsupported", flush=True)
        return constexpr_compilers

    def candidate(self):
        references = self.references()
        for name, dialect, source, accepted, oracle in CASES:
            path = self.source(name, source)
            for target in TARGETS:
                for standard in [dialect, dialect.replace("gnu", "c")]:
                    tag = f"{name}-{target}-{standard}"
                    self.run(tag, [self.ide, "cc", f"--target={target}", f"-std={standard}", "-fsyntax-only", path], 0 if accepted else 1)
            if oracle:
                for compiler in (["clang", "gcc"] if dialect == "gnu17" else references):
                    self.run(f"oracle-{name}-{compiler}", [compiler, f"-std={dialect}", "-fsyntax-only", path], 0 if accepted else 1)
        for name, source, dialect in [("offsetof", OFFSET_CONTROLS, "gnu17"), ("constexpr", CONSTANT_CONTROLS, "gnu23")]:
            path = self.source(f"runtime-{name}", source)
            for mode in ["none", "mir-stack", "fast", "quality"]:
                for form in ["-ffrontend-ssa", "-fno-frontend-ssa"]:
                    tag = f"runtime-{name}-{mode}-{form[2:]}"
                    binary = self.output / tag
                    command = [self.ide, "cc", f"-std={dialect}", form, f"-fregister-allocator={mode}", "-fverify-codegen"]
                    if mode != "none":
                        command.append("-fno-machine-fallback")
                    compiled = self.run(f"{tag}-build", command + [path, "-o", binary], 0)
                    if "CODEGEN_VERIFY" not in compiled.stdout:
                        raise RuntimeError(f"{tag}: no verification evidence")
                    self.run(f"{tag}-execute", [binary], 0)
            for compiler in (["clang", "gcc"] if dialect == "gnu17" else references):
                binary = self.output / f"oracle-runtime-{name}-{compiler}"
                self.run(f"oracle-runtime-{name}-{compiler}-build", [compiler, f"-std={dialect}", path, "-o", binary], 0)
                self.run(f"oracle-runtime-{name}-{compiler}-execute", [binary], 0)
        self.policy_observations(references)
        self.offset_observations()

    def module(self):
        # Reuse the generated test_units environment, including its authoritative
        # fatal sanitizer policy, then add only the documented module selector.
        tree = Path(self.args.build_directory).resolve()
        result = self.run("generated-test-command", ["ninja", "-C", tree, "-f", f"build-{self.args.config}.ninja", "-t", "commands", "test_units"], 0)
        launch = None
        for line in result.stdout.splitlines():
            words = shlex.split(line)
            for index, word in enumerate(words):
                if Path(word).name == "cmake" and words[index + 1:index + 3] == ["-E", "env"] and str(self.ide) in words[index + 3:]:
                    end = words.index(str(self.ide), index + 3)
                    if launch is not None:
                        raise RuntimeError("ambiguous generated test launch")
                    launch = words[index:end + 1]
        if launch is None:
            raise RuntimeError("generated test_units launch was not found")
        if self.args.require_fatal_sanitizers:
            policy = " ".join(launch)
            if "BUSTER_SANITIZER_POLICY_MODE=fatal" not in launch or "ASAN_OPTIONS=halt_on_error=1" not in policy or "UBSAN_OPTIONS=halt_on_error=1:exitcode=87" not in policy:
                raise RuntimeError("generated sanitizer launch does not preserve fatal policy")
        result = self.run("c_frontend_tests", launch + ["test", "--module=c_frontend_tests", "--verbose=1", "--ci=1"], 0, timeout=1200)
        summary = re.search(r"\[(\d+)/(\d+)\] Unit tests \(1 of \d+ modules selected\)", result.stdout)
        if summary is None or summary.group(1) != summary.group(2) or int(summary.group(1)) == 0:
            raise RuntimeError("missing successful nonempty single-module summary")

    def finish(self):
        current = hashlib.sha256(self.ide.read_bytes()).hexdigest()
        if current != self.binary_sha:
            raise RuntimeError("compiler artifact changed during probes")
        (self.output / "records.json").write_text(json.dumps(self.records, indent=2) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--phase", required=True, choices=["baseline", "candidate", "module"])
    parser.add_argument("--source-root", required=True)
    parser.add_argument("--expected-sha", required=True)
    parser.add_argument("--expected-tree")
    parser.add_argument("--ide", required=True)
    parser.add_argument("--evidence", required=True)
    parser.add_argument("--label")
    parser.add_argument("--build-directory")
    parser.add_argument("--config", default="Release")
    parser.add_argument("--require-fatal-sanitizers", action="store_true")
    args = parser.parse_args()
    proof = Proof(args)
    try:
        proof.identities()
        getattr(proof, args.phase)()
    finally:
        proof.finish()
    print(f"FRONTEND_TYPED_CONSTANTS_PROOF phase={args.phase} label={proof.label} result=pass commands={len(proof.records)}", flush=True)


if __name__ == "__main__":
    main()
