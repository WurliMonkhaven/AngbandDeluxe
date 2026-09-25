# AnybandUI

A native Windows frontend for Angband. The C engine runs in a separate process;
the C++ client uses SDL3, SDL_GPU and Dear ImGui. Python is development tooling
only. Current scope and behaviour live in the [specification](../docs/anybandui-specification.md).

## Playtest build

See [START HERE](PLAYTEST.md) for launch instructions and a short playtest route.
Windows ZIPs contain both executables, game data, font, generated audio, runtime
DLLs, licence notices, matching source and a SHA-256 manifest. Extract the whole
archive before launching. User saves/settings stay in the OS preference directory,
separate from the application. `--user-dir PATH` selects an isolated profile.

## Build

On Windows, install Visual Studio's C++ tools, CMake and Python, then:

```powershell
python -B anybandui/build.py --ninja --configure
.\build-anybandui-native\game\AnybandUI.exe
```

The build pins SDL3, Dear ImGui, cJSON and nlohmann/json revisions. First-time
configuration fetches those dependencies. RelWithDebInfo is the default;
`--config Debug` is useful for diagnosis but substantially slower for JSON work.
The applications use sibling `data`, `fonts` and `audio` directories when present.
Development builds can fall back to the source game-data directory. Explicit
`--data-dir PATH` and `--backend PATH` override discovery. `--check-assets` validates
runtime file discovery without opening a desktop window or creating a profile.

## Release-readiness checks

```powershell
python -B anybandui/readiness.py --package
```

This builds the applications and check targets, then runs client state/layout
checks, all real-engine integration tests, audio checks, GPU pixel checks,
1080p frame-pacing/private-memory samples, and the SDL pipe/movement benchmark
using a freshly generated first-floor save. Test profiles are disposable;
personal saves are never used. Logs and results live under
`build-anybandui/readiness-<timestamp>`. `--skip-build` reuses existing binaries.
Hidden detached-window checks cover rendering, input isolation, CRT scope,
redocking and monitor recovery. To include the coordinate-based offscreen
inventory and layout checks, pass `--ui-fixture PATH` with their captured UI
fixture (the development fixture is `build-anybandui/aesthetic-fixture.json`).

With `--package`, it also builds a ZIP, relocates it into a folder with spaces,
checks source-independent asset discovery, and exercises native birth,
cancellation, death/replay and dungeon travel using the packaged backend/data.
Packaging alone is available with `python -B anybandui/package_windows.py`.
The Windows packager currently expects the dependency tree under
`build-anybandui/_deps` and an installed Visual C++ x64 redist directory.

Useful individual checks:

```powershell
python -B anybandui/test_backend.py --backend build-anybandui-native/game/angband-backend.exe
.\build-anybandui-native\game\anybandui-client-tests.exe build-anybandui/unused-settings.json
.\build-anybandui-native\game\anybandui-gpu-tests.exe direct3d12
.\build-anybandui-native\game\anybandui-gpu-tests.exe direct3d12 --bench
.\build-anybandui-native\game\anybandui-audio-tests.exe build-anybandui-native/game/audio
```

Use a new path for each client settings check. GPU checks also accept `vulkan`.
Offscreen checks create no desktop window. Their timing measures CPU recording,
GPU backpressure and input-to-state latency, **not physical key-to-display latency**.
A human playtest is still needed to judge visual pacing, comfort and scanout.

## Architecture and remaining scope

Native interfaces cover character creation/replay, inventory and comparison,
shops, spells/targeting, character details, knowledge, options/keybindings,
resting and post-mortem history. The semantic dungeon view and overlays use
engine-authored data; the classic terminal remains a fallback for unsupported
interactions. GPU CRT and transition details are in [shaders/README.md](shaders/README.md).
The implemented wire contract is [protocol 0.1](../docs/anybandui-protocol-0.1.md).

The release pass targets Windows only. Signing, an installer, clean-machine
manual certification, accessibility validation and macOS/Linux certification
remain separate work. Protocol v1, alternate-engine conformance and reconnect
semantics remain future architecture; this build does not claim them.


Optional free dungeon camera: Settings > Display > Free dungeon camera.
Middle-drag pans, the wheel (or +/-) zooms, and Return to player recenters.
Keep player centered controls following; panning pauses it until Return to player.
Floors reset the camera. Fog of war and gameplay rules are unchanged.

## Game tuning

Settings → Game tuning provides grouped controls for `constants.txt`, including
search, a modified-only filter, per-setting reset, and Restore all defaults.
Advanced combat includes editable critical-hit formulas and tier tables.
Hover a setting for its description, range, and when it applies. A few tightly
coupled engine limits are shown read-only.

Save and Close writes `tuning-overrides.json` in the backend user directory;
the shipped `lib/gamedata/constants.txt` stays untouched. Changes take effect
the next time a character is opened, never in the middle of a running game.
Settings marked **New characters** control storage capacities or world dimensions.
Each save retains these in a companion `tuning-<save name>.json` in that same user
directory. Keep this companion when moving a tuned save between installations.
Older saves without a companion use the installed stock capacities. Rename and
delete through AnybandUI also handle the companion. Replaced tuning files retain
a `.bak` backup, and invalid values are rejected before saving.

## Dungeon presence effects

Settings → Animations has independent toggles for unique enemy auras, stairway
glow, hazardous terrain effects, and recall gathering. Uniques have broken
violet halos; Morgoth has a crimson corona and pale fractures. First sight,
waking, and injury briefly intensify them. Known stairs have a bright, softly pulsing glow,
lava has a persistent hot surface with brighter embers (including remembered tiles), and recall gathers blue motes as its countdown decreases.
These effects work without CRT and do not reveal unseen or hallucinated actors.

### Dungeon tiles

Settings → Display → Dungeon artwork selects ASCII (the default), Original,
Adam Bolt, David Gervais, Nomad, or Shockbolt Dark/Light. The picker previews a
small scene before Save. The free camera zooms and pans tiled dungeons too.
Artwork uses square cells; panels and classic fallback screens retain their
fonts. Dungeon colour inversion applies to ASCII, not to sprite artwork.

The backend exports optional tile layers separately from the existing ASCII
layers, using Angband's bundled mappings and read-only known-map rendering.
There are no monster/object identity guesses in the client. Transparent layers,
flavours, lighting, and Shockbolt's double-height sprites are supported. Missing
or invalid artwork falls back to ASCII. The two Shockbolt choices share a GPU
atlas. Graphics preferences never replace the engine's terminal visuals.

Original artwork and attribution remain in `lib/tiles` and `docs/copying.rst`;
Windows packages include both. PNG decoding uses stb_image from the pinned SDL
source dependency, with its license bundled in release packages.
