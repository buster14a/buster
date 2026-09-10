"""CI-only observation in disposable checkouts; never part of implementation PR."""
from pathlib import Path
p=Path('src/buster/lib/compiler/assembly/x86_64_completion_census.c')
s=p.read_text()
a='''    encoded = assembly_encode(arena, source,
                              (AssemblyEncodeOptions){.target = target,
                                                       .syntax = att ? ASSEMBLY_SYNTAX_ATT : ASSEMBLY_SYNTAX_INTEL});'''
b='''    string_print(S8("A07_SOURCE form={u32} att={u32} text={S8}"), form.id, (u32)att, source);
'''+a
assert s.count(a)==1
p.write_text(s.replace(a,b))
p=Path('src/buster/apps/ide/ide.c')
s=p.read_text()
a='''    u64 ledger_digest = buster_x86_metadata_coverage_digest(ledger, audit.entry_count, form_count);'''
b='''    for (u32 index = 0; index < census.record_count; index += 1)
    {
        BusterX86CompletionCensusRecord record = records[index];
        string_print(S8("A07_ROW form={u32} hash={u64:x,no_prefix} structural={u8} intel={u8} intel_reason={u8} intel_bytes={u32} att={u8} att_reason={u8} att_bytes={u32}\\n"),
                     record.form_id, record.stable_hash, record.structural_class,
                     record.intel_class, record.intel_source_reason, record.intel_byte_count,
                     record.att_class, record.att_source_reason, record.att_byte_count);
    }
'''+a
assert s.count(a)==1
p.write_text(s.replace(a,b))
