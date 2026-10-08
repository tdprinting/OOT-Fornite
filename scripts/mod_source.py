"""Read the mod in compilation order for source-based checks."""
from pathlib import Path
import re

FEATURE_INCLUDE = re.compile(r'^#include "(features/[^"\n]+\.inc)"\n', re.M)


def read_mod_source(path: Path) -> str:
    source = path.read_text(encoding="utf-8")
    return FEATURE_INCLUDE.sub(
        lambda match: (path.parent / match[1]).read_text(encoding="utf-8"), source
    )
