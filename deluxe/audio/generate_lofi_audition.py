"""Separate, original early-computer sound studies. Does not alter the game pack.

Python standard library only. Render with: python deluxe/audio/generate_lofi_audition.py
"""
from pathlib import Path
import html
import json
import math
import generate_audition as synth

RATE = synth.RATE
OUT = Path(__file__).resolve().parent / "audition-lofi"


def chip(duration, frequency, voice="triangle", end=None, decay=3):
    """Few harmonics, slight tuning wander, gently gated notes; no piercing square edges."""
    partials = ((1, 1), (3, -.111), (5, .04), (7, -.0204)) if voice == "triangle" else (
        (1, 1), (3, .22), (5, .08), (7, .025))
    phase = 0.0
    result = []
    end = frequency if end is None else end
    for i in range(round(duration * RATE)):
        t = i / RATE
        hz = (frequency + (end-frequency)*t/duration) * (1 + .0012*math.sin(math.tau*4.7*t))
        phase += math.tau*hz/RATE
        envelope = min(1, t/.005) * min(1, (duration-t)/.024) * math.exp(-decay*t)
        result.append(envelope*sum(g*math.sin(phase*p) for p, g in partials))
    return result


def small_speaker(data, home=False):
    """Rounded, narrow-band sound with a little coarse DAC texture, not a hiss bed."""
    low1 = low2 = bass = held = 0.0
    cutoff = 2300 if home else 1800
    alpha = 1-math.exp(-math.tau*cutoff/RATE)
    bass_alpha = 1-math.exp(-math.tau*100/RATE)
    result = []
    for i, value in enumerate(data):
        if i % 4 == 0:
            held = round(value*100)/100
        value = .8*value + .2*held
        low1 += alpha*(value-low1)
        low2 += alpha*(low1-low2)
        bass += bass_alpha*(low2-bass)
        result.append(math.tanh((low2-bass)*1.25)/1.25)
    return result


LABELS = {
    "ui": ("Interface click", "Soft key", "Micro switch"),
    "target": ("Target confirmed", "Terminal ready", "Address locked"),
    "potion": ("Potion use", "Warm refill", "Little replenishment"),
    "melee": ("Melee impact", "Damped knock", "Low register hit"),
    "spell": ("Spell cast", "Quiet computation", "Three-channel charm"),
    "crt": ("CRT shutdown", "Relay & decay", "Monitor off"),
}


def recipe(group, variant):
    v = variant
    voice = "pulse" if v else "triangle"
    n = synth.noise
    if group == "ui":
        return synth.mix(.09, (chip(.045, 660 if v else 480, voice, decay=28), .5, 0),
                         (n(.035, 1200, 110, 120+v), .16, 0)), .22
    if group == "target":
        return synth.mix(.28, (chip(.07, 550 if v else 440, voice), .5, 0),
                         (chip(.095, 825 if v else 660, voice, decay=6), .42, .09)), .30
    if group == "potion":
        # A modest stepped refill cue; lower pitches and no glittering tail.
        notes = (220, 277.18, 329.63) if not v else (261.63, 329.63, 392)
        layers = [(chip(.09, note, voice, end=note*1.04, decay=9), .4, .03+i*.075)
                  for i, note in enumerate(notes)]
        layers.append((n(.07, 650, 42, 130+v), .35, 0))
        return synth.mix(.40, *layers), .38
    if group == "melee":
        return synth.mix(.25,
            (chip(.12, 170 if v else 130, voice, end=75, decay=20), .8, .006),
            (n(.09, 2100 if v else 1250, 42, 140+v), .8, 0),
            (chip(.055, 380 if v else 310, voice, decay=38), .12, .003)), .48
    if group == "spell":
        notes = (330, 440, 660, 495) if not v else (392, 493.88, 587.33, 784)
        layers = [(chip(.10, note, voice, decay=6), .38, i*.055)
                  for i, note in enumerate(notes)]
        layers += [(chip(.32, 165 if not v else 196, "triangle", decay=10), .20, 0),
                   (n(.22, 900, 12, 150+v, .025), .10, 0)]
        return synth.mix(.49, *layers), .40
    # Quick electrical release, not a long cinematic power-down sweep.
    return synth.mix(.21,
        (n(.025, 1800, 120, 160+v), .60, 0),
        (chip(.07, 150 if v else 120, "triangle", end=65, decay=38), .55, 0),
        (chip(.10, 1900 if v else 1400, "triangle", end=500, decay=36), .09, .008)), .38


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    synth.OUT = OUT
    reel = [0.] * round(.25*RATE)
    manifest = []
    cards = []
    for group, (title, a, b) in LABELS.items():
        players = []
        for v, label in enumerate((a, b)):
            filename = f"{group}_{'ab'[v]}.wav"
            data, peak = recipe(group, v)
            samples = synth.write(filename, small_speaker(data, bool(v)), peak)
            start = len(reel)/RATE
            reel.extend(samples)
            reel.extend([0.]*round((.5 if v == 0 else .85)*RATE))
            manifest.append({"file": filename, "event": group, "treatment": label,
                             "reel_start": round(start, 3), "seconds": round(len(samples)/RATE, 3),
                             "peak_dbfs": round(20*math.log10(peak), 2)})
            players.append(f'<div><strong>{"AB"[v]} · {html.escape(label)}</strong>'
                           f'<audio controls preload="none" src="{filename}"></audio></div>')
        cards.append(f'<section><h2>{title}</h2><div class="pair">{"".join(players)}</div></section>')
    synth.write("audition_reel.wav", reel, max(map(abs, reel)))
    (OUT/"manifest.json").write_text(json.dumps(manifest, indent=2)+"\n", encoding="utf-8")
    page = '''<!doctype html><html lang="en"><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>AnybandUI / Quiet computing</title><style>
:root{color-scheme:dark}*{box-sizing:border-box}body{max-width:920px;margin:40px auto;
padding:0 24px;background:#141815;color:#e2e5db;font:16px/1.6 system-ui,sans-serif}
header{padding-bottom:24px;border-bottom:1px solid #3b493d}h1{font-size:40px;margin:4px 0}
.eyebrow,strong{color:#9db89b}.eyebrow{font:12px monospace;letter-spacing:.15em}
p{color:#aeb7aa;max-width:730px}section{margin:18px 0;padding:20px;background:#1d251e;
border:1px solid #354436;border-radius:8px}h2{font-size:19px;margin:0 0 14px}
.pair{display:grid;grid-template-columns:1fr 1fr;gap:24px}strong{display:block;margin:0 0 10px;
font-size:14px}audio{width:100%;height:38px}footer{font-size:13px;color:#aeb7aa;margin:30px 0}
a{color:#b2cbaa}@media(max-width:620px){.pair{grid-template-columns:1fr}h1{font-size:30px}}
</style><header><div class="eyebrow">AnybandUI / SOUND STUDIES 02</div>
<h1>Quiet computing</h1><p>Understated, warm and a little imperfect. Simple gated tones,
small-speaker colour and restrained early-DAC texture. No continuous tape hiss,
long reverb or arcade fanfares.</p><p>A: soft terminal. B: modest home computer.
These are new auditions; the in-game sound pack is unchanged.</p>
<strong>Complete reel · A then B within each category below</strong>
<audio controls preload="metadata" src="audition_reel.wav"></audio></header>
''' + "\n".join(cards) + '''<footer>Original synthesis only · 48 kHz mono WAV ·
<a href="../audition/index.html">Compare the first sound studies</a></footer>
<script>document.addEventListener('play',e=>{if(e.target.tagName==='AUDIO')
document.querySelectorAll('audio').forEach(a=>{if(a!==e.target)a.pause()})},true)</script></html>'''
    (OUT/"index.html").write_text(page, encoding="utf-8")
    print(f"Generated 12 new sounds and {len(reel)/RATE:.2f}s reel: {OUT}")


if __name__ == "__main__":
    main()
