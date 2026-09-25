# AnybandUI semantic API: proposed protocol v1

Status: implementation design, not an implemented or frozen public API. Examples
are illustrative fixtures. Publish machine-readable schemas and a conformance
suite before declaring protocol 1.0 stable. Product requirements and milestones
are in [the product specification](anybandui-specification.md).

The first playable build implements [development protocol 0.1](anybandui-protocol-0.1.md),
which deliberately does not advertise conformance to this v1 design.

## 1. Transport and versioning

The client starts a selected backend executable with explicit argument entries,
without a shell. A backend package defines the supported launch arguments.
Standard input and output carry one UTF-8 JSON object per LF-delimited frame;
CRLF is accepted. Embedded newlines in strings are JSON-escaped. Stdout contains
protocol frames only; stderr contains diagnostics. Hidden gameplay information is
valid protocol data, not a restricted class of fields. Use buffered parsing that
handles partial reads.

Before negotiation, frames are limited to 1 MiB. Negotiation may select a lower
limit, never a higher v1 limit. Oversized frames terminate the connection with a
diagnostic. Large collections use bounded pages/chunks. Read and write queues are
bounded; a blocked client pauses publication/engine progress rather than growing
memory without limit. Cosmetic events may be coalesced; prompts, command results
and gameplay-visible messages must not silently disappear.

Major version changes break compatibility. Minor versions add optional fields or
negotiated capabilities; required field/meaning changes require a new major.
Negotiate an exact major and the highest mutually supported minor. Ignore unknown
optional fields, but do not ignore unsupported required operations or capabilities.
No gameplay command is accepted before successful negotiation.

All IDs are opaque strings. Sequence/revision/turn counters use decimal strings
to avoid cross-language integer precision loss. Ordinary coordinates and bounded
quantities are JSON integers. Internal pointers, enum ordinals, array indices and
memory addresses are not public IDs. Human-readable labels are not stable IDs.

### Initial handshake

Client request:

```json
{"kind":"request","id":"r1","method":"hello","params":{"protocols":[{"major":1,"minor":0}],"client":{"id":"org.angband.anybandui","version":"0.1.0"},"max_frame_bytes":1048576}}
```

Backend response (capabilities abbreviated):

```json
{"kind":"response","id":"r1","result":{"protocol":{"major":1,"minor":0},"engine":{"id":"org.angband.angband","version":"4.2.6-anybandui-dev","build":"example-build","save_compatibility":"angband-example"},"capabilities":{"state.player":1,"state.map":1,"state.items":1,"commands":1,"prompts":1,"terminal.fallback":1},"max_frame_bytes":1048576}}
```

A capability version specifies behaviour, not just a boolean. The client must
understand that version to use it. Unknown optional capabilities are ignored.
Core v1 requires lifecycle, snapshots, player/map/items/messages, command discovery,
command execution and prompts. A backend must cover its remaining playable flows
with semantic prompts or the negotiated terminal fallback. Comparison, knowledge,
previews, stores, birth, history, controllers' suggested mappings and presentation
events are independently negotiated extensions. A mock transport with no gameplay
is explicitly identified as a test fixture, not a conforming playable backend.

## 2. Envelope and errors

Requests have `kind`, unique connection-scoped `id`, `method` and `params`.
Responses echo `id` and contain exactly one of `result` or `error`. Events have
`kind: event`, `event`, monotonic connection `seq`, optional game `session_id`,
and `data`. Game-scoped requests include the current session ID.

Errors contain a stable `code`, a user-facing `message` and optional structured
details. Define at least `invalid_request`, `unsupported_protocol`,
`unsupported_capability`, `wrong_session`, `wrong_phase`, `stale_revision`,
`stale_handle`, `invalid_argument`, `busy`, `cancelled`, `io_error` and
`internal_error`. Error details may reference actual engine state. An ordinary
gameplay failure is a command outcome with its normal cost/message, not always
an API validation error.

The client must not retry mutations automatically after a timeout. Within a
connection, duplicate request IDs with identical payloads return the original
cached response and never repeat a mutation. Reusing an ID with a different
payload is an error. Retain mutation records for the connection, bounded by an
advertised maximum request count; refuse new requests at that bound before
executing them. Reconnection creates a new connection, and does not authorize
replaying outstanding actions.

## 3. Lifecycle and scheduling

Supported phases: `launcher`, `birth`, `playing`, `store`, `dead`, `finished`.
Alongside phase, report readiness: `ready`, `processing` or `awaiting_prompt`.
Do not infer readiness from the arrival of a map update. A new/load operation
creates a session ID; unloading or restarting invalidates every old session ID,
entity handle, prompt and revision. A level transition creates a new level ID.

Only one gameplay command may be in flight. A command request returns an
acceptance response with an `action_id`. Subsequent correlated events deliver
zero or more prompts, visible messages and state updates, then exactly one
`action.completed` outcome. Completion means the engine has resolved the action,
not that a renderer has finished animating it. On connection loss the action's
outcome can be unknown; do not guess that it failed or replay it.

Publish snapshots at stable boundaries after ordinary engine updates. While the
engine is running, serve only a previously published snapshot and identify its
revision/readiness. Event subscribers mark data dirty or copy relevant
event values; they must not recursively process commands or publish partially
updated state. Prompt publication is a safe boundary for that prompt's choices.

On quit, ask the engine to use its normal save/exit flow. On client disconnect,
cancel an outstanding UI prompt where legal, stop accepting actions, and use an
existing engine-supported safe save/exit path when available. Do not promise
crash recovery beyond the engine's guarantees. Save failure must be reported;
graceful shutdown must not silently discard a live character after a failed save.

## 4. Operation catalogue

| Method | Purpose and constraints |
| --- | --- |
| `hello` | Negotiate protocol, frame limits and capabilities before a game |
| `catalog.get` | Descriptors for stats, slots, categories, commands, units and engine content, including undiscovered identities |
| `saves.list` | Compatible save references and engine-approved metadata; no arbitrary file reads |
| `session.new` / `session.load` | Start birth or load an engine-approved save reference |
| `session.save` / `session.close` | Execute ordinary engine save/close semantics at legal boundaries |
| `state.get` | Get one immutable snapshot or bounded pages tied to the same revision |
| `state.subscribe` | Subscribe to versioned updates; begin with a full snapshot |
| `inspect.get` | Actual properties and player-knowledge/perception metadata for a current public handle |
| `commands.list` | Commands and argument descriptors for the current UI context |
| `command.execute` | Submit a command and optional arguments through engine validation |
| `prompt.reply` | Reply to an exact outstanding prompt using its offered choices |
| `action.interrupt` | Request existing run/rest/repeat interruption at the engine's legal boundary |
| `items.compare` | Optional pure comparison, candidate plus explicit replacement slot/handles |
| `target.preview` | Optional estimate with explicit actual-state or player-known calculation basis |
| `knowledge.query` | Optional searchable content with learned/discovered metadata and pagination; may include unlearned entries |
| `messages.query` / `history.query` | Bounded message/event records with optional category/text filters and observation metadata |
| `terminal.input` | Negotiated fallback input only while its matching terminal context owns focus |

Filtering can happen in the client or backend. A requested player-known view
applies that scope consistently to matching, counting and sorting. Actual-state
views may include hidden facts. Knowledge-based filtering is a convenience for
consumers, not a mandatory access boundary or a prerequisite for API use.

## 5. Snapshots, IDs and invalidation

A snapshot contains session ID, revision, phase/readiness, level ID, engine turn
label and capability-appropriate collections. Sections may be fetched separately,
but every page must identify the same revision. If that revision has expired,
return `stale_revision`; never combine pages from different revisions silently.

Deltas contain `base_revision`, `revision`, upserts and removals. Apply all changes
atomically. A missing base revision, sequence gap or new session requires a full
resynchronization. Paginated snapshots have explicit start/end markers; the client
does not render an incomplete snapshot as current truth. Keep the previous model
while loading, clearly noninteractive where state-changing actions would be stale.

Public item handles stay stable through reorder, but not necessarily stack
split/merge, destruction or replacement. Report removals and new handles; never
reuse a removed handle in the same session. Movement between pack and equipment
retains identity when the engine can establish that safely. Creature handles may
remain stable while creatures are unseen or disguised. Export actual identity
and location separately from perceived identity and last-observed location.
Old observation records may remain in history, labelled as historical rather
than current. Loss of visibility alone need not invalidate actual-state handles.

Every mutation references the current revision. The backend validates session,
phase, revision and handle before resolving any pointer. A stale reference fails
without consuming a turn or accidentally acting on the next item at that index.
Game rules are then evaluated normally, with their ordinary costs and messages.

## 6. Semantic record families

Every field documents its meaning and provenance in the eventual schema. The API
may expose any hidden gameplay fact; no inspection, identification, per-field
approval or spoiler/debug mode is required to make such data eligible for export.
Implement fields incrementally according to capabilities, not player knowledge.

Numeric properties use stable ID, label, unit and explicit value records. The
`actual` record describes the engine value; `player_known` describes what the
character knows, with `known`, `unknown` or `not_applicable` status. Each value
record has `available`, `unavailable` or `not_applicable` status as appropriate;
an unavailable actual value is not the same as a value unknown to the player.
Remembered/perceived values carry observation context and can differ from actual
ones. Missing optional fields mean unsupported under capability rules.

Example property:

```json
{"id":"core.speed","label":"Speed","unit":"engine.speed","actual":{"status":"available","value":3},"player_known":{"status":"known","value":3,"display":"+3"}}
```

Property unknown to the character, with its actual value still available:

```json
{"id":"core.to_hit","label":"To hit","actual":{"status":"available","value":5},"player_known":{"status":"unknown"}}
```

A floor item's curse property can likewise drive a visual effect before inspection:

```json
{"id":"core.cursed","label":"Cursed","actual":{"status":"available","value":true},"player_known":{"status":"unknown"}}
```

The client may use `actual.value` without converting `player_known` to `known`
or sending an identification command. This is an example of ordinary API use,
not a special exception to an otherwise knowledge-restricted contract.

| Record | Required semantics |
| --- | --- |
| Player | Actual stats/resources/statuses alongside player-facing values and descriptions; no raw player struct |
| Item | Opaque handle, actual identity/properties, known identity/properties, appearance, location/slot, inscription and action descriptors |
| Equipment slot | Backend-defined ID, label, accepted action descriptors and occupant; no fixed ring/armour slot array in client |
| Creature | Actual identity/location/HP/statuses plus perceived appearance, health band, learned recall and observation context |
| Map cell | Level-relative coordinate, actual terrain/occupants/traps, remembered/observed terrain, detection markers and presentation references |
| Spell/action | Namespaced ID, label, actual parameters, player-facing description, costs/failure information and argument descriptors |
| Message | Stable sequence, player-facing text/category, repeat count, turn label if recorded and optional event/location references |
| Knowledge entry | Actual identity/properties, learned/discovery/kill metadata, descriptions and supported filters |
| Store entry | Stock handle, quantity and currently displayed prices; freshness validated at purchase |
| Run event | Actual event/identity, engine turn label and what was known/perceived at the time |

Namespace common semantics as `core.*` and custom ones under the engine ID.
Use `display` as an accessible fallback for unfamiliar numeric units. Formats
such as Angband's exceptional stats must not be reverse-engineered from labels.
Arrays and descriptors permit forks with different body plans or resource systems.
If the client cannot render a custom property specially, show its label/value
according to the front-end's chosen presentation, rather than guessing its meaning.

Presentation assets are package-relative logical IDs with glyph fallbacks.
Missing assets must not prevent play. Actual-identity and perceived-appearance
asset references can both be supplied for the client to choose between.
Input maps use semantic key/modifier
descriptions, not OS scan codes in the cross-platform contract.

## 7. Commands, prompts and confirmations

Commands expose stable IDs, labels, search terms, current context and ordered
argument descriptors. Arguments can be direction, position, item handle, spell,
quantity, enum, text or confirmation. Descriptors describe only permitted options;
the engine remains authoritative when the action executes.

Example execution request:

```json
{"kind":"request","id":"r21","method":"command.execute","params":{"session_id":"s1","revision":"84","command":"core.wield","args":{"item":"item-27","slot":"slot-main-hand"}}}
```

Do not bypass engine checks merely because a client supplied complete arguments.
An inscription, curse interaction, dangerous action or ambiguous selection can
still generate a prompt. The API may describe otherwise hidden failure reasons,
but that information must not change how an attempted command is executed or
avoid the costs it normally incurs. Information access does not grant mutation
rights outside the existing command path.

Example prompt:

```json
{"kind":"event","seq":"101","session_id":"s1","event":"prompt.requested","data":{"action_id":"a9","prompt_id":"p3","type":"confirmation","text":"Really perform this action?","choices":[{"id":"yes","label":"Yes"},{"id":"no","label":"No"}],"default":"no","cancellable":true}}
```

A reply contains session, action and prompt IDs and a typed value. Accept only
the current outstanding prompt; duplicates cannot confirm the following prompt.
Cancellation has an explicit representation and follows the original command's
semantics. Preserve engine default choices and confirmation text. On completion,
report `succeeded`, `failed`, `cancelled` or `interrupted` and the final
revision; failure is not a promise of zero turn cost.

AnybandUI maps keyboard, palette, context menu, mouse and controller to these same
requests. While a prompt owns input, ordinary movement keys cannot queue gameplay
behind it. No click or key is delivered to both the terminal fallback and the
semantic command path. User macros remain ordered engine actions and stop for
engine-required prompts and interruptions; the client supplies no tactical policy.

## 8. Query purity and previews

Inspection and comparisons operate on published actual/player-known data or isolated
copies with audited pure calculations. They may not equip items temporarily,
trigger recalculation in the live player, advance object knowledge or consume
RNG. A query that cannot be computed safely returns `unsupported_capability` or
a partial/unknown result with a clear reason.

Comparison returns named metrics, current/candidate values, differences and an
explicit `basis` of `actual` or `player_known`. A two-ring comparison asks which
slot is replaced. Actual-state comparison may use unidentified modifiers;
player-known comparison must preserve uncertainty about them.

Target preview declares the same basis and returns path/area cells, range and
uncertainty markers. Actual-state previews may use unseen terrain and occupants.
Estimates must not be described as resolved random outcomes. Any hypothetical
calculation must leave live state and live RNG untouched. A backend advertises
which calculation bases it supports; the client decides which results to show.

## 9. Presentation events and terminal fallback

Optional events may describe full projectile paths, beams, explosions, movement
and other engine actions, including unobserved ones. Include sequence,
session/level, actual source/target coordinates, resolved damage where available
and perception metadata for participants and path/area segments. Hidden details
need not be removed before serialization. The client chooses which events or
segments to render and may use additional state for deliberate effects, such as
a cursed-item treatment. Queue copies; engine event pointers are ephemeral.

The client may collapse cosmetic animations at any time. Presentation uses its
own clock and RNG. It cannot delay command acceptance, modify the engine's random
stream or alter a save. Level changes discard old-level animations.

The development fallback transmits terminal cells, colours, cursor, dimensions
and a context token. It is a view of engine-rendered output, not a screen-scraping
source for semantic records. Input is accepted only for the active context token.
Resize is a display operation, not a command. Capture engine-produced frames at
existing render boundaries; do not repeatedly redraw on query and accidentally
consume RNG. Audit mixed terminal/semantic modes with the same parity tests.

## 10. Saves, metadata and failures

The launcher associates an opaque save reference with the selected backend and
save compatibility ID. The backend performs authoritative format validation;
a matching manifest alone cannot make an incompatible save loadable. Preserve
the engine's ordinary death/permadeath and save semantics.

AnybandUI sidecars contain UI preferences, thumbnails and recorded history, which
may include actual events as well as their player-knowledge context.
They have their own schema version and explicit association with a save/run.
Missing, corrupt or stale sidecars fall back to the engine save and an incomplete
history notice. Write sidecars atomically; do not implement user-visible rollback
or resurrect characters as an accidental consequence of metadata backups.

On backend termination, keep the last view marked disconnected, disable commands
and offer return to launcher/reload through ordinary save loading. Do not fabricate
a successful save or resume an in-flight action. Diagnostics distinguish protocol
errors, engine exit and OS launch/dependency failure. Hidden gameplay facts may
be included in structured diagnostics when useful; logging is not a requirement
to dump the entire simulation.

## 11. Implementation deliverables before freezing 1.0

1. JSON Schemas for envelopes, negotiation, core records and each capability;
   positive and negative examples for every operation.
2. Adapter field inventory identifying the source/helper, actual/known semantics,
   lifetime and purity evidence for each exported field.
3. Reference backend and client protocol library, with no engine structs in the
   public headers and no shell-dependent process launch.
4. Conformance runner, deterministic fixtures and a mock variant with different
   stats/slots/commands and deliberately absent optional capabilities.
5. Tests for prompt cancellation/duplication, action completion, stale item/level
   handles, chunked snapshots, resynchronization, frame limits and disconnects.
6. Parity and actual/known-state tests from the product specification, plus native package
   smoke tests on Windows, macOS and Linux.
7. An adapter author guide explaining required core flows, optional capabilities,
   extension namespacing, save separation and compatibility guarantees.

Do not publish protocol 1.0 as stable while mandatory record fields, command
completion rules or actual/known-state semantics are still being inferred by client code.

### Fixed dungeon viewport

`presentation.viewport: 1` advertises `dungeon.viewport {width, height}`.
Dimensions are 9–240 columns and 5–128 rows, clipped to level bounds;
`{width: 0, height: 0}` restores the classic viewport. Requires a 4 MiB
negotiated frame budget and normal-play readiness. This read-only presentation
uses remembered map visuals, follows the player at the edges (or continuously
with `center_player`), and reveals the native targeting cursor as needed.
Free camera takes precedence while enabled. No turn or gameplay RNG is consumed.
