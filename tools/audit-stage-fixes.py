"""The independent fixes gained active PRs during local validation."""
import json
from pathlib import Path

# Do not publish duplicate code or alter someone else's active branches.
result = {"status": "superseded-before-publication", "existing_prs": {"184": 249, "222": [250, 251]}, "changes_pushed": False}
Path("staged-fixes.json").write_text(json.dumps(result, indent=2) + "\n")
print(json.dumps(result, indent=2))
