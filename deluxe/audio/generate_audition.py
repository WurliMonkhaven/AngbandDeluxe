"""Original procedural sound sketches; Python standard library only.

Run from any directory to rebuild audition/ (48 kHz, mono, signed 16-bit WAV).
No recordings, external samples, pretrained generators, or runtime game changes.
"""
from pathlib import Path
import array
import html
import json
import math
import random
import sys
import wave

RATE = 48000
TAU = math.tau
OUT = Path(__file__).resolve().parent / "audition"


def tone(length, start, end=None, decay=10, attack=.002, partials=((1, 1),)):
    end = start if end is None else end
    phase = 0.0
    data = []
    for i in range(round(length * RATE)):
        t = i / RATE
        phase += TAU * (start + (end - start) * t / length) / RATE
        envelope = min(1, t / attack) * math.exp(-decay * t)
        data.append(envelope * sum(g * math.sin(phase * p) for p, g in partials))
    return data


def noise(length, cutoff, decay, seed, attack=.001, high=False):
    rng = random.Random(seed)
    alpha = 1 - math.exp(-TAU * cutoff / RATE)
    low = 0.0
    data = []
    for i in range(round(length * RATE)):
        t = i / RATE
        white = rng.uniform(-1, 1)
        low += alpha * (white - low)
        sample = white - low if high else low
        data.append(sample * min(1, t / attack) * math.exp(-decay * t))
    return data


def mix(length, *layers):
    data = [0.0] * round(length * RATE)
    for sound, gain, delay in layers:
        offset = round(delay * RATE)
        for i, sample in enumerate(sound[:max(0, len(data) - offset)]):
            data[i + offset] += sample * gain
    return data


def space(data, wet=.12):
    # Short, irregular, low-passed reflections: atmosphere without a long wash.
    reflections = []
    low = 0.0
    for x in data:
        low += .12 * (x - low)
        reflections.append(low)
    return mix(len(data) / RATE + .18, (data, 1, 0),
               (reflections, wet, .037), (reflections, wet * .6, .079),
               (reflections, wet * .3, .131))


def write(name, data, peak):
    # Remove DC; fade boundaries even when a recipe is cut short.
    mean = sum(data) / len(data)
    data = [x - mean for x in data]
    fade = min(round(.012 * RATE), len(data) // 4)
    for i in range(fade):
        data[i] *= min(1, i / 48)
        data[-1-i] *= .5 - .5 * math.cos(math.pi * i / fade)
    scale = peak / max(max(abs(x) for x in data), 1e-9)
    data = [x * scale for x in data]
    pcm = array.array("h", (round(x * 32767) for x in data))
    if sys.byteorder != "little":
        pcm.byteswap()
    with wave.open(str(OUT / name), "wb") as f:
        f.setparams((1, 2, RATE, 0, "NONE", "not compressed"))
        f.writeframes(pcm.tobytes())
    return data


def recipes():
    for v in range(2):
        # A: dry, tactile. B: brighter, electrically coloured.
        yield "ui", v, mix(.12,
            (noise(.05, 1900 if not v else 4200, 120, 10+v), .6, 0),
            (tone(.10, 620 if not v else 1380, decay=75), .35, .003)), .32
        yield "target", v, mix(.30,
            (tone(.16, 740 if not v else 1120, decay=24), .55, 0),
            (tone(.20, 1110 if not v else 1680, decay=22), .42, .065),
            (noise(.03, 2500, 140, 20+v), .18, 0)), .42
        potion = [(tone(.10, 480, 180, 35), .5, 0),
                  (noise(.08, 1100, 42, 30+v), .8, 0)]
        for j in range(5):
            potion.append((tone(.10, 260+j*47, 430+j*75, 32),
                           .3/(1+j*.15), .065+j*.043))
        if v:
            potion.append((tone(.5, 1800, 2200, 8, .03,
                                 ((1,.4),(1.501,.2),(2.003,.1))), .3, .18))
        yield "potion", v, space(mix(.8, *potion), .08), .55
        yield "melee", v, space(mix(.42,
            (noise(.07, 1500, 50, 40+v), 1.2, 0),
            (tone(.19, 150, 48, 24), .8, .004),
            (noise(.18, 3800, 30, 50+v, high=True), .22, .01),
            (tone(.35, 790 if not v else 1250, decay=20,
                  partials=((1,.3),(1.47,.18),(2.09,.09),(2.71,.05))),
             .35 if not v else .9, .008)), .09), .7
        if not v:
            spell = mix(.85, (noise(.75, 1600, 5, 60, .065), .65, 0),
                (tone(.42, 240, 760, 4, .045), .2, 0),
                (tone(.25, 160, 60, 15), .55, .14),
                (noise(.24, 4400, 20, 61), .45, .14))
        else:
            spell = mix(.85, (noise(.30, 3100, 13, 62, .01, True), .35, 0),
                (tone(.60, 980, 1450, 6, .015,
                      ((1,.5),(1.501,.28),(2.003,.15))), .55, 0),
                (tone(.32, 100, 45, 15), .5, .01),
                (tone(.45, 1960, 2200, 9), .12, .09))
        yield "spell", v, space(spell, .16), .65
        yield "crt", v, mix(.24,
            (noise(.025, 5500, 170, 70+v, high=True), .65, 0),
            (tone(.075, 125, 40, 48), .65, .002),
            (tone(.14, 3400 if not v else 4600, 450, 38), .14, .005),
            (tone(.11, 1800, 650, 65), .08, .028)), .5


LABELS = {
    "ui": ("Interface click", "Dry switch", "Glass switch"),
    "target": ("Target confirmed", "Soft lock", "Tuning lock"),
    "potion": ("Potion use", "Cork & bubbles", "Alchemical bubbles"),
    "melee": ("Melee impact", "Heavy contact", "Metallic contact"),
    "spell": ("Spell cast", "Ember pulse", "Arcane discharge"),
    "crt": ("CRT shutdown", "Relay snap", "Charged snap"),
}


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    manifest = []
    reel = []
    for group, variant, data, peak in recipes():
        letter = "ab"[variant]
        filename = f"{group}_{letter}.wav"
        samples = write(filename, data, peak)
        reel.extend(samples)
        reel.extend([0.] * round(.65 * RATE))
        manifest.append({"file": filename, "event": group,
                         "treatment": LABELS[group][variant+1],
                         "seconds": round(len(samples)/RATE, 3),
                         "peak_dbfs": round(20*math.log10(peak), 2)})
    # Keep individual audition levels, rather than renormalizing the sequence.
    write("audition_reel.wav", reel, max(abs(x) for x in reel))
    (OUT / "manifest.json").write_text(json.dumps(manifest, indent=2)+"\n", encoding="utf-8")
    cards = []
    for group, (title, a, b) in LABELS.items():
        players = "".join(f'<div><strong>{letter.upper()} · {html.escape(label)}</strong>'
            f'<audio controls preload="none" src="{group}_{letter}.wav"></audio></div>'
            for letter, label in (("a", a), ("b", b)))
        cards.append(f'<section><h2>{html.escape(title)}</h2><div class="pair">{players}</div></section>')
    page = '''<!doctype html><html lang="en"><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Deluxe — Sound studies</title><style>
:root{color-scheme:dark}*{box-sizing:border-box}body{margin:40px auto;padding:0 24px;
max-width:940px;background:#0e1317;color:#e9e6df;font:16px/1.6 system-ui,sans-serif}
header{border-bottom:1px solid #34453c;padding-bottom:24px}h1{font-size:42px;margin:0}
.eyebrow{color:#80b78d;letter-spacing:.2em;text-transform:uppercase;font-size:12px}
p{color:#aebbb7;max-width:720px}section{background:#18201f;border:1px solid #2e3d36;
border-radius:10px;padding:20px 24px;margin:18px 0}h2{margin:0 0 14px;font-size:20px}
.pair{display:grid;grid-template-columns:1fr 1fr;gap:24px}strong{display:block;color:#80b78d;
font-size:14px;margin-bottom:10px}audio{width:100%;height:38px}.reel{margin-top:24px}
footer{color:#8e9c95;font-size:13px;margin:32px 0}@media(max-width:620px){.pair{grid-template-columns:1fr}h1{font-size:32px}}
</style><header><div class="eyebrow">Angband Deluxe / Audio sketches 01</div>
<h1>Sound studies</h1><p>Six moments. Two original synthesized treatments each.
A leans dry and tactile; B adds glass, metal and electrical colour.
These are audition sketches, not yet game sounds.</p>
<div class="reel"><strong>Play the complete set · A then B for each section below</strong>
<audio controls preload="metadata" src="audition_reel.wav"></audio></div></header>
''' + "\n".join(cards) + '''<footer>No recordings or third-party samples used.
48 kHz mono WAV. Playback levels are deliberately quieter for interface cues.
Only one player runs at a time.</footer><script>
document.addEventListener('play',e=>{if(e.target.tagName==='AUDIO')
document.querySelectorAll('audio').forEach(a=>{if(a!==e.target)a.pause()})},true);
</script></html>'''
    (OUT / "index.html").write_text(page, encoding="utf-8")
    print(f"Generated {len(manifest)} sounds, audition reel and player in {OUT}")


if __name__ == "__main__":
    main()
