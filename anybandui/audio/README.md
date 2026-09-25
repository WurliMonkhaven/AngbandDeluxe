# AnybandUI audio

`pack.json` maps sound cues to the nine shipped WAV files in this directory.
These original synthesized sounds use no recordings or legacy Angband samples.
The build copies them beside the executable into `audio/`.

Interface, target and CRT cues use one sample each. Potion, melee and spell cues
rotate between two variants. Settings / Audio controls master and category
volume; playback clears on focus loss and session restart. Engine sound names
map explicitly in `AudioPlayer::engine_cue`. Unknown names stay silent, and
snapshot/message-history reads never trigger audio.

Files are mono 48 kHz, 16-bit PCM. Keep peaks at or below 0.7 full scale and clips
under five seconds. Playback uses four preloaded SDL voices and a 100 ms per-cue
cooldown. Run `anybandui-audio-tests <staged-audio-directory>` for the active
headless playback tests.
