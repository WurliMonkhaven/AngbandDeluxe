# Stability and performance pass — 23 September 2026

## Results

- Full backend suite: 43 tests passed. Covers creation, native prompts, inventory,
  shops, spells, targeting, travel, options, save management, sound events and death/replay.
- Headless client checks passed, including settings persistence/cancellation,
  layout, resource bars, input ownership and session lifecycle.
- Dummy-device audio checks passed: sample loading, cooldown, focus muting,
  independent volumes and missing-pack handling.
- Offscreen CRT correctness checks passed on Vulkan and Direct3D 12.

The initial backend run had one intermittent failure in the spell-query parity
test. Its script sent a direction without checking whether a message pause or a
confirmation still needed handling, then sent Enter repeatedly. The test now
handles message acknowledgement, confirmation and aiming as distinct input
boundaries. The resulting full run and five consecutive repetitions of the spell test passed; the gameplay implementation was not
changed to accommodate the test.

## Measured responsiveness

SDL child-process transport, polled at approximately 60 Hz, using disposable
new-character saves. Redraw samples: 60 per location. Movement samples: 12 per
location, alternating between the starting staircase and an adjacent empty floor.

| Measurement | Town | Dungeon level 1 |
| --- | ---: | ---: |
| Redraw input-to-state median | 19.10 ms | 37.27 ms |
| Redraw input-to-state p95 | 21.65 ms | 38.80 ms |
| Redraw input-to-state maximum | 37.25 ms | 39.30 ms |
| Movement input-to-state median | 18.99 ms | 37.06 ms |
| Movement input-to-state p95 | 20.96 ms | 38.16 ms |
| UI-thread receive p95 | 2.71 ms | 3.72 ms |

These measure input submission through receipt/application of the new state,
not physical key-to-photon latency. The transport harness does not render the
full app. Display presentation, real device audio and long-running/deep-dungeon
sessions are not represented by these figures. No desktop automation was used.

1080p offscreen Vulkan stress test, strongest CRT preset, 219 measured frames per
scope after warmup:

| CRT scope | CPU recording p95 | Fence-wait p95 | Steady buffer growths |
| --- | ---: | ---: | ---: |
| Off | 0.14 ms | 0.21 ms | 0 |
| Game view | 0.27 ms | 8.71 ms | 0 |
| Full window | 0.31 ms | 9.53 ms | 0 |

Fence waits reflect this pipelined offscreen workload, not isolated GPU execution
time or desktop frame rate. The recorded results do not reproduce the previous
quarter-second input stall. Dungeon updates still cost more than town updates;
these measurements establish a repeatable baseline rather than proving all
possible performance problems absent.

## Repeatable checks

Build `anybandui-transport-tests`, then run from the repository root:

```
python anybandui/bench_session.py --backend build-anybandui-native/game/angband-backend.exe --transport build-anybandui-native/game/anybandui-transport-tests.exe
```

The runner creates and removes its own temporary saves under the build's
`game/test-runs` directory. It never opens user saves. The transport test also
checks clean save/exit, process restart and shutdown of blocked reader threads.
Latency guardrails reject medians over 100 ms or p95 over 150 ms, deliberately
allowing machine/load variation while catching large stalls.

The existing `anybandui-gpu-tests vulkan --bench` command runs the offscreen stress
test. Run `anybandui-gpu-tests vulkan` and `anybandui-gpu-tests direct3d12` for correctness.
