#!/usr/bin/env python3
from pathlib import Path

patch = Path('.github/scripts/patch_1790.py')
text = patch.read_text(encoding='utf-8')

old = 'updated, count = re.subn(pattern, replacement, text, count=1, flags=re.MULTILINE | re.DOTALL)'
new = 'updated, count = re.subn(pattern, lambda _match: replacement, text, count=1, flags=re.MULTILINE | re.DOTALL)'
if text.count(old) != 1:
    raise SystemExit('literal regex replacement seam not found exactly once')
text = text.replace(old, new, 1)

old = '''    if count != 1:
        raise SystemExit(f"{path}: expected one exact replacement, found {count}")
    target.write_text(text.replace(old, new, 1), encoding="utf-8")
'''
new = '''    if count != 1:
        if path == "docs/ci-cancellation-recovery.md" and count == 0:
            marker = "\\n## Validation and escalation\\n"
            addition = """A desktop artifact-retention failure after successful compiler/test work is
contained at the caller boundary rather than exposed as an immediate failed
matrix job. The same-commit composite reports whether it entered after nested
action resolution. An empty entry result permits exactly one second top-level
resolution attempt; once entered, only the composite's existing bounded
upload/API retry applies. If neither path retains evidence, the shard records
an error annotation but does not trigger sibling cancellation. Its
aggregate-required `Retain desktop logs` proof step remains absent, so
`CI complete` fails closed after the other shards have had a chance to finish.
Compiler, test, summary, checkout and other substantive shard failures remain
ordinary failures and retain native fail-fast/watcher behavior.
"""
            if text.count(marker) != 1:
                raise SystemExit(f"{path}: validation heading not found exactly once")
            target.write_text(text.replace(marker, "\\n" + addition + marker, 1), encoding="utf-8")
            return
        raise SystemExit(f"{path}: expected one exact replacement, found {count}")
    target.write_text(text.replace(old, new, 1), encoding="utf-8")
'''
if text.count(old) != 1:
    raise SystemExit('documentation fallback seam not found exactly once')
text = text.replace(old, new, 1)

old = '            and "lacks unique completion proof" in error\n'
new = '            and "concluded \'skipped\'; success required" in error\n'
if text.count(old) != 1:
    raise SystemExit('aggregate diagnostic assertion seam not found exactly once')
text = text.replace(old, new, 1)

old = '''      # The local action exposes whether it entered after resolving its nested
      # upload dependency. A pre-entry resolution failure gets one new
'''
new = '''      # The same-commit action resolves actions/upload-artifact@043fb46d1a93c77aae656e7c1c64a875d1fc6a0a for both internal attempts.
      # The local action exposes whether it entered after resolving its nested
      # upload dependency. A pre-entry resolution failure gets one new
'''
if text.count(old) != 1:
    raise SystemExit('desktop action pin annotation seam not found exactly once')
text = text.replace(old, new, 1)

patch.write_text(text, encoding='utf-8')
Path(__file__).unlink()
