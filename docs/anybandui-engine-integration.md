# Angband adapter and remaining engine surface

The desktop client lives in `anybandui/`. Angband-specific protocol code lives
in `anybandui/backend/`; it is built into `angband-backend`, not the desktop
client. The adapter runs on the engine thread at input boundaries. All JSON,
capabilities, snapshots, travel intent, catalogues, settings persistence and run
records belong to the adapter.

## Cleanup baseline

Compared with the pre-integration commit `167ba295`:

- Before cleanup: 23 added backend files under `src/`, plus 60 modified existing
  files (including CMake), with 689 additions and 122 deletions in existing files.
- After cleanup: no adapter files under `src/`; 58 modified existing files
  (including CMake), with 621 additions and 97 deletions.
- The smaller patch is still required for the full current player-facing UI.
  Relocating the adapter does not make it independent of Angband's internals.

All added developer menus, dialogs, placement modes, mutation requests, capability
advertisements and test-spawn catalogues have been removed. There is no hidden
build flag that restores them. Requests from older clients for those operations
are rejected. Upstream Angband's own wizard implementation is unchanged; isolated
automated tests can drive its existing terminal commands to prepare fixtures.

Depth milestones now observe `EVENT_NEW_LEVEL_DISPLAY` in the adapter and use the
existing generic history type. No extra history flag or rule in
`dungeon_change_level()` is needed. Initial load does not invent a visit, and
existing milestone text remains recognized. Ball-area preview geometry also
lives in the adapter; `project()` retains upstream calculation code, with only
an additional event flag to distinguish an arc from an explosion. The adapter's
preview must be checked against the engine's geometry when porting versions.

## Remaining hooks and why they exist

| Area | Engine files | Purpose |
| --- | --- | --- |
| Build | `CMakeLists.txt` | Optional backend entry point, JSON dependency, client build and Windows compiler fix. |
| Known map | `cave-map.c`, `cave.h`, `ui-map.c/.h`, `ui-display.c` | Read-only layered map extraction, observation/reset hooks; full-level and rectangular viewport rendering without gameplay RNG or knowledge changes. |
| Input context | `game-input.c/.h`, `cmd-core.c/.h`, `cmd-obj.c`, `cmd-pickup.c`, `ui-input.c/.h` | Item context for quantity prompts, known-effect context for aiming, continuation/direction/aim state and pickup-boundary events. |
| Native interfaces | `ui-birth.c/.h`, `ui-knowledge.c/.h`, `ui-spell.c/.h`, `ui-command.c/.h`, `ui-store.c/.h` | Optional birth/inventory/equipment/book/store hooks and rest prompt identification. Existing engine validation still performs actions. |
| Targeting | `ui-target.c/.h`, `ui-context.c/.h`, `target.c/.h` | Look/target state, world-coordinate clicks, target confirmation notification; fixes confused mouse walking. |
| Combat observations | `game-event.c/.h`, `mon-attack.c`, `mon-util.c`, `player-attack.c`, `player-util.c`, `project-mon.c`, `effect-handler-attack.c` | Resolved damage, healing and misses. No extra rolls or balance changes. |
| Motion observations | `mon-util.c/.h`, `mon-move.c`, `effect-handler-general.c` | Distinguish real walking from teleports and observe visible departure/arrival. |
| Projection observations | `project.c` | Arc/cone metadata in the existing explosion event. |
| Lifecycle/audio | `ui-death.c`, `message.c/.h` | Death-screen boundary and sound events for the independent client audio controls. |
| Structured descriptions | `obj-info.c/.h`, `ui-player.c/.h`, `cmd-cave.c`, `cmds.h` | Sections, combat metrics, character-sheet rows and a reusable level-feeling formatter; reuse engine calculations and wording. |
| Preferences/artwork | `ui-keymap.c/.h`, `ui-prefs.c/.h` | User-keymap provenance and restricted tile-mapping import. |
| Constants | `init.c/.h` | Optional constants source and validation through the actual parser. Overrides are opt-in and pinned per save by the adapter. |
| Correctness | `obj-power.c`, `ui-store.c` | Wide-arithmetic overflow guard and cancelled-purchase temporary-object cleanup. |

No game-data tables, savefile reader/writer, combat formulas, monster generation
rules or ordinary spell costs are changed by this cleanup. Correctness fixes and
hooks used by normal features remain rather than being removed merely to lower
a diff count.

## Porting boundary

A future engine package should implement the shared protocol through its own
adapter and advertise only supported capabilities. Start with session lifecycle,
commands/input, snapshots, prompts and terminal fallback. Add native inventory,
map rendering, targeting, shops and effects independently. Do not copy this
adapter's assumptions about stats, equipment slots, effects or constants into a
supposedly universal engine layer.

The current development protocol is documented in `anybandui-protocol-0.1.md`;
`anybandui-api.md` is a proposed future contract, not a stable implemented v1.

## Verification

The backend suite rejects removed developer methods both before and during play.
Normal gameplay regression coverage uses native inputs and disposable profiles.
Client checks, GPU/audio checks, detached-window checks and a relocated package
rehearsal are available through `python -B anybandui/readiness.py --package`.
Builds use `build-anybandui-native` and `build-anybandui`; older ignored build
outputs and Git history are not maintained source and should not be reused.
