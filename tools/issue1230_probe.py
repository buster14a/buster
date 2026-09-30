import ast, hashlib, json, pathlib, re, subprocess, sys
base, candidate, out = map(pathlib.Path, sys.argv[1:])
out.mkdir(parents=True, exist_ok=True)
def run(argv, **kw):
    r = subprocess.run(list(map(str, argv)), capture_output=True, text=True, **kw)
    print(json.dumps({"command": list(map(str, argv)), "exit": r.returncode, "stdout": r.stdout, "stderr": r.stderr}), flush=True)
    return r
text = (candidate / "src/buster/tests/compiler/driver/driver_test.c").read_text()
block = text.split("UnitTestResult compiler_driver_test_static_pointer_addresses(", 1)[1].split("String8 source = S8(", 1)[1].split(");\n    String8 forms", 1)[0]
source = "".join(ast.literal_eval(x) for x in re.findall(r'"(?:\\.|[^"\\])*"', block))
fixture = out / "family.c"
fixture.write_text(source)
for cc in ["gcc", "clang"]:
    assert run([cc, "--version"]).returncode == 0
    for dialect in ["c11", "gnu17"]:
        binary = out / (cc + "-" + dialect)
        flags = ["-std=" + dialect, "-Wall", "-Wextra"] + (["-pedantic-errors"] if dialect == "c11" else [])
        assert run([cc, *flags, fixture, "-o", binary]).returncode == 0
        assert run([binary]).returncode == 0
witnesses = {
 "cast": "int x;char*p=(char*)&x+1;int main(void){return p!=&((char*)&x)[1];}\n",
 "negative-int": "int arr[8];int*p=&(arr+3)[-1];int main(void){return p!=&arr[2];}\n",
 "negative-long": "int arr[8];int*p=&(arr+3)[-1L];int main(void){return p!=&arr[2];}\n",
}
ide_base = base / "build/Release/ide"
ide_fixed = candidate / "build/Release/ide"
for label, ide in [("baseline", ide_base), ("candidate", ide_fixed)]:
    print(json.dumps({"compiler":label, "sha256":hashlib.sha256(ide.read_bytes()).hexdigest()}))
    for form in ["-ffrontend-ssa", "-fno-frontend-ssa"]:
        for mode in ["none", "mir-stack", "fast", "quality"]:
            for name, body in witnesses.items():
                path=out/(name+".c"); path.write_text(body)
                binary=out/(label+"-"+name+"-"+form[1:]+"-"+mode)
                r=run([ide,"cc","-std=c11",form,"-fregister-allocator="+mode,"-fverify-codegen","-fno-codegen-fallback",path,"-o",binary])
                if label == "baseline" and name == "negative-long":
                    assert r.returncode != 0 and ("static initializer" in r.stderr or "global initializer" in r.stderr), r
                else:
                    assert r.returncode == 0, r
                    assert run([binary]).returncode == (1 if label == "baseline" else 0)
expected={"S1":("x",1),"S2":("arr",32),"S3":("o",0),"negative_int":("arr",8),"negative_long":("arr",8),"negative_row":("m",16)}
for dialect in ["c11", "gnu17"]:
    for form in ["-ffrontend-ssa", "-fno-frontend-ssa"]:
        for mode in ["none", "mir-stack", "fast", "quality"]:
            key=dialect+"-"+form[1:]+"-"+mode
            binary=out/key
            flags=["-std="+dialect,form,"-fregister-allocator="+mode,"-fverify-codegen","-fno-codegen-fallback"]
            assert run([ide_fixed,"cc",*flags,fixture,"-o",binary]).returncode == 0
            assert run([binary]).returncode == 0
            obj=out/(key+".o")
            assert run([ide_fixed,"cc",*flags,"-g0","-c",fixture,"-o",obj]).returncode == 0
            symbols=run(["readelf","-sW",obj]); relocs=run(["readelf","-Wr",obj])
            assert symbols.returncode == 0 and relocs.returncode == 0
            names={}
            for line in symbols.stdout.splitlines():
                m=re.match(r"\s*\d+:\s+([0-9a-fA-F]+)\s+\d+\s+\S+\s+\S+\s+\S+\s+(\d+)\s+(\S+)",line)
                if m: names[m[3]]=(int(m[1],16),m[2])
            for pointer,(target,addend) in expected.items():
                address=names[pointer][0]
                matches=[line for line in relocs.stdout.splitlines() if re.match(r"\s*0*"+format(address,"x")+r"\s",line) and re.search(r"\b"+re.escape(target)+r"\s+\+\s+0*"+format(addend,"x")+r"\s*$",line)]
                assert matches,(pointer,address,target,addend,relocs.stdout)
print("ISSUE1230_PROBE_PASS candidate_family_cells=16 candidate_address_comparisons=568 relocation_checks=96 baseline_witness_cells=24 candidate_witness_cells=24 reference_cells=4")
