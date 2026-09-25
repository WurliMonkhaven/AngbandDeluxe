# Angband Deluxe

A native Windows frontend for Angband. The C engine runs in a separate process;
the C++ client uses SDL3, SDL_GPU and Dear ImGui. Python is development tooling
only. Current scope and behaviour live in the [specification](../docs/deluxe-specification.md).

## Playtest build

See [START HERE](PLAYTEST.md) for launch instructions and a short playtest route.
Windows ZIPs contain both executables, game data, font, generated audio, runtime
DLLs, licence notices, matching source and a SHA-256 manifest. Extract the whole
archive before launching. User saves/settings stay in the OS preference directory,
separate from the application. `--user-dir PATH` selects an isolated profile.

## Build

On Windows, install Visual Studio's C++ tools, CMake and Python, then:

```powershell
python -B deluxe/build.py --ninja --configure
.\build-deluxe-native\game\angband-deluxe.exe
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
python -B deluxe/readiness.py --package
```

This builds the applications and check targets, then runs client state/layout
checks, all real-engine integration tests, audio checks, GPU pixel checks,
1080p frame-pacing/private-memory samples, and the SDL pipe/movement benchmark
using a freshly generated first-floor save. Test profiles are disposable;
personal saves are never used. Logs and results live under
`build-deluxe/readiness-<timestamp>`. `--skip-build` reuses existing binaries.
Hidden detached-window checks cover rendering, input isolation, CRT scope,
redocking and monitor recovery. To include the coordinate-based offscreen
inventory and layout checks, pass `--ui-fixture PATH` with their captured UI
fixture (the development fixture is `build-deluxe/aesthetic-fixture.json`).

With `--package`, it also builds a ZIP, relocates it into a folder with spaces,
checks source-independent asset discovery, and exercises native birth,
cancellation, death/replay and dungeon travel using the packaged backend/data.
Packaging alone is available with `python -B deluxe/package_windows.py`.
The Windows packager currently expects the dependency tree under
`build-deluxe/_deps` and an installed Visual C++ x64 redist directory.

Useful individual checks:

```powershell
python -B deluxe/test_backend.py --backend build-deluxe-native/game/angband-backend.exe
.\build-deluxe-native\game\deluxe-client-tests.exe build-deluxe/unused-settings.json
.\build-deluxe-native\game\deluxe-gpu-tests.exe direct3d12
.\build-deluxe-native\game\deluxe-gpu-tests.exe direct3d12 --bench
.\build-deluxe-native\game\deluxe-audio-tests.exe build-deluxe-native/game/audio
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
The implemented wire contract is [protocol 0.1](../docs/deluxe-protocol-0.1.md).

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
delete through Deluxe also handle the companion. Replaced tuning files retain
a `.bak` backup, and invalid values are rejected before saving.
