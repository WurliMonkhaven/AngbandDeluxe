# Development protocol 0.1

This documents the running prototype, not the proposed stable v1 contract.
The reference implementations are `src/main-deluxe.c`, `deluxe/client.cpp`
and the real-engine tests in `deluxe/test_backend.py`.

The backend is a local child process. UTF-8 JSON objects are separated by
newlines on stdin/stdout; diagnostics go to stderr. Frames must be smaller
than 1 MiB. One process hosts one game. Networking is not supported.

```json
{"kind":"request","id":"1","method":"hello","params":{"protocols":[{"major":0,"minor":1}]}}
```

Responses have `kind: "response"`, the matching `id`, and either `result` or an
`error` object containing `code` and `message`. Events have `kind: "event"`,
an `event` name and `data`. IDs must be unique nonempty strings up to 64 bytes.
Duplicates are rejected, not replayed. There is currently a 32,768-request
connection limit. This limit and duplicate policy are development limitations.

| Method | Parameters and result |
| --- | --- |
| `hello` | Offer protocol 0.1; receive selected protocol, engine and capability metadata. |
| `commands.list` | Return supported command IDs and labels. |
| `saves.list` | Return available save IDs and descriptions. |
| `saves.rename` | Main menu only. `save`: existing save ID; `name`: unused new ID using the same naming rules as New. Renames the file without changing character data. |
| `saves.delete` | Main menu only. `save`: existing save ID. Permanently removes that save file; the client confirms first. |
| `session.new`, `session.load` | `save`: 1–64 ASCII letters, digits, underscores or hyphens. New refuses overwrite; Load requires an existing save. |
| `catalog.get` | Terrain definitions; available after engine initialization. |
| `state.get` | Return the latest cached snapshot without advancing the simulation. |
| `inspect.get` | `session_id`, `handle`: current item or monster record. |
| `command.execute` | `session_id`, `revision`, `command`, optional `item`: queue a semantic action through the ordinary engine command path. |
| `terminal.input` | `session_id`, `context`, `key`: Unicode codepoint or enter/escape/backspace/tab/up/down/left/right. |
| `prompt.reply` | `session_id`, `prompt_id`, `value`: reply to the current prompt; null cancels. |
| `session.save`, `session.close` | `session_id`: save at a normal gameplay input boundary; Close then exits. |

The sole session ID is `session-1`. Use opaque revision and context strings from
the latest snapshot. Semantic commands require `readiness: "ready"`; nested
menus use terminal input or the active prompt. An accepted command response is
not proof it succeeded: `action.completed` reports resolution, and engine messages
and the resulting snapshot describe the outcome. State may change while a command
is waiting for more input.

`state.changed` supplies a full snapshot at engine input boundaries: player,
items, monsters, actual/known terrain, visibility, messages and terminal cells.
The terminal is 100 by 34 cells; each cell is `[Unicode codepoint, colour index]`.
The `cursor` record supplies zero-based `x`, `y` and `visible` for the terminal
cursor, including the selected stat during point-based character creation.
Item records distinguish `actual` from `player_known`; monster HP and actual map
data include hidden state. No entitlement or player-knowledge gate applies.
The presentation decides which information to display.

Normal ready play may additionally supply `dungeon` (capability
`presentation.dungeon: 1`). It contains an opaque `level_id`, world-coordinate
viewport origin `x,y`, `width,height`, and `cells` as height rows of width cells.
Each compact cell is exactly:
`[terrainGlyph, terrainColor, trapGlyph, trapColor, itemGlyph, itemColor,
actorGlyph, actorColor, perceivedFeature, lighting, seen, hallucinated, isPlayer]`.
Glyphs are Unicode codepoints; zero means an absent overlay. Layers are opaque
glyph cells ordered terrain, trap, item, actor. Booleans in this compact format
are 0/1. Visuals already reflect engine lighting, memory, piles, mimicry and
hallucinations; do not reconstruct those rules from actual monster/item records.
Actual semantic data remains available independently. This is a presentation
snapshot, not stable item/monster identity or an action handle.

The adapter observes normal engine map draws and stores only scalar results;
`state.get` does not invoke drawing or use RNG. New levels clear the cache and
change `level_id`; camera movement changes the viewport origin. Incomplete
viewports and nested interactions omit `dungeon`, requiring terminal fallback.
The client uses that fallback for targeting and preserves its cursor and controls.
Ordinary message acknowledgement is an exception: `message_pending: true` may
accompany `dungeon` with `readiness: "awaiting_prompt"`. Keep the dungeon visible
and show a white-on-black `- more -` overlay
at the bottom right of the game view, within the normal CRT effects pass.
Acknowledgement uses the existing controls, `terminal.input` and the current
input context. This flag comes from the engine's message pause, not terminal text;
it does not permit gameplay commands while the engine is waiting.
Do not infer semantic mode from terminal text or crop terminal cells for gameplay.

### Transport and latency

The native client drains stdout and stderr on dedicated readers, independent of
display frames. On Windows these readers use blocking pipe reads with explicit
shutdown cancellation; only stdin remains nonblocking. Complete JSON messages
cross a bounded queue in arrival order. Parsing runs off the UI thread; applying
state and rendering stay on the UI thread. Readers join before process handles
or SDL are destroyed, including return-to-menu restarts. Process exit is handled
only after queued final messages and EOF have been consumed.

Performance checks must exercise the actual SDL transport at display cadence.
A continuously reading Python harness measures engine work but misses small-pipe
backpressure between display frames. The headless `deluxe-transport-tests` target
accepts backend path, data path and a disposable user directory containing a
`ProtocolTest` dungeon save. It measures redraw-to-received-state latency at
60 Hz, checks for large stalls, then exercises close/restart and idle-reader
cancellation. It does not measure physical display/presentation latency.

Additional player fields supply `title`, `experience`, `max_experience`,
`next_level_experience` (absolute XP threshold, zero at maximum level), `max_level`,
`light`, `floor`, optional formatted `feeling`, `recall`, `descent`, `resting`,
`running`, `repeat`, `study`, `extra_moves`, `unignoring`, `trap_detected` and
optional `tracked_creature` with name, hp/max_hp and current visibility.

The player record includes `food` (current nutrition) and `food_max` (the engine's
maximum nutrition). Their ratio supplies the food bar's percentage; the client
does not hardcode Angband's food capacity.

`player.hp_warning` is the configured low-HP threshold in hitpoints, using
Angband's integer rounding; the warning applies strictly below it (zero disables
it). `player.death_pending` covers confirmed death and acknowledgement of the
fatal message, which precedes the engine's dead flag. Negative HP alone does not
imply death (bloodlust). Phase `dead` begins at the tombstone/retirement screen;
`finished` marks completion of the post-game interaction.

`debug.damage` is an explicit developer action. Supply integer `amount` from
1 to 30000 during normal ready play. It applies Angband's normal damage handling
without advancing a turn, including fatal-message acknowledgement and death.
It is rejected during character creation, stores, prompts or after death. This
is a state-changing debug operation, not a normal gameplay command.

Item and monster IDs are **snapshot-scoped** and expire on the next revision.
Inspecting a stale ID fails. Select current records before issuing an action.
Item actions still apply the engine's item tester, accessibility constraints and
inscription confirmations: exposing an item does not make it usable at a distance.
Each item supplies an `actions` array of command IDs for the inspection buttons,
filtered by item type and location using engine helpers. This is not a guarantee
that an action will succeed; current conditions and confirmations still apply.
The `description` string contains the full player-facing inspection prose,
including origin, effects and equipment calculations. It is generated with the
engine's inspection formatter and preserves paragraph breaks. Incidental dice
rolls inside that formatter are isolated by restoring RNG state before returning.
Unknown items retain the engine's unknown-item description; actual properties
remain available independently.

`prompt.requested` carries `prompt_id`, `type`, `text`, `maximum`, `initial`
and, for choices, `choices: [{id, label}]`. Types are confirmation (boolean),
quantity (bounded integer), text (bounded UTF-8 byte length), and choice (listed
string ID). Invalid values leave the prompt active. Some menus and message
acknowledgements still use terminal input.

Snapshots are captured on the engine thread and served from a cache. Object
descriptions use local metadata copies to avoid changing seen-state. Debug builds
assert that capture preserves the engine RNG state. This is not yet the complete
read-purity and gameplay-parity coverage required by the v1 specification.
