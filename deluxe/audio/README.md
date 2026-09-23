# Deluxe audio

Open `audition/index.html` in a browser to compare the twelve original sound
sketches. The complete reel plays A then B for each section, in page order.
Individual players stop other players automatically.

These are fully synthesized experiments: no recordings, external samples or
existing Angband sound assets. The original audition page remains a separate comparison tool. The active gameplay pack is now Soft Circuit (sound studies 04).

| Moment | A | B |
| --- | --- | --- |
| Interface click | Dry switch | Glass switch |
| Target confirmed | Soft lock | Tuning lock |
| Potion use | Cork & bubbles | Alchemical bubbles |
| Melee impact | Heavy contact | Metallic contact |
| Spell cast | Ember pulse | Arcane discharge |
| CRT shutdown | Relay snap | Charged snap |

Listen for character and comfort on repetition, rather than loudness alone.
The UI cues deliberately peak lower than impacts. Physical effects are stylized
approximations; this set is intended to choose a direction before production.
The shutdown sounds have a sharp initial transient and a short electrical tail.

Run `python deluxe/audio/generate_audition.py` from the repository root to rebuild
the WAVs, manifest and audition page. Only the Python standard library is needed.
Recipes use deterministic noise seeds, layered oscillators, filtered noise,
envelopes and short reflections. Output is 48 kHz mono 16-bit PCM with DC removal,
boundary fades and conservative peak levels. The game would play exported audio;
it would not run this synthesis code while playing.

## Playback integration

The build stages the Soft Circuit WAVs from `audition-soft-circuit/` and
`pack.json` in `audio/` beside the executable. Regenerate this pack with
`python deluxe/audio/generate_soft_circuit_audition.py`. Its softer levels and
muted contact/buzz textures are preserved without additional normalization. UI, target
and CRT cues use treatment A. Potion, melee and spell cues rotate A/B. Settings
/ Audio controls master and category volume; playback clears on focus loss and
session restart. Engine sound names map explicitly in `AudioPlayer::engine_cue`.
Unknown names stay silent, and snapshot/message-history reads never trigger audio.

Run `deluxe-audio-tests <staged-audio-directory>` for headless dummy-device tests.
The pack uses four preloaded SDL voices, a 100 ms per-cue cooldown and headroom
for overlapping effects. Additional packs should keep sample peaks at or below
0.7 full scale; files must be mono 48 kHz 16-bit PCM and no more than five seconds.
