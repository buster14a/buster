#!/usr/bin/env python3
"""Rebuild the checked-in A-profile AArch64 system-register projection.

The Arm XML archive is intentionally not vendored.  Invoke this developer
tool with an extracted official release directory and an output directory:

    python3 tools/generate_aarch64_system_registers.py SOURCE OUTPUT
    python3 tools/generate_aarch64_system_registers.py --check SOURCE
    python3 tools/generate_aarch64_system_registers.py --self-test

``--check`` writes a temporary projection and compares all three artifacts
with the checked-in files.  It never mutates the repository.
"""
from pathlib import Path
from xml.etree import ElementTree as ET
import argparse, hashlib, json, re, subprocess, sys, tempfile

MECH={'MRS':1,'MSRregister':2,'MRRS':3,'MSRRregister':4}

# The checked-in projection is pinned to the official 2026-06 A-profile
# release.  Rejecting both a truncated source tree and an unexpected release
# keeps deterministic artifacts from being generated from incomplete input.
EXPECTED_SOURCE_XML_FILES = 1943
EXPECTED_RELEVANT_MECHANISMS = 1400
EXPECTED_ACCEPTED_MECHANISMS = 402
EXPECTED_FIXED_ROWS = 392
EXPECTED_PARAMETERIZED_MECHANISMS = 10
EXPECTED_SOURCE_PARAMETERIZED_ROWS = 8
EXPECTED_RAW_S3_ROWS = 2
EXPECTED_FIXED_TARGET_NAMES = 202
EXPECTED_FIXED_ENCODINGS = 201
EXPECTED_READABLE_FIXED_NAMES = 200
EXPECTED_WRITABLE_FIXED_NAMES = 138
EXPECTED_BOTH_FIXED_NAMES = 136

# Stable, compact decision codes used by the normalized JSONL inventory.  The
# source predicates remain available for audit through feature_digest; raw XML
# condition/accessor prose is deliberately not copied into checked-in rows.
REASON_ACCEPTED = 0
REASON_FEATURE_FALSE = 1
REASON_ENCODING_UNRESOLVED = 2
REASON_FEATURE_FALSE_AND_ENCODING_UNRESOLVED = 3

ACCESS_CONDITION_NONE = 0
ACCESS_CONDITION_VHE = 1
ACCESS_CONDITION_D128 = 2
ACCESS_CONDITION_SRMASK = 3
ACCESS_CONDITION_SYSREG128 = 4
ACCESS_CONDITION_UNKNOWN = 5
ACCESS_CONDITION_LABELS = {
 ACCESS_CONDITION_NONE:'none',
 ACCESS_CONDITION_VHE:'feat_vhe',
 ACCESS_CONDITION_D128:'feat_d128',
 ACCESS_CONDITION_SRMASK:'feat_srmask',
 ACCESS_CONDITION_SYSREG128:'feat_sysreg128',
 ACCESS_CONDITION_UNKNOWN:'unknown',
}

def evaluator_self_test():
    cases = {
        'when FEAT_AA64 is implemented or UNKNOWN is implemented': True,
        'when UNKNOWN is implemented and FEAT_AA64 is implemented': False,
        'when (FEAT_AA64 is implemented or UNKNOWN is implemented) and FEAT_PAuth is implemented': True,
        'when FEAT_AA64 is not implemented or FEAT_PAuth is implemented': True,
        'when FEAT_AA64 is implemented and FEAT_PAuth is not implemented': False,
        'when FEAT_AA64 is implemented or FEAT_PAuth is implemented and UNKNOWN is implemented': True,
        'when (FEAT_AA64 is implemented) and (FEAT_D128 is implemented)': False,
        'when (FEAT_AA64 is implemented or UNKNOWN is implemented) and (FEAT_VHE is implemented)': True,
    }
    for expression, expected in cases.items():
        actual = evalc(expression)
        if actual != expected:
            raise SystemExit('evaluator self-test failed: %s -> %s (expected %s)' % (expression, actual, expected))
    print('AArch64 SysReg evaluator self-test: %d cases passed' % len(cases))

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--check', action='store_true', help='generate in a temporary directory and compare checked-in artifacts')
parser.add_argument('--self-test', action='store_true', help='run parser/evaluator unit tests and exit')
parser.add_argument('--mutation-test', action='store_true', help='mutate one name, encoding, and condition in isolated XML trees')
parser.add_argument('source', nargs='?', help='extracted official Arm SysReg XML directory')
parser.add_argument('output', nargs='?', help='generated output directory (default: checked-in generated directory)')
args = parser.parse_args()
if args.self_test and not args.mutation_test:
    # evalc is defined below; defer execution until the parser is initialized.
    _run_self_test = True
else:
    _run_self_test = False
if not _run_self_test and not args.source:
    parser.error('SOURCE is required unless --self-test is used')

root = Path(args.source).resolve() if args.source else Path('.')
if not root.is_dir() and not _run_self_test:
    parser.error('SOURCE is not a directory: %s' % root)
_repo_root = Path(__file__).resolve().parent.parent
_checked_in = _repo_root / 'src/buster/lib/compiler/assembly/generated'
_temporary_output = None
if args.check:
    _temporary_output = tempfile.TemporaryDirectory(prefix='a64-sysreg-')
    # Check mode is intentionally non-mutating even if a caller supplies an
    # output positional argument; compare a fresh temporary projection with
    # the checked-in artifacts instead.
    out = Path(_temporary_output.name)
else:
    out = Path(args.output).resolve() if args.output else _checked_in
out.mkdir(parents=True, exist_ok=True)
# Hardware-grounded Apple M1 A-profile. Keep target closure explicit and fail
# closed for unknown atoms.
TRUE={
'FEAT_AA64','EL1','EL2','FEAT_VHE','FEAT_AES','FEAT_AdvSIMD','FEAT_CRC32','FEAT_DIT','FEAT_DotProd','FEAT_FCMA','FEAT_FHM','FEAT_FP','FEAT_FP16','FEAT_FRINTTS','FEAT_FlagM','FEAT_FlagM2','FEAT_JSCVT','FEAT_LOR','FEAT_LRCPC','FEAT_LRCPC2','FEAT_LSE','FEAT_PAuth','FEAT_RDM','FEAT_SB','FEAT_SHA1','FEAT_SHA256','FEAT_SHA3','FEAT_SHA512','FEAT_SPECRES','FEAT_SSBS','FEAT_RAS','FEAT_PAN','FEAT_UAO','FEAT_TLBIOS','FEAT_TLBIRANGE','FEAT_DPB',
}
# parser modeled after research; unknown feature/implementation atoms false.
def normalize(c):
 c=(c or '').strip()
 if c.lower().startswith('when '): c=c[5:]
 c=re.sub(r'\bthe highest implemented Exception level is EL([123])\b',r'EL\1',c,flags=re.I)
 # Research treats all explicitly asserted ELs as available for compile-time subtraction.
 c=re.sub(r'\bSecure EL1 is implemented\b','SECUREEL1',c,flags=re.I)
 c=re.sub(r'\ban implementation implements TRCIMSPEC<n>\b','TRCIMSPEC',c,flags=re.I)
 c=re.sub(r'\bSystem register access to the trace unit registers is implemented\b','TRACEACCESS',c,flags=re.I)
 c=re.sub(r'\b([A-Za-z][A-Za-z0-9_]*) is not implemented\b',r'!\1',c,flags=re.I)
 c=re.sub(r'\b([A-Za-z][A-Za-z0-9_]*) is implemented\b',r'\1',c,flags=re.I)
 # values are not compile-time profile facts; unknown-fail-closed
 c=re.sub(r"\b(?:TRCIDR|MPAMIDR|UInt)\([^)]*\)(?:\.[A-Za-z0-9_]+)?\s*(?:!=|==|>|<|>=|<=)\s*'?[0-9x]+'?",'VALUE',c,flags=re.I)
 c=re.sub(r"\b(?:TRCIDR|MPAMIDR)\d+\.[A-Za-z0-9_]+\s*(?:!=|==|>|<|>=|<=)\s*'?[0-9x]+'?",'VALUE',c,flags=re.I)
 c=re.sub(r',\s*and\b',' and',c,flags=re.I); c=c.replace(',',' and ')
 c=re.sub(r'\band\b','&',c,flags=re.I); c=re.sub(r'\bor\b','|',c,flags=re.I)
 c=re.sub(r'[^A-Za-z0-9_()!&| ]',' ',c)
 return re.findall(r'[A-Za-z_][A-Za-z0-9_]*|[()!&|]',c)
def evalc(c):
 toks=normalize(c)
 if not toks:return True
 i=0;supp={x.upper() for x in TRUE}
 def atom():
  nonlocal i
  if i>=len(toks):raise ValueError()
  t=toks[i]
  if t=='(':
   i+=1;v=ore()
   if i>=len(toks) or toks[i]!=')':raise ValueError()
   i+=1;return v
  if t in ('!','&','|',')'):raise ValueError()
  i+=1;return t.upper() in supp
 def not_():
  nonlocal i
  if i<len(toks) and toks[i]=='!':i+=1;return not not_()
  return atom()
 def and_():
  nonlocal i
  v=not_()
  while i<len(toks) and toks[i]=='&':i+=1;rhs=not_();v=v and rhs
  return v
 def ore():
  nonlocal i
  v=and_()
  while i<len(toks) and toks[i]=='|':i+=1;rhs=and_();v=v or rhs
  return v
 try:v=ore();return bool(v and i==len(toks))
 except:return False

if _run_self_test:
    evaluator_self_test()
    raise SystemExit(0)

def fnv(b):
 h=0xcbf29ce484222325
 for x in b:h=((h^x)*0x100000001b3)&((1<<64)-1)
 return h

def digest64(data):
 return int.from_bytes(hashlib.sha256(data).digest()[:8], 'big')

def esc(s):
 out='\"'
 for c in s:
  o=ord(c)
  if c=='\\': out+='\\\\'
  elif c=='\"': out+='\\\"'
  elif o==0: out+='\\000'
  elif o==10: out+='\\n'
  elif o==13: out+='\\r'
  elif o==9: out+='\\t'
  elif o<32 or o>=127: out+='\\%03o'%o
  else: out+=c
 return out+'\"'
def txt(el, path):
 e=el.find(path);return '' if e is None or e.text is None else ' '.join(e.text.split())
def mixed_text(el):
 """Normalize text and nested XML content without joining token boundaries."""
 if el is None:return ''
 return ' '.join(part.strip() for part in el.itertext() if part.strip())
def access_condition_code(text):
 upper=text.upper()
 if not text:return ACCESS_CONDITION_NONE
 if 'FEAT_D128' in upper:return ACCESS_CONDITION_D128
 if 'FEAT_SRMASK' in upper:return ACCESS_CONDITION_SRMASK
 if 'FEAT_SYSREG128' in upper:return ACCESS_CONDITION_SYSREG128
 if re.fullmatch(r'WHEN\s+FEAT_VHE\s+IS\s+IMPLEMENTED', text, flags=re.I): return ACCESS_CONDITION_VHE
 return ACCESS_CONDITION_UNKNOWN
def parsebin(v):
 if not v:return None
 if re.fullmatch(r'0b[01]+',v):return int(v[2:],2)
 m=re.fullmatch(r'([A-Za-z]+)\[([0-9]+):([0-9]+)\]',v)
 return None

def parameter_bits(expression, index):
 """Evaluate one XML mixed fixed/variable bit expression for an index."""
 if not expression: return None
 value = 0
 width = 0
 parts=[]; start=0; depth=0
 for position, character in enumerate(expression):
  if character == '[': depth += 1
  elif character == ']': depth -= 1
  elif character == ':' and depth == 0:
   parts.append(expression[start:position]); start=position + 1
 parts.append(expression[start:])
 for part in parts:
  part = part.strip()
  if re.fullmatch(r'0b[01]+', part):
   bits = part[2:]; piece = int(bits, 2); piece_width = len(bits)
  else:
   match = re.fullmatch(r'[A-Za-z][A-Za-z0-9_]*\[([0-9]+)(?::([0-9]+))?\]', part)
   if not match: return None
   high = int(match.group(1)); low = int(match.group(2) or high)
   if high < low: return None
   piece_width = high - low + 1
   piece = (index >> low) & ((1 << piece_width) - 1)
  value = (value << piece_width) | piece
  width += piece_width
 return value

def encoding_values(enc, start, end, raw_s3):
 """Return fixed/parameterized packed encodings, or None when unresolved."""
 if raw_s3:
  # The generic S3 family deliberately leaves all fields symbolic; its
  # bounded parser/formatter supplies the runtime transform.
  return []
 result = []
 for index in range(start, end + 1):
  fields = []
  for key in ('op0', 'op1', 'CRn', 'CRm', 'op2'):
   value = parameter_bits(enc.get(key, ''), index)
   if value is None: return None
   fields.append(value)
  result.append((fields[0] << 14) | (fields[1] << 11) | (fields[2] << 7) | (fields[3] << 3) | fields[4])
 return result

def normalize_binding_spelling(text):
 return re.sub(r'<[A-Za-z][A-Za-z0-9_]*>', '<>', text)

source_xml_files = sorted(root.glob('*.xml'))
if len(source_xml_files) != EXPECTED_SOURCE_XML_FILES:
 raise SystemExit('AArch64 SysReg source XML inventory mismatch: found %d files (expected %d)' % (len(source_xml_files), EXPECTED_SOURCE_XML_FILES))
allrows=[]
for p in source_xml_files:
 try:r=ET.parse(p).getroot()
 except Exception as e: raise RuntimeError((p,e))
 for reg in r.findall('.//register'):
  if reg.attrib.get('execution_state')!='AArch64':continue
  name=(reg.findtext('reg_short_name') or '').strip()
  # itertext() retains mixed XML prose/markup while normalizing whitespace;
  # direct .text would silently lose nested relation elements.
  register_condition=' '.join(mixed_text(x) for x in reg.findall('./reg_condition'))
  arr=reg.find('./reg_array'); astart=txt(arr,'./reg_array_start') if arr is not None else ''; aend=txt(arr,'./reg_array_end') if arr is not None else ''
  try:astarti=int(astart) if astart else 0;aendi=int(aend) if aend else 0
  except:astarti=aendi=0
  for am in reg.findall('.//access_mechanism'):
   acc=am.attrib.get('accessor',''); typ=acc.split(' ',1)[0]
   if typ not in MECH:continue
   target=acc.split(' ',1)[1] if ' ' in acc else ''
   enc={e.attrib.get('n',''):e.attrib.get('v','') for e in am.findall('./encoding/enc')}
   access_array=am.find('./encoding/acc_array')
   if access_array is not None:
    access_range=txt(access_array, './acc_array_range')
    range_match=re.fullmatch(r'([0-9]+)\s*-\s*([0-9]+)', access_range)
    if range_match:
     astarti=int(range_match.group(1)); aendi=int(range_match.group(2))
   vals=[];valid=True
   for k,bits in [('op0',2),('op1',3),('CRn',4),('CRm',4),('op2',3)]:
    z=parsebin(enc.get(k,''));
    if z is None:valid=False;break
    vals.append(z)
   raw_s3=name.startswith('S3_')
   parameterized=('<' in name)
   transforms=encoding_values(enc, astarti, aendi, raw_s3) if parameterized else None
   if parameterized:
    valid = transforms is not None or raw_s3
    packed = transforms[0] if transforms else 0
   else:
    packed=((vals[0]<<14)|(vals[1]<<11)|(vals[2]<<7)|(vals[3]<<3)|vals[4]) if valid else 0
   access_condition=' '.join(mixed_text(x) for x in am.findall('./access_condition'))
   access_code=access_condition_code(access_condition)
   # An access condition is an additional compile-time gate on the mechanism,
   # distinct from the register's condition.  Preserve both in one explicit
   # conjunction so feature digests cover the exact mechanism predicate.
   if access_condition:
    register_expr = re.sub(r'^when\s+', '', register_condition, flags=re.I)
    access_expr = re.sub(r'^when\s+', '', access_condition, flags=re.I)
    if register_expr and access_expr:
     cond = 'when (%s) and (%s)' % (register_expr, access_expr)
    else:
     cond = 'when %s' % (register_expr or access_expr)
   else:
    cond = register_condition
   access_text=' '.join(mixed_text(x) for x in am.findall('./access_permission'))
   source_bytes=p.read_bytes()
   row={'name':name,'target':target,'condition':cond,'register_condition':register_condition,'access_condition':access_condition,'access_condition_code':access_code,'feature_profile_id':1,'feature_digest':digest64(cond.encode()),'access_condition_digest':digest64(access_condition.encode()) if access_condition else 0,'source_file':p.name,'accessor':acc,'mechanism':typ,'mode':('read' if typ in ('MRS','MRRS') else 'write'),'packed':packed,'valid_encoding':valid,'parameter_encodings':transforms or [],'array_start':astarti,'array_end':aendi,'source_digest':digest64(source_bytes),'access_digest':digest64(access_text.encode()),'parameterized':parameterized,'raw_s3':raw_s3,'accessor_alias':normalize_binding_spelling(name) != normalize_binding_spelling(target)}
   register_feature_ok=evalc(register_condition)
   access_feature_ok=evalc(access_condition) if access_condition else True
   feature_ok=register_feature_ok and access_feature_ok
   row['accepted']=feature_ok and valid
   row['register_eligible']=register_feature_ok and valid
   if row['accepted']:
    row['reason_code']=REASON_ACCEPTED
   elif not feature_ok and not valid:
    row['reason_code']=REASON_FEATURE_FALSE_AND_ENCODING_UNRESOLVED
   elif not feature_ok:
    row['reason_code']=REASON_FEATURE_FALSE
   else:
    row['reason_code']=REASON_ENCODING_UNRESOLVED
   allrows.append(row)
if len(allrows) != EXPECTED_RELEVANT_MECHANISMS:
 raise SystemExit('AArch64 SysReg relevant mechanism inventory mismatch: found %d rows (expected %d)' % (len(allrows), EXPECTED_RELEVANT_MECHANISMS))
# deterministic ordering by target/name, packed, mode, source
allrows.sort(key=lambda x:(x['target'],x['name'],x['packed'],x['mechanism'],x['source_file']))
sel=[x for x in allrows if x['accepted']]

# Keep the importer fail-closed even when a future Arm release changes its
# inventory.  Accepted rows must be completely representable by the packed
# runtime tables; a sentinel 0xffff is never a valid transform.  Each
# parameterized family is materialized for every XML-declared index and its
# transforms are one-to-one, while the generic S3 family intentionally uses
# the bounded runtime parser instead of precomputed transforms.
if not allrows:
 raise SystemExit('AArch64 SysReg source contains no relevant mechanisms')
for row in allrows:
 if row['accepted'] != (row['reason_code'] == REASON_ACCEPTED):
  raise SystemExit('AArch64 SysReg decision code mismatch for %s' % row['name'])
 if row['accepted'] and row['packed'] == 0xffff:
  raise SystemExit('AArch64 SysReg accepted row uses reserved 0xffff encoding: %s' % row['name'])
 if not row['accepted']:
  continue
 if row['raw_s3']:
  if not row['parameterized'] or row['parameter_encodings'] or row['packed'] != 0:
   raise SystemExit('AArch64 SysReg raw S3 row is not parser-backed: %s' % row['name'])
 elif row['parameterized']:
  expected_count = row['array_end'] - row['array_start'] + 1
  transforms = row['parameter_encodings']
  if expected_count <= 0 or len(transforms) != expected_count or len(set(transforms)) != len(transforms):
   raise SystemExit('AArch64 SysReg parameter transform invariant failed: %s' % row['name'])
  if any(value == 0xffff for value in transforms):
   raise SystemExit('AArch64 SysReg parameter transform uses reserved 0xffff: %s' % row['name'])
  if any(value > 0xffff or ((value >> 14) & 3) < 2 for value in transforms):
   raise SystemExit('AArch64 SysReg parameter transform is outside MRS/MSR encoding space: %s' % row['name'])
 elif not row['valid_encoding']:
  raise SystemExit('AArch64 SysReg accepted row has unresolved fixed encoding: %s' % row['name'])
 elif row['packed'] > 0xffff or ((row['packed'] >> 14) & 3) < 2:
  raise SystemExit('AArch64 SysReg fixed encoding is outside MRS/MSR encoding space: %s' % row['name'])
print('all',len(allrows),'sel',len(sel),'fixed',sum(not x['parameterized'] for x in sel if not x['raw_s3']),'param',sum(x['parameterized'] for x in sel if not x['raw_s3']),'s3',sum(x['raw_s3'] for x in sel))
print('names',len(set(x['target'] for x in sel if not x['parameterized'] and not x['raw_s3'])),'enc',len(set(x['packed'] for x in sel if not x['parameterized'] and not x['raw_s3'])))
# intern strings
strings=[]; offsets={}; pool=bytearray()
def intern(s):
 if s in offsets:return offsets[s]
 off=len(pool);pool.extend(s.encode());pool.append(0); offsets[s]=off;strings.append(s);return off
for x in sel:
 for k in ('name','target','source_file','accessor'):intern(x[k])
# Flatten each accepted parameterized family's index transform into a compact
# table.  Rows retain offset/count so runtime expansion never re-parses prose.
parameter_encoding_values=[]
for x in sel:
 x['parameter_encoding_offset']=len(parameter_encoding_values)
 x['parameter_encoding_count']=len(x['parameter_encodings'])
 parameter_encoding_values.extend(x['parameter_encodings'])
# output header
accepted_fixed = sum(not x['parameterized'] and not x['raw_s3'] for x in sel)
accepted_parameterized = sum(x['parameterized'] for x in sel)
accepted_named_parameterized = sum(x['parameterized'] and not x['raw_s3'] for x in sel)
accepted_raw_s3 = sum(x['raw_s3'] for x in sel)
fixed_targets = {x['target'] for x in sel if not x['parameterized'] and not x['raw_s3']}
fixed_encodings = {x['packed'] for x in sel if not x['parameterized'] and not x['raw_s3']}
fixed_modes = {name: {x['mode'] for x in sel if not x['parameterized'] and not x['raw_s3'] and x['target'] == name} for name in fixed_targets}
h_readable = sum('read' in modes for modes in fixed_modes.values())
h_writable = sum('write' in modes for modes in fixed_modes.values())
h_both = sum(modes == {'read', 'write'} for modes in fixed_modes.values())
profile_census = {
 'accepted_mechanisms': len(sel),
 'fixed_rows': accepted_fixed,
 'parameterized_mechanisms': accepted_parameterized,
 'source_parameterized_rows': accepted_named_parameterized,
 'raw_s3_rows': accepted_raw_s3,
 'fixed_target_names': len(fixed_targets),
 'fixed_encodings': len(fixed_encodings),
 'readable_fixed_names': h_readable,
 'writable_fixed_names': h_writable,
 'both_fixed_names': h_both,
}
expected_profile_census = {
 'accepted_mechanisms': EXPECTED_ACCEPTED_MECHANISMS,
 'fixed_rows': EXPECTED_FIXED_ROWS,
 'parameterized_mechanisms': EXPECTED_PARAMETERIZED_MECHANISMS,
 'source_parameterized_rows': EXPECTED_SOURCE_PARAMETERIZED_ROWS,
 'raw_s3_rows': EXPECTED_RAW_S3_ROWS,
 'fixed_target_names': EXPECTED_FIXED_TARGET_NAMES,
 'fixed_encodings': EXPECTED_FIXED_ENCODINGS,
 'readable_fixed_names': EXPECTED_READABLE_FIXED_NAMES,
 'writable_fixed_names': EXPECTED_WRITABLE_FIXED_NAMES,
 'both_fixed_names': EXPECTED_BOTH_FIXED_NAMES,
}
if profile_census != expected_profile_census:
 raise SystemExit('AArch64 SysReg Apple profile census drifted: %s (expected %s)' % (profile_census, expected_profile_census))
h=[]
h.append('/* Generated from official Arm A-profile SysReg XML 2026-06. Do not edit. */')
h.append('#pragma once\n#include <buster/lib/base.h>')
h.append('#define BUSTER_A64_SYSREG_GENERATED_SCHEMA_VERSION 2u')
h.append('#define BUSTER_A64_SYSREG_RELEVANT_MECHANISM_COUNT 1400u')
h.append('#define BUSTER_A64_SYSREG_ACCEPTED_MECHANISM_COUNT %du' % len(sel))
h.append('#define BUSTER_A64_SYSREG_FIXED_COUNT %du' % accepted_fixed)
h.append('#define BUSTER_A64_SYSREG_PARAMETERIZED_COUNT %du' % accepted_parameterized)
h.append('#define BUSTER_A64_SYSREG_SOURCE_PARAMETERIZED_ROW_COUNT %du' % accepted_named_parameterized)
h.append('#define BUSTER_A64_SYSREG_SOURCE_RAW_S3_ROW_COUNT %du' % accepted_raw_s3)
h.append('#define BUSTER_A64_SYSREG_FIXED_TARGET_NAME_COUNT %du' % len(fixed_targets))
h.append('#define BUSTER_A64_SYSREG_FIXED_ENCODING_COUNT %du' % len(fixed_encodings))
h.append('#define BUSTER_A64_SYSREG_READABLE_FIXED_NAME_COUNT %du' % h_readable)
h.append('#define BUSTER_A64_SYSREG_WRITABLE_FIXED_NAME_COUNT %du' % h_writable)
h.append('#define BUSTER_A64_SYSREG_BOTH_FIXED_NAME_COUNT %du' % h_both)
h.append('typedef struct BusterA64SysregGeneratedRow BusterA64SysregGeneratedRow;\nstruct BusterA64SysregGeneratedRow { u32 name_offset; u32 target_offset; u32 feature_profile_id; u32 source_file_offset; u32 accessor_offset; u16 packed_encoding; u8 mechanism; u8 flags; u16 array_start; u16 array_end; u32 parameter_encoding_offset; u32 parameter_encoding_count; u64 feature_digest; u64 source_digest; u64 access_digest; };')
h.append('#define BUSTER_A64_SYSREG_ROW_FLAG_PARAMETERIZED 1u\n#define BUSTER_A64_SYSREG_ROW_FLAG_ALIAS 2u\n#define BUSTER_A64_SYSREG_ROW_FLAG_RAW_S3 4u')
h.append('static char8 const buster_a64_sysreg_generated_pool[] = {')
for i in range(0, len(pool), 16):
 h.append('    '+', '.join('0x%02x' % c for c in pool[i:i+16])+',')
h.append('};')
h.append('static u16 const buster_a64_sysreg_generated_parameter_encodings[] = {')
for i in range(0, len(parameter_encoding_values), 16):
 h.append('    '+', '.join('0x%04x' % value for value in parameter_encoding_values[i:i+16])+',')
h.append('};')
h.append('static BusterA64SysregGeneratedRow const buster_a64_sysreg_generated_rows[] = {')
for x in sel:
 flags=(1 if x['parameterized'] else 0)|(2 if x['accessor_alias'] else 0)|(4 if x['raw_s3'] else 0)
 h.append('    {%d,%d,1,%d,%d,0x%04x,%d,%d,%d,%d,%d,%d,UINT64_C(0x%016x),UINT64_C(0x%016x),UINT64_C(0x%016x)},' % (intern(x['name']),intern(x['target']),intern(x['source_file']),intern(x['accessor']),x['packed'],MECH[x['mechanism']],flags,x['array_start'],x['array_end'],x['parameter_encoding_offset'],x['parameter_encoding_count'],x['feature_digest'],x['source_digest'],x['access_digest']))
h.append('};')
h.append('#define BUSTER_A64_SYSREG_GENERATED_ROW_COUNT ((u32)(sizeof(buster_a64_sysreg_generated_rows)/sizeof(buster_a64_sysreg_generated_rows[0])))')
h.append('#define BUSTER_A64_SYSREG_PARAMETER_ENCODING_COUNT ((u32)(sizeof(buster_a64_sysreg_generated_parameter_encodings)/sizeof(buster_a64_sysreg_generated_parameter_encodings[0])))')
h.append('#define BUSTER_A64_SYSREG_GENERATED_POOL_SIZE ((u32)(sizeof(buster_a64_sysreg_generated_pool)))')
h.append('BUSTER_CT_CHECK(BUSTER_A64_SYSREG_GENERATED_ROW_COUNT == BUSTER_A64_SYSREG_ACCEPTED_MECHANISM_COUNT);')
h.append('BUSTER_CT_CHECK(BUSTER_A64_SYSREG_GENERATED_POOL_SIZE > 0);')
(out/'aarch64-system-registers.generated.h').write_text('\n'.join(h)+'\n')
# JSONL is a normalized audit projection: retain every relevant mechanism and
# its stable identity/decision digests, but do not copy the XML condition or
# accessor prose.  Accepted parameterized rows carry their precomputed
# transforms; rejected rows retain only their bounded range/count and reason.
def transform_digest(values):
 data=b''.join(int(value).to_bytes(2, 'little', signed=False) for value in values)
 return digest64(data)

def json_projection(row):
 projection={
  'access_digest':row['access_digest'],
  'access_condition_code':row['access_condition_code'],
  'access_condition_digest':row['access_condition_digest'],
  'accessor_alias':row['accessor_alias'],
  'accepted':row['accepted'],
  'array_end':row['array_end'],
  'array_start':row['array_start'],
  'feature_digest':row['feature_digest'],
  'feature_profile_id':row['feature_profile_id'],
  'mechanism':row['mechanism'],
  'mode':row['mode'],
  'name':row['name'],
  'packed':row['packed'],
  'parameterized':row['parameterized'],
  'raw_s3':row['raw_s3'],
  'register_eligible':row['register_eligible'],
  'reason_code':row['reason_code'],
  'source_digest':row['source_digest'],
  'source_file':row['source_file'],
  'target':row['target'],
  'valid_encoding':row['valid_encoding'],
 }
 values=row['parameter_encodings']
 if row['parameterized']:
  projection['parameter_encoding_count']=len(values)
  if values:
   projection['parameter_encoding_digest']=transform_digest(values)
   if row['accepted'] and not row['raw_s3']:
    projection['parameter_encodings']=values
 return projection

# jsonl all relevant
with (out/'aarch64-system-registers.generated.jsonl').open('w') as f:
 for x in allrows:
  f.write(json.dumps(json_projection(x),sort_keys=True,separators=(',',':'))+'\n')
# manifest
sha=hashlib.sha256((out/'aarch64-system-registers.generated.jsonl').read_bytes()).hexdigest(); hh=hashlib.sha256((out/'aarch64-system-registers.generated.h').read_bytes()).hexdigest()
def audit_identity(row):
 return {'access_condition_code':row['access_condition_code'],'access_condition_digest':row['access_condition_digest'],'feature_digest':row['feature_digest'],'mechanism':row['mechanism'],'name':row['name'],'packed':row['packed'],'source_digest':row['source_digest'],'source_file':row['source_file'],'target':row['target']}

access_rows=[x for x in allrows if x['access_condition_code'] != ACCESS_CONDITION_NONE]
access_condition_summary={}
for code,label in ACCESS_CONDITION_LABELS.items():
 if code == ACCESS_CONDITION_NONE: continue
 group=[x for x in access_rows if x['access_condition_code'] == code]
 access_condition_summary[label]={'code':code,'total':len(group),'register_eligible':sum(x['register_eligible'] for x in group),'accepted':sum(x['accepted'] for x in group),'rejected':sum(not x['accepted'] for x in group)}
access_condition_removals=[x for x in access_rows if x['register_eligible'] and not x['accepted']]
if len(access_condition_removals) != 34:
 raise SystemExit('AArch64 SysReg access-condition removal invariant failed: %d' % len(access_condition_removals))
removal_groups={label:[audit_identity(x) for x in access_condition_removals if x['access_condition_code'] == code] for code,label in ACCESS_CONDITION_LABELS.items() if code not in (ACCESS_CONDITION_NONE, ACCESS_CONDITION_VHE)}
if {key:len(value) for key,value in removal_groups.items()} != {'feat_d128':20,'feat_srmask':8,'feat_sysreg128':2,'unknown':4}:
 raise SystemExit('AArch64 SysReg access-condition removal groups drifted')
accepted_access_condition_rows=[audit_identity(x) for x in access_rows if x['accepted']]
manifest={
 'schema_version':2,
 'status':'generated',
 'source':{
  'release':'2026-06','date':'30 Jun 2026','commit_id':'2026-06_rel',
  'url':'https://developer.arm.com/-/cdn-downloads/permalink/Exploration-Tools-Arm-Architecture-System-Registers/SysReg/SysReg_xml_A_profile-2026-06.tar.gz',
  'archive_sha256':'4795c769085ff9056d9f18abbd9e23d7b0f0a955214cfb2a2121a9698b50d509',
  'archive_bytes':43124482,
  'notice_sha256':'13a5c90f2accaf17573f73499a3940df6168d65cd17a893f685e686aa436a246',
  'raw_xml_policy':'not vendored; regenerate from independently obtained official archive',
 },
 'completeness':{
  'expected_xml_files':EXPECTED_SOURCE_XML_FILES,
  'observed_xml_files':len(source_xml_files),
  'expected_relevant_mechanisms':EXPECTED_RELEVANT_MECHANISMS,
  'observed_relevant_mechanisms':len(allrows),
 },
 'profile':{
  'name':'Apple M1 hardware-grounded A-profile',
  'census':profile_census,
  'excluded_capabilities':['EL3','FEAT_AMUv1','FEAT_MPAM','FEAT_SEL2','FEAT_NV','FEAT_NV2','FEAT_TRF','FEAT_PMUv3','FEAT_RASv1p1','FEAT_D128','FEAT_SRMASK','FEAT_SYSREG128','trace access','unknown atoms'],
  'evidence':{
   'cpuctl_gist':'https://gist.github.com/ryo/f533af313ac9dfd971f682b7ae951d63',
   'cpuctl_scope':'community 2022 M1 cpuctl sample covering Icestorm and Firestorm; not Apple-primary or exhaustive across every M1 revision',
   'm1n1_vhe':'https://github.com/AsahiLinux/m1n1/blob/06a4601a351ebfd1abb6abba9a44c34e40d94776/src/hv.c',
   'asahi_pmu_caveat':'https://github.com/AsahiLinux/linux/blob/e2e1930a9595bffafad92cec2b5504525efb9cd4/drivers/perf/apple_m1_cpu_pmu.c',
   'note':'The community cpuctl sample and m1n1 hypervisor source corroborate the selected EL1/EL2 and VHE baseline. Asahi Linux PMU support is platform-software evidence, not proof of architectural FEAT_PMUv3, so PMUv3 remains excluded.',
  },
 },
 'filter':{
  'true_capabilities':sorted(TRUE),'unknown_policy':'fail-closed','trace_access_implemented':False,
  'compile_time_gate':'register_condition AND access_condition',
  'runtime_asl':'access_permission text retained only as access_digest; never assembler legality',
  'evaluator':'recursive descent; consumes both RHS branches (no short-circuit parse)',
 },
 'projection':{
  'jsonl':'normalized mechanism inventory; raw XML condition/accessor text omitted',
  'digest_encoding':'first 64 bits of SHA-256',
  'reason_codes':{'0':'accepted','1':'feature_false','2':'encoding_unresolved','3':'feature_false_and_encoding_unresolved'},
  'access_condition_codes':{str(k):v for k,v in ACCESS_CONDITION_LABELS.items()},
 },
 'access_condition_audit':{
  'rows_with_access_condition':len(access_rows),
  'accepted_rows':accepted_access_condition_rows,
  'summary':access_condition_summary,
  'removed_from_register_only_census':{
   'total':len(access_condition_removals),
   'group_counts':{'D128':len(removal_groups['feat_d128']),'SRMASK':len(removal_groups['feat_srmask']),'SYSREG128':len(removal_groups['feat_sysreg128']),'unknown_atom':len(removal_groups['unknown'])},
   'groups':removal_groups,
  },
 },
 'inventory':{
  'relevant_mechanisms':len(allrows),'accepted_mechanisms':len(sel),
  'accepted_fixed_source_rows':accepted_fixed,
  'accepted_named_parameterized_source_rows':sum(x['parameterized'] and not x['raw_s3'] for x in sel),
  'accepted_parameterized_mechanisms':accepted_parameterized,
  'accepted_raw_s3_mechanisms':sum(x['raw_s3'] for x in sel),
  'accepted_raw_s3_families':len({(x['name'],x['target'],x['source_file']) for x in sel if x['raw_s3']}),
  'target_fixed_names':len(fixed_targets),'encoding_fixed_names':len(fixed_encodings),
  'readable_fixed_names':h_readable,'writable_fixed_names':h_writable,'both_fixed_names':h_both,
 },
 'artifacts':{
  'header_sha256':hh,'jsonl_sha256':sha,
  'header_file':'aarch64-system-registers.generated.h','jsonl_file':'aarch64-system-registers.generated.jsonl',
 },
}
(out/'aarch64-system-registers-manifest.json').write_text(json.dumps(manifest,indent=2,sort_keys=True)+'\n')

if args.check:
    names = ('aarch64-system-registers.generated.h',
             'aarch64-system-registers.generated.jsonl',
             'aarch64-system-registers-manifest.json')
    mismatches = []
    for name in names:
        expected = (_checked_in / name).read_bytes()
        actual = (out / name).read_bytes()
        if expected != actual:
            mismatches.append(name)
    if mismatches:
        raise SystemExit('AArch64 SysReg generated artifact drift: ' + ', '.join(mismatches))
    print('AArch64 SysReg generated artifact check: 3 files match')

if args.mutation_test:
    # Keep the licensed source tree untouched: symlink every page into a
    # temporary tree, then replace only one relevant page for each mutation.
    # A changed name, encoding, or predicate must alter at least one generated
    # artifact, proving that the importer is reading source semantics rather
    # than copying checked-in bytes.
    pages = []
    for candidate in source_xml_files:
        text = candidate.read_text(encoding='utf-8', errors='strict')
        if 'execution_state="AArch64"' in text and '<access_mechanism ' in text and '<register_page' in text:
            pages.append((candidate, text))
    if not pages:
        raise SystemExit('mutation self-test found no AArch64 register page')
    mutations = (
        ('name', 'reg_short_name', lambda text: re.sub(r'(<reg_short_name>)([^<]+)(</reg_short_name>)', r'\1\2_MUTATED\3', text, count=1)),
        ('encoding', 'encoding', lambda text: re.sub(r'(<enc[^>]*n="op0"[^>]*v=")([^"]+)(")', r'\g<1>0b10\3', text, count=1)),
        ('condition', 'reg_condition', lambda text: re.sub(r'(<reg_condition[^>]*>).*?(</reg_condition>)', r'\1FEAT_UNKNOWN_MUTATED is implemented\2', text, count=1, flags=re.S)),
        ('access-condition', 'access_condition', lambda text: re.sub(r'(<access_condition[^>]*>).*?(</access_condition>)', r'\1FEAT_UNKNOWN_MUTATED is implemented\2', text, count=1, flags=re.S)),
    )
    baseline = {name: (out / name).read_bytes() for name in ('aarch64-system-registers.generated.h', 'aarch64-system-registers.generated.jsonl', 'aarch64-system-registers-manifest.json')}

    # Completeness is independently mutation-tested for both classes of source
    # page.  Removing a relevant page must lower the mechanism census, while
    # removing an index/other non-register page must lower the XML census;
    # either truncation must be rejected before artifacts are emitted.
    relevant_page = pages[0][0]
    relevant_page_names = {page.name for page, _ in pages}
    nonrelevant_page = next((page for page in source_xml_files if page.name not in relevant_page_names), None)
    if nonrelevant_page is None:
        raise SystemExit('mutation self-test found no non-relevant XML page')
    for label, missing_page in (('relevant-page-completeness', relevant_page), ('non-relevant-page-completeness', nonrelevant_page)):
        with tempfile.TemporaryDirectory(prefix='a64-sysreg-completeness-') as temp_name:
            mutated_root = Path(temp_name) / 'xml'
            mutated_root.mkdir()
            for page in source_xml_files:
                if page == missing_page:
                    continue
                (mutated_root / page.name).symlink_to(page)
            mutated_out = Path(temp_name) / 'out'
            child = subprocess.run((sys.executable, str(Path(__file__).resolve()), str(mutated_root), str(mutated_out)),
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            if child.returncode == 0:
                raise SystemExit('mutation self-test accepted truncated source for %s' % label)

    for label, marker, mutate in mutations:
        with tempfile.TemporaryDirectory(prefix='a64-sysreg-mutation-') as temp_name:
            candidate, original = next(((page, text) for page, text in pages if marker in text), (None, None))
            if candidate is None:
                raise SystemExit('mutation self-test could not locate %s in any AArch64 page' % marker)
            mutated_root = Path(temp_name) / 'xml'
            mutated_root.mkdir()
            for page in source_xml_files:
                (mutated_root / page.name).symlink_to(page)
            mutated_page = mutated_root / candidate.name
            mutated_text = mutate(original)
            if mutated_text == original:
                raise SystemExit('mutation self-test could not locate %s in %s' % (marker, candidate.name))
            mutated_page.unlink()
            mutated_page.write_text(mutated_text, encoding='utf-8')
            mutated_out = Path(temp_name) / 'out'
            child = subprocess.run((sys.executable, str(Path(__file__).resolve()), str(mutated_root), str(mutated_out)),
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            if child.returncode != 0:
                raise SystemExit('mutation self-test generator failed for %s: %s' % (label, child.stderr.strip()))
            changed = any((mutated_out / name).read_bytes() != data for name, data in baseline.items())
            if not changed:
                raise SystemExit('mutation self-test did not change artifacts for %s' % label)
    print('AArch64 SysReg mutation self-test: completeness (relevant/non-relevant deletion), name, encoding, condition, access-condition drift detected')
