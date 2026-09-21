# Angband Deluxe development build

The applications are native C and C++: `angband-backend` runs the existing
Angband engine in a separate process, and `angband-deluxe` renders with SDL3,
SDL_GPU and Dear ImGui. Python is used only for building and testing.

This is the first playable development implementation, not completion of the
[product specification](../docs/deluxe-specification.md). Its protocol is 0.1;
the [v1 API document](../docs/deluxe-api.md) remains the target design.

## Build and run

Install Git, CMake 3.24 or newer, and a C/C++17 compiler. The first configure
fetches pinned SDL3, Dear ImGui, cJSON and nlohmann/json sources from GitHub.
Keep the source and dependency directories available while running this build:
game data and the font currently use build-time source paths.

On this Windows development machine, with Visual Studio Community's C++ tools:

```powershell
python deluxe/build.py --ninja --configure
./build-deluxe-native/game/angband-deluxe.exe
python deluxe/test_backend.py --backend build-deluxe-native/game/angband-backend.exe
```

For other compiler installations, configure CMake directly from an appropriate
developer shell. On Linux, install SDL3's platform development dependencies
first. These commands have not yet been validated on macOS or Linux:

```sh
cmake -S . -B build-deluxe -DSUPPORT_DELUXE_FRONTEND=ON -DBUILD_DELUXE_CLIENT=ON -DSUPPORT_BORG=OFF -DSUPPORT_SPOIL_FRONTEND=OFF
cmake --build build-deluxe --target OurExecutable angband-deluxe
./build-deluxe/game/angband-deluxe
python3 deluxe/test_backend.py --backend build-deluxe/game/angband-backend
```

Multi-configuration generators may add a Debug or Release directory. The
backend and client must be beside one another, or specify `--backend PATH`.
Both accept `--data-dir PATH` and `--user-dir PATH`. The client defaults to the
operating system's application preference directory for saves and settings.
Use a separate user directory when testing an existing save.

## Using this build

Create a new save name or choose a saved character. The game takes keyboard focus
automatically when starting/loading and after commands or prompts. Its thin blue
border indicates focus; click it to return from another panel. Character creation and remaining classic menus use Angband's
normal controls. Enter acknowledges its `-more-` messages. The engine's Help
command documents gameplay controls.

The side panels provide searchable items, inspection, selected-item actions,
creatures, a minimap and a searchable command list. The magnifying glass beside
Messages opens an inline search box. Inspection uses ticks and crosses for
boolean properties, and action buttons are filtered by item type and location.
Confirmation, quantity, text and ordinary item-choice prompts use native
widgets. Save controls are at the top left; Settings at the top right opens a
popup with UI scale choices from 75% to 150%. UI scale resizes text, controls and
spacing together. Character information stays fixed while tab contents scroll.
HP, SP and food bars are red, blue and green respectively.
UI settings persist.

The complete game screen always fits inside its view without scrollbars, using
the largest text that fits. Drag the horizontal divider above Messages to adjust
the game/message heights; the split persists between launches. Double-click the
divider to reset it. Message history scrolls separately. The character panel shows
food as a bar with its percentage and raw nutrition value.

The panels use player-facing descriptions, visible creatures and the known map.
The backend still provides actual state, including hidden information, alongside
player-known state for future presentation features. There is no backend disclosure
restriction. Reading these values does not identify objects or spend turns.

## Current limits and next milestones

- The main game surface still renders Angband's terminal cells through the GPU.
  The independent semantic panels already use structured API data. A fully
  semantic dungeon renderer and aesthetic overhaul remain future work.
- Native character creation, stores, spells, direction/target selection,
  comparison, advanced bindings, save profiles, run reports and full controller
  support remain to be implemented. Classic menus provide access in the meantime.
- Stable entity handles, deltas, reconnect/retry semantics, package discovery,
  alternate-backend conformance and drag-and-drop fork packages are not implemented.
- The build has been compiled and manually exercised on Windows. Cross-platform
  source support is not a substitute for macOS/Linux build and desktop tests.
- This is a source-tree build, not an installer or relocatable distribution.
  Accessibility integration and distribution licensing still need a release audit.

## Validation recorded on Windows

The native Debug build passed all 934 existing Angband unit tests and four
real-engine integration tests: protocol validation; gameplay/save/reload;
item prompt validation, cancellation and inscription; and same-save keyboard/API
wait-action parity with repeated inspection queries. Debug capture asserts RNG
purity. Manual desktop checks exercised character creation, load, item selection,
native inscription prompts, cancellation, saving and closing.

These checks do not establish parity for every command or validate other platforms.
The development wire contract is documented in
[protocol 0.1](../docs/deluxe-protocol-0.1.md).

## Dependencies

| Component | Pinned release | Licence |
| --- | --- | --- |
| SDL | 3.2.28 | zlib |
| Dear ImGui | 1.92.5 | MIT |
| cJSON | 1.7.19 | MIT |
| nlohmann/json | 3.12.0 | MIT |
| Cousine font from ImGui's source tree | bundled revision | SIL Open Font License 1.1 |

Exact source commits are in the CMake files. Cousine is by Steve Matteson,
digitized data copyright 2010 Google Corporation. The font remains in the fetched
dependency source tree. Angband's existing licensing applies to this fork.
