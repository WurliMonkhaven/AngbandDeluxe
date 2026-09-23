"""Sound studies 04: muted contacts and brief textured buzzes, no melodic cues."""
from pathlib import Path
import math
import random
import generate_dark_audition as dark

s = dark.s
RATE = s.RATE
OUT = Path(__file__).resolve().parent / "audition-soft-circuit"


def buzz(length, rate, seed):
    # Rough amplitude-modulated noise, rather than a pitched oscillator.
    rng = random.Random(seed)
    low = phase = 0.
    speed = rate
    data = []
    for i in range(round(length*RATE)):
        t = i/RATE
        if i % 480 == 0:
            speed = rate*rng.uniform(.78, 1.22)
        phase += speed/RATE
        low += .16*(rng.uniform(-1, 1)-low)
        gate = .32 + .68*(.5+.5*math.sin(math.tau*phase))**3
        env = min(1, t/.006)*min(1, (length-t)/.028)*math.exp(-8*t)
        data.append(low*gate*env)
    return data


def contact(length, seed, cutoff=1400):
    return s.noise(length, cutoff, 65, seed, .003)


def recipe(group, v):
    mix = s.mix
    if group == "ui":
        return mix(.06, (contact(.032, 300+v, 1500-v*250), 1, 0)), .13
    if group == "target":
        return mix(.17, (contact(.025, 310+v), .55, 0),
                   (buzz(.070, 95-v*15, 312+v), .35, .023),
                   (contact(.025, 314+v, 1000), .22, .093)), .17
    if group == "potion":
        return mix(.25, (contact(.035, 320+v, 1000), .4, 0),
                   (buzz(.15, 42+v*11, 322+v), .8, .021),
                   (contact(.025, 324+v, 1100), .17, .163)), .21
    if group == "melee":
        return mix(.16, (contact(.06, 330+v, 1700), .75, 0),
                   (buzz(.075, 67+v*19, 332+v), .35, .014),
                   (s.noise(.08, 430, 40, 334+v, .004), .35, .007)), .27
    if group == "spell":
        return mix(.27, (buzz(.18, 105-v*27, 340+v), .7, .006),
                   (contact(.03, 342+v, 1600), .3, 0),
                   (contact(.028, 344+v, 1200), .13, .12)), .22
    return mix(.13, (contact(.025, 350+v, 1700), .7, 0),
               (buzz(.060, 58+v*19, 352+v), .3, .006),
               (s.noise(.055, 500, 55, 354+v, .003), .25, .009)), .22


def soften(data, v):
    # Keep the coarse grain at low level, smooth the brittle upper edge.
    crushed = dark.grit(data, v)
    low = 0.
    alpha = 1-math.exp(-math.tau*1500/RATE)
    result = []
    for clean, rough in zip(data, crushed):
        low += alpha*((.75*clean+.25*rough)-low)
        result.append(low)
    return result


if __name__ == "__main__":
    dark.main(output=OUT, recipe_fn=recipe, process_fn=soften,
              title="Soft circuit", edition="04",
              description="Small contacts and brief, grainy buzzes. Softer attacks, less bass weight, lower playback levels. No melodic notes or pitched sweeps.",
              variants="A and B are close variations in contact texture and buzz rate, rather than different musical treatments.",
              labels={
                  "ui": ("Interface click", "Contact", "Felt contact"),
                  "target": ("Target confirmed", "Latch", "Soft latch"),
                  "potion": ("Potion use", "Intake", "Transfer"),
                  "melee": ("Melee impact", "Dry contact", "Damped contact"),
                  "spell": ("Spell cast", "Short activity", "Muted activity"),
                  "crt": ("CRT shutdown", "Relay release", "Power release"),
              })
