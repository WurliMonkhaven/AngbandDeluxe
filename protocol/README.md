# AnybandUI engine surface 1.0

This repository owns the engine/frontend contract. Supported engines implement
**full-v1**, defined by `full-v1.json` and `wire-v1.md`. The frontend requires every
listed capability; there are no reduced-integration support tiers. An engine with
no spellcasting for the current character still implements the spells methods and
returns appropriate empty/ineligible results. Intentional native-engine terminal
screens are supported; missing standard native interfaces are not substituted.

## Engine package

Place packages below `engines/` next to AnybandUI.exe. Each immediate child has an
`engine.anyband.json` manifest, its executable, and all required data/dependencies.
Use `--engines-dir PATH` for another location. The directory itself may also be a
single package. Discovery reads manifests without launching arbitrary executables.

```json
{
  "manifest_version": 1,
  "profile": "full-v1",
  "engine": {
    "id": "org.angband.angband",
    "name": "Angband",
    "version": "4.2.6-anybandui",
    "save_compatibility": "angband-4.2.6"
  },
  "protocol": {"major": 1, "minor": 0},
  "executable": "angband-anybandui.exe",
  "data_directory": "lib"
}
```

Executable and data paths must resolve inside the package, including symlink
resolution. IDs/save compatibility keys contain only ASCII letters, digits,
underscores, dots and hyphens, are at most 100 characters, and cannot be `.` or
`..`. Empty or invalid identities, missing files and unsupported versions are
rejected. A manifest declares compatibility; the live handshake verifies it
before character controls are enabled. A matching handshake is not a substitute
for testing that the engine actually implements its advertised capabilities.

## Process boundary

The frontend launches the declared executable directly, without a shell:

`ENGINE --data-dir ABSOLUTE_DATA_PATH --user-dir ABSOLUTE_PROFILE_PATH`

One child process hosts one session. UTF-8 JSON objects, one per line, travel over
stdin/stdout. Stdout contains protocol only; diagnostics go to stderr. The engine
must not open a separate gameplay window. No source checkout, shared engine ABI,
DLL injection or in-process engine linkage exists in the frontend.

The client sends `hello` offering `{major:1, minor:0}`, a 4194304-byte frame limit,
and native inventory/equipment selection. The response must return that selected
version, `profile: "full-v1"`, its manifest-matching engine identity/version/save
compatibility, the negotiated frame limit, and every capability/version in
`full-v1.json`. Failure or a ten-second timeout prevents gameplay. State/events
must not arrive before the handshake completes. The frontend accepts no game
state from an unverified engine. The default pre-negotiation frame limit is 1 MiB.

All listed methods are required. Phase/context restrictions and invalid player
actions return structured errors rather than indicating missing integration.
Responses identify the original request ID and contain either `result` or `error`.
Requests must not be executed twice. Stale context, revision, prompt and item
handles are rejected. An accepted command is not proof it succeeded: completion,
messages and resulting state describe the outcome. See the wire reference for
state, prompt, event, targeting, store, map, spell and tuning payloads.

The initial v1 preserves the current payload conventions, including palette IDs
and tile mappings described in the wire reference. A fork must translate its
internals to those conventions. This is a wire contract, not an invitation to
reuse vanilla structure layouts or enum values across forks. Incompatible payload
changes require a new major version; additional optional fields can be added
without removing or changing required behaviour.

## Ownership and behaviour

The engine owns rules, validation, RNG, knowledge, item eligibility, targeting,
prices, save files and command execution. Native UI must not duplicate simulation
or mutate game structures. Map/inspection/preview requests do not advance turns,
change knowledge or consume gameplay RNG. Effects report actual outcomes; walking
and teleportation are distinct events. Full-level camera data obeys remembered
knowledge and visibility, and clicks use world coordinates.

The frontend owns layout, themes, fonts, input presentation, native controls,
rendering, audio playback and visual effects. The engine supplies scalar snapshots
and events, never raw pointers. Terminal presentation preserves nested original
interfaces and their input contexts, including extension screens such as quests.

UI preferences live in the AnybandUI profile. Engine saves/preferences live in
`engines/<engine.id>/<save_compatibility>/` beneath it. Different engine IDs and
incompatible save families do not share saves. Existing legacy vanilla saves are
copied without overwrite into the vanilla family directory; originals remain.

## Conformance

`python -B protocol/check_engine.py PATH/engine.anyband.json` checks the packaged
process boundary, handshake, identity, required capabilities and launcher methods
in a disposable profile. This is a smoke check, not full feature certification.

An engine implementation must also pass native birth, inventory/equipment, spells,
shops, targeting, continuation/death messages, save/load/replay, known-map purity,
precise combat/motion events and its gameplay regression suite. The reference
Angband implementation and 65 engine integration tests live in the separate
`angband` repository on branch `4.2.6-anybandui`. Building AnybandUI never builds
or requires that repository.

Without a compatible engine the application still builds and opens normally,
showing `No supported Anyband binaries found`. Engine installation and selection
are independent of frontend installation. Manifests and protocol negotiation are
compatibility checks, not a sandbox: install only engines you trust.
