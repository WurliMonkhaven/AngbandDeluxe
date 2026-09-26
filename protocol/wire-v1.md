# AnybandUI engine protocol 1.0

This is the wire reference for the required full-v1 engine surface. The canonical
compatibility requirements are in `protocol/full-v1.json` and `protocol/README.md`.
Engine implementation and engine-specific tests live in the separate angband repo.

The backend is a local child process. UTF-8 JSON objects are separated by
newlines on stdin/stdout; diagnostics go to stderr. Frames must be smaller
than 1 MiB before negotiation. Clients offering `max_frame_bytes: 4194304`
receive a 4 MiB limit, required by full-v1 for the full-level camera. Other
clients retain the 1 MiB limit. One process hosts one game. Networking is not supported.

```json
{"kind":"request","id":"1","method":"hello","params":{"protocols":[{"major":1,"minor":0}]}}
```

Responses have `kind: "response"`, the matching `id`, and either `result` or an
`error` object containing `code` and `message`. Events have `kind: "event"`,
an `event` name and `data`. IDs must be unique nonempty strings up to 64 bytes.
Duplicates are rejected, not replayed. There is currently a 32,768-request
connection limit. This limit and duplicate policy are development limitations.

| Method | Parameters and result |
| --- | --- |
| `hello` | Offer protocol 1.0; receive selected protocol, engine and capability metadata. |
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
Native looking, targeting and aim-direction prompts retain `dungeon`; dedicated
recall screens still use terminal fallback and preserve their original controls.
Ordinary message acknowledgement is an exception: `message_pending: true` may
accompany `dungeon` with `readiness: "awaiting_prompt"`. Keep the dungeon visible
and show the native continuation message ribbon within the normal CRT effects pass.
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
backpressure between display frames. The headless `anybandui-transport-tests` target
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


### Native look and targeting (`interaction.targeting: 1`)

An active `targeting` snapshot contains `mode` (`look` or `target`), cursor `x/y`,
`interesting`, `can_confirm`, `candidates` and `path`. Point arrays contain world
`[x,y]` coordinates. These values come from the running engine targeting loop;
`path` is its projection path, not a client recomputation or an area/damage
prediction. `aiming: true` identifies the engine's direction prompt. Both are
compatible with `readiness: "awaiting_prompt"` and native dungeon presentation.
Optional `selected_target` records the established target's world position and
whether it is a monster. Monster records additionally include `condition` text.

All requests below require the latest snapshot's `context`. They reject
stale contexts, unrelated prompts and nested terminal screens:

- `targeting.begin`: `mode: "look" | "target"`, optionally integer `x/y`. Starts
  from normal play; coordinates must be interior dungeon tiles.
- `targeting.select`: integer world `x/y`. Relocates an active cursor without
  confirming. The engine updates its camera and interesting/free selection mode.
  During an aim-direction prompt, a visible-viewport coordinate opens targeting
  through the original engine mouse handler.
- `targeting.control`: `operation` is `confirm`, `cancel`, `next`, `previous`,
  `free`, `interesting`, `player`, `recall` or `target`. These feed the original
  engine controls. During an aim-direction prompt only `cancel` and `target`
  are accepted here; direction/current-target keys use `terminal.input`.

Look mode allows confirmation of cycled terrain/item locations through the
engine's free-location controls, for both keyboard and API confirmation.
Combat targeting retains the engine's eligibility checks.
Free location targeting remains allowed where the original engine permits it,
including locations that cannot be hit; the engine still decides the action's
range, obstruction and outcome. Cancelling a ranged target returns to its parent
prompt as usual. API queries/hover inspection do not move the cursor or consume
turns. Ordinary keyboard bindings continue to work, including Escape, t, +/- and
free/interesting mode; nested recall screens keep their original controls.

### Mouse movement (`interaction.mouse: 1`)

`dungeon.click` requires the latest `context` and integer world `x/y` within
the current viewport. Optional boolean `shift`, `control` and `alt` preserve
Angband mouse modifiers. Accepted only during normal ready play, it honors
the engine mouse-movement option and feeds an ordinary left click to Angband:
adjacent movement/melee, distant pathfinding, and clicking the player retain
the original rules. Active looking/aiming instead uses `targeting.select`.

`targeting.set` takes `context` and integer world `x/y` during normal ready
play. It immediately selects a targetable monster at that position, otherwise
a location, through the engine target setters. It consumes no turn and never
enters the interactive targeting loop. Invalid coordinates, stale contexts
and requests during another interaction are rejected. Combat range and
obstruction checks still apply when an action uses the target.

`dungeon.click` optionally accepts `exit_look: true`. During a native Look
interaction this validates the click, cancels Look, and dispatches the saved
world coordinate/modifiers at the next normal command boundary. This avoids
reinterpreting the tile after the camera recenters. Combat targeting, recall
screens and unrelated prompts still reject movement.

### Walk and pick up (`interaction.pickup: 1`)

Floor item records expose `can_pickup` from engine visibility, ignore and
carrying checks. `dungeon.pickup` accepts the latest `context` and integer world
`x/y` during normal ready play. It validates an observed item (including memory or hallucination), approaches
with engine pathfinding and a final walk, and issues normal pickup only after
uninterrupted arrival on the same level. A failed route or interruption
discards the intent. Ordinary automatic pickup may collect items on arrival.
The action does not promise a specific item from a multiple-item pile; the
engine retains its selection prompts. Escape may interrupt pending travel.

The context-menu pickup attempt uses the semantic cell object layer, rather
than actual floor item records or current visibility. Remembered/hallucinated
objects therefore remain selectable even if no real object exists. Arrival
checks the actual pile and carrying capacity; an empty destination ends quietly.
`can_pickup` on actual item records remains a current-visibility/capacity check
and does not determine whether a remembered pickup attempt is offered.

### Terrain actions (`interaction.terrain: 1`)

`terrain_actions` lists observed viewport tiles as `{x,y,action}` entries,
where action is `tunnel`, `up` or `down`. Eligibility uses remembered engine
terrain, not client glyph/name matching. `dungeon.terrain` accepts those fields
and the latest `context` in normal ready play. It shares travel cancellation
with pickup, approaches a diggable wall or stands on stairs, then queues the
engine command. Tunnelling uses its ordinary repeat and interruption rules.
Stale contexts, mismatched actions and ineligible terrain are rejected.

`direction_prompt: true` identifies the engine movement-direction prompt
used by tunnelling, opening and similar commands. It retains `dungeon`.
`targeting.select` supplies a mouse click to that original direction handler;
`targeting.control` accepts only cancel in this context, without ranged-target
confirmation. `aiming` continues to mean the separate ranged direction prompt.
Context-menu Tunnel continues with another engine repeat batch when the prior
batch expires. Success, impossible digging and ordinary disturbance end it.

### Native item choices (`prompts.items: 1`)

Item prompts retain `type: choice` and add `selection_kind: item`. Each eligible
choice has an `item_id` referencing the immediately preceding snapshot, plus an
optional engine inventory/equipment `shortcut`. Reply with the choice `id`, not
the item handle. Eligibility, inscriptions, quantities and confirmations remain
engine-owned. If a choice cannot be linked, the generic choice UI remains valid.
`item_selection: true` lets the native dungeon remain visible behind the prompt.
Item action lists expose specific Quaff/Read/Eat actions instead of generic Use
for consumables, plus eligible Take off, Fire and Throw actions.


### Native storefront (`interaction.store: 1`)

During an active store visit, snapshots include `store`: `name`, `home`,
`ready`, `no_selling`, optional `owner` and `owner_purse`, plus `stock` and
`inventory` arrays. Entries reference current `items` by `item_id`. Store/Home
stock is appended to `items` with location `Store`/`Home`; ordinary inventory
handles retain their usual meaning. Non-home entries include `unit_price` in
gold. Inventory entries include `eligible`; stock entries include `compare_with`
(current equipped item handles in matching engine equipment slots). Descriptions
use normal engine inspection and shop naming. Unit prices are display quotes;
stack/charge rounding means the client must not calculate a transaction total.

`store.buy` and `store.sell` accept `context` and `item`; they also mean retrieve
and stash when `home` is true. `store.leave` accepts `context`. All require the
current store context and `store.ready`, and reject concurrent prompts, stale
handles and incorrect item ownership/eligibility. Store readiness is separate
from normal gameplay `readiness`. Requests acknowledge acceptance, not completion;
subsequent snapshots/prompts report the engine result. Transactions use existing
quantity and confirmation prompts (confirmation text includes the exact price).
Cancel replies preserve the existing engine cancellation behavior.

`store.ready` is false throughout a transaction, including its prompts. If a
transaction pauses for a message, `message_pending` is true and `terminal.input`
acknowledges it using normal engine controls. At an idle storefront Escape leaves;
other raw gameplay keys are rejected instead of being queued for later movement.
Clients without this capability can continue to render the terminal fallback of
backends that do not offer it; this backend's native storefront requires a client
that understands the advertised capability.

Item records include `name_color`, the engine palette index for list/inspection
name text (item-type colour, with unreadable inventory books dimmed as in the
original UI). `color` remains the separate dungeon glyph colour.


### Spells (`spells: 1`)

Player records add `spellcasting` and `new_spells`. Readable, available book items
(and readable Store/Home stock) add `spells`, `book_available` and `choose_spells`.
Each spell has an opaque session/class-scoped `id`, `label`, `description`,
`level`, `mana`, current `failure` percentage, `status`, optional nonempty `info`,
`can_cast`, `can_study`, `low_mana`, and `needs_aim`. These are engine results,
not client calculations. Stock spells are inspectable but not actionable.

`command.execute` for `core.cast` or `core.study` optionally accepts `spell`
alongside a current book `item` handle and snapshot `revision`. The adapter
validates membership and eligibility, selects that book through normal item
checks/inscriptions, and supplies the spell through the normal selection hook.
Random-study classes reject explicit spell selection: send `core.study` with
the book only. `core.browse` browses a selected book or asks for one.

Spell prompts have `type: choice`, `selection_kind: spell`, `browse`, and
`choices` containing spell presentation fields plus an engine `shortcut`. Reply
with the choice's `id` (not a snapshot spell id), or null to cancel/close. A browse
prompt is inspection only; its result never casts or learns a spell. Snapshot
`spell_selection: true` retains the semantic dungeon while a spell prompt is open.
Do not infer casting eligibility from mana alone: the engine may permit casting
after a low-mana confirmation. Successful selection can lead to further engine
prompts or aiming; request acknowledgement never implies a completed cast.


`targeting.select` accepts optional `confirm: true`. During combat targeting it
applies relocation and confirmation atomically in the original target loop.
During an aim-direction prompt it selects the clicked monster/location and feeds
the original use-target input. Look mode ignores confirmation and only relocates;
movement-direction prompts retain ordinary mouse direction behaviour. Context,
coordinate and visibility-of-viewport input checks still apply. Missing/false
`confirm` preserves the previous select-then-confirm interaction.

`native_prompt: true` retains semantic dungeon presentation behind structured
native dialogs during normal play, including low-mana confirmations. It does
not override birth, stores, saved terminal screens, or other fallback contexts.

### Ground-item aura hints

Semantic `dungeon.items` entries may include `aura`: `artifact`, `rune`,
`cursed`, or an empty string. This is derived exclusively from the remembered
object and known runes; curses take precedence over artifacts, then runes.
Clients must suppress the effect for unseen, hallucinated or actor-covered
tiles. An absent field means no glow. The hint does not identify items or
change lighting, visibility, RNG or gameplay.

Ground item glow is independently enabled by the persisted `item_glow` setting
(default true), available under Animations / Dungeon indicators.


### Free dungeon camera

`presentation.camera: 1` advertises `dungeon.camera {enabled: boolean}`.
Configure it before character creation/loading or at a normal play boundary.
Changing it publishes a new state (and fresh revision/handles) without spending
energy. Enabling requires a negotiated 4 MiB frame budget. Default is disabled.

When enabled, `dungeon.full_level` is true, its origin is `(0,0)` and its cells
cover the whole level. The engine uses read-only known-map extraction: terrain,
traps and objects come from remembered knowledge, and actors obey visibility.
Hallucinated glyphs use deterministic presentation noise, not the engine RNG.
The classic cached viewport is unchanged when disabled. Clicks, aiming and
pickup accept world coordinates outside the terminal panel in full-level mode;
normal engine rules still resolve movement, directions, targeting and travel.

The client's `camera.enabled` and `camera.follow` preferences live under Display.
Middle-drag pauses following; the wheel zooms; Return to player recenters and
resumes following if enabled. Floor changes recenter automatically. Target
cursor changes are kept in view. Panning and zooming are entirely client-side.

### Game tuning

The `tuning` capability enables `tuning.get` and `tuning.set`, available both in
the launcher and during play without acquiring the gameplay command lock.
`tuning.get` returns `entries` (id, group, label, description, default, bounds,
category, structural/tier/advanced flags), a complete `values` object and a string
`revision`. After character initialization, `active_values` reports the values
used by that engine process. An invalid override file adds a `warning` and displays
stock defaults so the client can repair it.

`tuning.set` accepts `{revision, values}`. `values` is the entire desired set of
overrides; omitted IDs revert to installed defaults. A stale revision is rejected.
Values, critical tier ordering, and cross-setting constraints are validated before
an atomic replacement with backup. The response is an updated catalog. This does
not change the active game, state revision, or item handles. The backend applies
saved tuning on its next initialization. Structural settings are captured per save
and are only chosen afresh for new characters. The editor does not add or remove
critical tier rows.

### Scene effects

Monster observations include `unique` and `morgoth` flags. The renderer still
requires a visible, seen, non-hallucinated monster glyph. Feature catalog entries
include `fiery`.

#### Tile presentation

`presentation.tiles: 1` adds `dungeon.tiles { id }`, where 0 means ASCII and
1–6 match `lib/tiles/list.txt`. This presentation-only request is accepted at the
launcher or a normal command boundary, consumes no turns, and republishes the
view during play. Invalid IDs and fractional values are rejected.

When enabled, `dungeon.tileset` identifies the active mapping and `dungeon.tiles`
is a height × width array of eight integers per cell: terrain glyph/attribute,
trap glyph/attribute, object glyph/attribute, actor glyph/attribute. High-bit
pairs encode atlas column/row (`& 127`); other pairs use the ASCII fallback.
Existing `dungeon.cells` and terminal fields are unchanged. Tile generation
respects knowledge, lighting, camouflage and hallucination, and never draws
from gameplay RNG. The same frame budgets and view coordinates apply.