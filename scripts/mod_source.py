"""Read the mod in compilation order for source-based checks."""
from pathlib import Path
import re

FEATURE_INCLUDE = re.compile(r'^#include "(features/[^"\n]+\.inc)"\n', re.M)


def read_mod_source(path: Path) -> str:
    source = path.read_text(encoding="utf-8")
    return FEATURE_INCLUDE.sub(
        lambda match: (path.parent / match[1]).read_text(encoding="utf-8"), source
    )


def function(source, name):
    """The text of the first function called `name` in `source`, signature to closing brace (signature on one line)."""
    match = re.search(r"^[^\n;]*\b" + name + r"\([^;]*?\)\s*\{", source, re.M)
    assert match, name
    start = match.start()
    depth = 1
    end = match.end()
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]
