# Windows release rehearsal — 25 September 2026

Result: the final automated rehearsal passed. No reproducible gameplay blocker
was found in the exercised paths. This is not a clean-machine or hands-on
playtest certification.

All game journeys used disposable profiles. Personal saves and preferences were
not used. Visual checks rendered offscreen; detached-window tests used hidden
windows and did not drive the user's desktop.

## Passed

- Rebuilt both applications and all rehearsal check targets from current source.
- 61 real-engine integration tests: creation, cancellation, save/reload, dead
  saves and replay, shops/home, inventory/equipment, spells, knowledge, options,
  keybindings, rest, targeting, trap/door actions, confused movement, stairs,
  tunnelling, feedback events and prompt handling.
- Client state/settings/layout checks, audio checks and Direct3D 12 pixel checks.
- Eleven offscreen layout interactions and seven inventory/equipment interactions.
- Hidden native windows: independent rendering/context, input isolation, CRT
  scope, cursor mapping, persisted layout, close/redock/reopen, missing-monitor
  recovery and session-end cleanup.
- Relocated package in a path containing spaces: bundled asset discovery with
  an unrelated working directory, SHA-256 manifest verification, expected
  failure for a missing required font, audio, birth/cancel, death/replay and
  dungeon journeys. A separate earlier relocated run also passed all 61 engine
  tests after the tunnelling fixture correction.
- Visual inspection at 800×600, including Settings at 150% text scale, and
  1280×720 with large text and light styling. Compact navigation and modal
  footer remained accessible. Small game panels require scrolling; larger text
  reduces inventory visibility. These are usable tradeoffs, not a claim that
  every layout/font combination has been inspected.

## Performance sample

Final first-floor movement input-to-state: median 38.8 ms, p95 54.7 ms.
UI-thread receive: p95 4.7 ms. These include the test's 60 Hz polling cadence;
they are not physical keyboard-to-display latency measurements.

1080p GPU benchmark: zero steady buffer growths across Off, Dungeon and Full
rendering paths. Full-scope CPU recording p95 was 0.21 ms and GPU wait p95 was
1.96 ms. Sampled process private memory remained approximately stable after
warm-up. This is a short benchmark, not a multi-hour soak test.

## Rehearsal fixes

Three test assumptions produced intermittent failures or a hang:

1. Confused movement requested east even when that was a wall. Confusion can
   preserve the direction; vanilla then spends no turn on the blocked move.
   The fixture now requests an adjacent open tile while retaining the checks
   for no accidental look/aim mode and actual turn progression.
2. Long digging can reach a continuation prompt at the repeat boundary.
   Acknowledgement interrupts travel. The test now verifies a full repeated
   digging batch before bounded retries, and still requires a completed hole.
3. Practice casting could exhaust mana after random failures. Its setup sent a
   direction to a continuation prompt and then looped on Enter during aiming.
   It now handles each input boundary separately and stops after a bounded loop.

The readiness runner now builds and runs detached-window checks using its fresh
dungeon fixture. Optional `--ui-fixture` adds layout/inventory interaction tests.
No game rules or frontend behaviour were changed for these test fixes.

## Evidence and reproduction

Final logs and per-check results:
`build-deluxe/readiness-20260925-112317/`.

The historical candidate archive is retained in
`build-deluxe/readiness-20260925-112317/packages/` under its original filename.

Visual previews: `build-deluxe/rehearsal-visual/`.
Earlier diagnostic runs were retained, including failed runs; the final results
directory above is the consolidated passing run.

```powershell
python -B deluxe/readiness.py --package --ui-fixture build-deluxe/aesthetic-fixture.json
```

The UI fixture is a development capture, not part of the source distribution.
Omit that argument for a self-contained readiness run.

## Still requires hands-on verification

- Fresh-player discovery and comfort through a complete played session.
- Animation/audio feel and physical input-to-display latency.
- Mixed-DPI monitor dragging, unplugging a real monitor and live focus changes.
- Multi-hour play and long-session memory behaviour.
- A clean Windows machine without the development toolchain, including first
  launch/security prompts. Relocation on this development machine cannot prove
  independence from every system-installed dependency.
- Installer/signing and accessibility certification remain outside this pass.
