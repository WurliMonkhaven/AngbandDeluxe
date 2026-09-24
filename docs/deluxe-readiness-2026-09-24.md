# Windows release-readiness pass — 24 September 2026

## Outcome

The rebuilt development app and a relocatable 31.2 MiB Windows playtest ZIP pass
the checks below. This is an automated/offscreen readiness pass on the development
machine, not a claim of clean-machine installation or human usability certification.

## Changes

- Consolidated runtime path discovery. Packaged data/fonts/audio/backend resolve
  beside the executable; explicit path overrides remain supported.
- Missing required assets are reported before font loading/engine startup.
  An inaccessible save/settings directory produces an explanatory message.
- Unified active and deferred prompt ownership for save, movement, targeting,
  blast-preview and character-detail eligibility. Added a regression check for
  input arriving before a deferred prompt becomes visible.
- Added a Windows packager with local release runtime DLLs, data, font, audio,
  dependency notices, matching working-tree source and SHA-256 file manifest.
- Added a repeatable readiness runner and replaced obsolete development docs.
- Repaired randomized test setup: wizard quantity/text replies are now valid
  and checked, nearby-monster clearing is actually performed, trap fixtures avoid
  pre-existing detected traps, and wall tests select known solid terrain rather
  than relying on a glyph. The disarm and preview-versus-explosion assertions
  remain intact. The movement benchmark excludes known traps and nearby combat.

## Validation

- All 59 real-engine integration tests passed in the final run (26.6 seconds).
  Coverage includes creation/cancellation, inventory/store actions, prompts,
  movement/travel, targeting, save/reload, death and native replay.
- Client state/layout/settings checks passed.
- Offscreen Direct3D12 pixel checks passed, including CRT composition,
  transitions, resize/history and foreground layering.
- Dummy-device audio checks passed.
- The formerly intermittent trap/wall checks passed 12 fresh-map repetitions
  (36 checks) after the fixture repairs.
- Relocated ZIP contents were verified against their SHA-256 manifest in a path
  containing spaces, with a different working directory. Asset resolution stayed
  inside the package. Temporarily removing the packaged font correctly failed
  validation instead of falling back to a source-tree font.
- Four engine journeys passed using the packaged backend and data: native birth,
  cancellation, death/replay and dungeon traversal. Packaged audio passed too.
- No personal saves were used or packaged.

## Performance baseline

Windows, optimised RelWithDebInfo, Direct3D12. The transport check loads a fresh
first-floor save and polls at 60 Hz. Nearby combat is removed from that disposable
fixture so interruptions cannot masquerade as latency failures.

| Measurement | Median | 95th percentile |
| --- | ---: | ---: |
| Movement input to received state | 20.4 ms | 37.4 ms |
| Dungeon redraw through SDL transport | 20.0 ms | 37.4 ms |
| UI-thread receive | — | 3.34 ms |

The 1080p GPU benchmark runs 600 frames per scope, retains scene-transition
history and uses the strong CRT preset with delta-dot reconstruction.

| CRT scope | CPU recording p95 | GPU backpressure p95 | Private memory, warm → end |
| --- | ---: | ---: | ---: |
| Off | 0.055 ms | 0.102 ms | 123.91 → 123.91 MiB |
| Game only | 0.219 ms | 1.700 ms | 164.50 → 164.43 MiB |
| Full | 0.221 ms | 2.114 ms | 164.43 → 164.43 MiB |

All scopes had zero steady upload-buffer growth. These are short sampled
baselines, not a long-duration leak proof or physical key-to-monitor latency.
Results on other GPUs and systems will differ.

## Reproduction and handoff

Run `python -B deluxe/readiness.py --package` from the repository root. The
latest evidence for this pass is `build-deluxe/readiness-20260924-142906/`;
fixture-repeat evidence is `build-deluxe/readiness-fixture-repeat-final.log`.
The Windows ZIP is in that run's `packages` directory. Launch instructions and
the human playtest route are in `deluxe/PLAYTEST.md` and the ZIP's START-HERE.md.

Next validation should be a human session on a fresh Windows machine: extraction,
first launch, resize/fullscreen, text readability, subjective responsiveness,
complete save/death/replay flows and a longer session. Signing/installer work,
accessibility and macOS/Linux remain outside this pass.
