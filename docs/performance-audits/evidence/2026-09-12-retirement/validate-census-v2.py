import csv, hashlib, json, sys, ctypes, tarfile, shlex
from pathlib import Path
root = Path(__file__).resolve().parent
prefix, count = sys.argv[1], int(sys.argv[2])
lib = ctypes.CDLL(str(root/'libcensus_hash.so'))
lib.buster_hash_64.argtypes = [ctypes.c_char_p, ctypes.c_uint64]
lib.buster_hash_64.restype = ctypes.c_uint64
def census_hash(data): return lib.buster_hash_64(data, len(data))
archive_override = Path(sys.argv[3]) if len(sys.argv) > 3 else None
def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda: f.read(1024*1024), b''): h.update(chunk)
    return h.hexdigest()
def table(path):
    with path.open() as f: return list(csv.DictReader(f, delimiter='\t'))
reference = None
validated = 0
for shard in range(count):
    d = root / (prefix + '-' + str(shard))
    manifest = dict(line.split('=', 1) for line in (d/'manifest.txt').read_text().splitlines())
    binaries = {name: digest(d/name) for name in ['candidate-ide.exe', 'baseline-ide.exe']}
    for name, tag in [('candidate-ide.exe','compiler'), ('baseline-ide.exe','baseline')]:
        data = (d/name).read_bytes()
        assert len(data) == int(manifest[tag+'_bytes'])
        assert census_hash(data) == int(manifest[tag+'_hash'])
    inputs, recipes = {}, {}
    for row in table(d/'inputs.tsv'):
        path = d/'inputs'/row['path']
        assert path.stat().st_size == int(row['bytes']), path
        data = path.read_bytes()
        assert census_hash(data) == int(row['buster_hash_64'])
        inputs[row['path']] = hashlib.sha256(data).hexdigest()
        recipes[row['path']] = shlex.split(row['fixture_flags'])
    identity = (binaries, inputs)
    if reference is None: reference = identity
    assert identity == reference, 'binary or frozen input mismatch'
    archive_argv, archive_objects = {}, {}
    if not (d/'groups').is_dir():
        archive_path = archive_override or root/(d.name+'.groups.tar.gz')
        with tarfile.open(archive_path,'r:gz') as archive:
            for member in archive:
                if not member.isfile(): continue
                name = member.name.removeprefix(d.name+'/') if archive_override else member.name
                if not name.startswith('groups/'): continue
                if name.endswith('.argv'):
                    archive_argv[name] = archive.extractfile(member).read()
                elif name.endswith('.o'):
                    data = archive.extractfile(member).read()
                    archive_objects[name] = (len(data), census_hash(data), hashlib.sha256(data).hexdigest())
    rows = {r['row']: r for r in table(d/'rows.tsv')}
    results = table(d/'results.tsv')
    assert len({r['row'] for r in results}) == len(results), 'duplicate execution'
    assert {r['row'] for r in results} == {k for k,v in rows.items() if v['selected']=='1'}, 'missing execution'
    for result in results:
        row = rows[result['row']]
        group = d/'groups'/row['group']
        argument_path = group/(row['allocator']+'.argv')
        raw = argument_path.read_bytes() if argument_path.is_file() else archive_argv[str(argument_path.relative_to(d))]
        assert raw.endswith(b'\0')
        argv = [x.decode() for x in raw.split(b'\0')[:-1]]
        baseline = row['allocator']=='none'
        recorded_directory = Path(argv[0]).parent
        assert recorded_directory.is_absolute() and recorded_directory.name == d.name
        assert Path(argv[0]).name == ('baseline-ide.exe' if baseline else 'candidate-ide.exe')
        assert argv[1:3] == ['cc','-c']
        assert argv[argv.index('-target')+1] == row['target']
        assert '-mcpu='+manifest['cpu'] in argv
        assert '-fregister-allocator='+row['allocator'] in argv
        assert ('-ffrontend-ssa' if row['frontend_ssa']=='1' else '-fno-frontend-ssa') in argv
        assert ('-fPIC' if row['PIC']=='1' else '-fno-pic') in argv
        assert str(recorded_directory/'inputs'/row['fixture']) in argv
        assert '-fverify-codegen' in argv
        assert ('-fno-machine-fallback' in argv) == (not baseline)
        assert ('-fcodegen-fallback-census' in argv) == (not baseline)
        expected = [str(recorded_directory/('baseline-ide.exe' if baseline else 'candidate-ide.exe')), 'cc', '-c', '-g0', '-v',
                    '-fwrapv', '-fno-strict-aliasing', '-funsigned-char', '-target', row['target'], '-mcpu='+manifest['cpu'],
                    '-fPIC' if row['PIC']=='1' else '-fno-pic', '-ffrontend-ssa' if row['frontend_ssa']=='1' else '-fno-frontend-ssa',
                    '-fregister-allocator='+row['allocator'], '-fverify-codegen', '-fmachine-fallback' if baseline else '-fno-machine-fallback',
                    '-I'+str(recorded_directory/'inputs/tests'), str(recorded_directory/'inputs'/row['fixture']), '-o',
                    str(recorded_directory/'groups'/row['group']/(row['allocator']+'.o'))] + recipes[row['fixture']]
        if not baseline: expected.append('-fcodegen-fallback-census')
        assert argv == expected, (row['row'], argv, expected)
        obj = group/(row['allocator']+'.o')
        if int(result['object_bytes']):
            if obj.is_file():
                data = obj.read_bytes()
                size, observed_hash = len(data), census_hash(data)
            else:
                size, observed_hash, _sha256 = archive_objects[str(obj.relative_to(d))]
            assert size == int(result['object_bytes']), obj
            assert observed_hash == int(result['object_hash']), obj
        if result['disposition'].startswith('strict-success'):
            assert int(result['fallbacks']) == 0 and int(result['object_bytes']) > 0
            assert result['counters_valid'] == '1' and result['function_records_valid'] == '1'
        validated += 1
report = {'rows_validated':validated,'binaries_sha256':reference[0], 'inputs_sha256':reference[1],
          'object_hashes_reconciled':True, 'limits':'Does not freeze inherited host/resource headers or environment; paired with join-census.py counter/attribution checks.'}
(root/(prefix+'-validated-v2.json')).write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({'rows_validated':validated,'binaries_sha256':reference[0], 'inputs':len(reference[1])}))
