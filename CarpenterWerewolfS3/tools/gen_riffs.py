#!/usr/bin/env python3
"""Regenerates arps.cpp's RIFFS[] table so every one of the 200 riffs is
genuinely musically distinct -- the RETROWAVE/GRINDHOUSE/SEVENTIES/EIGHTIES
categories previously cycled only 4-5 unique note/velocity patterns across
44 renamed slots each. Keeps the 24 CLASSIC riffs (already unique) verbatim,
keeps every existing name/group/category/count, replaces only the content.

A one-off content-generation script, not part of the firmware build --
rerun it (`python3 tools/gen_riffs.py` from the repo root) only if the
Retrowave/Grindhouse/70s/80s riff vocabulary below is deliberately being
reworked; it edits arps.cpp in place.
"""
import re, os, sys

SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "arps.cpp")

with open(SRC) as f:
    text = f.read()

# ---- extract the 24 CLASSIC riff blocks verbatim (single-line format) -----
classic_block_re = re.compile(
    r'\{"([^"]+)",\s*\{([^}]*)\},\s*\n\s*\{([^}]*)\},\s*(\d+),\s*RIFF_CAT_CLASSIC\},'
)
classic_matches = classic_block_re.findall(text)
assert len(classic_matches) == 24, f"expected 24 classic riffs, found {len(classic_matches)}"

# Keep the CLASSIC section's original source text verbatim (safer than
# reformatting it from the regex-extracted fields).
classic_start = text.index('const Riff RIFFS[] = {')
classic_section_start = text.index('{"ACID 1"')
classic_section_end = text.index('{"OUTRUN 1"')
classic_text = text[classic_section_start:classic_section_end].rstrip() + "\n"

# ---- group names per category (order preserved from the original file) ----
GROUPS = {
    "RETROWAVE": ["OUTRUN", "NEON DRIVE", "NIGHT RACER", "CHROME EDGE", "SUNSET GRID",
                  "TURBO", "ARCADE GLOW", "NIGHT CITY", "LASER HIGHWAY", "VELOUR", "RETROGRADE"],
    "GRINDHOUSE": ["CREEPER", "STALKER", "BASEMENT", "GRAINHOUSE", "VHS DECAY",
                   "BACK ALLEY", "MEATHOOK", "DOUBLE BILL", "REEL BURN", "PROJECTOR", "DRIVE-IN"],
    "SEVENTIES": ["FUNK STRUT", "BOOGIE NIGHTS", "DISCO INFERNO", "SLEAZE", "SHAG CARPET",
                  "WAH STRUT", "SATIN GROOVE", "AFTER HOURS", "VELVET GROOVE", "8-TRACK", "CENTERFOLD"],
    "EIGHTIES": ["NEON GATE", "CHASE SCENE", "MONTAGE", "MALL RATS", "VHS COVER",
                 "DEAD END", "POWER SUIT", "SAT MORNING", "RAD WAVE", "SYNTH PATROL", "WORKOUT"],
}
REST = -1

# =============================================================================
# Per-category vocabulary: several distinct 16-step NOTE shapes and VELOCITY
# contours, each capturing a real facet of the style, hand-written with
# enough variety that systematic combination (below) covers 44 slots without
# forcing repetition. -1 = rest (matches the file's own convention).
# =============================================================================

def shift(pattern, amount):
    return tuple(REST if n == REST else n + amount for n in pattern)

RETROWAVE_SHAPES = [
    (0,REST,12,REST, 0,REST,12,REST, 0,REST,12,REST, 0,REST,12,REST),   # octave pulse
    (0,REST, 7,REST, 0,REST, 7,REST, 0,REST, 7,REST, 0,REST, 7,REST),   # fifth pulse
    (0,REST,12,REST, 7,REST,12,REST, 0,REST,12,REST, 7,REST,12,REST),   # octave/fifth alternation
    (0, 4, 7,12,16,19,24,19,19,24,19,16,12, 7, 4, 0),                    # rising/falling triad run
    (0,REST,REST,REST, 0,REST,REST,REST, 0,REST,REST,REST, 7,REST,12,REST), # sparse drive pulse
    (0,REST, 7,REST,12,REST, 7,REST, 0,REST, 7,REST,12,REST, 7,REST),   # walking fifth/octave
    (0,12, 7,12, 0,12, 7,12, 3,12, 7,12, 3,12, 7,12),                    # trance-gate triad
    (0, 7,12,19,12, 7, 0, 7,12,19,12, 7, 0,REST, 0,REST),                # wide arpeggio hook
    (0,REST,12,REST, 0,12, 0,REST, 7,REST,12,REST, 7,12, 7,REST),       # syncopated octave
    (0, 0,12,12, 7, 7,12,12, 0, 0,12,12, 7, 7,12,12),                    # doubled octave pump
    (0,REST, 5,REST, 7,REST,12,REST, 0,REST, 5,REST, 7,REST,12,REST),   # fourth/fifth climb
    (0,12,REST,12, 0,12,REST,12, 7,12,REST,12, 7,12,REST,12),           # gated octave drive
    (0, 3, 7,10,12,10, 7, 3, 0, 3, 7,10,12,10, 7, 3),                    # minor-7 arpeggio
    (0,REST,12,19,REST,19,12,REST, 0,REST,12,19,REST,19,12,REST),       # wide neon run
]
RETROWAVE_VELS = [
    (112,0,90,0,112,0,90,0,112,0,90,0,112,0,90,0),
    (100,0,88,0,100,0,88,0,100,0,88,0,100,0,88,0),
    (105,0,85,0,85,0,85,0,105,0,85,0,85,0,85,0),
    (98,90,90,90,98,90,90,90,98,90,90,90,98,90,90,90),
    (110,0,0,0,110,0,0,0,110,0,0,0,95,0,95,0),
    (115,0,92,0,108,0,92,0,115,0,92,0,108,0,92,0),
    (120,0,84,0,120,0,84,0,102,0,84,0,102,0,84,0),
    (95,88,88,88,95,88,88,88,95,88,88,88,95,88,88,88),
    (108,0,86,0,96,0,86,0,108,0,86,0,96,0,86,0),
    (118,70,90,70,118,70,90,70,102,70,90,70,102,70,90,70),
]

GRINDHOUSE_SHAPES = [
    (0,REST,1,REST,2,REST,1,REST,0,REST,REST,REST,-2,REST,REST,REST),   # chromatic crawl up/down
    (0,REST,REST,REST,REST,REST,REST,6,REST,REST,REST,REST,REST,REST,0,REST),  # tritone stab
    (0,REST,REST,REST,-2,REST,-3,REST,0,REST,REST,REST,-2,REST,-3,REST),  # descending chromatic
    (0,0,REST,1,0,0,REST,REST,0,0,REST,1,0,0,REST,REST),                # close cluster
    (0,REST,3,REST,1,REST,6,REST,0,REST,3,REST,1,REST,6,REST),          # sleazy interval leap
    (0,REST,-1,REST,-3,REST,-1,REST,0,REST,-1,REST,-3,REST,-1,REST),    # low chromatic sink
    (0,1,0,-1,0,1,0,REST,0,1,0,-1,0,1,0,REST),                          # trembling minor 2nd
    (0,REST,REST,6,REST,REST,0,REST,REST,6,REST,REST,0,REST,REST,REST), # sparse tritone toll
    (0,REST,4,REST,3,REST,6,REST,0,REST,4,REST,3,REST,6,REST),          # dissonant crawl wide
    (0,-1,-2,-3,-4,-3,-2,-1,0,-1,-2,-3,-4,-3,-2,-1),                      # slow chromatic descent
    (0,REST,2,REST,REST,-1,REST,REST,0,REST,2,REST,REST,-1,REST,REST),  # queasy stagger
    (6,REST,0,REST,6,REST,0,REST,REST,6,REST,0,REST,6,REST,0),          # tritone alternation
    (0,3,REST,-3,0,3,REST,-3,0,3,REST,-3,0,3,REST,-3),                   # unstable rocking third
    (0,REST,REST,1,REST,REST,-2,REST,REST,REST,3,REST,REST,REST,-1,REST), # random stalking hits
]
GRINDHOUSE_VELS = [
    (60,0,70,0,80,0,80,0,70,0,0,0,70,0,0,0),
    (60,0,0,0,0,0,0,115,0,0,0,0,0,0,70,0),
    (60,0,0,0,80,0,60,0,70,0,0,0,70,0,70,0),
    (60,80,0,70,70,80,0,0,70,70,0,60,70,60,0,0),
    (80,0,70,0,70,0,115,0,70,0,60,0,60,0,105,0),
    (70,0,60,0,90,0,50,0,80,0,0,0,60,0,90,0),
    (65,55,70,0,65,55,70,0,65,55,70,0,65,55,70,0),
    (75,0,0,95,0,0,60,0,0,105,0,0,55,0,0,0),
    (85,0,65,0,55,0,100,0,85,0,65,0,55,0,100,0),
    (90,75,60,45,90,75,60,45,90,75,60,45,90,75,60,45),
]

SEVENTIES_SHAPES = [
    (0,REST,0,REST,REST,0,REST,7,0,REST,0,REST,REST,0,REST,5),          # funk syncopation
    (0,REST,12,REST,0,REST,12,REST,7,REST,12,REST,0,REST,12,REST),      # octave disco pulse
    (0,2,3,5,7,5,3,2,2,3,5,7,5,3,2,0),                                    # walking pentatonic bass
    (REST,0,REST,REST,5,REST,REST,0,REST,REST,3,REST,0,REST,REST,REST), # offbeat disco stab
    (0,REST,0,12,REST,0,REST,7,0,REST,0,12,REST,0,REST,5),               # octave-jump funk
    (0,REST,3,REST,5,REST,7,REST,0,REST,3,REST,5,REST,7,REST),          # ascending soul bass
    (0,0,REST,0,REST,0,0,REST,7,7,REST,7,REST,5,5,REST),                 # ghost-note groove
    (0,REST,REST,0,REST,3,REST,0,REST,REST,5,REST,0,REST,3,REST),       # sparse syncopated walk
    (0,7,0,7,REST,0,7,0,5,7,5,7,REST,5,7,5),                             # bouncing fifth groove
    (0,REST,10,REST,0,REST,10,REST,7,REST,10,REST,0,REST,10,REST),      # dominant-7 disco pulse
    (0,3,0,REST,3,0,REST,7,0,3,0,REST,3,0,REST,5),                       # minor funk stagger
    (REST,0,3,REST,0,REST,5,REST,REST,0,3,REST,0,REST,5,REST),          # syncopated minor walk
    (0,REST,0,REST,7,REST,0,REST,0,REST,0,REST,5,REST,0,REST),          # steady quarter groove
    (0,2,0,REST,5,REST,0,7,0,2,0,REST,5,REST,0,7),                       # strutting bassline
]
SEVENTIES_VELS = [
    (115,0,45,0,0,95,0,100,112,0,45,0,0,95,0,98),
    (115,0,85,0,110,0,85,0,105,0,85,0,115,0,85,0),
    (100,92,84,100,92,84,100,92,84,100,92,84,100,92,84,100),
    (0,95,0,0,95,0,0,110,0,0,95,0,105,0,0,0),
    (110,0,60,90,0,100,0,88,108,0,60,90,0,100,0,85),
    (0,105,0,0,105,0,0,105,0,0,110,0,95,0,0,0),
    (105,70,0,80,60,90,70,0,100,65,0,75,55,85,65,0),
    (95,0,0,85,0,70,0,90,0,0,80,0,100,0,60,0),
    (90,75,90,75,0,90,75,90,80,75,80,75,0,80,75,80),
    (108,0,80,0,102,0,80,0,96,0,80,0,108,0,80,0),
]

EIGHTIES_SHAPES = [
    (0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0),                                    # gated single-note pulse
    (0,7,12,19,24,19,12,7,0,7,12,19,24,19,12,7),                          # rising/falling run
    (0,0,12,0,7,7,12,7,0,0,12,0,5,5,12,5),                                # gated octave/fifth hook
    (0,REST,REST,7,REST,REST,0,REST,12,REST,REST,7,REST,REST,0,REST),   # action stab pulse
    (0,0,0,0,7,7,7,7,0,0,0,0,12,12,12,12),                                # staircase pulse
    (0,4,7,12,7,4,0,4,7,12,7,4,0,4,7,12),                                 # major arpeggio drive
    (0,7,0,7,12,7,12,7,0,7,0,7,12,7,12,7),                                # fifth/octave gate hook
    (0,0,0,12,0,0,0,12,7,7,7,19,7,7,7,19),                                # gated leap pulse
    (0,REST,7,REST,0,REST,7,REST,12,REST,7,REST,0,REST,7,REST),         # anthemic fifth pulse
    (0,5,0,5,7,5,7,5,0,5,0,5,7,5,7,5),                                    # fourth/fifth gate drive
    (0,0,7,7,0,0,12,12,0,0,7,7,0,0,12,12),                                # power-chord pump
    (0,3,7,12,15,12,7,3,0,3,7,12,15,12,7,3),                              # minor arpeggio drive
    (0,REST,0,12,REST,0,REST,7,0,REST,0,12,REST,0,REST,7),               # synth-pop hook
    (0,12,0,12,7,12,7,12,0,12,0,12,7,12,7,12),                            # gated octave/fifth run
]
EIGHTIES_VELS = [
    (120,40,60,40,100,40,60,40,120,40,60,40,100,40,60,40),
    (95,100,105,110,95,100,105,110,95,100,105,110,95,100,105,110),
    (110,70,95,70,105,70,95,70,110,70,95,70,105,70,95,70),
    (118,0,0,118,0,0,118,0,118,0,0,118,0,0,118,0),
    (105,85,90,85,108,85,90,85,105,85,90,85,112,85,90,85),
    (115,80,90,95,80,78,100,80,85,95,90,85,80,78,100,90),
    (100,95,80,95,105,95,80,95,100,95,80,95,110,95,80,95),
    (112,60,75,60,108,60,75,60,112,60,75,60,100,60,75,60),
    (108,0,92,0,104,0,92,0,116,0,92,0,104,0,92,0),
    (96,88,96,88,110,88,96,88,96,88,96,88,110,88,96,88),
]

GATE_POOL = {
    "RETROWAVE": [55,60,65,70,75],
    "GRINDHOUSE": [70,75,80,85,90,95],
    "SEVENTIES": [30,35,40,45,50],
    "EIGHTIES": [60,65,70,75,80],
}
TRANSPOSE_POOL = {
    "RETROWAVE": [0, 5, -5, 3, 7, -3],
    "GRINDHOUSE": [0, 1, -1, 2, -2, 4],
    "SEVENTIES": [0, 2, -2, 3, -3, 5],
    "EIGHTIES": [0, 5, -5, 3, -3, 7],
}

CATEGORY_DATA = {
    "RETROWAVE": (RETROWAVE_SHAPES, RETROWAVE_VELS),
    "GRINDHOUSE": (GRINDHOUSE_SHAPES, GRINDHOUSE_VELS),
    "SEVENTIES": (SEVENTIES_SHAPES, SEVENTIES_VELS),
    "EIGHTIES": (EIGHTIES_SHAPES, EIGHTIES_VELS),
}

def clamp_notes(pattern, lo=-9, hi=30):
    out = []
    for n in pattern:
        if n == REST:
            out.append(REST)
        else:
            out.append(max(lo, min(hi, n)))
    return tuple(out)

# Generic downbeat-weighted fallback accent, used only for a step that has
# a real (non-rest) note but whose paired velocity contour happens to carry
# a 0 there (contours and note shapes are chosen independently -- see the
# main loop -- so a contour built around a different shape's rest positions
# can land a 0 on a step this shape actually wants to sound).
_FALLBACK_ACCENT = (108,78,88,78,104,78,88,78,108,78,88,78,104,78,88,78)

def zero_vel_at_rests(notes, vels):
    out = []
    for i in range(16):
        if notes[i] == REST:
            out.append(0)
        elif vels[i] > 0:
            out.append(vels[i])
        else:
            out.append(_FALLBACK_ACCENT[i])
    return tuple(out)

classic_notes_set = set(
    tuple(int(x.strip()) for x in notes.split(",")) for _, notes, _, _ in classic_matches
)

seen = set()
all_riffs = []  # list of (name, notes, vels, gate, catmacro)

def make_unique(name, notes, vels, gate, cat):
    key = (notes, vels, gate)
    if key not in seen:
        seen.add(key)
        return notes, vels, gate
    # Perturb deterministically until unique: try every velocity pool entry
    # first (a genuinely different accent shape, not just a gate nudge),
    # then every gate, before finally nudging one velocity value as a last
    # resort. Musical variety over cheap-and-inaudible variety.
    for velraw in vels_pool_global:
        v2 = zero_vel_at_rests(notes, velraw)
        for g_try in gates_global:
            key = (notes, v2, g_try)
            if key not in seen:
                seen.add(key)
                return notes, v2, g_try
    vlist = list(vels)
    for i in range(16):
        if vlist[i] != 0:
            for delta in range(1, 25):
                vlist[i] = max(1, min(127, vels[i] + delta))
                key2 = (notes, tuple(vlist), gate)
                if key2 not in seen:
                    seen.add(key2)
                    return notes, tuple(vlist), gate
    raise RuntimeError(f"could not de-duplicate {name}")

import itertools, random

for cat, group_names in GROUPS.items():
    shapes, vels_pool = CATEGORY_DATA[cat]
    gates = GATE_POOL[cat]
    transposes = TRANSPOSE_POOL[cat]
    vels_pool_global = vels_pool
    gates_global = gates

    # (shape_idx, transpose_idx) combos, shuffled with a fixed seed so the
    # 44 slots draw 44 DISTINCT combos -- guarantees distinct NOTE content
    # by construction rather than hoping a hash formula spreads evenly.
    combos = list(itertools.product(range(len(shapes)), range(len(transposes))))
    rnd = random.Random(f"notes-{cat}")
    rnd.shuffle(combos)
    assert len(combos) >= 44, f"{cat}: only {len(combos)} shape/transpose combos, need 44"

    # Velocity and gate choices cycle independently (their own shuffled
    # order), so the accent shape and note-length vary independently of
    # which note shape was picked -- not tied together.
    vel_cycle = (list(range(len(vels_pool))) * ((44 // len(vels_pool)) + 1))
    rnd2 = random.Random(f"vel-{cat}")
    rnd2.shuffle(vel_cycle)
    gate_cycle = (list(range(len(gates))) * ((44 // len(gates)) + 1))
    rnd3 = random.Random(f"gate-{cat}")
    rnd3.shuffle(gate_cycle)

    used_notes_this_cat = set()
    next_spare = 44   # extra shuffled combos beyond the first 44, for swaps
    for i, gname in enumerate(group_names):
        for vi in range(4):
            slot = i * 4 + vi
            shape_idx, transp_idx = combos[slot]
            notes = clamp_notes(shift(shapes[shape_idx], transposes[transp_idx]))
            # A note pattern that happens to match a CLASSIC riff (or,
            # belt-and-braces, one already used earlier in this same
            # category) isn't wrong structurally, but it IS a repeat in the
            # one sense a player actually hears without checking velocity —
            # swap in a spare shuffled combo instead of accepting it.
            while (notes in classic_notes_set or notes in used_notes_this_cat) and next_spare < len(combos):
                shape_idx, transp_idx = combos[next_spare]
                next_spare += 1
                notes = clamp_notes(shift(shapes[shape_idx], transposes[transp_idx]))
            used_notes_this_cat.add(notes)
            velraw = vels_pool[vel_cycle[slot]]
            vels = zero_vel_at_rests(notes, velraw)
            gate = gates[gate_cycle[slot]]
            notes, vels, gate = make_unique(f"{gname} {vi+1}", notes, vels, gate, cat)
            all_riffs.append((f"{gname} {vi+1}", notes, vels, gate, f"RIFF_CAT_{cat}"))

print(f"Generated {len(all_riffs)} new riffs across 4 categories, all globally unique among themselves.")

# ---- also verify against the 24 CLASSIC riffs for global uniqueness -------
classic_keys = set()
for name, notes, vels, gate in classic_matches:
    ntup = tuple(int(x.strip()) for x in notes.split(","))
    vtup = tuple(int(x.strip()) for x in vels.split(","))
    classic_keys.add((ntup, vtup, int(gate)))
assert len(classic_keys) == 24

collisions = 0
for name, notes, vels, gate, catmacro in all_riffs:
    if (notes, vels, gate) in classic_keys:
        collisions += 1
        print("COLLISION with classic:", name)
assert collisions == 0
print("No collisions with the 24 CLASSIC riffs. Total unique riffs:", len(classic_keys) + len(all_riffs))

# =============================================================================
# Render the new file
# =============================================================================
def fmt_row(vals):
    return ", ".join(f"{v:3d}" for v in vals)

out_parts = [classic_text]
# all_riffs is already in category/group insertion order since GROUPS is an
# ordered dict walked in order above.
for name, notes, vels, gate, catmacro in all_riffs:
    block = (
        f'{{"{name}",\n'
        f'  {{{fmt_row(notes)}}},\n'
        f'  {{{fmt_row(vels)}}}, {gate}, {catmacro}}},\n'
    )
    out_parts.append(block)

new_riffs_body = "".join(out_parts)

new_text = (
    text[:classic_start + len('const Riff RIFFS[] = {\n')]
    + new_riffs_body
    + "};\n"
    + text[text.index("const uint8_t NUM_RIFFS"):]
)

with open(SRC, "w") as f:
    f.write(new_text)

print("Wrote", SRC)
