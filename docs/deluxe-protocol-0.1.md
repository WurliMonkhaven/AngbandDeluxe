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

The player record includes `food` (current nutrition) and `food_max` (the engine's
maximum nutrition). Their ratio supplies the food bar's percentage; the client
does not hardcode Angband's food capacity.

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
