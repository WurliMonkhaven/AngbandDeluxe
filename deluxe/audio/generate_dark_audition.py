"""Sound studies 03: dark, dry, coarse electronic cues. Original synthesis only."""
from pathlib import Path
import html
import json
import math
import random
import generate_audition as s

OUT = Path(__file__).resolve().parent / "audition-dark"
RATE = s.RATE


def circuit(length, frequency, seed, decay=16, fall=.5):
    """An unstable pulse circuit: detuned edges and brief, irregular dropouts."""
    rng = random.Random(seed)
    phase = other = 0.
    gate = 1.
    data = []
    for i in range(round(length*RATE)):
        t = i/RATE
        if i % 360 == 0:
            gate = .25 if rng.random() < .22 else rng.uniform(.65, 1)
        hz = frequency * (1 - fall*t/length)
        phase += hz/RATE
        other += hz*1.071/RATE
        pulse = (1 if phase % 1 < .29 else -.408)
        rough = .7*pulse + .3*math.sin(math.tau*other)
        envelope = min(1, t/.0025)*min(1, (length-t)/.018)*math.exp(-decay*t)
        data.append(rough*gate*envelope)
    return data


def grit(data, variant):
    """Coarse held samples, asymmetric drive and dark reconstruction filtering."""
    step = 7 if not variant else 9
    levels = 24 if not variant else 17
    held = low = dc = 0.
    alpha = 1-math.exp(-math.tau*(2100 if not variant else 1700)/RATE)
    bass = 1-math.exp(-math.tau*105/RATE)
    out = []
    for i, x in enumerate(data):
        if i % step == 0:
            drive = math.tanh(x*2.6 + .12)-math.tanh(.12)
            held = round(drive*levels)/levels
        low += alpha*(held-low)
        dc += bass*(low-dc)
        out.append(low-dc)
    return out


LABELS = {
    "ui": ("Interface click", "Contact", "Latch"),
    "target": ("Target confirmed", "Acquire", "Lock"),
    "potion": ("Potion use", "Intake", "Transfer"),
    "melee": ("Melee impact", "Fault", "Fracture"),
    "spell": ("Spell cast", "Intrusion", "Discharge"),
    "crt": ("CRT shutdown", "Cut power", "Disconnect"),
}


def recipe(group, v):
    n, t, mix = s.noise, s.tone, s.mix
    if group == "ui":
        return mix(.075, (n(.026, 2500, 115, 201+v), .8, 0),
                   (circuit(.046, 370-v*60, 210+v, 58), .3, .003)), .21
    if group == "target":
        return mix(.22, (circuit(.075, 570-v*80, 220+v, 24, .08), .45, 0),
                   (circuit(.055, 270-v*35, 222+v, 38), .25, .085),
                   (n(.035, 1500, 80, 224+v), .24, .082)), .26
    if group == "potion":
        return mix(.39, (n(.22, 950, 13, 230+v, .028), .6, 0),
                   (circuit(.20, 215+v*31, 232+v, 10, -.15), .18, .018),
                   (n(.045, 2000, 80, 234+v), .45, .18),
                   (t(.13, 145, 83, 25, .003), .35, .185)), .33
    if group == "melee":
        return mix(.23, (n(.085, 2600, 48, 240+v), .95, 0),
                   (circuit(.14, 195+v*45, 242+v, 27, .6), .7, .002),
                   (n(.09, 650, 32, 244+v), .25, .021)), .43
    if group == "spell":
        # A fragmented, downward electrical burst, without pitched reward notes.
        layers = [(circuit(.24, 430-v*85, 250+v, 12, .63), .4, 0),
                  (n(.25, 1800, 12, 252+v, .013), .3, 0)]
        for j, delay in enumerate((.045, .083, .137)):
            layers.append((n(.036, 2600-j*400, 90, 260+v*5+j), .32-j*.07, delay))
        layers.append((t(.18, 165, 82, 18, .004), .32, .045))
        return mix(.40, *layers), .36
    return mix(.17, (n(.018, 2600, 140, 280+v), .8, 0),
               (circuit(.062, 280+v*60, 282+v, 42, .73), .3, .004),
               (t(.085, 1700, 530, 55, .002), .045, .009),
               (t(.08, 135, 58, 38, .002), .3, 0)), .34


def main(output=OUT, recipe_fn=recipe, process_fn=grit, labels=LABELS,
         title="Black circuit", edition="03",
         description="Dark contacts, coarse pulses, unstable electronics. Dry and brief. No bright arpeggios or cheerful confirmation melodies.",
         variants="A: clipped circuitry. B: rougher, lower-fidelity circuitry. Both deliberately restrained."):
    output.mkdir(parents=True, exist_ok=True)
    s.OUT = output
    reel = [0.]*round(.25*RATE)
    manifest, cards = [], []
    for group, (title, *names) in labels.items():
        players = []
        for v, name in enumerate(names):
            filename = f"{group}_{'ab'[v]}.wav"
            data, peak = recipe_fn(group, v)
            data = s.write(filename, process_fn(data, v), peak)
            manifest.append({"file": filename, "event": group, "treatment": name,
                             "reel_start": round(len(reel)/RATE, 3), "seconds": len(data)/RATE})
            reel.extend(data)
            reel.extend([0.]*round((.55 if not v else .85)*RATE))
            players.append(f'<div><strong>{"AB"[v]} · {html.escape(name)}</strong>'
                           f'<audio controls preload="none" src="{filename}"></audio></div>')
        cards.append(f'<section><h2>{title}</h2><div class="pair">{"".join(players)}</div></section>')
    s.write("audition_reel.wav", reel, max(map(abs, reel)))
    (output/"manifest.json").write_text(json.dumps(manifest, indent=2)+"\n", encoding="utf-8")
    page = '''<!doctype html><html lang="en"><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Deluxe / @TITLE@</title><style>
:root{color-scheme:dark}*{box-sizing:border-box}body{max-width:900px;margin:40px auto;
padding:0 24px;background:#0b1011;color:#c8d1ca;font:15px/1.6 monospace}
header{border-bottom:1px solid #34443e;padding-bottom:24px}h1{font-size:36px;margin:8px 0}
.eyebrow,strong{color:#80a58f}p,footer{color:#91a29a}section{margin:18px 0;padding:20px;
border:1px solid #293a33;background:#121b18}h2{font-size:17px;margin:0 0 14px}
.pair{display:grid;grid-template-columns:1fr 1fr;gap:24px}strong{display:block;margin-bottom:9px}
audio{width:100%;height:38px}footer{margin-top:30px;font-size:12px}
@media(max-width:620px){.pair{grid-template-columns:1fr}}
</style><header><div class="eyebrow">ANGBAND DELUXE / SOUND STUDIES @EDITION@</div>
<h1>@TITLE@</h1><p>@DESCRIPTION@</p><p>@VARIANTS@</p>
<strong>Complete reel · A then B for each category below</strong>
<audio controls preload="metadata" src="audition_reel.wav"></audio></header>'''
    for key, value in (("TITLE", title), ("EDITION", edition), ("DESCRIPTION", description), ("VARIANTS", variants)):
        page = page.replace("@"+key+"@", html.escape(value))
    page += "\n".join(cards)
    page += '''<footer>Original synthesis only. 48 kHz mono WAV. Audition only;
the in-game sound pack is unchanged.</footer><script>
document.addEventListener('play',e=>{if(e.target.tagName==='AUDIO')
document.querySelectorAll('audio').forEach(a=>{if(a!==e.target)a.pause()})},true);
</script></html>'''
    (output/"index.html").write_text(page, encoding="utf-8")
    print(f"Generated 12 sounds and {len(reel)/RATE:.2f}s reel in {output}")


if __name__ == "__main__":
    main()
