#!/usr/bin/env python3
"""One-off script that wired ms20/dx7/stringens/prophetvs into engine.cpp's
24 dispatch switches by cloning each switch's ENGINE_JUPITER8 case line (the
last case before `default:` in every one of them) and substituting its jp8
prefix for each new engine's own prefix. Not part of the firmware build --
kept for the record of how the mechanical part of the wiring was done."""
import re
import os

SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "engine.cpp")

with open(SRC) as f:
    text = f.read()

# (EngineId enum value, function-name prefix)
NEW_ENGINES = [
    ("ENGINE_MS20", "ms20"),
    ("ENGINE_DX7", "dx"),
    ("ENGINE_STRINGENS", "se"),
    ("ENGINE_PROPHETVS", "vs"),
]

pattern = re.compile(r'^(\s*)case ENGINE_JUPITER8:(\s*)(return )?jp8([A-Za-z_]\w*)(.*)$', re.MULTILINE)

def make_replacement(m):
    indent, gap, ret, funcTail, rest = m.groups()
    ret = ret or ""
    lines = [m.group(0)]
    for enumName, prefix in NEW_ENGINES:
        lines.append(f"{indent}case {enumName}:{gap}{ret}{prefix}{funcTail}{rest}")
    return "\n".join(lines)

new_text, n = pattern.subn(make_replacement, text)
assert n == 24, f"expected 24 ENGINE_JUPITER8 case lines, found {n}"

with open(SRC, "w") as f:
    f.write(new_text)

print(f"Patched {n} switch statements in {SRC}")
