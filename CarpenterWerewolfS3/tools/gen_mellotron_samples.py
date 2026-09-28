#!/usr/bin/env python3
"""
gen_mellotron_samples.py — bakes the two Mellotron tape loops used by the
Mellotron engine (mellotron.cpp) into mellotron_samples.h.

The real M400 has one physical tape per key, recorded from a real choir or
string section, running about 8 seconds before rewinding. There is no way to
get that onto this hardware's flash budget in full fidelity, so both loops
are cut down to a few seconds and re-encoded as mono, signed 8-bit PCM
(lossy, but tape hiss and wow/flutter already mask 8-bit quantising noise)
at 11025 Hz.

The Choir loop is now built from a real recorded vocal sample rather than
synthesised (see gen_choir()'s own docstring for the source, the loop
window chosen inside it, and the crossfade that makes the cut loop
seamlessly) — an actual recording is a strictly better "male choir" than any
amount of synthesis this build tried first. The Strings loop is still
synthesised (several slightly-detuned sawtooth-ish ensembles plus a slow
shared tremolo, all at integer-Hz rates over an exact 1-second buffer, which
is what makes IT loop with zero discontinuity — see gen_strings()).

Run from the repo root:
    python3 tools/gen_mellotron_samples.py
"""
import audioop
import math
import struct
import wave

SR = 11025
LOOP_SECONDS = 1
N = SR * LOOP_SECONDS

# ─── Choir: real recorded sample ────────────────────────────────────────────
# tools/samples/choir_source.wav is a 2.5 s, 44.1 kHz/16-bit/stereo trim of a
# user-supplied recording of a low male choir "ah" — trimmed down from the
# original ~9 s take to just the region used below, since that's the only
# part this script ever reads. Autocorrelation pitch-tracking across the
# original recording found it was not one sustained note: it moves through a
# short phrase (roughly C2 -> ~106 Hz -> F2 -> G2 as the singers move chords).
# Picking one of those as the loop source keeps the instrument in tune when
# the keyboard pitch-shifts it — layering a keyboard pitch-shift on top of a
# segment that is ALREADY changing pitch would make every note sound like it
# was sliding. The G2 region (roughly 5.75 s - 8.0 s of the original
# recording, comfortably clear of both the attack transient at the start of
# the take and the release at the end) was both the longest steady segment
# and the closest to an exact standard pitch (98.00 Hz), so that's what
# choir_source.wav's [CHOIR_LOOP_START_S, CHOIR_LOOP_START_S+CHOIR_LOOP_LEN_S)
# window below points at.
CHOIR_SOURCE_WAV = "tools/samples/choir_source.wav"
CHOIR_LOOP_START_S = 0.30   # offset into choir_source.wav's own timeline
CHOIR_LOOP_LEN_S = 2.00     # before the crossfade below shortens it
CHOIR_CROSSFADE_S = 0.15    # loop-seam crossfade, at the source sample rate
CHOIR_BASE_HZ = 98.0        # the loop region's own pitch (G2), measured by
                            # autocorrelation -- see the block comment above


def clamp8(x: float) -> int:
    v = int(round(x * 127.0))
    if v > 127:
        v = 127
    if v < -128:
        v = -128
    return v


def _crossfade_loop(samples, crossfade_n: int) -> list:
    """Standard sample-loop crossfade: blends a segment's tail into its own
    head, shortening it by `crossfade_n` samples, so the result loops with no
    seam click. A real recording (unlike this file's synthesised Strings
    loop, built from integer-Hz partials specifically so a raw wraparound is
    already seamless) has no such guarantee -- wrapping it as-is would stitch
    together two unrelated waveform points every time the loop repeats.

    Why this actually works: the returned buffer has length M = L -
    crossfade_n, defined as out[i] = samples[i + crossfade_n] for the bulk of
    it, with the LAST crossfade_n samples overwritten by a linear blend of
    the segment's true tail and its true head. At the blend's own two edges
    that means out[M-1] is (mostly) samples[crossfade_n-1] and out[0] is
    samples[crossfade_n] -- adjacent samples in the ORIGINAL recording, and
    therefore already continuous, rather than two arbitrary points.
    """
    L = len(samples)
    X = crossfade_n
    M = L - X
    out = list(samples[X:L])   # length M; out[M-X:] gets overwritten below
    for k in range(X):
        tail = samples[L - X + k]
        head = samples[k]
        frac = k / X
        out[M - X + k] = tail * (1.0 - frac) + head * frac
    return out


def gen_choir() -> list:
    """Load, mono-mix, loop-crossfade and downsample the real choir
    recording described in the module-level comment above. Returns a list of
    floats normalised to +/-0.85, same convention as gen_strings()."""
    w = wave.open(CHOIR_SOURCE_WAV, "rb")
    nch, sw, src_sr, nframes = (w.getnchannels(), w.getsampwidth(),
                                 w.getframerate(), w.getnframes())
    raw = w.readframes(nframes)
    w.close()
    mono_bytes = audioop.tomono(raw, sw, 0.5, 0.5) if nch == 2 else raw

    start = int(CHOIR_LOOP_START_S * src_sr)
    length = int(CHOIR_LOOP_LEN_S * src_sr)
    seg_bytes = mono_bytes[start * sw : (start + length) * sw]
    seg = list(struct.unpack("<%dh" % (len(seg_bytes) // 2), seg_bytes))

    looped = _crossfade_loop(seg, int(CHOIR_CROSSFADE_S * src_sr))

    looped_int = [max(-32768, min(32767, int(round(v)))) for v in looped]
    looped_bytes = struct.pack("<%dh" % len(looped_int), *looped_int)
    resampled_bytes, _ = audioop.ratecv(looped_bytes, sw, 1, src_sr, SR, None)
    resampled = struct.unpack("<%dh" % (len(resampled_bytes) // 2), resampled_bytes)

    peak = max(abs(v) for v in resampled) or 1
    return [(v / peak) * 0.85 for v in resampled]


def gen_strings(n: int) -> list:
    """Several slightly-detuned sawtooth-ish ensembles (a handful of odd+even
    harmonics each) for the tape-string-section character, plus slow shared
    tremolo. Detunes are all integer Hz so the loop stays seamless."""
    voices_hz = [196, 197, 233, 294, 295, 392]
    n_harmonics = 5
    tremolo_hz = 4.0
    tremolo_depth = 0.05
    out = [0.0] * n
    for vi, hz in enumerate(voices_hz):
        amp0 = 0.5 / len(voices_hz)
        phase0 = vi * 1.1
        for h in range(1, n_harmonics + 1):
            hamp = amp0 / h
            for i in range(n):
                t = i / SR
                trem = 1.0 + tremolo_depth * math.sin(2 * math.pi * tremolo_hz * t + vi)
                out[i] += hamp * trem * math.sin(2 * math.pi * hz * h * t + phase0)
    peak = max(abs(x) for x in out) or 1.0
    return [x / peak * 0.85 for x in out]


def emit(name: str, samples: list, sr: int, base_hz: float) -> str:
    bytes_ = [clamp8(s) & 0xFF for s in samples]
    lines = []
    lines.append(f"#define {name}_LEN {len(bytes_)}")
    lines.append(f"#define {name}_SR {sr}")
    lines.append(f"#define {name}_BASE_HZ {base_hz:.1f}f")
    lines.append(f"static const int8_t {name}_DATA[{name}_LEN] = {{")
    for row in range(0, len(bytes_), 20):
        chunk = bytes_[row:row + 20]
        signed = [b - 256 if b > 127 else b for b in chunk]
        lines.append("    " + ",".join(str(v) for v in signed) + ",")
    lines.append("};")
    return "\n".join(lines)


def main():
    choir = gen_choir()
    strings = gen_strings(N)
    header = []
    header.append("#pragma once")
    header.append("#include <stdint.h>")
    header.append("")
    header.append("// Generated by tools/gen_mellotron_samples.py — do not hand-edit.")
    header.append("// Choir: a real recorded sample, loop-crossfaded and downsampled to")
    header.append("// 11025 Hz signed 8-bit PCM. Strings: synthesised, same format, an")
    header.append("// exact 1-second seamless loop -- see the script's own docstring.")
    header.append("")
    header.append(emit("MEL_CHOIR", choir, SR, CHOIR_BASE_HZ))
    header.append("")
    header.append(emit("MEL_STRINGS", strings, SR, 196.0))
    header.append("")
    with open("mellotron_samples.h", "w") as f:
        f.write("\n".join(header) + "\n")
    print(f"wrote mellotron_samples.h: choir {len(choir)} samples, strings {len(strings)} samples")


if __name__ == "__main__":
    main()
