#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# IL CIAPPA: makes app/voice_data.h, the voices that say "CIAPPA".
#
# Each voice is rendered by espeak-ng (Italian, "ciàppa" -> tS'ap:a), trimmed, normalised and analysed offline:
#   - pitch marks (one per glottal period in the voiced parts, every 5 ms in the unvoiced ones) for the PSOLA engine,
#     so the firmware can sing the word at any pitch and stretch it to any length, formants kept;
#   - the word's segments: tS (the "CI"), the first A, the PP closure and burst, the last A;
#   - the loop inside the first A, for CIAAAAAAPPA while a key is held.
# The header is committed: the firmware build doesn't need espeak-ng.
#
#   tools/make_voice.py ESPEAK_NG_BINARY ESPEAK_DATA_PARENT app/voice_data.h [--wav DIR]
import os
import subprocess
import sys
import tempfile
import wave

import numpy as np

SR = 22050
WORD = "ciàppa"
# name on screen, espeak voice, speed (wpm), pitch (0..99)
VOICES = [
    ("UOMO", "it", 120, 40),
    ("DONNA", "it+f3", 120, 55),
    ("NONNA", "it+grandma", 110, 50),
    ("NONNO", "it+grandpa", 110, 35),
]


def render(espeak, data, voice, speed, pitch):
    with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as f:
        path = f.name
    subprocess.run([espeak, "--path=" + data, "-v", voice, "-s", str(speed), "-p", str(pitch), "-a", "180",
                    "-w", path, WORD], check=True)
    w = wave.open(path)
    assert w.getframerate() == SR and w.getsampwidth() == 2 and w.getnchannels() == 1
    x = np.frombuffer(w.readframes(w.getnframes()), np.int16).astype(np.float64) / 32768.0
    os.unlink(path)
    return x


def frames(x, n=220, hop=110):
    return [(i, x[i:i + n]) for i in range(0, len(x) - n, hop)]


def analyse(x):
    """voicing and period every 5 ms (hop 110)"""
    hop, n = 110, 662          # 30 ms window: two periods at 70 Hz
    lmin, lmax = SR // 450, SR // 65
    out = []
    peak = np.max(np.abs(x))
    for i in range(0, len(x), hop):
        seg = x[i:i + n]
        if len(seg) < n:
            seg = np.pad(seg, (0, n - len(seg)))
        e = np.sqrt(np.mean(seg ** 2))
        zc = np.mean(np.abs(np.diff(np.sign(seg)))) / 2
        # normalised cross-correlation over a fixed span (no bias against long lags)
        span = 300
        a0 = x[i:i + span]
        if len(a0) < span or e == 0:
            out.append((i, e, zc, 0.0, 0))
            continue
        best, lag = -1.0, 0
        ea = np.dot(a0, a0)
        for l in range(lmin, lmax):
            b = x[i + l:i + l + span]
            if len(b) < span:
                break
            d = np.sqrt(ea * np.dot(b, b))
            r = np.dot(a0, b) / d if d > 0 else 0
            if r > best + 1e-9:
                best, lag = r, l
        out.append((i, e, zc, best, lag))
    voiced = [(e > 0.04 * peak and r > 0.6 and zc < 0.2) for (_, e, zc, r, _) in out]
    # octave errors: each voiced frame's period against the median of its voiced neighbours
    lags = np.array([o[4] for o in out], float)
    fixed = []
    for k, o in enumerate(out):
        nb = [lags[j] for j in range(max(0, k - 4), min(len(out), k + 5)) if voiced[j]]
        med = float(np.median(nb)) if nb else lags[k]
        lag = lags[k]
        if voiced[k] and med > 0 and abs(lag - med) > 0.2 * med:
            lag = med
        fixed.append((o[0], o[1], o[2], o[3], int(round(lag))))
    out = fixed
    # tidy: no voiced island shorter than 20 ms, no gap shorter than 10 ms
    v = voiced[:]
    for k in range(1, len(v) - 1):
        if v[k - 1] and v[k + 1]:
            v[k] = True
    run = 0
    for k in range(len(v) + 1):
        if k < len(v) and v[k]:
            run += 1
        else:
            if 0 < run < 4:
                for j in range(k - run, k):
                    v[j] = False
            run = 0
    return out, v


def trim(x):
    env = np.convolve(np.abs(x), np.ones(110) / 110, "same")
    on = np.where(env > 0.02 * env.max())[0]
    a, b = max(0, on[0] - 220), min(len(x), on[-1] + 440)
    y = x[a:b].copy()
    fade = 64
    y[:fade] *= np.linspace(0, 1, fade)
    y[-fade:] *= np.linspace(1, 0, fade)
    return y / np.max(np.abs(y)) * 0.89


def runs(flags):
    r, start = [], None
    for k, f in enumerate(list(flags) + [False]):
        if f and start is None:
            start = k
        elif not f and start is not None:
            r.append((start, k))
            start = None
    return r


def pitch_marks(x, info, voiced):
    """(position, period, voiced) per mark, positions increasing"""
    hop = 110
    marks = []
    vr = runs(voiced)
    pos = 0

    def period_at(p):
        k = min(len(info) - 1, p // hop)
        return info[k][4] or 110

    def unvoiced_to(end):
        nonlocal pos
        while pos < end:
            marks.append((pos, 110, 0))
            pos += 110

    for (a, b) in vr:
        sa, sb = a * hop, min(len(x), b * hop + 440)
        unvoiced_to(sa)
        p = period_at(sa + 220)
        # first mark: the biggest peak in the first period
        t = sa + int(np.argmax(x[sa:sa + p]))
        if marks and t - marks[-1][0] < 40:
            marks.pop()
        while t < sb:
            p = period_at(t)
            marks.append((t, p, 1))
            lo, hi = t + int(0.8 * p), t + int(1.2 * p) + 1
            if hi >= len(x):
                break
            t = lo + int(np.argmax(x[lo:hi]))
        pos = marks[-1][0] + marks[-1][1]
    unvoiced_to(len(x))
    # periods as the spacing to the neighbours (what PSOLA's window needs)
    out = []
    for k, (t, p, v) in enumerate(marks):
        if v:
            dl = t - marks[k - 1][0] if k else p
            dr = marks[k + 1][0] - t if k + 1 < len(marks) else p
            p = int(round(min(max(dl, dr), 1.3 * p)))
        out.append((t, max(30, min(1000, p)), v))
    return out


def segments(x, info, voiced):
    """tS start, A1 start, A1 end, A2 start, end (samples)"""
    hop = 110
    vr = [r for r in runs(voiced) if r[1] - r[0] >= 4]
    assert vr, "no vowel"
    a1 = vr[0]
    env = np.array([e for (_, e, _, _, _) in info])
    onset = int(np.argmax(env > 0.05 * env.max()))
    # the PP closure: the longest quiet stretch after the first A; the last A: the first voicing after it
    quiet = [k > a1[1] and e < 0.03 * env.max() for k, e in enumerate(env)]
    qr = max(runs(quiet), key=lambda r: r[1] - r[0])
    a2 = next(k for k in range(qr[1], len(voiced)) if voiced[k])
    return [onset * hop, a1[0] * hop, a1[1] * hop, a2 * hop, len(x)]


def build_voice(espeak, data, name, voice, speed, pitch, wavdir):
    x = trim(render(espeak, data, voice, speed, pitch))
    info, voiced = analyse(x)
    seg = segments(x, info, voiced)
    marks = pitch_marks(x, info, voiced)
    # robot voices come out with no voicing at all from the analysis: they still need marks to sing
    a1, a1e = seg[1], seg[2]
    loop_a = a1 + (a1e - a1) * 38 // 100
    loop_b = a1 + (a1e - a1) * 66 // 100
    f0 = [SR / p for (t, p, v) in marks if v]
    if wavdir:
        w = wave.open(os.path.join(wavdir, name.lower() + ".wav"), "wb")
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes((x * 32767).astype(np.int16).tobytes())
        w.close()
    print("%-6s %5d samples (%.2f s), %3d marks (%d voiced, f0 %.0f Hz), seg %s" % (
        name, len(x), len(x) / SR, len(marks), len(f0), np.median(f0) if f0 else 0, seg), file=sys.stderr)
    return dict(name=name, pcm=(x * 32767).astype(np.int16), marks=marks, seg=seg, loop=(loop_a, loop_b),
                f0=float(np.median(f0)) if f0 else 120.0)


def emit(voices, path):
    L = []
    L.append("/* SPDX-License-Identifier: MPL-2.0 */")
    L.append("/* Generated by tools/make_voice.py from espeak-ng (GPL-3.0-or-later; its output is not covered) saying")
    L.append(" * \"%s\". Do not edit. */" % WORD)
    L.append("#pragma once")
    L.append("#include <stdint.h>")
    L.append("#define VOICE_SR %d" % SR)
    L.append("#define NVOICES %d" % len(voices))
    L.append("typedef struct { uint16_t pos; uint16_t per; } vmark_t;   /* per: the period in samples, bit 15 voiced */")
    L.append("typedef struct {")
    L.append("    const char *name;")
    L.append("    const int16_t *pcm; uint32_t len;")
    L.append("    const vmark_t *mark; uint32_t nmark;")
    L.append("    uint32_t seg[5];        /* tS, A1, end of A1 (PP), A2, end */")
    L.append("    uint32_t loop_a, loop_b; /* inside A1: CIAAAAPPA */")
    L.append("    float f0;               /* the voice's own pitch, Hz */")
    L.append("} voice_t;")
    for v in voices:
        n = v["name"].lower()
        L.append("static const int16_t PCM_%s[%d] = {" % (n, len(v["pcm"])))
        pcm = v["pcm"].tolist()
        for i in range(0, len(pcm), 16):
            L.append("    " + ",".join(str(s) for s in pcm[i:i + 16]) + ",")
        L.append("};")
        L.append("static const vmark_t MARK_%s[%d] = {" % (n, len(v["marks"])))
        m = v["marks"]
        for i in range(0, len(m), 8):
            L.append("    " + " ".join("{%d,%d}," % (t, p | (0x8000 if v else 0)) for (t, p, v) in m[i:i + 8]))
        L.append("};")
    L.append("static const voice_t VOICES[NVOICES] = {")
    for v in voices:
        n = v["name"].lower()
        L.append('    {"%s", PCM_%s, %d, MARK_%s, %d, {%s}, %d, %d, %.1ff},' % (
            v["name"], n, len(v["pcm"]), n, len(v["marks"]), ", ".join(str(s) for s in v["seg"]),
            v["loop"][0], v["loop"][1], v["f0"]))
    L.append("};")
    with open(path, "w") as f:
        f.write("\n".join(L) + "\n")


def main():
    espeak, data, out = sys.argv[1:4]
    wavdir = sys.argv[sys.argv.index("--wav") + 1] if "--wav" in sys.argv else None
    if wavdir:
        os.makedirs(wavdir, exist_ok=True)
    voices = [build_voice(espeak, data, *v, wavdir) for v in VOICES]
    emit(voices, out)
    total = sum(len(v["pcm"]) * 2 + len(v["marks"]) * 4 for v in voices)
    print("%s: %d bytes of flash" % (out, total), file=sys.stderr)


if __name__ == "__main__":
    main()
