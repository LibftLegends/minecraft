# Minecraft 1.0 Roadmap

Backend implementation update (2026-10-26; source inspection only): chunk
publication now treats failure to roll back a newly added publication receipt
as an invalid transaction state when the combined session checkpoint fails.
Corrupt-session recovery now validates the backup and atomically repairs the
primary save before loading the backup state into live stores; repair failures
are returned. Fluid bucket transfer now checks rollback results across the
block, inventory, journal, fluid snapshot, and item-ID allocator, returning an
invalid transaction state if restoration fails. Generation crash/fault recovery
and cross-store acceptance coverage remain open. Block mining now reports an
invalid transaction state if journal rollback or its rollback checkpoint fails
during preparation. Equipment loadout preparation applies the same fail-closed
rollback rule. Farmland till rollback checks all changed participants and its
recovery checkpoint; equipment repair checks journal restoration after
prepared-state serialization failure. Craft, pickup, and timed-processing
prepare APIs now restore the pre-request journal when their initial checkpoint
fails, preserving exact retry behavior; restoration failure is surfaced as an
invalid transaction state. The hostile damage tick restores hit points and
equipment wear if durability updates fail and reports equipment rollback
failure explicitly. Generation crash/fault recovery and cross-store acceptance
coverage remain open. Inventory block placement now checks journal transitions
and durable checkpoints on both rollback branches, failing closed when those
records cannot match the participant state. Prepared block-placement recovery
also reports an invalid transaction state if it cannot roll back the placed
block after inventory restoration fails. Card-item transfer preparation
restores its pre-request journal after an initial checkpoint failure while
preserving an already prepared retry for recovery. `GameSession::stop` now
returns a failed final snapshot result without destroying the live session;
menu transitions stay in the active phase so the host can retry, and process
shutdown returns failure instead of reporting a clean exit. `GameSession` also
has a backend start entrypoint that accepts a stable nonzero 32-byte world ID;
the ID selects the save namespace independently of the seed. Host-config
snapshot v3 binds both ID and SHA-256(seed), rejecting mismatched opens; v1/v2
snapshots migrate only under the legacy SHA-256(seed) identity. Seeds that
exceed the world's bounded seed field are rejected. The legacy seed-only start
keeps its prior SHA-256-derived identity for compatibility.
`WorldSaveCatalog` now provides persistent create/list/find/rename and policy
update APIs for named worlds, with CSPRNG-generated IDs, bounded versioned
metadata, checksum validation, and atomic writes constrained to `saves/`.
Its WCT1 v2 records store the nonzero owner actor ID and sorted allowlist;
v1 records remain readable with owner zero. WCT1 v3 adds character-admission
mode and player capacity; v1/v2 default to world-bound-only and one player.
WCT1 v4 persists sorted banned character IDs, and both join authorization APIs
reject those identities. The owner can update the ban list; ejecting an active
session still requires host-session integration.
WCT1 v5 adds generator and ruleset compatibility ranges with owner-managed
updates; the saved world's own versions must remain inside the configured
ranges. Versions 1-4 migrate to exact-version ranges.
WCT1 v6 persists a character import-category mask plus progression, item-count,
and item-value caps. `authorize_character_import` checks a trusted host-derived
summary against those limits and the world's access and ban policies. Versions
1-5 migrate to a zero-import policy.
WCT1 v7 adds an owner-managed approved-character list; `APPROVAL_REQUIRED`
accepts an owner, an actor in the world allowlist, or a specifically approved
character ID. Versions 1-6 migrate with no character approvals.
`authorize_access` enforces
private, invite-only, allowlist, friends-only, and public access, with the
friendship result supplied by the authenticated host.
`authorize_join` checks character ownership mode, source world, freshness,
content versions against the saved compatibility ranges, approval, bans, and
the host's active-player count.
`reserve_join` adds a mutex-serialized per-character lease and reserves a
capacity slot for the live catalog instance; `release_join` frees both. It
also returns the same lease for an exact retry with the same actor/request ID,
and rejects reuse of that ID with changed join data. `validate_join_lease`
rechecks current access, bans, approvals, content ranges, ownership mode, and
capacity immediately before publication. Every online session must use these
APIs for reservation accounting to be complete.
Leases are process-local and are cleared by process restart; durable join
publication, crash reconciliation, and save/character commit are still open.
Catalog policy edits can migrate ownership mode while no join lease is active.
Rename, access-list, and
admission-policy mutation APIs now require the authenticated owner actor ID and
reject other callers.
`GameSession::start_from_catalog_admitted` reserves a catalog join lease using
host-authenticated actor and character inputs, opens only a compatible world,
revalidates policy before returning an active session, retains the reservation
if final save fails, and releases it after a successful final save. The legacy
catalog start overload and public direct world-ID start overload fail closed
because they have no authenticated join context. Network session publication,
durable leases, and character profile filtering remain open. Session snapshot
checkpoints now revalidate active admission leases; shutdown can still save the
last authoritative state after policy revocation before it releases the lease.
Lease release treats an already-missing process-local reservation as an
idempotent success so a stale lease cannot strand an otherwise clean shutdown.
`GameSession` now creates a stable CSPRNG character ID by default and persists
it with a `WORLD_BOUND`/`PORTABLE` mode in host-config payload v4. Hosts may
configure an identity before start; legacy payloads migrate to a world-bound
identity derived from the world ID. This stores identity alongside each
world's session snapshot; account-owned portable profile separation and
cross-world state filtering are not implemented yet.
Atomic join publication, durable replay records, and crash reconciliation of
process-local leases remain open. Terrain chunks now persist independently and
are cleaned up after confirmed catalog deletion; inclusion in WBK backup and
restore and crash-atomic coordination with session snapshots remain open.
The WVC1 per-world chunk store now loads and saves initial and streamed
generated chunks, restores cached chunks before generation, and persists direct
block placement, deletion, state replacement, authoritative replication edits,
undo/redo, deferred generated edits, and persisted fluid overrides. A failed
authoritative edit write restores the exact pre-edit voxel snapshot.
Regeneration updates its terrain
payload alongside the in-memory chunk and restores the previous terrain
payload when a later in-process commit step rolls back. Store operations
serialize through a per-instance mutex. Crash-
atomic coordination, cross-process writes, and coordinated
terrain/entity/revision snapshots still need integration; the current chunk
store is not yet a complete world-save transaction.
`GameSessionPersistence` exports and imports bounded, checksummed WBK1 v3
bundles bound to the source world ID, containing the MCSP snapshot, optional
revision-metadata sidecar, and WTA1 archive of that world's persisted chunks.
The importer continues to accept legacy WBK1 v2 bundles, which leave terrain
untouched. Malformed MCSP, revision metadata, terrain records, a destination ID
mismatch, or a current host-config ID mismatch is rejected before installation.
For v3 imports, WBJ1 v2 records the old snapshot, revision sidecar, and terrain
archive before replacement; recovery restores all three or removes terrain if
the world had no prior terrain directory. Catalog-aware backup and restore
require the world owner; restore also requires typing the exact world name. A
catalog maintenance lease blocks new admitted joins, and a process-local world
registry blocks direct GameSession starts during these operations. Cross-process
maintenance coordination and crash durability across the separate snapshot and
terrain stores remain open. Owner-confirmed deletion journals the two session
files in WBJ1, removes the catalog entry, then removes the per-world terrain
directory under the WDL1 recovery marker. Terrain cleanup retries after a
post-commit interruption.
The legacy `authorize_join` capacity
check still uses a host count; hosts must use `reserve_join` for serialized
reservation. Network publication still needs admitted-start integration.
Portable character profile separation and
destination filtering remain open. No tests or builds were run.

**Status:** implementation in progress; the documented APIs and code slices remain partial.
**Audience:** Minecraft implementation agent (Luna), reviewers, and future maintainers.
**Date:** 2026-09-21

**Libft integration contract:** [CardGame, Scripting, Inventory and Recipe-Book Integration](../Libft/Docs/card_game_scripting_inventory_crafting_integration_design.md)
defines the engine, inventory, crafting, and World Loom adapter boundaries.
Update its implementation ledger alongside this roadmap as integration work
lands; a design or API foundation does not complete the gameplay loop.

**Libft implementation contract (2026-10-02):**
[CardGame, Scripting, Inventory and Recipe-Book Integration](../Libft/Docs/card_game_scripting_inventory_crafting_integration_design.md)
specifies existing versus proposed APIs, player actions and mandatory decisions,
all current card effects, scripting bindings, transactional inventory/crafting,
placeholder recipes, ownership boundaries, and repository-specific tests.
As those tasks are delivered, update this roadmap and that document together,
recording implementation, integration, and validation separately. The design
document itself does not complete any roadmap feature.

## 1. Goal

Turn world regeneration into an intentional, understandable game system rather
than an administrative revision tool. A player should be able to use a
dedicated in-world block to inspect a map, choose chunks, pay a configurable
world resource such as magical dust, draw and play cards from a regeneration-
only deck, preview the result, and ask the authoritative world service to
regenerate those chunks safely. Keep the broader survival foundations–world
items, inventory, crafting, smelting, gear, food, health, hunger, and fauna–in
the same roadmap, but give them a separate release gate so they do not delay
validation or delivery of the central regeneration loop.

Game 1.0 must also make worlds durable, portable, and configurable. Players
must be able to save and resume multiple worlds, choose whether a character is
owned by one world or can travel between worlds, and configure exactly which
character ownership modes a world accepts.

This roadmap distinguishes two milestones. **Regeneration 1.0** is the first
complete, playable World Loom/card-driven regeneration release. **Game 1.0**
is the broader survival release that builds on it with the full item,
crafting, survival, combat, farming, fauna, customizable player-avatar, and
custom-structure progression package, plus a small Card Table minigame built
on Libft CardGame. This includes villages and villagers, and a late progression
volcano/boss feature. Before either release, build a deliberately tiny
end-to-end playable vertical slice to validate the core interaction and atomic
world update.

The same design must leave room for basic non-hostile animals–sheep, cows,
pigs, and chickens–without allowing regeneration to duplicate, erase, or
teleport existing creatures unexpectedly.

This proposal builds on the existing Minecraft world-revision pipeline and the
Libft compression/analytics work. It must not introduce a second source of
truth for terrain or move expensive generation onto the render thread.

## 2. Existing implementation to extend

The current Minecraft tree already contains useful foundations:

- `World` exposes revision selection, protection, preview, and regeneration
  operations. `World::RevisionRequest` carries a generation configuration,
  mode, stage mask, selected coordinates, and protected coordinates.
- The existing revision preview has protected, selected, transition, and
  unchanged chunk states. The current client exposes a read-only preview with
  `M`.
- Regeneration uses the world-generation pipeline's `REGENERATE` operation.
  Work is prepared off-thread and results are committed through the world
  result-commit path.
- Terrain settings and generation stages are configurable; regeneration can
  use a different configuration from the world's baseline.
- Libft's Game module documents item definitions, inventories, equipment, and
  recipe/crafting data. Minecraft does not yet have a complete integrated
  world-item drop/pickup loop or the requested recipe-book/station gameplay.
- `EntityState` is a serializable motion/state representation, but it is not a
  complete creature simulation or renderer. There is no finished neutral-mob
  behavior/rendering system to extend yet.
- Player movement/collision geometry exists, but it is not a rendered,
  customizable character model. The slice may use a placeholder; Game 1.0 must
  provide visible player avatars and a persistent appearance-customization
  path.

The old Libft `WORLD_REGENERATION.md` remains useful background for revisions,
protection, and transitions. This document specializes that proposal into a
player-facing resource-and-card workflow and defines the missing interfaces,
transactions, and validation.

### Libft branch compatibility note

The checked-out Minecraft submodule is on
`agent/compression-analytics-cardgame-scripting` at `4922eb88`. This is the
CardGame branch to audit and integrate; do not assume APIs from the divergent
`very-real-engine-checkout` branch are available. The checked-out CardGame
module is implemented and documented in
`Libft/Modules/CardGame/README.md`. Its Hearthstone-, Magic-, and
Yu-Gi-Oh!-style samples are simplified demonstrations of the generic engine,
not implementations of those games' complete official rules.

The current Libft foundation provides configurable card types/zones, ordered
decks and hands with stable physical instance IDs, deterministic RNG, effect
callbacks and operation buffers, turn/phase graphs, event ordering, resource
costs and allowances, choices, usage limits, combat/stat modifiers, format
legality and exceptions, deck codes/hashes, snapshots/deltas, state/rules
hashes, command records, replay/result storage, and player-view replay
redaction. Minecraft should integrate this engine through a small adapter;
do not build a second card-game runtime in Minecraft.

The current Libft worktree also adds sequenced/replayable
`CARD_GAME_INTENT_DRAW_CARD` commands (command-log format v5, with v1–v4 read
compatibility and bounded multi-draw count), plus a first `submit_action`
request-ID idempotency layer. It supports a configured discard/burn policy
with deterministic discard reshuffle and typed empty/overflow host intents.
Libft Scripting and its CardGame bridge now expose deterministic FNV-1a
fingerprints for the ordered native binding ABI and registered serialized
script-effect content. Equivalent content fingerprints match and content
changes alter the result; session import enforcement and Minecraft host-schema
and policy inclusion remain open. Minecraft-owned revisioned chunk
selection, start-cost quoting, and 4–7-card opening-deal pricing are specified
in the linked Libft integration design; World Loom APIs belong to Minecraft,
not this Libft work. The older statement that instability, mandatory discard,
the 20-card deck/content adapter, and World Loom card-session foundations were
unimplemented is superseded by the current source checkpoint below: these
foundations are now present in Minecraft, while complete card content,
production multiplayer integration, crash-safe recovery, and end-to-end
validation remain open.

Respect these current API boundaries when planning integration:

- `card_game_deck_code` accepts up to 500 total cards, but the current active
  `card_game_engine` match-state arrays are bounded by
  `FT_CARD_GAME_MAX_CARDS == 128`. A deck string that can encode 500 cards is
  not proof that a 500-card match can be played. The minigame's requested
  25-to-500-card formats require a Libft capacity/snapshot/delta update before
  advertising 500-card active decks; do not silently lower the product
  requirement or overflow the existing state format.
- CardGame callbacks, callback context pointers, and function pointers are
  process-local. Register the same stable effect IDs/callback mapping on each
  server or replay process before use. Persist IDs and versioned data, never
  addresses or raw `void *` context.
- The engine's built-in effect operations cover generic card-game state (for
  example health, mana, events, instance damage/healing, and stat modifiers).
  They do not edit Minecraft chunks. Minecraft maps a resolved card effect to
  a validated immutable generation-policy intent, then uses its existing
  authoritative preview/prepare/commit pipeline.
- `card_game_resolution_stack` is a separate component. If a profile uses it,
  Minecraft must include its state and resolution boundary in the profile's
  persistence, replay, and transaction design rather than assuming it is
  automatically part of the engine snapshot.
- `card_game_format` validates deck legality; it is not a booster-pack,
  collection, rarity, or card-crafting economy. Those remain Minecraft-owned
  content/economy systems.

Any future Libft code changes must follow `Libft/AGENTS.md`; in particular,
APIs use stable IDs rather than serialized function pointers, and fallible
lifecycle operations are explicit. This roadmap update changes documentation
only; it does not modify the CardGame implementation.

## 3. Design rules

1. **Server authority:** the client requests previews, draws, and regeneration;
   it never commits terrain, resource, deck, or creature state itself.
2. **Safety is not a card effect:** protection rules, ownership, world bounds,
   and player-edit preservation cannot be bypassed by a card.
3. **Preparation is asynchronous:** generation, lighting, mesh construction,
   compression, and persistence must not run while holding a world lock or on
   the render thread.
4. **Publish complete state:** a regenerated chunk is not made visible until
   its required block, light, and render data are valid for the same generation
   revision. Never clear old light or mesh data merely to signal that work has
   started.
5. **Deterministic inputs:** the same world seed, chunk coordinate, generator
   version, revision configuration, and played effects produce the same
   result. Random choices use explicit server-owned deterministic streams.
6. **Bounded work and storage:** bound selected chunks, draws, cards in hand,
   message sizes, queue sizes, decompressed output, and per-frame publication.
7. **No silent loss:** if a bounded queue or reservation cannot accept an
   operation, report a recoverable error and leave authoritative state intact.
8. **Analytics are observational:** disabling analytics cannot change the
   generated world, random sequence, scheduling priority, or player outcome.

## 4. Player-facing loop

The interactable block is provisionally named the **World Loom**. The name and
asset can change; its stable gameplay role is a regeneration station.

1. A player interacts with a World Loom they are allowed to use.
2. A map opens, showing known chunks, protected chunks, the station's safe
   area, and candidate chunks. Unknown territory remains fogged unless the
   world's map policy explicitly reveals coarse world information.
3. The player selects chunks. The UI immediately shows the number of paid
   chunks, any extra capacity supplied by cards, total dust cost, and why any
   chunk is protected or unavailable.
4. The server exposes revisioned select/unselect operations and a read-only,
   authoritative start-cost query for the selected set. The player chooses an
   opening deal of four cards by default, with a supported range of four to
   seven; see Section 7.1 for its separate price formula.
5. The player spends magical dust (or the configured resource) to draw a
   chosen number of cards from the regeneration deck. The next draw costs more
   than the previous draw in the same draw action.
6. The player plays cards onto the selected chunk set, chooses valid targets,
   and may use configured draw/discard or scope-modifying effects.
7. The client requests a server preview. The preview includes the resulting
   biome/feature policy and a warning for any terrain, fluid, structure, or
   mob-state consequence the server can determine.
8. On confirmation, the server revalidates the request, reserves its resources,
   prepares per-chunk jobs, and returns a session ID. The UI shows accepted,
   queued, generating, ready-to-commit, committed, skipped, or failed status.
9. Each successful chunk is published atomically. The map and all clients
   receive its new revision. Failed or cancelled chunks keep their prior world
   state and follow the refund rules below.

Player cancellation is allowed only before a regeneration session is accepted
and its generation has started. Once the first generation job begins, the
session is non-cancellable by default and continues through its committed
per-chunk results. A server may still stop work for safety, shutdown,
authorization loss, stale revisions, or an unrecoverable failure; that is a
server failure path, not a player-selected cancellation, and follows the
transaction/refund rules. Cards that explicitly lock cancellation, such as
`Volatile Stabilizer`, lock it as soon as they are played.

The supporting progression loop is: gather or receive world item stacks, put
them in a deliberately sized inventory, use a recipe book at the required
station, smelt ores and cook food in a furnace, craft equipment and physical
cards, then move eligible physical cards into the separate regeneration deck.
This is a recipe-selection interface, not a shaped crafting grid.

The station is an interface and an ownership/permission anchor, not the owner
of terrain data. Opening the UI must not pause the world or block ordinary
movement, rendering, edits, or networking.

### 4.1 Current World Loom API groundwork

The world revision service now exposes a compare-and-set chunk-selection API.
Each draft has a 64-bit selection revision; stale edits are rejected, changes
advance the revision, and selection requires an initialized, unprotected chunk
within the configured cap and a hard safety limit of 256 chunks. Manual
protection changes invalidate stale selection revisions and remove newly
protected chunks from the selection. The authenticated network draft API also
provides `unprotect` to remove a manual protection marker with the same
compare-and-set selection revision and durable request handling.
Its authenticated `preview` query refreshes occupancy and returns a bounded
circle of chunk states and protection reasons for server-authoritative
eligibility display.
`query_revision_start_cost` is read-only and checks the expected selection
revision and versioned world-owned pricing-policy revision, the selected
chunks, the 4–7 card opening range, checked cost arithmetic, and a configured
total-cost cap. Pricing policy updates use compare-and-set; quotes return both
revisions. Opening prices follow `B4, 2*B4, 4*B4, 8*B4`. The current runtime
validator verifies stale policy rejection and a configured six-card quote.
World revision metadata v3 persists the pricing policy and the World Loom
station chunk anchor with a checksum using safe file replacement; v1 and v2
metadata still load. Atomic visibility and the platform safe-replace durability
contract are provided. Selected-chunk regeneration also stages its worker requests and atomically admits the entire set to the generation queue. The World Revision validator verifies over-capacity rejection leaves the queue empty, then verifies normal regeneration succeeds. The validator also confirms that cancellation is rejected once generation becomes active and that the accepted batch completes through normal result draining. Draft revisions remain cancellable. These checks establish queue admission and the active cancellation boundary only; resource debit, chunk reservations, barriers, CardGame startup/deal, and the durable session commit remain open.

World Loom integration has advanced beyond selection and quote groundwork.
The single-player `GameSession` now coordinates the quoted resource debit,
opening-card deal, persisted card/rules state, selected-chunk reservation and
barrier activation. The committed decision window has a persisted five-minute
deadline and deterministic choice/discard recovery. Authenticated status
responses expose per-chunk publication state and aggregate completed jobs,
regenerated/skipped chunk counts, and the current regeneration error code.
Barrier state is visible in
the revision map and the GPU renderer draws a cyan world-space perimeter.
These paths have strict warning-as-error compile coverage, but the current
directly linked application still fails during runtime validation with a
Windows heap/stack corruption exception; treat the affected runtime behavior as
unverified until a consistent supported build can run.

The network server still requires a production identity/access provider and
durable request recovery. Multiplayer occupancy aggregation, commit-time
refresh, real station placement/lifecycle, reach/claim enforcement, crash-safe
generation publication, and live WebSocket transport remain open. Boat movement
now has a reliable versioned client intent/result transport slice. The
authenticated server callback receives the peer public key and must resolve it
to the host actor ID before calling `GameSession::move_boat_for_actor`; clients
cannot submit actor IDs. Successful results carry the committed entity state,
which the server reliably fans out to the other connected peers. Production
identity resolution, steering/UI, and client entity-store wiring remain open.
`GameSession::handle_network_boat_move` now provides the adapter from the
authenticated key to the configured host actor resolver and then to the
revisioned movement transaction; the host must install both that resolver and
the separate actor authorizer, and register the callback with the runtime.
The client does not yet expose authoritative teleport hooks. The
World Revision validator source now exercises swept player collision against
selected chunk boundaries on both horizontal axes, including large steps and
movement from inside a reserved area; strict syntax compilation passes, while
runtime validation remains unverified.

The recipe catalog also enforces the timed-processing boundary: generic craft
actions reject the seven furnace recipes, while the persisted processing API
queues those recipes for 200 ticks per item. A focused runtime harness confirms
that immediate smelting is rejected without consuming input or fuel and that
the processing path accepts the same recipe. The world-item validator source
contains the same regression check, but its full runtime remains unverified
until the direct application heap/stack corruption is resolved.

Source-only backend update (2026-10-26): recipe query rows now carry inventory,
catalog, recipe-book, and station revisions with immutable output metadata;
`GameSession` exposes value-only inventory snapshots and authorized
catalog-wide station queries. Craft requests pass queried inventory, catalog,
and recipe-book revisions through version-3 durable journal records and cache
entries while retaining version-1/version-2 readers. Actor-bound craft records
use version 5 to bind both the equipment content revision and authenticated
actor ID, retaining readers through version 4 and version-3 writes for legacy
actor-free routes. The actor ID is part of exact retry identity. Prepared
station-craft recovery rechecks the saved station identity and revision before
replaying; if the host cannot verify continuity, the request remains pending
and does not consume ingredients. Prepared Card Press research recovery also
checks the saved station block revision before charging materials or unlocking
a recipe; exact research retries preserve the original post-debit inventory
revision in their result. The regeneration host-output consumer now
skips effect callbacks for journal records already committed and resumes
producer acknowledgement; aborted records fail closed. Producer acknowledgement
requires a committed consumer journal record, even when callback wiring is
absent. Prepared-callback retry still requires idempotent consumer callbacks
across the callback/journal crash window. Recipe reads and crafts share the item
transaction mutex, so a query cannot observe a partially applied inventory
transaction. Catalog-wide recipe queries now accept a batch count and return
primary/byproduct metadata and ingredient deficits scaled for it; the existing
API still defaults to one item. These source changes have not been compiled or
tested. Terminal craft-journal retries use cache-only replay and cannot execute
again after the cached result is evicted. `GameSession::query_recipe_snapshot()`
returns one revision-stamped row and deficits transactionally. Aborted pickup,
processing-start, and CardGame transfer journal retries now fail closed rather
than rerunning their mutations. Complete
recipe/drop coverage, station lifecycle, timed-job failure recovery, and
query/craft regression coverage remain open.

The network protocol also accepts an owner-bound `quote` action and sends a
direct `world_revision_quote` response containing the result and, on success,
the authoritative amounts and revisions. Request retries use the retained
quote result. Protocol transport/retry validation is still outstanding; the
validator now captures the response to check its amount/revision fields, exact
retry behavior, and changed-byte rejection. Live WebSocket transport remains
unverified; the quote remains read-only and does not commit regeneration.
Quotes also report host-authorized bonus chunks separately and charge only
paid chunks; the validator covers a mixed paid/bonus selection.
World also accepts a host-fed occupied-chunk set. Each occupied chunk and its
eight neighbors are rejected during selection and quoting; occupancy updates
remove newly unsafe selections and advance the selection revision. The
validator covers selection removal, adjacent rejection, and unchanged-set
revision stability. `WorldRevisionServer` now requires a host occupancy
provider and refreshes the occupied set before begin, selection, protection,
quote, and confirm actions. The provider must aggregate every live player's
authoritative chunk. `GameSession::bind_world_revision_server` wires it into
the server, and the start committer refreshes it again before resource and
reservation commit. The host must run both callbacks on its authoritative
simulation owner; atomicity with concurrent player-location changes remains a
host scheduling contract.
The current single-player `GameSession` now feeds its floored player chunk on
authoritative ticks and skips redundant same-chunk updates. Multiplayer
aggregation is supplied through the host occupancy provider.

Authorized World Revision mutations also receive a direct result message with
the request ID, result code, revision identifier, selection/policy revisions,
and selected count. The request cache retains that state snapshot for exact
retries; the network API also supports owner-bound draft cancellation. Validator
source checks the repeated begin response and cancellation replay.

The Libft inventory keeps its mutable item map private in production builds. Persistence and reset paths use revisioned `clear_items()`. The current Libft worktree's full test executable passes 6,505/6,505 on Linux (localhost-dependent network cases require execution outside the sandbox). This supersedes the older report of unrelated GPU/render symbol failures for the Libft test link. Minecraft `make test` has not reached its validators in this checkpoint: building the `ft_vox` prerequisite failed while recursively producing `ft_vox_analytics` (`[Minecraft][Analytics] output missing`), so production/analytics dependency and validator runs still need to be completed.

### 4.2 Multiplayer regeneration reservation and card window

Starting a regeneration is a world session with an explicit reservation
boundary. After the initiating player selects the chunks, the server performs
an occupancy and safety check before drawing cards or spending regeneration
resources.

The initiating player may not select a chunk that currently contains another
player, nor any of the eight horizontally neighboring chunks around that
occupied chunk. This one-chunk exclusion ring prevents a player from being
trapped by a regenerated border or used to troll a neighboring player. The
rule applies to every player present in the world, including the initiator. A
selection request that intersects an occupied chunk or exclusion ring is
rejected with a clear reason and can be retried after the area is vacated. The
server rechecks occupancy immediately before reservation; client maps and
stale previews cannot bypass it.

Once a valid selection is accepted and cards are drawn, the session enters its
card-decision window. The server places visible, glowing temporary barrier
walls around the affected chunk footprint before generation work begins. These
walls are deliberately visible rather than invisible collision, render as a
clear warning boundary, block movement and ordinary teleport/portal entry, and
cannot be mined, opened, climbed, or bypassed by clients. They are temporary
session state, not saved terrain or player-owned blocks. Players already inside
the proposed footprint must leave before the reservation can be accepted; the
server never strands a player inside a regenerating chunk.

The default card-decision window is **five real-time minutes**, measured by
server time rather than render frames or client timers. During the window, the
initiating player and explicitly authorized session participants may play cards
and modify the current immutable regeneration snapshot. Other players may see
the boundary and session status but cannot alter the card sequence. The UI
shows the remaining time, current locked-in cards, resolved effects,
instability changes, resource costs, and any `NO_EFFECT`, `PARTIAL`, or
conflict results.

When the five-minute window expires, the server locks the current card choices,
snapshot, instability deltas, and resource ledger. Any resources already spent
are not refunded because the player failed to make a decision in time. The
server then begins chunk regeneration under exactly that locked ruleset; late
card commands, deck changes, or client retries are rejected as stale. There is
no ordinary player cancellation after the session is locked. Cards that
explicitly lock cancellation earlier, such as `Volatile Stabilizer`, follow
the same rule immediately when played.

Generation, lighting, mesh preparation, persistence, and publication continue
under the existing asynchronous per-chunk transaction rules. The glowing
barriers remain until all affected chunks have either published their complete
new block/light/mesh state or reached an explicit safe failure state. On
success, barrier removal and player access are one authoritative publication
event; on failure, the old chunk remains protected and drawable until the
server has resolved the failure and the session's refund/retention policy.
Barrier state, session owner, participant permissions, timer deadline, locked
card sequence, occupancy reservation, and resource charges are persisted so a
disconnect or server restart cannot open a regeneration area accidentally.

Required multiplayer tests cover occupied target chunks, occupied neighboring
chunks, players entering during the card window, barrier collision and render
visibility, portal/teleport attempts, timer expiry, late card commands,
resource non-refunds after timeout, disconnect/reconnect, server restart,
partial chunk failure, and atomic barrier removal only after complete chunk
publication.

## 5. Station and protected area

### 5.1 Default safe zone

By default, the World Loom's chunk and the eight horizontally adjacent chunks
form a protected 3-by-3 chunk square. If the station is in chunk `(sx, sz)`,
then every coordinate satisfying:

```text
abs(chunk_x - sx) <= 1 && abs(chunk_z - sz) <= 1
```

is in the station safe zone. The server calculates this from the station's
persisted position; the client cannot submit a smaller radius. Cards, extra
scope, administrator-like card effects, or malformed requests cannot select
these chunks. The UI must show the exclusion clearly and explain it.

The revision manager now accepts a compare-and-set Loom station chunk anchor.
Its chunk and eight neighbors are protected independently from manual
protection, selected chunks in the safe zone are removed, and the station
anchor is persisted in metadata v3. The metadata codec retains v1/v2 readers.
The host must still resolve and verify the real station block/identity and
configure this anchor; reach, ownership, claims, and station lifecycle are not
provided by this coordinate policy alone. `get_revision_protection_info`
reports generation protection, the Loom safe zone, manual protection, and
occupied-player exclusion separately. `RevisionPreviewEntry` carries these
reason flags, and the current GameSession preview overlay colors Loom and
occupied-player exclusions separately from other protection. This is preview
rendering; the production World Loom map UI is still not connected.

The radius is a world rule with a default of one chunk, not a constant buried
in the UI. It may be configurable per world later, but lowering it must be an
explicit world-owner setting and must not silently affect existing stations.

Automatic protection for player-edited chunks, claims, containers, structures,
and other important state remains in force outside this station zone. A
preview must identify these reasons separately. The server performs the final
check at confirmation and again before commit if relevant revisions changed.

### 5.2 Station lifecycle

The station record needs a stable ID, world ID, block coordinate, owner or
access policy, safe-zone policy version, and active regeneration session ID (if
any). Breaking or moving the station during an active job must not invalidate
or orphan the job: either deny removal until it finishes/cancels, or let the
server retain the session and its original policy. The first implementation
should deny removal while a session is active.

## 6. World saves, character scope, and admission rules

World persistence and character admission are foundational Game 1.0 systems,
not optional convenience settings. A world save must be sufficient to resume a
world without regenerating or guessing authoritative state, while a character
profile must clearly declare which world-owned state it is allowed to carry.

### 6.1 World save lifecycle

The world manager must support:

- creating worlds with stable IDs, names, seeds, generator/ruleset versions,
  difficulty, ownership, access policy, and spawn policy;
- listing, renaming, backing up, restoring, exporting, importing, and deleting
  worlds with explicit confirmation for destructive operations;
- autosave, manual save, orderly-shutdown save, and crash recovery without
  blocking the render thread or holding world/chunk locks during slow I/O;
- saving terrain revisions, player edits, entities, inventories, progression,
  stations, claims, regeneration sessions, world settings, admission policy,
  and migration version; and
- atomic snapshot publication with a journal or temporary-file recovery path.

A failed save must leave the last known-good save intact and must not silently
report success. World files are authoritative persistence, not render caches:
meshes, GPU buffers, transient light queues, worker requests, and analytics
records may be discarded and rebuilt. Imported saves must be validated for
size, IDs, revisions, checksums, supported migrations, and content versions
before they become selectable.

### 6.2 Character ownership modes

Every character has a stable character ID and an explicit ownership mode:

| Mode | Meaning | Cross-world use |
| --- | --- | --- |
| `WORLD_BOUND` | Progression and inventory belong to one world ID. | Rejected by other worlds unless explicitly migrated by the owner. |
| `PORTABLE` | Character is owned by the player/account and may join compatible worlds. | Allowed only when the destination policy accepts portable characters. |

The mode is selected at character creation and is not silently changed by
joining a world. Changing `WORLD_BOUND` to `PORTABLE` requires an explicit,
validated transaction and a backup/audit record; the first implementation may
make the mode immutable until migration safety exists.

Portable characters must not smuggle world-owned state between worlds. Store
character-owned state separately from world-owned state. The portable subset
may include appearance, cosmetics, and explicitly marked recipes or abilities;
claims, stations, containers, local discoveries, world currency, pets, and
world-bound items remain with their source world unless a versioned transfer
rule explicitly says otherwise.

### 6.3 World admission policy

Each world stores a versioned admission policy with at least these modes:

1. `WORLD_BOUND_ONLY`: only characters already bound to this world may join.
2. `PORTABLE_ALLOWED`: compatible portable characters may join.
3. `APPROVAL_REQUIRED`: portable characters require an invitation, allowlist,
   or explicit owner/admin approval.
4. `FRESH_CHARACTER_ONLY`: only newly created characters are accepted; existing
   progression is not imported.

The policy must also control solo, invite-only, friends-only, allowlist, and
public access; imported inventory, recipes, abilities, equipment, and
cosmetics; progression and item/value caps; content-version ranges; spawn and
death rules; concurrent sessions; and whether the owner can revoke, eject, or
ban a character ID. WCT1 v6 stores import categories and caps, and its
validator checks host-derived summaries; integration with actual profile
splitting and save commit remains open. WCT1 v7 also persists character approvals.
WCT1 v4 persists owner-managed bans;
live-session ejection and audit events remain open.

The UI must show the effective policy before world creation and before joining:
what the character may bring, what remains in the source world, what is denied,
and whether approval is required. The server rechecks it at login, transfer,
save, and commit boundaries. Client UI is informative only.

### 6.4 Join transaction and conflict handling

Joining is an idempotent server transaction:

```text
authenticate character
    -> load world and policy versions
    -> validate compatibility and admission
    -> calculate portable/world-bound state split
    -> reserve a safe spawn and required capacity
    -> publish one authoritative character session
    -> save resulting world and character revisions
```

If any step fails, neither side is partially changed. Duplicate requests,
disconnects, crashes, and retries must resolve to one result. A per-character
lease prevents a portable character from duplicating items by joining multiple
worlds concurrently. Incompatible policy or content versions produce an
explicit migration-required state instead of silently applying another ruleset.

### 6.5 Required validators and tests

Add deterministic tests for save/reload, autosave, backup/restore,
import/export, corruption detection, interrupted-save recovery, every
admission mode, world-bound rejection, portable joins without duplication,
allowlist/approval, fresh-character-only worlds, incompatible versions,
capacity overflow, duplicate join requests, disconnect recovery, leases, and
preservation of world IDs, character modes, policies, revisions, edits,
entities, inventories, and regeneration sessions.

The admission matrix must also cover subsystem ownership explicitly: physical
World Loom card/deck instances, ordinary inventory/equipment, and recipe-book
grants with character/world scope and source-event identity. Verify that active
regeneration sessions never cross worlds; destination policy filters portable
grants without deleting source-world state; legacy records without scope follow
a versioned migration rule rather than becoming implicitly portable; and
prepare/commit retries, crash recovery, backup/restore, and concurrent joins
cannot duplicate a physical item/card ID or unlock entitlement.

World Loom lifecycle acceptance must test the exact irreversible boundary:
draft cancellation succeeds without spending, but cancellation and selection
edits fail after the committed start even while the card decision window is
open. Restart/retry must preserve the original deadline and committed card,
resource, barrier, and rules state. The current `GameSession` source always
applies a five-minute deadline; the product requirement specifies this window
for multiplayer, so solo behavior must be explicitly selected by policy and
tested rather than inheriting that timer accidentally.

## 7. Map, selection, and preview

The map is a chunk-level planning view, not a second editable world.

### 6.1 Map information

Show only information allowed by the world's exploration policy. The default
should show visited/generated map cells and fog unknown cells rather than
revealing every unvisited biome. For known chunks, the map may show:

- chunk coordinate and approximate surface height;
- known biome or mixed-biome indication;
- protected, selected, transition, unavailable, or unchanged status;
- player edits/claimed areas and the protection reason, without exposing
  private data to unauthorized clients;
- whether the chunk is loaded, saved, currently regenerating, or awaiting a
  revision update;
- the World Loom's protected 3-by-3 region and configured reach/selection
  limits.

The map must not transmit full block palettes just to display a preview.
Coarse previews use bounded summaries. A world-owner setting may later reveal
coarse biome data beyond explored cells, but it must be a deliberate policy.

### 6.2 Preview is advisory, server result is authoritative

The client can draw a local preview from a server-produced summary, but cannot
generate the canonical preview independently and then treat it as truth. A
preview request identifies the current chunk revisions and card/config IDs.
The response includes a preview token tied to those exact inputs. Confirmation
with stale revisions returns a stale-preview response and requires a refresh.

The preview should summarize biome weights, height/style changes, ore and
feature profiles, water/lava policy, transition edges, preserved edits, and
known entities that will be preserved or respawned. Do not promise exact
individual ore positions unless the generation algorithm exposes a safe,
deterministic preview.

## 7. Resource economy and transaction semantics

### 7.1 One configured resource, two explicit cost components

Magical dust is the example resource, not a hard-coded item name. A
world/game configuration identifies the resource ID, display name, icon, and
integer cost values. The UI shows costs separately:

```text
chunk cost  = selected paid chunk count * dust_per_chunk
draw cost   = geometric draw schedule for requested card draws
total cost  = chunk cost + draw cost
```

A card that grants extra chunk capacity changes the number of chunks the
effects can target; it does not secretly alter the paid-chunk count or the
resource ledger. The preview labels paid chunks and bonus chunks separately.

### 7.2 Exponentially increasing draw cost

Each additional card in one draw action has an exponentially increasing
marginal price. Let `C0` be the configured first-card price and `r` the integer
growth factor (default `2`):

```text
marginal_cost(i) = C0 * r^(i - 1), for i in [1, draw_count]
draw_cost(n)     = sum(marginal_cost(i), i = 1..n)
```

For example, with `C0 = 2` dust and `r = 2`, marginal costs are `2, 4, 8, 16`;
four draws cost `30` dust. This is computed with checked integer arithmetic,
never floating point. Configuration sets a maximum draw count and cost cap.
Overflow, an invalid growth factor, or a cost beyond the cap rejects the
request before changing dust or deck state. The player may choose fewer draws.

Cards that later draw additional cards use the same configured schedule and
are charged to the same operation ledger unless a card definition explicitly
declares another cost source. A card may not produce an unbounded free-draw
loop; the server caps total draws per session and resolves effect chains with
a deterministic action/event limit.

### 7.3 Chunk cost and scope

The default price is a fixed integer amount per paid chunk. Cost is calculated
from the validated selection, not a client-provided count. Configuration may
later price special dimensions or regeneration modes differently, but this
must be previewed and versioned. Do not introduce hidden distance or biome
surcharges in the first version.

Card-granted scope is explicit: for example, a `Surveyor's Reach` effect may
allow two additional selected chunks without charging the per-chunk resource.
Such chunks still count against the hard per-session maximum, protection
rules, and generation budget. The server records the card effect responsible
for every bonus slot. Cards cannot make safe-zone or protected chunks eligible.

#### Opening-deal price and session-start API

Expose server-owned draft operations equivalent to
`begin_regeneration_selection`, `select_regeneration_chunk`,
`unselect_regeneration_chunk`, `query_regeneration_start_cost`, and
`commit_regeneration_start`. Selection calls use a selection revision and
request ID, validate each coordinate against the same bounds/protection/
occupancy/reservation policy used at commit, and are idempotent on exact retry.
The cost query is read-only and returns structured counts, currency ID,
component costs, total, revision, and a rejection reason where applicable.
This specifies the server API/data contract only; client UI design and
implementation are not part of this contract.

The opening deal defaults to four cards and accepts four through seven. Let
`B4` be the configured total opening-deal price for four cards:

```text
opening_card_cost(n) = B4 * 2^(n - 4),  n in [4, 7]
```

The totals are `B4`, `2*B4`, `4*B4`, and `8*B4`. This opening-deal price is
distinct from the subsequent per-draw geometric schedule below. Checked
integer math and configured caps are mandatory. Commit revalidates the exact
selection revision and quote, then atomically reserves/debits resources,
starts the CardGame with that opening-hand size, persists the idempotency
result, and freezes selection. Failure changes none of dust, selected chunks,
reservations, barriers, or card zones. Once this commit succeeds, player
cancellation is unavailable under the default regeneration policy.

### 7.4 Reservation and refunds

All authoritative costs use a server-side ledger and idempotent transaction
IDs. No client performs inventory subtraction and then asks the server to
accept it.

- Drawing cards is its own transaction: on success, dust is deducted and the
  exact cards move from the regeneration deck into its regeneration hand. A
  failed draw leaves both resource and deck order unchanged.
- Starting regeneration reserves per-chunk dust for the selected eligible
  chunks. It is charged only for a chunk whose regenerated result is
  successfully committed. Failed, skipped, or not-yet-started chunks release
  their reservation.
- Played cards are held by the session. If no chunk commits, return them to
  the regeneration hand and release all chunk reservations. Once at least one
  chunk commits, the played cards move to the regeneration discard pile; the
  session's preview states which selected chunks each effect covers.
- On disconnect or process restart, the server resumes a persisted job when
  safe, or cancels it and releases all uncommitted reservations. The same
  session/request ID must never charge twice.
- Resource deduction, deck-zone transitions, session creation, and the
  idempotency record are one durable transaction. If persistence cannot
  prepare that transaction, nothing is consumed and no job is launched.

The exact persistence primitive may be implemented in Minecraft, but should
use Libft's safe/atomic file facilities where their documented guarantees fit.
Do not synchronously perform slow disk I/O while holding world/chunk locks.

### 7.5 Dust acquisition and anti-duplication rules

The minimum vertical slice may seed dust through a test fixture. Regeneration
1.0 needs one clear, server-authoritative in-game source/reward and a persistent
ledger, but it does not need to wait for the full inventory economy. For Game
1.0, use this proposed resource loop:

1. The player explores and mines configured, uncommon crystal deposits made
   from the existing crystalline blocks (for example amethyst, amber, frost
   crystal, and shimmer stone) or finds a small amount in a world cache.
2. Mining yields the normal crystal block item. At a Workbench, a deterministic
   `Refine Resonance` recipe converts configured crystal block items into a
   distinct magical-dust item. Exact source blocks, tool tiers, yields, and
   recipes are configuration data.
3. The player deposits dust at the World Loom. The server validates the item
   stack and inventory revision, then atomically removes it and credits the
   Loom/player regeneration ledger. That balance pays only for regeneration
   chunk costs and card draws by default; card crafting uses its own paper,
   ink, and catalyst materials so dust is not charged twice for the same loop.

The tutorial grants enough initial dust for a first successful operation.
Later sources should reward exploration/mining rather than routine per-frame
or repeatable actions. Track depleted deposits as player-authored world state:
regeneration, chunk rollback, retry, or a card effect must not respawn a
previously mined crystal deposit or duplicate its dust yield. Any intentional
deposit regeneration needs its own explicit cooldown, cap, and persistent
node identity. The resource configuration must report expected acquisition
per hour against average draw and chunk costs, and the UI must show the balance,
source, and cost before confirmation. Never let a dust-reward event be replayed
or collected twice.

## 8. Regeneration-only deck and cards

### 8.1 Deck bounds and separation from player inventory

Every player has a base regeneration deck owned by this feature. The active
deck is a separate subsystem, not an inventory container: it cannot be dropped
as a whole or consumed by ordinary gameplay, and it is not the player's Card
Table minigame deck or match state. Both features may use Libft CardGame, but
they must use separate profiles, engine sessions, zones, and persistence
records. Individual crafted card items may be carried in inventory before
being inserted into the regeneration deck, or after being removed from it.
Moving one into a deck transfers that unique instance; it must never exist in
inventory and a deck, or in two deck contexts, at once.

The configured deck contains **at least 20 and at most 100 card instances**.
The initial starter deck contains 20 deliberately low-power cards. Deck edits
are transactional: a card cannot be removed if that would leave fewer than 20
cards, a card definition cannot appear more than **4 times**, and a card cannot
be inserted if the total would exceed 100. The four-copy limit is counted
across the draw pile, operation hand, discard pile, and any temporarily held
instance in the active regeneration session; moving a card between those
zones does not create another copy. Normal draws/plays move an instance
between zones and do not change deck size. The user can craft/obtain cards,
insert them through the World Loom deck screen, and remove them back to
inventory subject to those bounds and available inventory space.

The four-copy limit is keyed by stable card-definition ID, not display name or
localized text. It is checked again when a deck is loaded, when a card is
crafted or transferred into the deck, and when a session starts. A malformed
or legacy deck above the limit is quarantined for repair and cannot be used to
start regeneration; the server must never silently truncate it or allow an
over-cap deck to bypass the rule through draw/discard operations.

Each card instance has a unique ID even when several copies share one card
definition. The deck persists its ordered draw pile, hand, discard pile,
configuration version, and deterministic shuffle state. Progression may
unlock definitions, but unlocking alone does not insert a copy. When the draw
pile is empty, reshuffle the discard pile using the server's deterministic,
persisted shuffle stream. If no drawable cards exist, return a clear no-cards
result; never silently create a card. Hand size, simultaneous sessions, and
cards played per session are separately bounded by configuration.

The regeneration hand has a hard cap of **10 cards**. When an effect draws a
card while the hand is already full, the card is not retained and the draw is
recorded as a **burned card**. Each burned card adds the configured amount of
world instability to the current regeneration snapshot. Burning is
server-authoritative, deterministic, visible in the preview/event log, and
does not silently discard the card without applying its instability result.
This rule applies to ordinary draws, card-generated draws, and craft-only
cards such as `Ultimate Greed`.

### 8.2 Effect representation

Card definitions are configuration-driven and refer to stable IDs. Prefer a
small declarative set of validated regeneration operations in the first
implementation. Example operations include:

- exclude a biome from an allowed set;
- guarantee a biome when the current world rules and selected chunk footprint
  can legally satisfy that guarantee;
- adjust biome weights within configured bounds;
- enable/disable an ore or increase one ore's distribution profile by an exact
  configured amount within world-defined caps;
- guarantee at least one valid village footprint in the generated chunks when
  the village feature is enabled and the selected area has enough space;
- add a bounded water, lava, cave, structure, or surface-feature profile;
- add bounded surface lava pools with the same containment and safety checks as
  every other generated fluid;
- modify feature density within world-defined safe limits;
- draw cards, draw two then discard one, or discard cards through the
  regeneration deck API;
- grant a bounded number of extra chunk targets;
- add a bounded amount of world instability when the draw pile and discard pile
  are both empty and the player requests another card;
- add a constraint or select a supported regeneration stage.

Do not serialize raw function pointers, `void *` contexts, scripts, or process
addresses into card definitions, network messages, save files, or replay data.
If native callbacks are introduced later, use a stable callback ID resolved
from an immutable registry, validate its arguments, and include its ruleset
version in the configuration digest. Arbitrary callbacks must not receive a
mutable `World` pointer from a worker.

### 8.3 Precedence and conflict resolution

Resolve generation policy in this order:

1. Engine invariants and world safety constraints (bounds, valid block IDs,
   station safe zone, claims/protection, fluid containment, resource limits).
2. Current authoritative world state and player-modified content that must be
   preserved.
3. World-owner generation configuration and selected regeneration mode/stage
   mask.
4. Create a fresh immutable working snapshot by deep-copying the current
   authoritative world-generation rules. This snapshot is the only policy
   object cards may adapt for this regeneration session.
5. Apply played card effects in a documented order (the default is the order
   played by the player) to the snapshot's current state.
6. Deterministic generation using the resulting immutable snapshot.

Cards may modify values only inside configured ranges and may not remove
mandatory safety constraints. Conflicting cards either compose by an explicit
operation (`add`, `multiply`, `replace`, `intersect`, or `exclude`) or are
rejected as incompatible during preview. Never rely on container iteration
order to resolve conflicts.

The authoritative world rules are never modified by regeneration. Every new
regeneration request pulls the current base rules again and creates a new
working snapshot; the previous session's adapted snapshot is never reused as
the next session's base. A successful regeneration persists its generated
chunks and the resolved snapshot digest for audit/replay, but does not write
card changes back into the world's permanent generation configuration.

Effects always inspect the snapshot as it exists at the moment they are
played, including changes made by earlier cards. For example, if an earlier
card excludes desert, a later card that guarantees desert is incompatible or
has no legal effect according to the declared conflict policy; it must not
silently consult the original world rules and undo the earlier card. Likewise,
excluding a biome that is already absent, increasing an ore already at its
configured cap, or guaranteeing a feature that is disabled by the current
snapshot produces a deterministic no-effect result.

The preview must show each card's evaluated result as `APPLIED`, `PARTIAL`,
`CONFLICT`, or `NO_EFFECT`, with a human-readable reason and the before/after
summary. A `NO_EFFECT` card is never silently consumed: by default the player
can remove it before confirmation. If the player explicitly confirms a
session containing it, the server may move it to discard according to the
configured card policy, but the resulting snapshot and audit record must make
the no-op visible. Safety violations and invalid card targets remain hard
errors, not no-ops.

The server canonicalizes the resolved policy and computes a digest. The digest
includes generator version, terrain config, mode/stage mask, card definition
versions and ordered instances, and relevant world policy. Persist it with
the revision and chunk metadata.

### 8.4 Starter deck

The current versioned starter multiset is defined by the CardGame integration
design and totals exactly 20 physical instances:

| Card | Copies | Backend effect contract |
|---|---:|---|
| Biome Exclusion | 4 | Choose an eligible biome and remove it without removing the final viable biome. |
| Deep Seam | 4 | Choose an ore and scale its current spawn rate by 3/2 within checked caps. |
| Village Charter | 2 | Add one feasible village plan per use. |
| Surface Magma | 2 | Add a bounded, contained surface lava-pool plan. |
| Double Draft | 2 | Request two draws, then require one physical-card discard when possible. |
| Biome Compass | 4 | Adjust weights among currently eligible biomes without restoring excluded biomes. |
| Surveyor's Reach | 2 | Add one eligible chunk to this session under the same reservation and protection rules. |

This is the implemented content contract for this version, not a final balance
freeze. Definitions, effects, and copies are versioned together; changing
them requires a content-version update and deterministic replay compatibility.
The Minecraft adapter seeds this exact multiset into Libft's native deck and
hand. The backend validator checks all four allowed opening sizes, per-definition
counts, total count, and physical instance uniqueness across hand and draw pile.
It also exercises Double Draft's acknowledged intent, two sequenced draw
attempts, mandatory one-card discard, and exact request retries. It runs in
`--validate-all` and directly as `--validate-regeneration-cards`.

The card-session API also checks selected biomes and ores against the active
generation config before consuming a card. Biomes currently use one-based
config indices and ores use enabled ore block IDs. The biome mapping is local
to the config because Voxel has no stable biome ID field; it is included in the
canonical regeneration manifest hash over the complete serialized generation configuration (with Voxel signature bits binding callback presence),
CardGame content version, every regeneration policy limit, and per-biome Compass
adjustments. Initialization and snapshot restore reject mismatched manifest inputs. Revision-checked transfers now move the seven starter cards and the currently registered rare cards into and out of the draw pile, preserving their 64-bit item IDs through a session-persisted mapping to CardGame instance IDs. Inventory return uses an atomic revision-checked add that reports the committed slot. Validator coverage includes exact retries, stale revisions, four-copy rejection, unsupported remaining rare cards, full-inventory rollback, and identity-preserving return. Mapping and request results persist in GameSession v8, but transfers are not coordinated with the shared transaction journal; several rare-card definitions and effects still need registration.

`GameSession` exposes checkpointed card play, draw and mandatory-discard
entry points. Each action and its bounded retry result are saved with the rules
overlay and journal; failed session persistence restores the prior adapter
snapshot. Card-session and rules access through the session is read-only.
Card inventory import/export now use `WorldItemCardTransferTransaction`: a
versioned request record binds the operation, slot/card ID and expected
inventory revision; the prepared record and pre-action session are checkpointed
before mutation, then inventory, CardGame replay state and terminal journal
state are saved together. Startup recovery reapplies prepared card transfers,
and exact retries use the adapter's persisted transfer cache. The world-item
validator now covers recovery from a reloaded pre-action snapshot and exact
retry; strict MinGW syntax compilation passes for the transaction, session,
and validator. Runtime execution remains blocked by the Make runner's Git
Bash `NtCreateDirectoryObject` access-denied failure. The validator now injects
terminal-save failure and checks inventory/card rollback, the PREPARED journal
record, and successful retry; the focused executable has not been run. Session
checkpoints now use the safe replacement path: they flush file contents, sync
the containing directory on POSIX, and request write-through replacement on
Windows. Transfers
remain disconnected from World Loom network actions, and host-intent delivery
and regeneration-start commits remain open.

Card play now submits through a replayable host-effect command. The command
binds the physical hand instance, effect event, discard destination, request
identity, and fixed-size typed input payload; the engine applies the event and
discard together and restores the prior snapshot on failure. Command-log v13,
full replay v11, and RCS1 v8 preserve the new play inputs with older readers
retained. This does not close shared-journal coordination for host-intent
delivery or regeneration-start commits.

`RegenerationWorldRules` accumulates accepted card intents in a capped,
session-local overlay. It composes ore scaling in play order, keeps excluded
biomes excluded when Compass adjusts configured weights, and records bounded
village, magma-pool, and target-scope plans. A rules-aware
`World::begin_world_revision` path materializes checked ore scaling into the
manager-owned generation config and deterministically remaps excluded biome
selections within each baseline biome zone. The revision validator checks the
committed ore rate and excluded-biome result grid. The rules-aware path requires
full generation stages per selected chunk. When Compass adjustments are active,
it samples `max(1, 1 + adjustment)` weights among eligible biomes, with a stable
zone hash; excluded biomes cannot be restored. While a draft is pending,
consumed card intents refresh its manager-owned generation config. Surveyor's
Reach selects its typed target through the bonus-chunk path, subject to the
configured selection cap; a failed session checkpoint rolls back that new
selection and the rules snapshot. The validator checks a Deep Seam applied
after draft creation changes the pending config and confirms a bonus chunk is
excluded from chunk pricing. The authorized World Loom server action still
does not provide session rules. Surveyor's Reach target scope is consumed by
revision selection. Magma plans are frozen into regeneration requests and
produce chunk-local contained lava pools with persisted feature-kind outcomes.
The pool planner finds sites only after terrain generation, has no footprint
reservation or pre-consumption feasibility check, and can fail a chunk when no
clear site exists. Village Charter plans now generate two log-and-leaf cottages
with a coarse-dirt path in a bounded 9-by-9 block area contained by one chunk;
the worker assigns each plan to a distinct selected chunk and publishes each
structure with its chunk. Village IDs, structure continuation
metadata, storage, NPC spawn points, multi-chunk reservations, and card-play
terrain-site feasibility checks remain open. GameSession rejects either
feature card before consuming it when the revision has no selected chunks.
Village plans go to distinct selected chunks; start fails before queueing if
the selected chunk count cannot host every village plan.

The overlay also has a bounded, checksummed version-3 byte codec that includes
the server-supplied generation/content manifest hash. Restore rejects a
mismatched hash, then validates and replays typed intents against the supplied
base config and policy; the reader remains compatible with version 2. CardGame state and the rules overlay are saved together
in the current GameSession snapshot, so intent acknowledgement and rule
changes share a session checkpoint. This remains separate from the atomic
World Loom start transaction and chunk reservation commit. Overlay and revision
metadata writes now flush file contents and use write-through replacement on
Windows, with parent-directory sync on POSIX.

Volatile Stabilizer has a configurable, nondecreasing overload-risk curve and
explicit success-reduction and overload-surge values; no example probability or
instability amount is a default. The effect rolls through the persisted server
CardGame RNG and resolves immediately. Its use ordinal and hidden result are
stored in the version-3 rules overlay, while client-facing play results omit the
roll. Focused `--validate-regeneration-cards` and `--validate-item-catalog`
passes cover configured policy, outcome persistence, item mapping, and invalid
policy rejection. The GameSession validator also covers both outcomes, exact
retry, and snapshot restore with a committed selected chunk;
`--validate-world-revision` passes this path. Committed chunk reservations are
rehydrated before streaming loads them, subject to policy-cap and protection
checks. Danger/reward opportunity planning and crash-atomic host-intent delivery
remain open, as do the other rare card planners.

### 8.4.1 Illustrative starter deck example

The player starts with a **20-card regeneration deck**. These are illustrative
basic definitions and balance content, not fixed implementation data:

| Card | Example effect | Purpose |
|---|---|---|
| Biome Exclusion (4 copies) | Removes one selected biome from the snapshot's eligible biome set | Demonstrates safe biome filtering |
| Deep Seam (4 copies) | Increases one selected ore profile by exactly **50%** relative to the current snapshot, subject to world caps | Adds a clearly bounded amount of one ore without bypassing distribution limits |
| Village Charter (2 copies) | Guarantees one additional valid village footprint in the generated chunk set | Multiple copies compose additively: two copies request two villages, subject to space and safety limits |
| Surface Magma (2 copies) | Enables a bounded surface-lava-pool profile | Adds a risky feature with containment and safety validation |
| Double Draft (2 copies) | Draws two cards, then forces one discard from the resulting hand | Demonstrates card draw/discard sequencing |
| Biome Compass (4 copies) | Adds a small configured set of eligible biome weights | Teaches composable biome selection without an unsafe replacement |
| Surveyor's Reach (2 copies) | Grants one additional target chunk for this session | Demonstrates bonus scope and explicit accounting |

These example counts total the required 20-card starter deck. Card definitions
are configuration content and can change without changing the deck-size
invariant.

Starter effects should be weak, transparent, and deterministic. In particular,
each `Village Charter` instance represents one village guarantee. Multiple
instances therefore request multiple villages rather than strengthening one
village. Each instance may fail with `NO_EFFECT` when no additional valid
footprint remains because the selected chunks are too small, protected,
already occupied, or the village feature is disabled;
`Surface Magma` may only create small enclosed pools in legal terrain; and
`Double Draft` must respect hand, draw, discard, and per-session limits.
Progression can later add cards such as `Biome Guarantee` (force one eligible
biome when the snapshot can satisfy it) and `Fortune's Draw` (draw one card)
after the preview, snapshot, and rollback systems are reliable. Avoid cards
that erase edits, override protection, create unbounded structures, or
guarantee rare loot.

### 8.5 Craft-only cards

Craft-only cards are discovered and crafted through the Card Press or another
explicit progression path. They are not inserted into the regular 20-to-100
regeneration deck by default and must not appear in the normal starter draw
pool. Keep every craft-only card in this section, including future rare cards,
so deck legality and progression availability remain easy to audit.

| Card | Availability | Effect |
|---|---|---|
| Volcano Mark | Craft-only rare card | Adds one bounded, regeneration-only volcano feature plan to the selected chunks, subject to the required footprint, protection, terrain, progression, and boss-content rules. |
| Ultimate Greed | Craft-only rare card | Draws three cards; cards beyond the 10-card hand cap are burned and add instability, then the card adds its configured instability amount to the regenerated chunks. |
| Biome Rupture | Craft-only rare card | Removes two selected eligible biomes from the current regeneration snapshot and adds its configured instability amount to the regenerated chunks. |
| Ore Overgrowth | Craft-only rare card | Doubles the selected ore's current spawn profile in the regeneration snapshot, up to world-defined safety caps, and adds its configured instability amount to the regenerated chunks. |
| Shipwreck Chart | Craft-only rare card | Guarantees one additional shipwreck in a valid generated ocean area, adds bounded underwater mobs, increases instability, and scales loot quality with the affected chunks' resulting instability. |
| Ruined Village Relic | Craft-only rare card | Adds one bounded ruined-village structure with hostile spawners and rare loot, then adds its configured instability amount to the regenerated chunks. Loot quality scales with the affected chunks' resulting instability. |
| Unstable Catalyst | Craft-only rare card | Applies one hidden random modifier from the validated undiscovered-effect pool to the selected chunks, permanently records it, and increases world instability. |
| Desert Pyramid Seal | Craft-only rare card | Adds one regeneration-only desert pyramid built from sandstone, with bounded hostile spawners and a central loot chamber; increases instability and scales danger and rewards with the affected chunks' resulting instability. |
| Ancient Canopy Seal | Craft-only rare card | Adds one regeneration-only giant forest tree with a climbable interior, ladders, trapdoors, darkness-based hostile spawners, and rare loot; increases instability and scales danger and rewards with the affected chunks' resulting instability. |
| Spider Nest Seal | Craft-only rare card | Adds one regeneration-only spider nest connected to an existing cave system, with bounded spider spawners and loot-bearing corpses; increases instability and scales danger and rewards with the affected chunks' resulting instability. |
| Volatile Stabilizer | Craft-only rare card | Normally reduces instability slightly, but its overload chance escalates every time it is used in the same regeneration session. An overload creates a much larger instability surge and unlocks a higher danger/reward opportunity. |

`Volcano Mark` is consumed by the regeneration session that accepts it. It may
be stored as a crafted card before use, but it is never generated by ordinary
starter-deck draws. The preview must show its required footprint, affected
chunks, blocked/protected areas, expected terrain and hazard changes, and any
reason it has `NO_EFFECT` or is rejected. A volcano must never appear in
ordinary world generation merely because the card exists in a player's
collection.

`Ultimate Greed` is also consumed by the accepting regeneration session. Its
three draws use the same server-authoritative 10-card hand cap and empty-deck
rules as ordinary draws. Any draw over the cap burns that card and adds the
configured burned-card instability; if a draw occurs after both piles are
empty, the corresponding empty-deck instability is added as well. These
instability contributions are in addition to the card's configured instability
cost and apply only to the chunks regenerated by that session. They do not
change the base world rules or permanently destabilize unrelated chunks. The
preview must show the three-card draw, retained versus burned cards, each
instability contribution, the resulting danger threshold, and any cards that
would be `NO_EFFECT` in the resulting snapshot.

`Biome Rupture` consumes two biome-exclusion selections and applies both to the
snapshot state at the moment it is played. If fewer than two selected biomes
are currently eligible, each unavailable exclusion is reported separately as
`NO_EFFECT`; the card never removes mandatory or already-absent biomes by
silently changing another biome. Its configured instability is added even
when one exclusion is ineffective only if the player explicitly confirms the
resulting partial effect; a fully invalid card is rejected or left unplayed
according to the normal card preview policy. The preview must show both biome
targets, their individual results, the exact instability increase, and the
resulting danger threshold.

`Ore Overgrowth` applies a `2.0x` multiplier to one selected ore profile as it
exists in the current snapshot, not to the original world configuration. If
the world cap prevents a full doubling, the preview reports the applied capped
value as `PARTIAL` and shows the exact result. If the ore is disabled, absent,
or otherwise unavailable in the current snapshot, the card reports `NO_EFFECT`
and does not silently substitute another ore. Its configured instability is
shown and applied under the same confirmation and audit rules as the other
craft-only rare cards.

`Shipwreck Chart` is ocean-only. It consumes one shipwreck guarantee and can
only apply when the current snapshot contains enough valid ocean area and the
shipwreck structure footprint fits without crossing protected or incompatible
terrain. Multiple accepted copies request multiple shipwrecks, subject to the
world's structure and loot budgets. On land, in a non-ocean biome, or when no
valid ocean footprint remains, the card reports `NO_EFFECT` or `PARTIAL` with
the exact reason; it never converts land into ocean merely to satisfy the
card. Shipwrecks use a stable cross-chunk structure plan and are tagged
`OCEAN_ONLY` in the feature registry. A shipwreck may include a bounded,
configured population of hostile underwater mobs. Those mobs must obey water
volume, entity, difficulty, spawn, and protection budgets; the card must not
create an unbounded underwater spawn source.

Shipwreck loot is resolved from the resulting persisted instability level of
each affected chunk after the card's instability delta is applied. Higher
instability may unlock better loot tiers and a stronger bounded chance for rare
items, using a versioned monotonic table with a hard maximum. Loot and
underwater-mob placement use deterministic server seeds and are committed once
with the structure/chunk revision, so previews, retries, reloads, and
reconnects cannot reroll or duplicate them. The preview must show the
instability increase, underwater-mob count/danger profile, loot tier/range,
water-depth requirements, and any protected or invalid footprint areas.

`Ruined Village Relic` is a deliberate risk/reward card. Its structure plan
must contain a bounded number of hostile spawners, a configured rare-loot
profile, valid collision/navigation space, and a stable structure instance ID.
The spawners are part of the generated structure and must obey the world's
entity, difficulty, protection, and hostile-mob budgets; the card must not
create an unbounded spawn source. Player-built or protected content remains
protected, and an invalid footprint produces `NO_EFFECT` or `PARTIAL` rather
than moving the village into protected land.

The loot profile is resolved from the resulting persisted instability level of
each affected chunk, after the card's instability delta is applied. Higher
instability may unlock better loot tiers and a stronger bounded chance for rare
items, but it must use a versioned monotonic table with a hard maximum. Loot
must be generated from a deterministic server seed and committed once with
the structure/chunk revision; retries, reloads, and repeated preview requests
must not reroll or duplicate it. The preview must show the instability tier,
spawner count, danger profile, loot tier/range, and any protected or invalid
parts of the footprint.

`Desert Pyramid Seal` is a regeneration-only desert feature. The generator
must require a valid desert footprint and the sandstone block palette before
accepting the card; it must not place a pyramid in a non-desert biome or add
desert terrain merely to satisfy the card. The pyramid contains a bounded
number of hostile spawners and a protected central loot chamber. Its structure
plan is cross-chunk safe, uses a stable instance ID, and preserves player or
protected content.

The pyramid's resulting chunk instability controls both its danger profile and
its reward tier through versioned monotonic tables: higher instability can add
more dangerous spawner settings and unlock better central-chamber loot, subject
to hard entity, difficulty, item-value, and structure limits. Loot and spawner
placement use deterministic server seeds and commit once with the generated
structure revision. The preview shows the instability increase, spawner
profile, expected danger, loot tier/range, desert-footprint requirements, and
any `NO_EFFECT` or `PARTIAL` result.

`Ancient Canopy Seal` is a regeneration-only forest feature. It requires a
valid forest footprint and places one above-ground giant tree with a stable
cross-chunk structure plan. The interior must contain a bounded climbable
route using ladder blocks, navigable openings using trapdoors where configured,
and a central or upper loot area. The structure must not appear in a non-forest
biome or convert unrelated terrain merely to satisfy the card.

The tree's spawners are deliberately placed in enclosed, low-light sections
of the interior. The darkness is part of the encounter design: players must
bring or create illumination to see and safely navigate the structure. Spawner
activation, mob counts, light thresholds, ladder/trapdoor collision, and
vertical traversal are all server-authoritative and bounded. The structure
must never trap a player irrecoverably, block protected content, or create an
unbounded hostile spawn source.

As with the desert pyramid, the affected chunks' resulting instability selects
monotonic, versioned danger and reward tiers. Higher instability may increase
darkness-area danger, spawner settings, and rare-loot quality within hard
entity, difficulty, item-value, and structure limits. The preview shows the
forest-footprint check, vertical bounds, ladder/trapdoor route, light-risk
areas, spawner profile, loot tier/range, instability increase, and any
`NO_EFFECT` or `PARTIAL` result.

`Spider Nest Seal` is an underground regeneration-only feature. The generator
must first locate a valid existing cave-system connection in the resolved
chunk snapshot. It must not create a disconnected underground room or tunnel
solely to satisfy the card. If no cave connection with sufficient footprint,
headroom, support, and protected-content clearance exists, the card reports
`NO_EFFECT` before consumption.

The nest contains a bounded number of spider spawners and loot-bearing corpse
props or entities. Spawners must connect to the cave volume, obey mob,
difficulty, entity, light, and per-structure budgets, and must never create an
unbounded hostile spawn source. Corpses have stable IDs and deterministic loot
tables; they are lootable once, persist their opened state, and cannot be
duplicated by retries, chunk reloads, regeneration, or reconnects.

The affected chunks' resulting instability selects monotonic, versioned spider
danger and corpse-loot tiers within hard caps. Higher instability may increase
spawner danger and improve loot quality, but it cannot bypass ownership,
protection, item-value, or entity limits. The preview shows the cave connection,
underground footprint, spawner count, expected danger, corpse-loot tier/range,
instability increase, and any invalid or protected area.

`Volatile Stabilizer` is a deliberate kiss/curse gamble. Each successful use
normally reduces the affected chunks' instability by a configured amount. Each
additional use in the same regeneration session increases its configured
overload chance; the exact chance curve and reduction/surge values are content
configuration, not hard-coded card behavior. The server resolves the overload
immediately when the card is played using the session's deterministic RNG and
records the result before presenting the updated preview.

When it overloads, the normal reduction is replaced or followed by a much
larger configured instability surge. The resolved surge is permanently attached
to the affected chunk revisions and unlocks the resulting generic danger and
reward band, such as a high-tier hostile-tower opportunity. The player sees
that the card overloaded, the resolved instability change, and the resulting
danger/reward band, but cannot use cancellation, a retry, reload, rollback, or
another card to remove the committed overload.

Playing the card consumes its instance and commits its resolved instability
event to the session ledger. It immediately locks player cancellation for that
regeneration session, even if terrain generation has not started yet. If the
player continues, the regenerated chunks must use the committed instability
result and its eligible kiss/curse feature opportunity; the result cannot be
rerolled by abandoning and restarting the session.

`Unstable Catalyst` is an intentional hidden-risk card. At authoritative card
resolution, the backend selects one eligible modifier from a versioned,
bounded pool of content the player has not yet unlocked or owns. The selected
modifier ID, deterministic seed, ruleset version, affected chunks, and
instability delta are recorded server-side, but the specific effect is not
shown to the player in the normal preview. The UI must still disclose that an
unknown permanent modifier will be applied, its affected footprint, the exact
instability increase, and any general danger range required for informed
consent. The server may reveal the effect later through a discovery, event, or
content-specific in-world outcome.

The hidden modifier is immutable: no later card, regeneration, rollback,
death, save restore, migration, or ordinary world-rule edit may remove or
reroll it. It may be superseded only by a separately designed explicit
end-of-world or administrative migration, never by normal gameplay. The
modifier applies to the selected chunk records and persists with them; it does
not silently spread to unrelated chunks. If a selected chunk cannot accept a
modifier, that chunk is rejected before the card is consumed.

The eligible pool is deliberately limited to dangerous or potentially
dangerous modifiers. Its initial generic candidates are:

- ruined monster towers;
- hostile camps;
- spawner complexes;
- cursed mines;
- enemy fortifications;
- monster nests; and
- dangerous underground vaults.

The pool may also contain explicitly registered `CROSSOVER_VARIANT` entries,
such as an underwater ruined tower or another ocean-compatible hostile
structure. A crossover entry may use concepts from a card feature–such as a
shipwreck, volcano, or ruined village–but it is an instability-driven variant,
not a hidden replay of that card's exact guarantee. Later candidates must be
explicitly classified as `GENERIC_INSTABILITY` or `CROSSOVER_VARIANT` and
tagged `DANGEROUS` or `POTENTIALLY_DANGEROUS` by the versioned feature registry.

The pool must not silently include ordinary biome, ore, harmless structure, or
cosmetic effects. Every candidate must pass the same protection, ownership,
footprint, entity, fluid, difficulty, and generation-budget rules as an
ordinary card. It must never grant arbitrary items, bypass character/world
permissions, overwrite player content, or create unbounded entities. Selection
is server-authoritative and deterministic for the committed event, while the
effect identity remains hidden from ordinary player-facing output. Retries,
previews, reconnects, and reloads must not reroll or duplicate the modifier.

### 8.6 Empty-deck draws and world instability

Drawing beyond an empty draw pile is not a free reshuffle. If both the draw
pile and discard pile are empty, an accepted extra-draw request adds a bounded
amount of **world instability** to the current regeneration session. The first
implementation should make this an explicit, versioned snapshot value rather
than silently changing the permanent world rules. The world owner can later
configure whether instability is session-local, world-persistent, or disabled;
that choice must be explicit before implementation.

Instability is cumulative for the current snapshot and increases the danger of
the chunks being regenerated. It may influence only validated, configured
features, for example:

- higher hostile-mob spawn density and stronger hostile-mob equipment tiers;
- additional hostile spawn points or bounded mob spawners;
- more uneven terrain and harder navigation than the base generation profile;
- harsher but bounded cave, hazard, or surface-feature distributions; and
- stronger combinations of existing hazards without bypassing protection,
  fluid containment, chunk bounds, or entity-count limits.

Keep instability content separate from explicit biome/reward cards. The
content registry should classify generated features as:

- `BIOME_SPECIFIC`: requested by a biome card and valid only for its biome or
  terrain family, such as desert pyramids, forest canopies, shipwrecks, or
  cave-connected spider nests;
- `GENERIC_INSTABILITY`: selected by per-chunk instability thresholds and
  eligible across many compatible biomes, such as ruined monster towers,
  hostile camps, spawner complexes, cursed mines, enemy fortifications,
  monster nests, and dangerous underground vaults; or
- `CROSSOVER_VARIANT`: a generic danger concept adapted to a compatible biome,
  such as an underwater ruined tower or shallow-ocean hostile settlement
  rather than a normal above-ground village.

Biome cards control deliberate biome-specific content and rewards. Instability
controls the increasing presence and difficulty of generic hostile content and
dangerous traversal. There may be crossover, but a generic instability event
must not silently duplicate a card's guaranteed structure or bypass a card's
biome requirements. The registry records the relationship explicitly so an
instability feature can either use a compatible variant or be ineligible.

The first generic instability feature should be a **ruined monster tower**.
Once a chunk crosses its configured eligibility threshold, regeneration gives
the tower a low, deterministic chance to appear if there is enough space,
terrain support, protection clearance, and a valid chunk footprint. Its
contents scale with the affected chunk's resulting instability:

- low instability: ordinary hostile mobs and modest loot;
- medium instability: multiple floors, stronger mobs, useful materials, and
  bounded spawner content; and
- high instability: elite mobs, spawners, rare crafting materials, and powerful
  card ingredients.

The tower is generic and may appear in many land biomes, but not every biome.
Ocean chunks require a compatible `CROSSOVER_VARIANT` or are excluded. The
primary ocean tower variant should be built taller so its occupied floors,
spawners, and loot remain above the waterline, with a valid water approach so
players can reach it using boats. Its footprint must include sufficient water
approach clearance. A separate shallow-water or
underwater ruined-tower variant may use aquatic navigation, water-compatible
blocks, underwater mobs, and submerged loot, but it must be explicitly
configured rather than assumed. The same compatibility approach applies to
other generic instability features without turning every biome into a special
case.

This creates the intended risk/reward loop: stable worlds are safer but offer
fewer dangerous opportunities, while player-driven instability makes travel
and regeneration harder but unlocks better opportunities. The world becomes
more dangerous because of deliberate player choices–powerful cards,
overdrawing, empty-deck draws, and other accepted risk events–not because
generation randomly ignores the configured rules.

#### Instability kiss/curse rule

Every instability increase must follow a **kiss/curse** contract. The curse is
the additional danger imposed by the instability level; the kiss is a real,
server-authoritative opportunity or reward unlocked by accepting that danger.
An instability feature must not be purely punitive. Examples include stronger
hostile mobs paired with better loot, more spawners paired with rare crafting
materials, harder terrain paired with undiscovered feature access, or a higher
hazard tier paired with better card ingredients.

The relationship is defined by versioned monotonic tables: increasing a
chunk's instability may never reduce its configured reward tier, and increasing
the reward tier may never silently remove the corresponding danger. Both sides
have hard caps for entities, terrain, item value, generation time, and player
progression. The preview normally shows the danger and reward bands before
confirmation. Hidden-risk cards may hide the exact selected feature, but must
still disclose that the permanent modifier carries both a danger cost and a
reward opportunity.

If a valid instability level has no configured reward opportunity, the
instability increase is rejected rather than creating a punishment-only state.
Likewise, rewards must never be granted without the associated danger being
committed to the same chunk revision. Loot, discoveries, card ingredients, and
feature access are deterministic, persisted, and cannot be rerolled or
duplicated by retries, reloads, or rollback.

Instability must not directly create arbitrary entities, weapons, or terrain.
Each threshold maps to a versioned danger profile with explicit maximums,
preview text, deterministic seeds, and a per-session/entity budget. The UI
must show the current instability, the next threshold, and the expected danger
changes before confirmation. If no configured instability profile exists, the
extra draw is rejected rather than silently producing an undefined result.

Instability effects apply to the working snapshot after the draw request and
before generation. They therefore compose with the cards currently played,
but they never modify the authoritative base rules. The resolved instability
level, threshold profile, spawned hazards, and card/draw events are persisted
with the regeneration audit record so retries cannot reroll or duplicate the
danger. Add dedicated later design work for whether instability survives a
successful regeneration, decays over time, can be reduced by gameplay, and
how multiplayer worlds share its ownership and visibility.

#### Per-chunk instability state

Instability is tracked independently for every chunk. It is not one global
world number and it must not be inferred again from the current terrain. The
authoritative record for each chunk contains at least:

- world ID and chunk coordinates;
- current integer instability level, clamped to the world's configured maximum;
- instability ruleset/profile version used to interpret the level;
- world-generation revision and resolved regeneration snapshot digest that
  last changed the level;
- monotonically increasing instability revision/event ID for stale-result and
  duplicate-commit protection; and
- bounded audit information identifying the source event, such as a card,
  burned draw, empty-deck draw, challenge-start setting, decay, or gameplay
  reduction.

When a chunk is generated for the first time, its instability starts at `0` by
default. A chunk regeneration begins from that chunk's persisted current level
and applies only the instability delta accepted for that regeneration. A
multi-chunk card effect applies the same validated delta independently to each
affected chunk; a failed, skipped, protected, or stale chunk receives no
change. Chunks outside the regenerated footprint are never changed as a side
effect.

World creation may expose an optional **starting instability** challenge
setting. The default is `0`. If the player explicitly selects a higher value,
every newly generated chunk in that world starts at that validated level
instead of zero. The setting is stored in the immutable world-creation
configuration, shown clearly before world creation, bounded by the maximum
instability level, and cannot be changed retroactively to rewrite existing
chunk records. A later ruleset may offer a deterministic per-region challenge
profile, but it must resolve to an explicit starting level for each chunk.

#### Persistence and update contract

Per-chunk instability must be saved and restored with authoritative world
state:

1. Capture the current chunk instability record with the chunk's voxel/world
   revision when a chunk is saved or unloaded.
2. Commit terrain, entity, and instability changes through one idempotent
   regeneration transaction, or leave all three at their previous revisions.
3. Write records atomically and recover them from the last known-good save
   after interruption or corruption.
4. Load the record before a chunk becomes joinable or drawable; missing records
   are initialized from the world starting-instability setting, never from
   arbitrary stale worker memory.
5. Include instability revision, profile version, and snapshot digest in
   worker requests/results so stale generation cannot overwrite newer levels.
6. Replicate the level and relevant danger-profile summary to authorized
   clients, while keeping internal audit details server-owned.
7. Include the records in backup, restore, export, import, migration, and
   deterministic test fixtures. Compression is allowed, but must not merge
   neighboring chunks into an ambiguous shared value.

The renderer may cache the level for display, but it is not authoritative.
Lighting, meshing, unloading, regeneration retries, and analytics must never
reset a chunk's instability. A save/load cycle must reproduce the same
per-chunk levels and danger-profile decisions for the same ruleset and event
history.

Required tests cover default-zero generation, nonzero challenge starts,
independent neighboring chunk levels, repeated regeneration, partial
multi-chunk failure, stale worker rejection, save/unload/reload, backup/
restore, import/export, migration, duplicate event IDs, and multiplayer
replication. Tests must prove that changing one chunk's instability cannot
change an unrelated chunk and that a regenerated chunk's persisted level is
used as the starting point for its next regeneration.

### 8.7 Card recipe discovery and Card Press crafting

Use a hybrid progression: exploration and controlled experimentation discover
card recipes, while crafting a recipe the player already knows is deterministic.
Do not make ordinary card crafting a blind random roll. Players should be able
to pursue a useful effect family, understand the cost tier, and reliably
reproduce a card after learning its recipe.

**Exploration discoveries:** ruins, unusual terrain, progression milestones,
and other configured world rewards may provide a blueprint, research note, or
recipe fragment. A complete blueprint unlocks an exact Card Press recipe.
Fragments can reveal a family or ingredient clue, but must not silently craft
or duplicate a card. Discovery is progression/account data; the actual card is
still a unique item instance that must be crafted and transferred into the
World Loom deck.

**Known recipes:** the recipe book displays the exact card output, ingredients,
Card Press requirement, cost tier, and any unlock requirement. Crafting a known
recipe always produces that card definition–never a random substitute. Inputs,
output capacity, discovery state, and station revision are checked and
committed as one idempotent server transaction. If crafting fails, neither
materials nor a card are consumed/created.

**Controlled experimentation:** the Card Press may offer a separate Research
Synthesis action for an unknown recipe. This is not a 2D crafting grid: the
player selects an intended effect family and a small set of named ingredient
roles/categories through the recipe-book UI (for example, substrate,
biome/ore/fluid catalyst, and stabilizer). The selected materials constrain
the candidate pool and affect its weights: ore-tagged input biases toward ore
effects, a biome sample toward that biome family, and a fluid catalyst toward
water/lava features. A substrate/catalyst tier sets the eligible power tier.
The UI previews the cost, tier, and possible result families before the player
confirms; it must not imply an exact result when the experiment is random.

On a successful experiment, the server chooses one valid card from that
configured, bounded candidate pool, creates the card instance, and records the
discovered exact recipe. The player can then craft that result deterministically
from the revealed recipe. Candidate results must all be useful and legal for
the paid tier–no blank outcomes, safety-rule bypasses, or low-cost chance at a
high-tier card. A discovery-biased pool should favor an eligible card the
player has not discovered yet; repeated experiments must not become a
reload/reroll exploit. If all eligible cards are known, the configuration may
allow known outcomes for additional copies, or direct the player to a different
research tier.

Random selection is server-authoritative. The accepted experiment has a unique
request ID, and the chosen result, consumed inputs, discovery unlock, and
created card are committed together. Draw from a server-owned persisted random
stream and store the selected result in the experiment event. Persist enough
event/result data that a retry, save recovery, replay, or reconnect returns
the same outcome. Never use a client-provided seed or permit a reload to reroll
after materials were committed. Show the candidate families and probability/
weight information needed for informed consent; do not hide a material-cost
gamble behind a normal known-recipe button.

**Cost and power:** more expensive recipe tiers may produce more impactful
cards, but power is explicit and bounded. Each card definition has a validated
effect budget/complexity tier (such as target scope, biome influence, ore
profile strength, or number of compatible operations). Each recipe tier has a
minimum material cost and a maximum allowed effect budget. Every
possible randomized result must fit the tier paid for; randomness selects the
card identity within the tier, not a lucky over-budget power roll. Costs and
budgets are configuration data, reviewed against regeneration limits and
starter-deck strength. Stronger cards must still obey the world safety and
preservation precedence in Section 8.3. By default, card crafting does not
consume magical dust; dust is reserved for drawing and regenerating. A world
config may deliberately add a separate dust cost, but it must be disclosed and
must not replace or obscure the normal material recipe.

The card catalog, recipe definitions, experiment candidate pools/weights,
ingredient tags, discovery rewards, tier budgets, and unlock progression are
all game configuration. Libft CardGame provides the match/deck/effect runtime;
Minecraft owns exploration rewards, Card Press access, material economy, and
the mapping from recipe outcomes to generation effects. Do not put raw
callbacks or mutable world pointers in a card or recipe.

Tests must verify exact known-recipe output, blueprint unlocks, ingredient
influence on experiment eligibility/weights, tier caps for every candidate,
discovery bias, no-result/fully-discovered policy, and deterministic replay of
the committed outcome. Inject failures at material reservation, random-result
selection, output creation, discovery persistence, and commit; either all
effects commit once or none do. Duplicate request IDs, disconnects, save
reloads, and deliberately repeated experiment requests must not duplicate
materials/cards or reroll a completed experiment. Confirm the normal Card Press
UI remains a recipe book and never becomes an ingredient grid.

### 8.8 Libft CardGame adapter and World Loom integration

**Implementation checkpoint (refreshed 2026-10-22; source inspection only):**
the linked Libft design ledger now references the fetched branches. Libft
contains typed host intents, snapshot/delta/replay support, session-scoped
request-idempotent actions, policy-aware draw and mandatory discard, the
script bridge, revisioned inventory and recipe-book APIs, and prepared craft
transactions. Minecraft source contains the 20-card starter adapter, typed
card choices/effects, a `GameSession::commit_regeneration_start` path for
actor/Loom/request-scoped quote validation, resource debit, opening deal,
selected-chunk reservation, barriers, and a persisted decision deadline, plus
recipe query/discovery/crafting entry points. These are implementation
foundations, not proof that every card/content rule or the durable multiplayer
flow is complete.

The current worktree also distinguishes a newly recorded recipe-discovery
event from a recipe becoming newly known. Minecraft checkpoints a distinct
event receipt even for an already-known recipe, skips checkpointing an exact
retry, and rolls the recipe-book state back if saving fails. The full Libft
suite passes 6,505/6,505; `ItemCatalogValidator` source contains the Minecraft
checkpoint/retry cases, but they have not executed on this worktree. Minecraft
runtime validation remains open because its `make test` prerequisite build did
not produce `ft_vox_analytics`.

Earlier `--validate-world-revision` success records apply to the lower-level
selection/quote and queue-admission slices. A later directly linked runtime
validation was reported to fail with Windows heap/stack corruption. The latest
fetched branches were not built or run in this checkpoint, so neither result
validates their current end-to-end behavior. Keep production authentication
and access providers, live occupied-chunk refresh/exclusion rings, durable
request and host-intent recovery, multiplayer reservations, crash-safe
generation publication, complete card/content coverage, and end-to-end
validators open. This remains backend work only; no UI is specified here.

The linked backend design now defines the missing durable CardGame-to-world
intent boundary as at-least-once delivery with stable identity, a prepared
consumer journal, atomic participant checkpoint, then producer acknowledgement.
Its crash matrix and replay tests are design requirements, not evidence that
this protocol is implemented. Keep World Loom/card actions pending until their
effects are durably committed; do not expose them as successful based solely on
the Libft action/outbox commit. See [Durable action-to-host-intent
protocol](../Libft/Docs/card_game_scripting_inventory_crafting_integration_design.md#durable-action-to-host-intent-protocol).

Use the checked-out Libft `CardGame` module as the rules-neutral runtime for
card identity, ordered zones, draws/shuffles, effect dispatch, choices,
usage limits, and match/session state. Minecraft owns the world-specific
meaning of cards and every world mutation. The existing
`RegenerationCardSession` is the Minecraft adapter boundary: it maps versioned
Minecraft card content to Libft definitions and turns validated engine results
into Minecraft-owned intents. Keep that boundary thin and versioned; do not add
a parallel `MinecraftCardGameAdapter` or second card-game runtime.

Use distinct engine profiles/instances for these contexts:

1. **World Loom regeneration:** one server-authoritative session per active
   regeneration operation/player. Configure typed zones for its draw pile,
   active hand, discard and, if required, a resolving/effect-order zone. Keep
   the existing 20-to-100 owned-card rule, geometric dust draw pricing,
   chunk selection, safe zone, preview, resource reservations/refunds and
   world job outside the generic engine. Use the rules-neutral ordered-zone
   APIs; do not put terrain cards onto the creature board just to reuse
`play_card` semantics.
2. **Card Table minigame:** a separate match session with its own profile,
   format, phases, zones, RNG stream, command sequence, deck validation,
   snapshots and replay. It may use the engine's configured board/combat
   paths where they match the Minecraft rules. A minigame match never borrows
   or mutates the player's World Loom deck zones.

The Minecraft adapter registers stable card, card-type, zone, phase, event,
effect, usage-limit and predicate IDs from a versioned content catalogue.
Callback registries are recreated on each process from that catalogue before
snapshots/replays are loaded. Callbacks must be deterministic for the same
rules version, command and state; callbacks must not write to the world,
perform I/O, retain engine-owned pointers, or serialize function/user-data
pointers. The current generic operation buffer covers card-game values such
as health, mana, emitted events, instance damage/healing and stat modifiers;
it is not a terrain edit API.

The Libft scripting bridge exposes invocation-scoped `card_actor_id()`,
`card_instance_id()`, and `session_phase()` reads alongside typed host-intent
emission. Scripts can call `random_bounded(bound)` during an effect; it uses
the match-owned persisted RNG and its state rolls back with a failed event.
World policy validation and mutation remain in the Minecraft adapter and
coordinator. Resolved choice lookup is available to the active effect actor.
Script-side draw/discard operations are implemented. `RegenerationCardSession` persists the bounded context payload and script bridge manifest hash in its checksummed RCS1 version 4 snapshot; GameSession stores that adapter snapshot. Restore installs context keys and values, then accepts an optional catalog only after verifying the caller-supplied trusted content manifest and policy hash, before restoring engine state. Versions 1–3 remain readable for scriptless sessions. GameSession now accepts a trusted catalog before startup and binds it to the active generation-policy manifest. Authored catalog delivery and host authentication remain content integration work.

The GameSession lifecycle now accepts an authenticated catalog before
`start()` and binds it to the active generation-policy manifest for new and
saved sessions. Authored catalog delivery and host authentication remain
content integration work.
GameSession also preflights starter card effects against current world rules
before consuming the physical card. Capped plan/target effects and invalid
guaranteed outcomes fail without mutation; cached request replays retain the
adapter's exact-result behavior. The World Revision validator checks cap
rejection and unchanged plan state.
Surveyor's Reach preflights the target through the same loaded/protected/
occupied/selection-cap checks as bonus selection before consuming the card.
The typed target is also carried by the accepted-rules journal and applied to
draft selection in the same transactional manager update. Duplicate targets are
rejected; a newly protected or occupied target is removed from selection and
revalidated before generation starts.
Rare card definition 11, Ore Overgrowth, now persists a typed ore choice,
preflights checked 2x scaling against the configured cap, and composes in play
order with Deep Seam. The World Revision validator covers that composition and
the cap rejection. The crafted physical item now imports as definition 11,
which is registered in the CardGame profile and accepted by the session
snapshot codec. Rare card instability and the remaining rare-card effects
remain open.

The World now owns a per-chunk instability record store with a configurable
challenge default, hard cap, monotonic reward/danger bands, versioned permanent
modifier and feature outcome IDs, and generation revision/rules-hash
provenance. Its bounded checksummed sidecar codec validates against the
configured policy hash, and World Revision validation covers cap/stale-write
rejection, roundtrip and corruption rollback. Ore Overgrowth applies its
configured instability cost transactionally, and successful chunk publication
stores generation provenance. Reward/danger bands are high-water tiers: later
stabilizer reductions do not revoke unlocked tiers, and the WCI1 v1 roundtrip
validator now covers that behavior. Reward/danger gameplay content remains
open.
Version-9 combined GameSession checkpoints can now carry the instability
store payload alongside CardGame and regeneration-rule state. The host must
configure the matching instability policy before startup so policy-bound
records can be restored. Versions 1–8 remain readable. This preserves the
store across session restart.
Ore Overgrowth now requires an explicit host-configured positive instability
delta, bound into the generation manifest. Before consuming the card, GameSession
checks the delta against every selected chunk; pending intent application updates
those records and checkpoints them with card/rule acknowledgement, restoring
the previous store if the save fails. Other rare-card costs, generated feature
outcomes remain open. Before regeneration queues
work, the World now ensures each selected chunk has a bounded instability
record. Successful publication records a per-chunk monotonic generation
revision and a domain-separated hash of the serialized rules overlay (or the
base configuration signature for revisions without a card overlay); failed or
stale jobs do not record provenance. Runtime generation-publication validation remains
outstanding.
The World exposes a restricted public view containing only chunk coordinates
and reward/danger bands; it omits raw values, modifiers, feature outcomes,
generation revisions, and rules hashes. The authorized World Revision server
now provides an `instability_view` query with a request ID and returns only that
public view. A separate host callback must authorize visibility for each
actor/Loom/chunk tuple; queries fail closed if no callback is installed. Exact
retries return the captured view, and changed payload reuse is rejected.
Validator source checks redaction, visibility denial, and retry behavior; the
network-level runtime path remains unverified.
Destroying and reusing a World now clears its per-chunk records while retaining
the configured policy, preventing state from crossing seed/session boundaries.

For a regeneration card, validate the player command and its card-instance ID,
resolve the card through the registered stable effect ID, and produce a
bounded immutable `regeneration_policy_intent` in Minecraft-owned memory.
That intent contains only IDs, numeric parameters, target chunk coordinates,
and relevant config/content versions. Validate it against selected chunks,
generator capabilities, protection, feature footprints, and effect budgets;
then feed the resolved immutable policy to the existing preview and
world-generation path. If using an engine event as the adapter boundary, the
event type and source card/effect IDs are stable; target coordinates and
parameters remain in a validated, request-ID-keyed Minecraft command record.
An engine event or callback must never be treated as authority to mutate a
chunk directly.

Treat a World Loom action as a cross-system transaction. The Libft engine can
make its own card operation transactional, but it cannot atomically commit
Minecraft inventory, dust, deck membership, regeneration escrow, a multi-chunk
world revision, and persistence. A single authoritative coordinator must
validate and reserve these together under an idempotent request ID, durably
record the accepted intent, and define compensation/refund behavior for
failure/cancellation. The coordinator must not hold world locks while the
engine runs callbacks or while chunk generation occurs. Use snapshot/restore
for rollback only for engine state included in the pinned engine contract.
Once a chunk commits, later session failure
does not undo that chunk; settle only the corresponding per-chunk costs and
card effects according to explicit policy.

Use the engine-owned deterministic RNG for deck shuffle/draw and persist its
state with the session. Use engine snapshots/deltas or authoritative commands
only after verifying the exact state they include. The Minecraft session
record additionally stores world request/idempotency state, resource escrow,
target chunks, applied policy digest, structure plans and per-chunk commit
results. Do not assume these external fields are in a `card_game_snapshot`.
Clients receive filtered card state: a player's own hand may be visible to
that player, but opponents' hands and hidden piles must not be broadcast in a
full engine snapshot.

The present 20-to-100 World Loom deck fits the engine's current 128-card
active match-state bound. Keep the two limits independently validated; a
future content change must not silently push an active deck past engine
capacity.

### 8.9 Card Table minigame

Game 1.0 includes a small, fully playable card-game activity at a Card Table
(name provisional), built on a separate Libft `card_game_engine` instance.
Start with one explicit Minecraft rules profile and a two-player match; the
engine's higher player-count limit is not a promise that Minecraft implements
rules or UI for every player count. Use the engine's match-start
configuration, turn/phase graph, registered card types/zones, card/effect
callbacks, resource-cost/allowance APIs, choice ledger, usage-limit ledger,
modifier/combat operations, command sequencing, state hashes, deck codes and
replay/result archive where the configured rules need them. Configure a
resolution stack separately when the profile needs stack/chain behavior, and
persist/hash/replay it through the Minecraft match adapter because it is a
separate component.

Minecraft's card catalogue defines card definitions, set/corpus revisions,
rarities, printings, booster contents/odds, collection ownership, and
Minecraft-specific effect callbacks. `card_game_format` is the legality
boundary for a selected profile/corpus: use its legal-card list, copy limits,
ban/restricted entries and typed exceptions, then bind the match/deck code to
the format hash. It is not a booster-generation or collection system. A deck
code is an import/export representation, not proof that a deck is owned,
legal, or available in the current world; verify format, corpus, ownership,
and instance IDs server-side before a match.

The intended minigame deck contract remains **25 to 500 cards**, with profile
specific main/extra/side/leader zones and copy limits. The current deck-code
codec can encode up to 500 total cards, but active match state is presently
bounded by 128 cards. Before enabling a format whose legal deck can exceed
that active limit, update Libft engine storage, snapshots/deltas, hashes,
serialization and replay capacity, with format-version migration. Until then,
clearly mark those formats unsupported and never truncate an imported deck.

The initial minigame loop is: select a legal owned deck (or a tutorial loaner),
validate its code and format on the server, initialize a seeded match, draw
opening hands, submit sequenced commands, resolve effects/choices/phases and
combat, determine a server-owned result, then save/export a replay. Use
server-authoritative hidden zones and publish only player-appropriate views.
The engine's player-view replay projection is useful for sharing; opponent
hidden-zone contents and private event payloads must be irreversibly removed.
Any reward from a match uses a separate idempotent Minecraft reward
ledger, cannot mint repeated dust/cards through replay or reconnect, and is
not required for the core minigame loop.

The Hearthstone-, Magic-, and Yu-Gi-Oh!-style samples in Libft are simplified
engine simulations, not a complete implementation of those games' official
rules. The Minecraft minigame must ship only rules defined by its own
versioned profile. It may draw design examples from
different games, but must not claim parity with their full rulesets. Keep
rules data and registered effect programs modular so future profiles can add
conditional resources, summon/action allowances, zones, timing windows,
stack admission/order, triggers, and combat policy without changing the
generic engine for each card.

### 8.10 Required changes at the Libft and Minecraft boundaries

**Libft CardGame changes:**

- Raise active match capacity beyond 128 so it supports a 500-card deck across
  its configured deck zones, plus any additional live card instances the
  profile can create. Define and enforce one total match-instance limit rather
  than implying 500 cards are independently available in every zone. Update
  engine storage, card/deck/hand/zone validation, snapshots, deltas, state
  hashing, command/replay serialization, and version migration together;
  changing only `FT_CARD_GAME_MAX_CARDS` or relying on the 500-card deck-code
  codec is insufficient. The existing 20-to-100 regeneration deck needs no
  capacity increase.
- Provide one versioned authoritative match-action path for every
  player-originated state change. At this revision, `card_game_command_type`
  records play-card, end-turn, and advance-phase, while operations such as
  choosing an option, mulligan, payment, and several zone changes have direct
  APIs. Extend the generic command/replay model with stable action IDs and
  bounded fixed-width arguments, or expose a supported composite-action
  interface that lets a caller journal these actions without bypassing engine
  validation. Do not serialize function pointers or callback user data.
- For profiles that use `card_game_resolution_stack`, either include its
  configured ordering/admission state in the engine's authoritative snapshot,
  delta, state hash, and replay contract, or explicitly keep it in a composite
  match-state API. It is separate from `card_game_engine` today, so a caller
  must not assume it follows automatically from an engine snapshot.
- Do not add Minecraft terrain, chunk, inventory, or dust concepts to Libft's
  generic operation enum. Existing stable effect IDs/events and registered
  callbacks are enough to bridge card resolution to a game-owned policy
  adapter. Add a generic custom-operation facility only if another consumer
  needs it; it must use stable operation type IDs, bounded value payloads, and
  registered validators/handlers rather than game-specific code or pointers.

**Minecraft changes:**

- Add a configuration loader/registry that builds the Libft card definitions,
  types, zones, turn phases, formats, effect IDs, usage limits, predicates,
  and callback bindings from versioned Minecraft content. A saved match stores
  profile/format/corpus/config versions and stable IDs; startup re-registers
  process-local callbacks before restoring state.
- Implement two distinct wrappers over the same engine: a `WorldLoomSession`
  that owns the single-player regeneration profile plus a Minecraft
  `RegenerationRequest`/policy-intent journal, and a `CardTableMatch` that
  owns a normal match profile and a composite match record. Do not implement a
  duplicate deck/hand/shuffle/effect runtime in Minecraft, and do not share
  mutable zones or RNG state between the wrappers.
- Keep world authority, dust/inventory accounting, safe-zone checks, chunk
  selection, generation, structure placement, and commit/refund logic in
  Minecraft's existing server/world owner. The adapter turns an accepted
  CardGame action into a bounded immutable game intent; the server validates
  and commits that intent through the established World Loom transaction.
- Keep collection ownership, boosters/rarities/printings, card crafting,
  localization, UI, player permissions, and reward settlement in Minecraft.
  Use Libft `card_game_format` for ruleset legality and the canonical deck
  code/hash for interchange, not as substitutes for owned-card checks or
  Minecraft's collection and booster systems.
- Build live network views from the authoritative engine state but filter
  hidden zones per recipient. Store the engine state together with Minecraft
  request IDs, reward-ledger entries, and any external resolution state needed
  to resume a match. Use Libft replay projection for sharing only after the
  Minecraft wrapper has removed its own private payloads as well.

## 9. Request and job architecture

### 9.1 Immutable request data

The UI submits intent, not generation output. A regeneration request should
contain at least:

```text
protocol_version
request_id                         // idempotent per player session
world_id, station_id, player_id     // player identity is authenticated server-side
preview_token and base revision ID
selected chunk coordinates + expected chunk revisions
requested paid chunk count
played card instance IDs in order
targets and validated choices for each card
requested regeneration mode/stage mask
client sequence number
```

Do not trust a client-supplied player ID, dust balance, card effect, seed,
generator digest, protection state, or calculated cost. Bound every count and
payload length before allocation.

### 9.2 Server validation and preparation

The server validates station access, reach, world/session state, selection
size, chunk revisions, safe/protected status, player resource balance, card
ownership and zones, card targets, deck/hand limits, config version, and
idempotency. It then resolves costs/effects and builds immutable per-chunk
inputs.

Pseudo-code:

```text
handle_regeneration_request(request):
    if request_id already completed:
        return recorded_result

    authenticate_connection_and_station_access(request)
    validate_bounds_and_request_limits(request)
    verify_preview_token_and_chunk_revisions(request)
    verify_all_selected_chunks_are_known_and_eligible(request)
    reject_station_safe_zone_and_all_protected_chunks(request)
    verify_resource_and_regeneration_deck_state(request)

    policy = resolve_world_config_and_card_effects(request)
    validate_policy_against_world_invariants(policy)
    reserve_session_and_per_chunk_resource_costs(request, policy)
    prepared_jobs = capture_immutable_chunk_inputs(request, policy)
    if any_preparation_failed:
        rollback_all_reservations_and_leave_world_unchanged()
        return error

    persist_idempotent_session_record()
    enqueue_jobs_by_priority(prepared_jobs)
    return accepted_session_id
```

The request/response layer should remain server-neutral so the current local
world adapter and a future separate-process/multiplayer server use the same
validation and revision semantics. In the intended topology, the client sends
requests to an authoritative local or remote server process; client prediction
may show UI feedback but may not publish canonical chunk data.

### 9.3 Worker boundaries and priority

Workers receive immutable data only: coordinates, block/generation snapshots,
configuration values, card-resolved policy, generator version, and seed
material. A worker never receives the mutable `World`, a live chunk pointer,
renderer-owned state, or a client connection object.

Prepare generation and required local lighting/mesh data away from world
locks. Publish on the authoritative world owner thread through the existing
generation-result commit boundary. Do not join/create a worker for each chunk;
use the persistent worker pipeline already present.

Priority order should be explicit:

1. accepted block edits, collision-critical updates, and their bounded light
   repairs;
2. visible regeneration results ready to commit and nearby required remeshes;
3. active regeneration generation near the player/station;
4. ordinary nearby chunk streaming;
5. distant regeneration, map summaries, compression, and analytics export.

Use bounded queues, fair aging, cancellation tokens, and a per-frame commit
budget. A regeneration burst must not starve block breaking, normal chunk
streaming, input, or rendering. Cancellation is cooperative between safe
stages, not an unsafe interruption of a worker in the middle of mutating
shared data.

### 9.4 Per-chunk prepare and atomic commit

Each selected chunk is a separate transaction so a large selection does not
hold the world lock for its entire duration:

```text
capture snapshot at block/light/chunk revision R
generate candidate blocks and metadata off-thread
apply preservation mask and transition constraints
calculate/validate light for the candidate revision
build candidate mesh/render data off-thread
validate candidate output and generation digest
on authoritative owner thread:
    recheck chunk identity, revision R, protection, and session epoch
    prepare persistence/replication records
    atomically publish complete candidate state
    increment revisions and release per-chunk dust charge
if validation is stale or any preparation fails:
    discard candidate; keep old blocks, light, mesh, and revision intact
```

The visible chunk must not go black while a replacement is being calculated.
The old block/light/mesh tuple remains drawable until the new tuple is
published together. If current chunk architecture cannot atomically swap all
three, implement a render-safe immutable chunk snapshot/pointer publication
first and test it before this feature ships.

## 10. Generation semantics and preservation

### 10.1 Mode and stage behavior

Reuse the existing regeneration modes/stage masks instead of inventing a
parallel enum. The first UI may expose a constrained subset:

- decoration refresh;
- underground/ore refresh;
- terrain and biome refresh;
- full regeneration only after separate destructive confirmation.

Cards can add supported stages or change policy within the selected mode, but
must not silently escalate a decoration refresh into full terrain replacement.
The preview lists the effective stages after card resolution.

### 10.2 Player content

Default behavior is preservation. The server must retain player-modified
blocks, protected structures, station blocks, containers, and claimed content
according to the world policy. If the current provenance representation cannot
distinguish generated baseline from player changes reliably, affected chunks
are ineligible for full regeneration until that gap is fixed; do not guess.

Transition chunks blend generated terrain against protected/unchanged borders.
Generated edits never overwrite protected blocks. The preview shows any
transition band and warns when a card's requested feature cannot cross a
protected boundary.

### 10.3 Water, lava, and special features

Water/lava cards describe a validated feature profile, not arbitrary point
placement. The generator must ensure fluids have terrain-supported boundaries,
valid source/flow state, and deterministic chunk-edge behavior. No pond may
float above terrain, flood a tree/structure unexpectedly, or terminate at a
chunk edge with a visible leak. Cross-chunk features use coordinate-stable
ownership and neighbor input snapshots. If the neighboring chunk is protected,
the generated feature must terminate/blend safely rather than mutate it.

Add property tests for every emitted fluid cell: it belongs to a valid feature,
its support/boundary rules hold, and edge continuation agrees when adjacent
chunks are generated in either order.

The built-in biome set must include an **ocean** biome with a stable biome ID,
deep-water and sea-floor rules, shoreline transition rules, an allowed
underwater terrain profile, and a clear distinction from rivers, lakes, and
ordinary coastal land. Ocean generation must be deterministic across chunk
boundaries and must not place land-only vegetation or structures in water.
Ocean cards and `OCEAN_ONLY` features query the resolved biome/terrain snapshot;
they do not bypass it or manufacture an ocean solely to satisfy a structure
guarantee. A generated ocean must preserve valid water containment, floor
support, lighting, and safe transitions at its borders.

Shipwrecks are the first ocean-only structure. Their global plan must include
an anchor, rotation, bounding box, valid water-depth range, required ocean
footprint, loot/content version, and stable structure instance ID. A wreck may
cross chunk boundaries, but every fragment must agree on ownership and must be
generated only when the complete plan passes ocean, protection, terrain,
collision, and structure-budget validation.

### 10.4 Extensible custom generation features and structures

Custom content must use a versioned, data-driven generation contract rather
than special-case code for each structure. A `custom_feature_definition`
should contain stable feature/definition IDs, schema and content versions,
allowed generation source (`DEFAULT_WORLD`, `REGENERATION_ONLY`, or both),
minimum/maximum footprint in chunks, allowed biome/height/terrain tags,
placement and rotation rules, required stages, feature/effect budget, and
references to configured block palettes, structure pieces, entities, and
progression unlocks. Callbacks, where unavoidable, are stable registered IDs;
never serialize function pointers or give generation workers mutable `World`
access.

Plan a cross-chunk structure once in global world coordinates using a stable
`structure_instance_id`, anchor, bounding box, required chunk footprint, and
generation/config digest. Then produce chunk-local terrain/mesh fragments
from that shared plan. Each chunk stores a structure-fragment reference with
the instance ID, global bounds, local fragment bounds, anchor relation, and
which neighboring chunk edges the structure continues across. The renderer
must understand that a village road/building/volcano contour crossing a chunk
edge is one continuous structure–not a completed structure ending at each
chunk boundary. It must use consistent global coordinates, seam ownership,
face culling, lighting inputs, and neighbor dependencies so the edge does not
become a gap, duplicate surface, or false opaque wall. Do not copy the whole
structure into every chunk or send structure meshes over the network.

Before work begins, the server validates the complete footprint against the
selected chunks, safe zone, protected/player-edited content, claims, terrain
constraints, and per-session budgets. A feature requiring a contiguous area
must not silently shrink, move into protected land, or generate only the
selected fragment. Reject an incomplete selection with the missing footprint
shown on the map, or require the player to choose another valid anchor. For a
large multi-chunk feature, persist a group/instance manifest and per-chunk
candidate status. Prepare all necessary fragments before making the feature
active; then commit/publish through bounded per-chunk work while keeping the
instance marked incomplete and non-interactive until every required fragment
is committed. Crash recovery resumes or rolls back the group without leaving
an untracked half-structure. Villagers, boss triggers, and loot become active
only after the structure manifest is complete.

The custom-feature registry is extensible through game configs/content packs:
new structures, palettes, footprints, eligible biomes, regeneration cards,
NPC role tables, and unlock conditions can be added without changing the
chunk format or renderer for each new content definition. Config validation
rejects unknown block/entity IDs, invalid footprints, unbounded piece counts,
unsafe fluid placement, conflicting exclusive features, and impossible
unlock/cost references before world generation starts.

### 10.5 Village and volcano feature requirements

**Villages** are custom structure instances whose roads, plots, buildings,
storage, and NPC spawn points may span any number of chunks. A village placed
on a chunk border must have the fragment/continuation metadata above, and the
renderer must show a single connected village as neighboring chunks load,
regardless of generation order. The server assigns a stable `village_id` and
ties its structures, villagers, owned resource stockpiles, reputation, and
generation provenance to that ID. Village generation may be enabled for
ordinary world generation or reserved for a regeneration card by config.

**Volcano** is a late progression feature and is explicitly
`REGENERATION_ONLY`: the default world-generation configuration never places
one. Based on the chunk-regeneration context, –minimum 9x9– means a minimum
footprint of **9 by 9 chunks (81 contiguous chunks)**; confirm this unit before
implementation if blocks rather than chunks were intended. The feature card
requires an unlocked high tier, previews the entire footprint and cost, and is
rejected if any part intersects the protected 3-by-3 zone or other protected
content. The configured footprint may be larger than 9x9, never smaller.
By default, all 81-or-more footprint chunks count toward the normal per-chunk
dust cost; any configured bundled/discount price is explicit in the preview
and cannot bypass safe-zone, protection, or total-work limits.

The volcano's terrain, crater, internal passages, arena, and lava are generated
from one global plan and validated for support, containment, lighting, and
chunk-edge continuity. It contains a configured boss encounter. The boss may
spawn only after the whole volcano manifest is committed, its arena is valid,
and the player meets the configured unlock/encounter conditions. Boss health,
phases, attacks, arena bounds, persistence, death, and rewards are
server-authoritative. The boss has one stable identity per structure instance;
chunk reloads, repeated requests, and regeneration retries cannot respawn or
duplicate a defeated boss or its unique reward. A deliberate reset, if ever
allowed, is a separate explicit world rule.

## 11. Neutral mobs and fauna

### 11.1 Scope and prerequisites

Neutral mobs are non-hostile ambient creatures that can flee, follow, graze,
forage, or react to players but do not initiate attacks. Sheep, cows, pigs, and
chickens are the initial roster. Their behavior and spawn rules are Minecraft
gameplay policy; Libft networking transports validated state but does not
decide game rules.

The current `EntityState` and entity-update message are not enough to ship
these creatures: they provide state serialization but there is no complete
authoritative AI owner or creature renderer. Before adding production mobs,
establish an entity simulation owner, stable entity IDs, spawn/despawn
replication, persistence, collision/navigation boundaries, and a renderer
submission contract. Do not make chunk generation or worker jobs own live mob
objects.

### 11.2 Initial species should feel distinct

| Species | Habitat and idle behavior | Distinct interaction/identity |
|---|---|---|
| Sheep | Small flocks on suitable grassy surfaces; graze and wander between nearby safe points | Grazing changes wool state; eating valid grass can regrow wool after a cooldown. Coat colors can be configured, not tied to combat stats. |
| Cow | Slower, larger herds in grassland; pause, graze, and regroup when one moves | Follow configured feed and support a cooldown-based milk interaction. A startled cow flees briefly and may bump through an open path, but never attacks. |
| Pig | Curious groups around grass/forest edges; sniff and root at safe ground patches | Can uncover a configured low-value forage item or seed at a bounded rate; seeks mud/water-like resting patches when available. Rooting must never edit terrain without an explicit server-side feature rule. |
| Chicken | Small flocks around grass and farm-like terrain; peck at the ground and follow nearby flock members | Lays an egg on a server-timed cooldown at a valid nest/ground location; chicks follow an adult and use a distinct smaller movement profile. |

These are examples of behavior contracts, not promises that drops, breeding,
or farming are implemented in the first milestone. Keep the first release
small: idle/wander, navigation bounds, flee/follow response, save/load, and
rendering. Raw meat and useful materials are server-authored item drops, using
the ground-item and inventory contracts in section 20. Add wool/egg/milk/forage
interactions behind configured rules after the base entity loop is stable.

### 11.3 Configured spawn and lifecycle rules

Spawn rules should be data-driven per species: biome allowlist, surface/block
requirements, light/time/weather constraints, group-size range, local/global
population caps, despawn rules, and deterministic spawn probability. Validate
surface support, collision volume, fluid state, and proximity to protected
player builds before spawning. Regeneration may refresh eligible natural
spawns only if policy explicitly allows it; it must preserve named/tamed or
otherwise player-owned creatures and must not duplicate entities on retries.

Use stable entity IDs and idempotent spawn records. Chunk commit reconciles
natural-spawn records by deterministic spawn key; it does not re-run live
entity spawning every time a mesh/light result is committed. Entity movement is
server-authoritative; clients interpolate received state and never decide
canonical position, drops, cooldowns, or breeding outcomes.

### 11.4 Controlled animal breeding

Hunting is not the only intended source of animal materials. Players can raise
larger herds through an explicit, bounded breeding loop. Each species config
defines its breeding feed item (for example, wheat for sheep/cows, a configured
root vegetable for pigs, and seeds for chickens), adult age, health/eligibility
rules, breeding cooldown, offspring growth time, and local/global population
caps.

The server accepts a breeding action only when two distinct, eligible adult
animals of the same species are nearby, neither is on cooldown, both have a
valid path/space for offspring, and the area population cap allows another
animal. Feed is consumed transactionally and the offspring receives a unique
entity ID and deterministic parent/spawn record. Replayed feed or breeding
requests cannot consume more food or create another baby. The baby starts at a
safe supported position; it cannot spawn inside a block, in lava, or inside a
protected station/building.

Use a configurable cooldown (a few in-game minutes is a reasonable first
balance target) and a longer configured maturation time. Optional feeding to
accelerate growth is a later tuning rule, not a way to bypass caps/cooldowns.
When a chunk unloads, persist the remaining simulation-time deadlines; do not
advance babies by unbounded offline catch-up when the chunk reloads. A bounded
catch-up policy is explicit world configuration. Breeding, growth, and animal
death are server-tick events and persist atomically.

### 11.5 Future optional species

After the four initial animals, examples that add genuinely different play
could include:

- a **marmot** that warns nearby animals and retreats into a den;
- a **duck** that moves between land and water and nests near shore;
- a **glow-moth** that gathers around configured light sources at night.

These remain optional content and must use the same bounded, configurable
behavior/spawn interfaces. They are not prerequisites for chunk regeneration.

### 11.6 Villager NPCs and village relationships

Villagers are a separate non-hostile NPC archetype that uses the same stable
entity ownership, persistence, replication, and renderer contracts as fauna,
but has a village identity and configurable job/relationship behavior. Each
villager has a stable entity ID, `village_id`, role/job ID, schedule state,
navigation target, and bounded carried/work inventory. Example roles include
farmers tending village plots, gatherers collecting configured nearby
renewable resources, and keepers maintaining village stockpiles. Gathered
items are deposited into server-owned village storage; villagers cannot
generate unlimited resources, collect outside configured bounds, or modify
player-owned terrain. Work cadence, resource quotas, pathfinding area, and
population caps are configuration data.

Track a bounded, persistent reputation value per player and village. Taking
from a marked village-owned container or resource plot without permission
emits one authoritative theft event; the village can respond with a warning,
refusal to trade, or other configured non-hostile reaction. A player can make
a deliberate gift/donation, such as depositing magical dust into a village
contribution store. Validate the gift and debit it transactionally, then
increase village reputation exactly once. Positive reputation can unlock
trades, recipes, regeneration clues/cards, assistance, or more favorable
dialogue. Rewards and penalties are thresholds defined in data; they must not
be granted by spoofed client events, repeated packets, or taking an item and
immediately returning it unless the rules explicitly permit that recovery.

Villagers react to player actions through bounded server events, not a scan of
all players/items every frame. Use a local village spatial index and a capped
number of scheduled NPC decisions per tick, with fair priority for nearby,
visible interactions. Clients receive compact state/action updates and
interpolate walking/working animations. Resource gathering, theft detection,
gift/reputation changes, trade unlocks, and villager schedules persist across
chunk unload and server restart. Regeneration must preserve a village's
identity/reputation and never reset its stockpile or rewards by regenerating
one of its chunks.

## 12. Compression and persistence on the Libft branch

Compression is for storage/transport payloads, not live generation buffers.

- Keep canonical chunk data in its normal owned representation while workers
  generate, light, mesh, and validate it.
- Serialize a complete immutable chunk snapshot with an explicit schema
  version, generator/config digest, chunk coordinates, revisions, lengths,
  and integrity check before compression.
- Compress snapshots in a background persistence/network preparation stage;
  do not compress every block, every delta, or every frame.
- Prefer a whole-snapshot compression unit. Small block deltas can use the
  Networking framing directly unless measured payload size justifies bounded
  batch compression.
- Include the codec identifier and uncompressed size. Reject impossible or
  over-limit lengths before allocation. A decompression API used on
  network-controlled data must enforce an output cap; do not assume the
  generic vector decompressor is safe against expansion merely because it
  returns an error code.
- Validate checksum, IDs, revisions, sizes, and complete parse before
  replacing live chunk state. Failed decompression or parse leaves the previous
  chunk untouched.
- Compression failure may leave a save/recovery task pending or mark the
  session retryable; it must not discard the authoritative in-memory chunk or
  publish a partial file.

Use the public Compression API present on
`agent/compression-analytics-cardgame-scripting` only after checking the exact
header and error contract at implementation time. Do not copy Compression
internals into Minecraft or introduce a direct zlib dependency.

## 13. Analytics and performance instrumentation

Use the Libft Analytics module on the named compression/analytics branch to
measure this system. Minecraft registers stable regions and calls the public
analytics session API; Libft owns event buffers, file output, and its exporter
thread. Do not add a second Minecraft-owned logging/export thread or copy large
world buffers merely for analytics.

Suggested regions/flows:

```text
regen.ui.open
regen.map.request / regen.map.response
regen.preview.resolve_cards
regen.preview.build
regen.request.validate
regen.resource.reserve
regen.snapshot.capture
regen.worker.queue_wait
regen.generate.total
regen.generate.biomes / terrain / ores / fluids / structures
regen.light.calculate
regen.mesh.build
regen.result.validate
regen.commit.wait / regen.commit
regen.persist.serialize / compress / write
regen.network.snapshot / delta
mob.spawn.validate / mob.ai_tick / mob.serialize / mob.render_submit
farm.crop_growth / farm.harvest
animal.breed.validate / animal.breed.commit / animal.growth
inventory.recipe_availability / inventory.craft.commit
furnace.process / item.pickup.commit
combat.damage.resolve / combat.armor.resolve / combat.projectile.update
magic.cast.validate / magic.effect.apply
```

Instrument stage/job boundaries and aggregate counts/bytes; do not create a
scope per voxel, light node, block, or entity every frame. Use flow IDs to
correlate a request through validation, worker generation, commit, and client
visibility. Record selected/committed/failed chunk counts, queue depth/age,
stale result count, bytes compressed, compression ratio, retry counts, and
reason-coded skips. Export at the configured frame interval (normally 120 for
live frame summaries); detailed traces are sampled and run off the hot path.

Analytics failure, queue overflow, or exporter shutdown must not block world
generation or make a chunk fail. Report the analytics error through the
existing error/status path, and ensure normal builds can omit analytics
instrumentation entirely. Compare identical seeded runs with analytics
disabled and enabled; generated chunk hashes must match.

### Initial latency targets to validate

Treat these as explicit starting acceptance targets, then record a baseline on
the minimum supported hardware before tuning:

- Local request validation/accepted-or-rejected response: p95 <= 50 ms,
  p99 <= 100 ms, excluding network RTT.
- Player block edit while regeneration is queued/running: p95 <= 50 ms from
  accepted request to authoritative visible change, p99 <= 100 ms. Regeneration
  must not make this worse than the pre-feature baseline by more than 5%.
- Ready regenerated chunk to atomic drawable publication: p95 <= 100 ms,
  p99 <= 250 ms; it must never expose a partial block/light/mesh tuple.
- No generation, compression, file write, or unbounded queue-drain operation
  may occur synchronously in the render frame. Track maximum frame duration and
  p95/p99 under a fixed stress workload, not only average FPS.
- Analytics-on overhead is measured separately. The ordinary gameplay build
  remains the performance reference; analytics cannot be required for
  correctness or persistence.

If the hardware baseline cannot meet a target, report measured distributions
and adjust only with an explicit rationale. Do not hide stalls by using a long
timeout or averaging them away.

## 14. Persistence and network messages

Persist versioned records for:

- World Loom identity, owner/access policy, and safe-zone policy version;
- regeneration deck definition/version, unique card-instance IDs, ordered
  deck/hand/discard zones, and server-owned deterministic shuffle state;
- resource reservations and idempotency keys for active/completed sessions;
- regeneration session inputs, resolved effect digest, per-chunk status, and
  refund/charge results;
- generator version, chunk generation digest/stage mask, block/light/chunk
  revisions, and protected player overrides;
- neutral entity stable ID, species/config version, position/motion, lifecycle
  state, and ownership/taming state when those systems exist.

Never serialize raw pointers, callback addresses, mutex bytes, renderer state,
or process-local worker handles. Use explicit integer encoding and validate
counts/lengths before allocating. Saves are transactional: parse into
temporary state, validate all cross-references, then commit.

Future client/server messages should be intent/result oriented:

```text
REGEN_MAP_QUERY / REGEN_MAP_SUMMARY
REGEN_PREVIEW_REQUEST / REGEN_PREVIEW_RESULT
REGEN_DRAW_REQUEST / REGEN_DRAW_RESULT
REGEN_START_REQUEST / REGEN_SESSION_ACCEPTED or REGEN_REJECTED
REGEN_PROGRESS / CHUNK_REGEN_COMMITTED / REGEN_SESSION_FINISHED
ENTITY_SPAWN / ENTITY_STATE / ENTITY_DESPAWN
```

Messages carry protocol version, world/session/request IDs, bounded counts,
and expected revisions. A duplicate request ID with identical payload returns
the original result. Reuse of an ID with a different payload is rejected.
Chunk content is sent as authoritative snapshots/deltas, not meshes. Snapshot
compression uses the versioned/capped path above. Block-edit and control
traffic retain priority over bulk regenerated snapshots.

## 15. Failure handling

Every operation must have a defined outcome for:

- stale preview or changed chunk revision;
- invalid/duplicate/unowned card instance;
- insufficient dust, overflowed cost, or ledger persistence failure;
- protected/safe-zone chunk selection;
- unsupported generator/config/card version;
- queue full, allocation failure, worker error, cancellation, or shutdown;
- stale worker result after a newer edit/revision;
- lighting, meshing, serialization, compression, disk, or network failure;
- client disconnect during draw, preview, or multi-chunk commit;
- corrupted deck/session/chunk save data;
- invalid or oversized compressed payload;
- analytics exporter failure.

Before authoritative per-chunk commit, failure preserves the old chunk tuple
and releases that chunk's escrow. After a chunk commits, later failure does
not roll it back silently; the session reports partial completion, and all
clients converge on committed per-chunk revisions. The UI must distinguish
partial completion from total success.

## 16. Testing plan

Tests must exercise the actual public workflow and commit path. A test that
only checks a configuration helper or calls the generator directly is not
evidence that the player-facing feature works.

### 16.1 Unit and property tests

- Geometric draw schedule for zero, one, maximum, and over-maximum draws;
  checked overflow/cost-cap behavior and invalid config rejection.
- Chunk cost from server-validated coordinates, including zero, duplicate,
  maximum, protected, and bonus-scope selections.
- Draw failure leaves dust, deck order, hand, discard, and idempotency ledger
  unchanged; successful duplicate request charges exactly once.
- Duplicate card definitions retain distinct instance IDs; lookup of missing
  instance fails without mutating any deck zone.
- Deterministic shuffle/draw replay from persisted seed/state; no card
  duplication/loss across deck, hand, and discard transitions.
- Card target validation, ordering, composition rules, incompatible effects,
  per-effect limits, draw-chain limit, and bonus-chunk cap.
- Canonical policy digest is stable for equivalent inputs and changes when a
  relevant config/card/generator version changes.
- Protection mask is exactly the 3-by-3 square for negative and positive world
  coordinates; cards cannot make it selectable.
- Unknown/fogged chunks cannot be selected unless world policy permits it.
- Fluid profile invariants: no unsupported floating ponds, chunk-edge leaks,
  invalid sources, or mutation of protected neighbors.
- Each neutral species obeys spawn caps, valid surface/collision conditions,
  configured habitat, and deterministic spawn identity.
- Sheep wool, cow interaction cooldown, pig forage, and chicken egg/chick
  timers are server-tick based and survive save/load without duplication.

### 16.2 Regeneration integration tests

Use a deterministic small world with at least a 5-by-5 chunk area, a World
Loom, known player edits, protected structures, and a fixed seed.

- **Pre-Phase 1 slice gate:** reuse the existing `M` revision preview,
  generation worker, and result-commit path when their tests pass. In a small
  deterministic world, preview one target outside the protected 3-by-3 area,
  spend one configured dust balance, play one configured card, and regenerate
  exactly one chunk. Verify its prior drawable state remains until the
  block/light/mesh replacement commits atomically. Repeat with stale revision,
  cancellation, duplicate request, and injected prepare/commit failures; the
  resource ledger and chunk state must be conserved. Do not require the full
  inventory, crafting, fauna, or avatar-customization system for this gate.
- End-to-end: open station, map, select a valid chunk, draw, play a biome/ore
  card, preview, confirm, receive a session, wait for completion, and compare
  authoritative chunk revision/config digest.
- Attempt to select all nine safe chunks; server rejects each regardless of
  client preview manipulation or a scope card.
- Preserve an edited block, container, station, and named entity through
  regeneration; protected content remains byte/state-identical.
- Regenerate adjacent chunks in both orders and compare seam heights, fluid
  containment, lighting, and mesh visibility.
- Induce allocation, queue, worker, persistence, compression, and network
  failures at every prepare/commit boundary. No partial live state or lost
  escrow is allowed.
- Cancel before work, during generation, after one chunk commit, and after all
  results are ready. Verify completed chunks remain committed and all
  uncommitted per-chunk dust is refunded exactly once.
- Force stale revisions by editing a chunk after preview and while a worker is
  generating; stale result is discarded and the prior/current chunk remains
  drawable.
- Restart the server during a session and verify resume-or-cancel policy,
  ledger idempotency, deck zones, and refunds.
- Verify legacy revision preview and non-card revision calls still work.

### 16.3 Multiplayer and replication tests

- Player A starts regeneration; player B sees progress and the same committed
  chunk revisions without receiving intermediate worker memory.
- Client A disconnects/retries each request; no double draw, charge, card
  consumption, or chunk revision increment occurs.
- A malicious client supplies another player's identity, fake dust, fake
  generator/card digest, unowned card IDs, huge selection, duplicate
  coordinates, out-of-range positions, and stale preview token; server state
  stays unchanged.
- Delay, reorder, duplicate, drop, and corrupt progress/snapshot messages with
  Libft's deterministic network impairment tools. Recovery uses revisions and
  snapshots; no client treats a mesh as canonical world data.
- Concurrent player edits and regeneration on one chunk: a newer player edit
  wins by revision validation, stale generation is discarded, and nearby
  clients converge.
- Entity spawn/state/despawn is idempotent; reconnect does not duplicate
  animals; entity updates cannot cross world/chunk bounds.

### 16.4 Compression and analytics tests

- Compressed snapshot round-trip preserves the complete canonical chunk and
  generation metadata; malformed/truncated/oversized data leaves destination
  unchanged.
- Decompression cap rejects zip-bomb-like expansion before unbounded
  allocation; codec/version mismatch follows explicit fallback/error behavior.
- Compression error does not block rendering or discard in-memory world state.
- Analytics enabled/disabled seeded runs produce identical chunk, light,
  mob-spawn, deck, and resource-ledger hashes.
- Analytics exporter failure and queue saturation do not block world-owner
  commit or gameplay. Runtime metrics report dropped events/error status.
- Compare normal and analytics builds under the same scenario: frame p50/p95/
  p99, player-edit latency, queue ages, generation/commit time, memory, and
  allocation counts. Do not gate only on average FPS.
- Ensure instrumentation is absent or disabled in the normal gameplay build
  according to the build configuration; no Analytics worker is started unless
  requested.

### 16.5 Performance stress matrix

Run repeatable seeded trials on minimum, typical, and high-end supported
hardware. Include cold startup, active world generation, maximum allowed
selection, expensive card combinations, multiple connected clients, low disk
space/write failure, and analytics on/off. Record at least ten trials for
baseline comparisons, warm-up separately, and report p50/p95/p99 plus worst
observed and test-runner load.

Gate on the latency targets in section 13, no unbounded queue growth, no
render-thread generation/compression/I/O, no block-edit starvation, no black
or flickering chunks, and deterministic output for identical input hashes.

### 16.6 Item, crafting, and survival tests

- Ground item stack spawn, pickup, stack merge, despawn, save/unload/reload,
  and network retry are idempotent. Replaying a pickup request never duplicates
  the item or removes it twice.
- Full inventory, partial stack capacity, invalid item IDs, out-of-range
  pickup, and disconnect during pickup leave item and inventory totals
  conserved.
- Recipe book has no grid dependency. Missing ingredients, wrong/missing
  station, locked recipe, insufficient output room, stale inventory revision,
  and duplicate craft request cannot consume ingredients without producing
  the declared output.
- Furnace fuel and ore processing survive save/restart; failed output commit
  does not lose ore or consume fuel twice. Smelt/cook duration uses server
  ticks, not client wall clocks.
- A known Card Press recipe always produces its exact card definition and a
  unique instance ID. Exploration blueprints unlock the intended recipe;
  research experiments use ingredient-constrained candidate pools, disclose
  their tier/result families, and bias toward undiscovered cards.
- Higher-cost experimental pools never exceed their configured effect budget.
  Verify the selected result and server RNG state survive retries, disconnect,
  save/reload, and replay without a reroll; injected transaction failures
  either commit materials, discovery, and card once or commit none.
- Paper-reed fiber produces paper sheets and binder; paper plus binder makes
  card stock. Brown mushrooms provide a common dark ink, flowers provide
  colored pigment/ink, and mineral pigments are gated by configured recipes.
  Test material conservation, renewable regrowth, station checks, and output
  capacity.
- Crystal-block processing produces the configured magical-dust amount exactly
  once. Depositing dust at a World Loom atomically debits the inventory and
  credits the resource ledger; retries cannot duplicate it. Regeneration/reload
  cannot respawn depleted resource deposits. Known card recipes do not consume
  dust unless the versioned recipe explicitly says so.
- Insertion/removal between inventory and deck is atomic and enforces the
  20/100 bounds. Card crafting remains recipe-book driven, never a 2D grid.
- Player health/hunger transitions, food values, healing limits, death,
  respawn, and save/load obey configured rules. Hunger cannot underflow health
  or create negative values. Thirst, if enabled, has independent tests and can
  be disabled without changing hunger/health behavior.
- Hunger exhaustion crossing 4.0 consumes exactly one saturation point before
  food, carries its remainder correctly, and remains deterministic under
  batched movement/action events. Test full/empty saturation and food bounds.
- Natural-heal thresholds at 17, 18, 19, and 20 food plus zero/nonzero
  saturation select the intended slow/fast/no-heal path. Test configured
  difficulty starvation floors and the 100-HP scale.
- Grain crop growth/harvest/replant and the 3-grain-to-bread recipe are
  deterministic and survive chunk unload/reload. Farming may not duplicate
  seed or crop drops on repeated harvest requests.
- Breeding consumes the correct feed exactly once, rejects juveniles,
  cooldown pairs, mismatched species, blocked offspring positions, and capped
  population, and persists baby growth/cooldowns without offline time jumps.
- Raw/cooked meat and harvested fruit have configured, deterministic hunger
  effects. Animal loot cannot duplicate across death, chunk unload, or retries.

### 16.7 Custom structures, villagers, and progression tests

- Reject invalid custom-feature definitions: unknown IDs, invalid/default
  generation-source flags, footprints outside limits, unsupported block/entity
  references, unbounded structure pieces, and missing cross-chunk metadata.
- Generate the same village with each neighboring chunk order and unloaded
  neighbors. Roads/buildings crossing every horizontal chunk edge render as
  one continuous structure, with no gap, duplicate face, false boundary wall,
  incorrect light seam, or duplicate whole-structure mesh.
- Interrupt a multi-chunk structure job after planning, candidate preparation,
  and partial chunk publication. Recovery must resume or roll back the same
  structure instance; villagers/loot/bosses remain inactive until the full
  manifest is complete.
- Verify a volcano is absent from default world generation and cannot be
  requested without its regeneration unlock. Accept a valid minimum 9-by-9
  contiguous chunk footprint; reject 8-by-9, disconnected, safe-zone,
  protected, or otherwise ineligible selections without partial mutation.
  Verify larger configured footprints, resource cost preview, fluid/lighting
  seams, and persistence too.
- The volcano boss spawns once only after the committed instance and arena are
  valid. Save/reload, regeneration retry, disconnect, and defeated-state
  replay cannot respawn the boss or duplicate its one-time reward.
- Villager jobs gather only configured resources inside their bounds, respect
  quotas/population caps, and deposit into village-owned stock. Tick-budget
  tests prove NPC decisions/pathfinding cannot monopolize world updates.
- Unauthorized taking from village stock emits one negative relation event;
  valid dust gifts debit the giver and credit the village/reputation together.
  Repeated requests, failed persistence, disconnects, and inventory/ledger
  exhaustion preserve item totals and cannot farm reputation.
- Village reputation, stock, NPC schedules, structure IDs, and progression
  flags survive save/load and chunk regeneration. No regeneration reroll can
  reset a theft penalty, repeat a gift reward, re-unlock an owned reward, or
  restore a depleted crystal deposit.
- First-time biome/structure discoveries, configured regeneration milestones,
  village reputation, and the volcano reward unlock only their configured
  cards/recipes/armor/weapon tiers. Repeated chunk generation is idempotent;
  server validation rejects locked recipes/equipment regardless of client UI.

## 17. Implementation sequence

### Phase 0 – validate the existing foundation

1. Inventory the existing `World` revision API, `M` preview, asynchronous
   generation pipeline, result-commit path, chunk/light/render publication,
   and their tests.
2. For each vertical-slice requirement, record **already implemented and
   passing**, **partially implemented**, or **missing**. Source/API existence
   alone is not proof of completion.
3. Reuse verified working pieces. Fix or adapt gaps at their current boundary;
   do not rebuild the preview, worker, or commit pipeline from scratch.
4. Record the current chunk serialization/revision contract, failure behavior,
   normal-build performance, and seeded output hashes.
5. Verify the exact Compression, Analytics, and CardGame APIs on the selected
   Libft branch and record the pinned CardGame revision. Specifically keep the
   500-card deck-code capacity separate from the current 128-card active
   match-state bound. Do not change the submodule pointer as part of this
   roadmap.

### Pre-Phase 1 – finish the minimum playable vertical slice

This is the first implementation gate, before the broader Phase 1 work. Its
purpose is to validate the central regeneration interaction as early as
possible, not to rebuild existing infrastructure or wait for the survival
economy. Use the Phase 0 inventory to reuse every slice component that is
already working and implement only the missing pieces.

1. Interact with one World Loom (a temporary/test-placed station is acceptable
   if the production block asset is not ready) and open the existing map/preview.
2. Show the default protected 3-by-3 region; select exactly one valid chunk
   outside it in a small test world.
3. Use one server-owned dust balance/resource ledger. Seed a small test balance
   or grant it through one explicit prototype reward; do not wait for the full
   inventory/drop/crafting economy.
4. Use the pinned Libft CardGame engine through the planned Minecraft adapter
   for one draw, one card play/effect, and the required deck/hand/discard
   transition. A minimal configured deck is enough; the full 20-card starter
   deck and editing are later Regeneration 1.0 work. The effect must resolve
   into a Minecraft-owned immutable policy intent, not write the world.
5. Preview, confirm, and regenerate the selected chunk through the existing
   authoritative request/worker/result-commit path wherever it passes Phase 0.
6. Keep the previous chunk drawable until the replacement block/light/mesh
   state is valid, then publish it atomically. Verify player content and
   protected neighbors remain unchanged.
7. Inject stale revision, rejected request, cancellation, and prepare/commit
   failure. The chunk and dust ledger must remain consistent; a retry must not
   charge or commit twice.
8. A simple placeholder avatar is sufficient here; final character assets or
   customization must not block this core interaction test.

The gate passes only when a player can complete this one-chunk loop repeatedly
and the tests prove preservation, rollback, idempotency, and atomic visual
publication. Do not begin broad survival implementation before recording this
result.

### Phase 1 – Regeneration 1.0

Expand the proven slice into a complete player-facing regeneration release:

1. Complete World Loom permissions, map/fog policy, multi-chunk selection,
   server-calculated 3-by-3 protection, preview tokens, and stale-revision
   feedback.
2. Add the configured dust acquisition/payment loop, exponential draw pricing,
   resource reservations/refunds, and a 20-card starter deck using Libft
   CardGame ordered zones, stable instance IDs, and deterministic shuffle
   state. Keep the Minecraft 20-to-100 ownership limit and deck transfers in
   the game adapter.
3. Add the Minecraft CardGame adapter, starter card catalogue, effect-ID
   registration, and conversion from successful card resolution into a
   validated immutable generation-policy intent. Keep the engine's card-match
   state separate from the external World Loom resource/world transaction.
4. Add effect precedence, deterministic resolved-policy digest, and safe
   preview/confirmation workflow. Card manufacturing can remain configured
   starter content at this milestone.
5. Harden bounded priority scheduling, per-chunk prepare/commit, cancellation,
   persistence/compression, and authoritative client replication.
6. Meet atomic block/light/mesh publication and strict frame/edit-latency
   gates. Analytics-on/off runs must yield the same world result.
7. Add the versioned custom-feature/structure planning interface, including
   default-world versus regeneration-only policy, complete chunk footprints,
   stable structure-instance IDs, and renderer-visible cross-chunk fragments.
   Production villages/volcano content comes later.
8. Provide a valid default player avatar for other clients; the full appearance
   customization flow is a Game 1.0 gate.

Regeneration 1.0 does **not** wait for the complete item catalog, crafting
stations, farming, health/hunger, combat, card-recipe experimentation, or all
four animal species. Dust and cards use the smallest authoritative contracts
needed for this milestone and can later be adapted to the full item economy.

### Phase 2 – Game 1.0 item and inventory foundation

1. Audit Libft Game's item/inventory/recipe APIs on the target branch; define
   Minecraft stable item IDs, block-item mappings, stack rules, and adapters.
2. Implement ground item spawn, pickup, merge, persistence, and replication;
   cover all registered blocks with the documented drop rules.
3. Add bucket-based water/lava transfer, grass spread, and the remaining
   persistent item/world interactions.
4. Establish configurable inventory capacity and transactional item transfers.

Backend API checkpoint (2026-10-22, source inspection only): Libft now has
revisioned inventory snapshots and staged add/transfer/craft transactions;
Minecraft has session-owned inventory, ground-item persistence/replication,
and request/revision-keyed craft actions. These are implemented foundations,
not completion of this phase. The world-wide physical-ID authority, complete
item acquisition policy, live interaction coverage, and supported-platform
runtime validation remain open.

Progress update (2026-10-10): The Minecraft item catalogue validates all 64
built-in block policies, the currently registered runtime rules, and 99
content definitions. World-item and session codecs persist physical item IDs;
session load rejects duplicate IDs across the character inventory and ground
items and advances the recipe output allocator above stored IDs. Ground-item
spawning and snapshot restore also reject repeated physical IDs within the
store. This is validated for those stores only. Runtime content-block registration,
bucket fluid transfer, live ground-item change replication, and a world-wide ID
registry remain open. Chunk subscription now sends a reliable bounded snapshot
of published ground items, including physical item identity and modifiers, to
the client runtime callback. This establishes initial per-chunk synchronization;
the bounded `WorldItemReplicaStore` can apply those snapshots transactionally.
Visible item-store mutations advance a change revision; the network tick refreshes
subscribed snapshots with a bounded round-robin budget. This carries spawn,
merge, pickup, and despawn updates using full chunk refreshes. Item-specific
deltas and reconnect/revision race validation remain open.

### Phase 3 – Game 1.0 crafting and survival loop

1. Add recipe-book crafting (no grid), station checks, workbench, furnace,
   fuel, ore smelting, and cooking.
2. Add paper plants, farming/grain/bread, apple/pear harvesting, and the
   configured food catalog.
3. Implement authoritative health and hunger; keep thirst optional and
   independently configurable.
4. Add the initial tools, armor, weapons, shields, staffs, and combat/magic
   rules, with balance and performance tests.

Backend API checkpoint (2026-10-22, source inspection only): recipe-book query,
discovery-grant, hand-craft, and station-craft entry points now exist in the
Minecraft session/catalogue path, backed by Libft recipe-book and prepared
craft transactions. Do not mark recipe-book crafting or station checks done
until the complete recipe/content set, placed-station ownership/reach rules,
save/recovery behavior, and runtime validators pass. Keep this backend progress
separate from the still-open farming, food, equipment, combat, and balance
deliverables above.

Combat integration status (2026-10-14): Journalled melee attacks now use the
catalog-resolved equipped main-hand rule through an identity-and-durability
keyed transient cache. This caches the complete equipment rule, including
attack and mining values; the mining transaction still resolves its own
requirements. Balance, full combat validation, and mining cache integration
remain open. This source change has not been compiled or tested.

Progress update (2026-10-10): The bootstrap recipe catalog has 88 registered
recipes, and validation enforces recipe-acquired output coverage. Furnace
recipes now require one coal per input and enter a bounded server-tick queue.
Inputs and fuel use the revision-checked Libft processing transaction; exact
start retries do not consume again, and full-inventory outputs remain pending.
Actual furnace ownership, station access, UI, unload behavior and crash-safe
coordination remain open. Queue values and retained start results now have a
version-1 catalog codec stored in combined session snapshot version 5; versions
1-4 remain readable. Strict compile passes, but runtime restore coverage remains
open.

### Phase 4 – Game 1.0 card manufacture and discovery

1. Add paper/card materials and the Card Press using the recipe-book model.
2. Add exploration blueprints, deterministic known recipes, and optional
   ingredient-constrained Research Synthesis with disclosed candidate pools
   and bounded effect tiers.
3. Connect crafted unique card items to both World Loom deck transfers and the
   card collection/minigame catalogue without changing stable definition,
   printing, or physical-instance IDs.

### Phase 5 – Game 1.0 Card Table minigame

1. Build the Minecraft adapter on the pinned Libft CardGame module; configure
   one explicit two-player profile with versioned phases, zones, resource
   rules, effect IDs, turn rules, usage limits, and a legal-card format.
2. Use the Libft format/hash and deck-code APIs for legality and sharing, with
   server-side checks for current corpus, card ownership, copies, and format
   revision. Keep booster rarity/odds and collection ownership in Minecraft.
3. Implement the initial Card Table UI for deck selection, opening draw,
   choices, legal actions, turn/phase status, effects, combat, result, and
   replay. Client UI proposes actions; the server submits sequenced commands
   and owns the authoritative match state.
4. Complete the intended 25-to-500-card format range only after Libft's active
   match-state capacity, snapshots/deltas, serialization, hashes, and replay
   support 500-card decks. Until that prerequisite lands, cap enabled formats
   to the supported engine capacity and label larger formats unavailable.
5. Add optional minigame rewards through a separate idempotent reward ledger;
   never let match/replay retries duplicate dust, cards, unlocks, or gear.


### Cross-cutting Game 1.0 world persistence and admission gate

Before calling any later Game 1.0 phase complete:

1. Ship world creation, multiple-world save/load, autosave, backup/restore,
   import/export, crash recovery, migration, and corruption handling.
2. Ship `WORLD_BOUND` and `PORTABLE` character profiles with a server-owned
   portable-state split and per-character transfer lease.
3. Ship world admission policies for world-bound-only, portable-allowed,
   approval-required, and fresh-character-only worlds, with clear UI previews
   and transactional join/leave behavior.
4. Pass the save, reload, duplicate-join, policy, migration, and no-duplication
   validators in section 6.5 before enabling public multiplayer worlds. Include
   the card-deck, inventory, recipe-unlock, active World Loom session, and
   legacy-scope cases; a session lease alone is not proof of durable
   no-duplication across restore/rollback.

### Phase 6 – Game 1.0 player avatars and customization

1. Add the final block-style player rig/model, default appearance, and
   authoritative pose/animation replication.
2. Add the player appearance UI and saved, validated customization profiles.
3. Confirm armor, tools, shields, bows, and staffs display on the model without
   changing gameplay collision or combat authority.

### Phase 7 – Game 1.0 neutral fauna

1. Complete authoritative entity ownership, persistence, spawn/despawn, and
   renderer submission; implement sheep as the first species vertical slice.
2. Add sheep drops/breeding/maturation, then cows, pigs, and chickens with
   configured behavior, feed, cooldowns, and drop tables.
3. Verify regeneration cannot duplicate, erase, or relocate protected
   creatures, eggs, drops, or breeding state.

### Phase 8 – Game 1.0 custom structures and regeneration progression

1. Add a configurable village feature pack with structure fragments that cross
   chunk borders and a bounded village population of job-configured villagers.
2. Add villager resource gathering/storage, theft reactions, and transactional
   positive reputation for gifts such as magical dust.
3. Add progression definitions that connect first-time regeneration outcomes,
   structure/biome discoveries, village relationships, and boss rewards to
   unlocks for higher card, armor, and weapon tiers.
4. Add the regeneration-only volcano content after its footprint planner,
   persistence, structure renderer, and boss-combat tests pass. Enforce the
   minimum 9-by-9-chunk footprint and do not add it to default generation.
5. Preserve structure instance state, village stock/reputation, progression
   unlocks, and one-time boss rewards across regeneration and retries.

### Phase 9 – broader content and extension

1. Add richer draw/discard, bonus-scope, feature, and combination cards.
2. Add further structure definitions and progression content through config,
   without adding per-structure special cases to the chunk renderer.
3. Add optional animal species only after entity and performance gates pass.

Do not skip the normal-build tests between phases. Do not call the design
implemented merely because the preview UI or generator helper works in
isolation.

## 18. Acceptance criteria

### 18.1 Pre-Phase 1 vertical-slice gate

The vertical slice is a small internal playable milestone, not either full
release. It is accepted when:

- the Phase 0 inventory identifies existing working pieces and the slice
  reuses the current revision preview, asynchronous generation, and commit
  path wherever tests prove them ready;
- one World Loom interaction opens the map, marks the protected 3-by-3 area,
  and lets the player choose exactly one valid chunk outside that area;
- one configured dust balance and one configured regeneration card can pay
  for, preview, confirm, and complete that chunk's regeneration;
- the authoritative result is prepared asynchronously and the prior drawable
  block/light/mesh tuple stays visible until a valid replacement is atomically
  published;
- protected/player-authored content remains unchanged, and stale revision,
  cancellation, duplicate request, and injected prepare/commit failures leave
  world and resource state consistent;
- repeated end-to-end tests complete without duplicate charge, duplicate
  commit, black/partial chunk publication, or render-thread generation.

The slice may seed dust and a minimal deck through explicit development
configuration. It does not require the general item economy, card discovery,
or a final character model.

### 18.2 Regeneration 1.0 release gate

Regeneration 1.0 is accepted when the complete World Loom loop is reliable and
playable, without requiring the broader survival package:

- players can interact with an authorized World Loom, inspect the configured
  map/fog view, choose eligible chunks, pay dust, draw/play cards, preview the
  effect, and confirm a server-authoritative job;
- the 3-by-3 safe zone and all ownership/protection rules are enforced on the
  server and cannot be bypassed with cards or crafted requests;
- the configured dust source/payment ledger, geometric draw pricing, unique
  card identities, 20-card starter deck, 20-to-100 deck bounds, persistence,
  idempotency, cancellation, and refunds pass boundary and failure-injection
  tests;
- the server-side selection API supports exact select/unselect, revisioned
  cost quotes, and an opening hand of four through seven cards whose total
  price doubles at each increment; quote and commit races, stale selections,
  insufficient funds, and retries cannot partially reserve, charge, or deal;
- starter cards have validated effects, deterministic policy resolution,
  preview warnings, and a versioned configuration digest; full Card Press
  manufacturing is not required for this gate;
- the World Loom uses Libft CardGame for card definitions, unique instances,
  deck/hand/discard transitions, deterministic shuffling, and effect dispatch;
  Minecraft still owns dust, chunk selection, safety, policy intents and world
  commit, with no mutable world access from card callbacks;
- selected chunks and connected clients converge through authoritative
  revisions, with persistence/compression recovery and stale-result handling;
- the versioned custom-generation contract can validate default-world versus
  regeneration-only features, full multi-chunk footprints, and per-chunk
  structure continuation metadata without requiring a finished village or
  volcano content pack yet;
- generation, lighting, mesh creation, compression, and persistence do not
  block the render thread or hold world locks while doing expensive work;
- old chunk block/light/mesh state remains visible until a complete valid
  replacement commits; stale/failing results leave it unchanged;
- water/lava generation and chunk-boundary tests prove no floating/leaking
  features, even though portable bucket gameplay may be part of Game 1.0;
- every player has a valid default rendered avatar where other players are
  shown; full model customization is a Game 1.0 gate;
- the normal and analytics builds produce identical deterministic world
  results, strict latency budgets pass, and the selected Libft
  compression/analytics branch builds and passes required tests without
  relying on unmerged `very-real-engine-checkout` changes.

### 18.3 Game 1.0 release gate

Game 1.0 includes Regeneration 1.0 plus the broader survival and player
identity systems. It is accepted when:

- every registered current block has a valid standalone item/drop policy,
  including grass-to-dirt, and ground items support transactional drop,
  pickup, stacking, save/load, and replication;
- water and lava exist as world fluids and can be legally transferred using
  empty, water, and lava buckets under server validation;
- the ocean biome has deterministic depth, floor, shoreline, lighting, and
  chunk-boundary rules; craft-only Shipwreck Charts can create only validated
  ocean shipwrecks and never place them on land;
- boats can be crafted, placed, boarded, piloted, saved, recovered, and
  replicated transactionally, including ordinary water access to ocean
  structures;
- craft-only Ruined Village Relics create bounded hostile spawners and rare
  loot, with deterministic loot quality increasing monotonically from the
  affected chunks' resulting instability and never rerolling on retry;
- craft-only Shipwreck Charts increase affected-chunk instability, create only
  bounded ocean-valid underwater mobs, and scale deterministic loot quality
  monotonically from the resulting instability without duplication;
- craft-only Desert Pyramid Seals generate only in valid desert footprints,
  use sandstone construction with bounded hostile spawners and a central loot
  chamber, and scale both danger and rewards monotonically from instability;
- craft-only Ancient Canopy Seals generate only in forests, use ladder and
  trapdoor traversal through a dark giant-tree interior, and provide bounded
  spawner danger and instability-scaled rewards;
- craft-only Spider Nest Seals generate only when connected to a valid cave
  system, use bounded spider spawners and one-time loot-bearing corpses, and
  scale danger and rewards monotonically from instability;
- the recipe book, workbench, furnace, fuel, smelting, cooking, inventory,
  paper/card materials, Card Press recipes, and deck transfer preserve item
  counts transactionally;
- exploration unlocks and ingredient-constrained Research Synthesis discover
  cards; known recipes craft exact cards, while random results are bounded by
  their disclosed tier, committed once, persisted, and replay-safe;
- the Card Table offers a complete server-authoritative two-player minigame
  profile using Libft CardGame, with Minecraft-owned card sets/collection,
  deck legality, user interface and effect definitions; shared deck codes are
  validated against the selected format and owned cards;
- advertised minigame formats support the intended 25-to-500-card deck range
  in active match state, snapshots/deltas, persistence and replay, not merely
  in the deck-code codec. Unsupported larger formats are not shown as playable;
- live match views and shared replays preserve hidden-zone privacy, while match
  results and any rewards are settled through Minecraft's authoritative
  idempotent session/reward records. Genre-inspired samples are not described
  as full Hearthstone, Magic or Yu-Gi-Oh! rules implementations;
- health and hunger are authoritative and persistent; farming, grain/bread,
  fruit, raw/cooked foods, and optional thirst follow configured rules;
- underwater breath is authoritative and persistent, refills at the surface,
  and causes recurring health damage after the configured 60-second default
  until the player surfaces or dies;
- starter tools, armor, weapons, shields, staffs, and combat/magic profiles
  have explicit recipes, balance configuration, and server validation;
- players have a rendered character model and can customize an allowed,
  persistent appearance profile; appearance cannot change collision, reach,
  movement, damage, or other authoritative gameplay stats;
- players can create, save, resume, back up, restore, import, and delete
  multiple worlds safely; interrupted or corrupt saves recover to the last
  known-good snapshot;
- characters explicitly use either `WORLD_BOUND` or `PORTABLE` ownership, with
  world-owned and portable state kept separate and protected from duplication;
- each world exposes versioned admission rules for world-bound-only,
  portable-allowed, approval-required, and fresh-character-only access, plus
  clear controls for what a portable character may bring;
- the custom-structure framework renders a village continuously across chunk
  borders; villagers have bounded gathering jobs, village-owned storage,
  negative responses to unauthorized taking, and persistent positive
  reputation for accepted gifts such as magical dust;
- regeneration is a real progression path: validated discoveries/unlocks
  grant access to higher card, armor, and weapon tiers. A late milestone adds
  a regeneration-only volcano with a minimum 9-by-9-chunk footprint, a
  persistent boss encounter, and unique one-time rewards; it never appears in
  default world generation;
- sheep, cows, pigs, and chickens have staged but complete spawn, simulation,
  persistence, replication, rendering, drops, and controlled breeding before
  they are described as shipped features;
- the tests prove regeneration cannot duplicate, erase, or relocate protected
  animals, eggs, drops, breeding state, or player-built content.

### 18.4 Shared quality and performance gates

Both releases must pass the relevant existing unit, integration, sanitizer,
failure-injection, persistence, networking, and performance suites. Report
latency distributions (including p50/p95/p99 and worst observed), enforce the
budgets in this document, demonstrate bounded queues/memory, and verify
analytics-on/off equivalence. A passing prototype must not be reported as a
complete release if its milestone's own criteria remain unmet.

## 19. Non-goals for the first vertical slice

- Rebuilding already-working revision preview, generation worker, or result
  commit code merely to match a new plan; Phase 0 decides what to reuse.
- Requiring the full item catalog, inventory, crafting, smelting, farming,
  health/hunger, combat, four animal species, or card manufacturing before the
  one-chunk slice can be tested.
- Treating the regeneration deck or its active hand as normal inventory; card
  items outside the deck are the transferable inventory objects.
- Reimplementing Libft's generic card-game engine in Minecraft; Minecraft
  supplies the content, game-specific profiles, adapter, UI and world-facing
  behavior instead.
- Requiring the separate Card Table minigame before the one-chunk slice or
  Regeneration 1.0; it is a Game 1.0 feature and uses an isolated match.
- Allowing clients to regenerate terrain or choose their own seed, regenerating
  protected chunks, or silently deleting player content.
- Sending chunk meshes over the network or running analytics, compression,
  disk I/O, lighting propagation, or full mesh rebuilds in the render frame.
- Requiring final player-model assets/customization to pass the minimum slice;
  a temporary placeholder is acceptable there.

Regeneration 1.0 also does not require the complete survival economy, animal
breeding, the separate Card Table minigame, combat progression, experimental
card crafting, or customizable avatars, nor does it require finished
village/villager or volcano/boss content.
The versioned custom-structure and cross-chunk renderer contract is required
in Regeneration 1.0; those specific content packs remain explicit Game 1.0
work rather than being removed from the roadmap.

## 20. World items, inventory, and recipes

### 20.1 Current gap and item data model

There is no complete Minecraft-owned ground-item drop/pickup system in the
current source tree. A first in-memory entity-store slice now provides
chunk-indexed item stacks, nearby queries, owner pickup locks, and a
revision-checked inventory transfer with actor/request deduplication and an 8-block
reach ceiling. Exact retained requests now replay terminal failures as well as
successes, and changed parameters are rejected. Each transfer attempt uses a
scoped Libft transaction ID. `--validate-world-items` covers partial pickup,
success/failure replay, changed-payload rejection, and preservation of the
remaining world stack. A versioned, checksummed value snapshot now round-trips
entity state and retained request outcomes, with corruption rollback covered by
the validator. GameSession now stores the character/inventory buffer and ground-item snapshot together in one checksummed session file, keyed by SHA-256 of the seed under saves/world_items and written through atomic replacement. Its validator confirms the character stats, inventory and ground entities round-trip together and rejects a corrupted combined file without changing restored character state. Chunks, card state, generation configuration and a shared live transaction recovery journal are not included. Save errors are logged and exposed in session status, but shutdown currently continues after a failed save. The request
cache is bounded and FIFO-evicts after 4,096 entries, so durable retry
retention is still open. The single-player simulation collects nearby drops within 1.5
blocks. The break interaction creates drops for configured built-in block
rules and compensates by removing the drop if the block remains after a failed
edit. This does not yet provide a durable atomic
block/drop transaction. Item rendering, network replication, item movement, chunk/card-session
persistence, generation-configuration binding, and live cross-system crash
recovery remain open. A revision-checked `merge_stacks` API now merges
compatible ordinary ground stacks within one block, and successful block drops
attempt to merge into a compatible nearby entity after the block edit commits.
Owner-lock metadata is preserved and unique instances are rejected. Each world
entity enforces one internal stack at spawn and restore. Nearby queries cap
examined entities at 1,024 and returned snapshots at 256; overflow leaves the
caller's output unchanged, and a drop remains separate if the query is full.
The validator covers the shared nearby target selector, partial transfer, full-target no-op, stale revisions and query overflow; the `--validate-world-items` gate now executes a downward raycast, deletes a loaded dirt block, and verifies its staged drop merges into a nearby stack. The broader `--validate-block-edit` suite still times out later in emissive-delete region convergence. Pending world-item entities are now hidden from queries/pickup and block mining publishes a staged drop after the voxel edit result is known; publication retries are idempotent, and serialization refuses to save while such a pending transaction exists. This closes the live pickup race, not crash consistency. Libft's Game module has item-definition, inventory, and
recipe/crafting building blocks, but these do not by themselves provide
Minecraft world entities, rendering, collision, station discovery, or the
server transaction workflow. Audit those APIs on the selected Libft branch
before deciding what to reuse; avoid creating a second incompatible item
catalog if the existing definitions meet the needs.

Current integration groundwork registers one acquisition rule for every
built-in voxel block, uses stable numeric IDs for the block-item range,
resolves grass to dirt, treats fluids as container collection, and keeps
bedrock admin-only. The session also initializes the Libft item-definition
catalog and configures the character inventory to 30 slots without a weight
cap. Magical dust and empty/filled bucket definitions are registered. The
catalog accepts explicit rules for registered runtime block IDs and can
validate coverage against the live runtime registry, but content-specific
runtime rules are not registered yet. `--validate-item-catalog` and
`--validate-all` now exercise built-in and live runtime registry coverage plus
the key air/grass/fluid/bedrock acquisition policies; the catalog validator
reports full built-in policy coverage and zero production runtime rules; the
last recorded run had 64 built-ins, lava registration raised the count to 65,
and flowing-fluid identities now raise it to 67 pending validator rerun. It now
also registers a synthetic runtime block to prove missing-rule rejection and
successful self-item rule coverage before unregistering it. Production
content-specific runtime rules are still not registered. This exposed a
Libft `ft_map` growth bug: insertion doubled capacity for every new key,
eventually preventing later item definitions from being stored. Both copy and
move insertion now grow only when the map is full. Libft character saves now
serialize inventory from a locked, value-only snapshot, including physical
item IDs and item modifiers; older saves without instance IDs load with ID 0. The Libft inventory transfer API now offers a transaction-ID overload with an explicit scope and a bounded, volatile 4,096-entry replay cache; the compatibility overload has no deduplication contract. Focused transfer regressions pass 12/12, including scoped-ID isolation and fail-closed capacity behavior. `WorldItemStore::pickup` now uses the scoped transfer API for each exact quantity attempt, while its actor/request cache handles pickup retries. Both caches are process-local, and the pickup cache evicts old request IDs. Saved-world integration still needs durable inventory recovery and atomic coordination with mining/drop transactions. `GameSession` now exposes hand-only recipe query/craft actions through `MinecraftRecipeCatalog`; actions require the expected inventory revision and use a bounded 4,096-result session request cache whose high-water mark rejects evicted IDs. The item-catalog validator passes a successful craft, exact retry, changed-payload rejection, and stale-revision retry through this API. The hand-craft cache is stored in combined session snapshot version 5 alongside processing-job state; versions 1–4 remain readable, with older files loading empty action/job caches. Hand crafting still uses station 0. Furnace jobs use the timed queue and save codec, but placed-furnace ownership/reach checks, station UI/network transport and crash-safe resource coordination remain open. Durable mining/drop transactions, the broader recipe action surface, and content-specific runtime policies also remain open.

Backend caller-API follow-up (2026-10-22; design only): the current GameSession
recipe query/craft endpoints require an expected inventory revision, but the
query result does not include that revision, and GameSession has no public
immutable inventory-view/list-query API. The combined
`MinecraftRecipeCatalog::query_recipes` already exists internally, but returns
no inventory/catalogue revision and reads the live inventory once per recipe.
Libft's `game_crafting` currently has no snapshot query overload. Add an
actor-scoped value inventory snapshot and a generic snapshot-based crafting
query, then expose the existing catalog aggregation through an authorized
GameSession call. All result rows must use one inventory snapshot and report
the scoped revision tuple: world/session epoch, inventory revision,
character-state revision, catalogue revision, and station identity/revision
when applicable. Zero is invalid; revisions must not wrap or be compared
across epochs. A query that cannot hold one coherent tuple returns stale state
with no partial rows, and the mutation revalidates the tuple at commit. Craft
mutations bind the request ID and exact revisions, return structured results,
replay exact retries and reject changed-payload ID reuse. This is a backend API
requirement, not a UI task; see the [Libft integration design](../Libft/Docs/card_game_scripting_inventory_crafting_integration_design.md#9-recipe-book-api-and-unlocks).

Define an immutable item definition with a stable `item_definition_id`,
display/localization key, icon/model key, stack limit, category/tags, weight
(optional metadata only; not a mandatory carrying mechanic), and relevant
durability/food/equipment/card metadata. A stack is `(definition_id, count,
validated stack metadata)`. Unique equipment and card instances also carry a
stable instance ID and instance-specific state. Do not use names or pointers
as identity.

All item mutations are server-authoritative and transactional. An inventory
has a revision. A request names the expected revision and a request ID; the
server validates capacity, stackability, ownership, and duplicate handling
before changing it. A failed operation leaves the complete inventory and
world-item state unchanged.

### 20.2 Ground item entities

A dropped stack is a lightweight world entity with a stable entity ID,
definition/stack data, position and velocity, spawn tick, owner/pickup-lock
expiry when applicable, and persistence version. It is not a block and must
not be stored inside a chunk's block palette.

The server owns spawn, movement/collision policy, stack merge, pickup, and
despawn. The client receives spawn/state/despawn messages and interpolates for
rendering only. A pickup transaction validates player reach, current entity
ID/revision, item definition, available inventory capacity, and any owner lock.
Partial pickups reduce the world stack and add exactly the same quantity to
inventory in one transaction. Full pickups remove the world entity only after
the inventory addition is prepared. Duplicate pickup requests return the
original result.

Compatible nearby stacks may merge only when definition and all stack-relevant
metadata match and the merged count stays within the configured stack limit.
Use a short server-configured pickup lock after a player drops an item, then
allow ordinary pickup. Despawn age and unloaded-chunk behavior are explicit
per item category; valuable drops cannot disappear just because a chunk
temporarily unloads. Persist or deterministically recover each entity exactly
once across save/load and server restart.

The client needs a minimal visual representation, pickup feedback, and a
bounded nearby-item render list. Do not scan every world item every frame;
index items by chunk and query only nearby candidates. Item movement/rendering
must not share mutable entity storage with chunk generation workers.

### 20.3 Inventory size and economy tuning

The inventory should feel roomy enough to gather and craft, but not like a
portable warehouse. Start with a configurable slot-based inventory; a candidate
initial layout is 30 carried slots total, including a 9-slot quickbar, with
per-item stack limits. Equipment slots are separate and storage containers
provide expansion in the world. Do not add a weight meter in the first pass:
stack and slot limits are simpler to teach and balance. The exact slot and
stack values are provisional and must be tuning data, since the complete item
economy is not yet known.

When crafting or moving items, compute the full result before changing any
slot. Recipe-book output should be disabled or show a clear capacity message
when the output cannot fit. Support explicit partial pickup when some but not
all items fit; never silently delete the remainder. Future storage expansion
must not change item identity or serialized stack semantics.

### 20.4 Recipe-book crafting, no grid

Crafting is selected from a recipe book. The player chooses a recipe and count;
the system checks required ingredients in the player's inventory and checks
that the player is using the required station when the recipe calls for one.
There is no placement of ingredients into a 2D grid and no recipe inferred
from a grid pattern.

Each recipe definition contains:

```text
recipe_id and version
ingredient requirements: item ID/tag + quantity
output item ID(s), quantities, and instance-generation rules
required station type (or NONE for hand-craft)
unlock/discovery policy
craft duration, if any
required tools/consumed catalysts, if any
```

Libft `game_crafting` now has a station-aware backend boundary: a recipe can
be assigned one immutable station ID, and both recipe queries and prepared
crafts enforce that ID. Legacy calls use station 0 and fail closed for
station-bound recipes. This does not validate a world station, player reach, or
station revision; those checks and atomic request deduplication remain in the
Minecraft host.

The recipe book lists known recipes, ingredient quantities, output, required
station, and which ingredients/station are missing. Showing undiscovered
recipes is a world option; by default, reveal recipes through progression,
found schematics, or initial basic knowledge. The interface may offer craft
one/craft maximum but always previews the exact ingredient/output delta.

The server accepts `(request_id, recipe_id, count, inventory_revision,
station_id, station_revision)`. It verifies range, station type/state,
unlock, ingredients, tools, output capacity, and limits; reserves output
capacity; then atomically consumes inputs and creates outputs. A duplicate
request ID returns the stored result. A disconnect or persistence failure
cannot consume inputs without producing or reserving the output.

### 20.5 Crafting stations and progression

The first stations are:

| Station | Main role | Example operations |
|---|---|---|
| Hand | Simple recipes with no station requirement | Basic components, starter recipes |
| Workbench | Gear and durable crafted items | Pickaxes, axes, swords, armor, repair components |
| Furnace | Time-based processing with fuel | Ore to bars, raw meat to cooked meat, later glass/brick |
| Paper Mill or equivalent | Process plant fiber into paper | Paper sheets and card stock |
| Card Press | Craft known card recipes exactly or perform a clearly separated configured research experiment | Paper/card stock, pigment/ink, effect-family catalysts, and tiered materials |
| World Loom | Manage the separate regeneration deck and run regeneration | Insert/remove crafted card items, inspect deck zones, map/preview/confirm |

Station access is checked server-side: station exists, is in range, is usable,
and is not broken/claimed/inaccessible. The UI can open remotely only if an
explicit later rule permits it. Station recipes may use Libft Game's recipe
blueprints if their current contract supports the required ingredients,
station gating, transaction, and error behavior; Minecraft still owns world
reach/permission validation and UI.

### 20.6 Furnace, smelting, and cooking

A furnace stores input, fuel, output, and progress as persistent server state.
Coal is the initial example fuel; fuel type and burn duration are configured.
Ore input plus valid fuel produces the corresponding metal bar after a
server-tick duration. Raw meat and other supported foods can be cooked into
their cooked item definitions. The recipe book describes what a furnace can
process, while the furnace UI exposes its actual slots/progress; this is not a
crafting grid.

Starting a process reserves its input and fuel. Progress advances only under
the server simulation clock. On completion, output is committed if capacity is
available; otherwise the process remains safely complete/pending and retains
its input/progress. Save, unload, shutdown, and failure cannot consume fuel or
ore twice. The first implementation need not simulate realistic heat or fuel
physics; use deterministic configured durations and explicit fuel units.

Bars are ingredients for progressively better pickaxes, axes, swords, and
armor. Each item definition declares mining tier, durability, damage/defense
properties, and allowed equipment slot. The game config controls material
tiers and recipes; the crafting engine checks requirements but does not hard-
code a specific game's progression. An invalid tool must not bypass block
mining restrictions, and equipment stats are validated by game rules.

### 20.7 Paper plants, card materials, fruit, and food items

Add a farmable grain crop (working name: **wheat**). Players prepare farmland,
plant seeds, and wait for the crop to mature through a small, visible number of
growth stages. Mature harvesting yields grain/wheat plus replanting seeds;
harvesting an immature crop gives little or no grain and may return fewer
seeds. Growth, yield, soil, hydration, and light requirements are
configuration data. A recipe-book entry converts three grain into one bread
without requiring a crafting-grid arrangement. Grain is also a configured
feed for sheep/cows; it is not directly edible in the first food catalog.

Crop growth is an event/tick system, not a per-frame scan of every farmland
block. Track crop state by chunk and next growth opportunity; only loaded,
eligible chunks need active updates. On unload, persist a bounded growth
deadline and apply a capped offline-growth policy on reload. Harvest is a
server transaction against the crop's current maturity/revision and must not
duplicate grain or seeds if the request is retried.

Add a renewable reed-like **paper plant** (working name: paper reed) that can
grow near suitable wet ground. Harvesting yields paper-reed fiber. Use a
simple, explicit processing chain:

```text
paper-reed fiber + water -> paper sheet       (Paper Mill)
paper-reed fiber -> plant-fiber binder        (Paper Mill)
paper sheets + plant-fiber binder -> card stock (Paper Mill)
brown mushroom + water + binder -> dark ink   (Paper Mill)
red/yellow flower pigment + water + binder -> colored ink (Paper Mill)
crushed mineral + water + binder -> advanced pigment/ink (Workbench/Paper Mill)
```

The exact quantities and whether a recipe consumes water or returns its
container are configured. The brown mushroom is the common early dark-ink
source; red/yellow flowers provide early colored pigments; selected mineral
items (such as amethyst, quartz, or amber) can provide later colors/effect
catalysts. A configured wood-to-charcoal recipe may be added later, but basic
ink must not depend on fuel availability or rare minerals. Keep paper-sheet,
card-stock, pigment, ink, and binder as distinct item definitions only where
they have a real recipe or gameplay role. Basic ink and card stock must be
reachable from renewable/common materials; rare materials gate
stronger/specialized recipes rather than basic participation.

Regrowth is deterministic and bounded so the paper plant is a renewable source
rather than a one-time world-generation exploit. Biome, soil, water, growth,
yield, and processing rules are configuration data. Crop/plant processing is
server-tick and station-driven, never a per-frame scan.

Card recipes use named material roles instead of arbitrary hidden combinations:

- **Substrate:** card stock is required for every physical card and sets the
  base recipe tier/quality.
- **Ink/pigment:** supplies the card's visible motif and may constrain the
  configured experiment pool, but cosmetic color alone must not secretly add
  combat or generation power.
- **Effect-family catalyst:** a tagged ore/crystal, biome block sample, seed,
  or validated fluid sample biases or selects the card's effect family. Its
  item tags are explicit configuration, not inferred from its display name.
- **Stabilizer/binder:** plant fiber/starch is sufficient for basic cards;
  selected quartz, amethyst, amber, frost crystal, or shimmer-stone materials
  can be required for higher tiers and bound their effect budget.

The Card Press/Research Synthesis UI presents these roles and ingredient
choices in the recipe book; it is not a crafting grid. A fluid catalyst, if
allowed, consumes only the configured amount and returns the empty bucket in
the same transaction. Do not consume magical dust by default; see Section 7.5.

The Card Press combines paper/card stock with configured pigment/ink and
materials. A known recipe names and deterministically creates its exact card.
An optional Research Synthesis action is a distinct, explicitly uncertain
discovery path: selected ingredient categories constrain and bias a bounded
candidate pool, while the server records the result and reveals its recipe for
future exact crafting (see Section 8.5). The output is always a unique card
item instance. Crafted cards can be kept in inventory, inserted into the World
Loom deck, or removed from the deck back to inventory, but the item cannot be
duplicated by crafting retries or deck-transfer retries.

Apple and pear trees provide harvestable fruit as a world-generation/tree
feature. Picking ripe fruit should not require destroying the tree. Species,
yield, regrowth interval, and biome suitability are configurable; a harvested
fruit item is server-owned and can be eaten or used in future recipes.

Neutral animal drops include raw meat and other useful materials through
per-species drop tables. Raw meat can restore a modest configured hunger amount
or have another explicit tradeoff; cooking it in a furnace creates a more
effective food. Do not add random harmful effects in the first survival pass.
Drop counts and probabilities are deterministic for a server event and are
committed exactly once with the animal's death state.

### 20.8 Complete item catalog, block drops, grass spread, and fluid buckets

Every gameplay item is a registered, stable item definition that exists
independently of the inventory UI. It can be represented in an inventory,
spawned as a persistent ground-item entity, transferred through a recipe, or
referenced by a block-drop rule. Block IDs and item IDs are different types of
identity: a block item explicitly names the block it places, while a fluid
bucket carries a fluid kind and quantity. Never infer an item ID by reusing a
block ID.

#### Existing block-item coverage is exhaustive

The current built-in Voxel registry contains 67 IDs including air (therefore
66 non-air built-ins). Audit and test against the canonical runtime registry,
not a hand-maintained partial list. As of the registry inspected for this
roadmap, the non-air names are:

| Group | Existing built-in blocks | Default drop item |
|---|---|---|
| Soil and terrain | `voxel:grass`, `voxel:dirt`, `voxel:sand`, `voxel:gravel`, `voxel:clay`, `voxel:coarse_dirt`, `voxel:podzol`, `voxel:mud`, `voxel:red_sand`, `voxel:sandstone`, `voxel:terracotta`, `voxel:salt`, `voxel:wet_sand` | Same block item, except grass drops dirt |
| Common and regional stone | `voxel:stone`, `voxel:bedrock`, `voxel:permafrost`, `voxel:canyon_rock`, `voxel:slate`, `voxel:moss_rock`, `voxel:granite`, `voxel:andesite`, `voxel:diorite`, `voxel:obsidian`, `voxel:mossy_stone`, `voxel:cracked_stone`, `voxel:limestone`, `voxel:basalt`, `voxel:frozen_stone`, `voxel:chalk`, `voxel:volcanic_rock`, `voxel:quartz`, `voxel:amethyst`, `voxel:amber`, `voxel:frost_crystal`, `voxel:shimmer_stone` | Same block item |
| Ores | `voxel:coal_ore`, `voxel:iron_ore`, `voxel:gold_ore`, `voxel:diamond_ore`, `voxel:emerald_ore`, `voxel:copper_ore` | Same ore-block item; the furnace may consume that item to make its configured bar/material |
| Wood and plants | `voxel:shrub`, `voxel:oak_log`, `voxel:oak_leaves`, `voxel:cactus`, `voxel:pine_log`, `voxel:pine_leaves`, `voxel:birch_log`, `voxel:birch_leaves`, `voxel:red_flower`, `voxel:yellow_flower`, `voxel:tall_grass`, `voxel:fern`, `voxel:dead_bush`, `voxel:red_mushroom`, `voxel:brown_mushroom`, `voxel:mushroom_stem`, `voxel:lily_pad`, `voxel:seagrass`, `voxel:ladder`, `voxel:trapdoor` | Same block item |
| Cold-region blocks | `voxel:snow`, `voxel:ice`, `voxel:packed_ice`, `voxel:packed_snow` | Same block item |
| Fluid | `voxel:water`, `voxel:lava` (source); `voxel:flowing_water`, `voxel:flowing_lava` | Source cells are reserved for bucket transfer; flowing cells have no direct item |

`voxel:air` never drops. The explicitly required exception is
`voxel:grass` – `voxel:dirt` item. Every other existing non-air, non-fluid
block has a self-drop mapping by default, including decorative plants and
ore blocks; a future exception must be an explicit, reviewed drop-table
override rather than a missing mapping. `voxel:bedrock` keeps its self-drop
mapping for creative/admin or future configured mining, while remaining
unbreakable in ordinary survival until the game rules deliberately say
otherwise. Fluids are handled by the bucket rules below, not by making a
floating fluid-block item.

The registry validator is the source of truth: enumerate every built-in and
runtime block ID at startup/test time and require exactly one declared policy:
placeable self-drop, explicit alternate drop, non-dropping technical block,
or fluid handled by a container. For every block item, validate that its
`places_block_id` resolves to the intended block. New registered blocks must
fail content validation if their drop policy was omitted. This prevents a
new biome block from silently becoming unobtainable or duplicating an item.

#### Item definitions required for the first playable economy

This is the minimum planned catalog, grouped by purpose. Exact quantities,
stack limits, yields, and recipe costs remain game configuration and are
balance values–not hard-coded assumptions in the inventory or recipe engine.

| Item family | Required item definitions |
|---|---|
| Block items | One placeable item for every applicable current block above; new placeable farmland, mature/plantable crop or seed representations as needed, paper-plant block, workbench, furnace, Paper Mill, Card Press, World Loom, and later storage blocks. A block drop references an item definition; it does not create a bespoke item type per drop event. |
| Fluid containers | Empty bucket, water bucket, lava bucket. The empty bucket is crafted from configured metal bars at a workbench. Filled buckets have stack limit 1; an empty bucket may stack only if its item-state representation is identical. A filled bucket is a single fluid-container item with `fluid_kind`, fixed capacity, and no arbitrary client-writable payload. |
| Water vehicles | Boat item and authoritative boat entity. Boats use configured wood/plank recipes, carry bounded passengers, navigate water and shore transitions, and persist their owner/state/position without becoming duplicated item entities. |
| Wood and basic materials | Oak/pine/birch logs and leaves as block items; configured planks and sticks as processed items; stone and gravel block items; coal/fuel; plant fiber; paper reed fiber; paper/card stock; configured ink/pigment/binding material. |
| Regeneration resource | Magical dust as a stable item/resource ID, refined from configured crystal block items and optionally found in exploration caches; depositing it transfers value exactly once into the World Loom/player regeneration ledger. |
| Mining and metal progression | Coal, iron, gold, copper, diamond, and emerald ore-block items matching the existing blocks; smelted iron/gold/copper bars (only where a recipe is defined); configured crystals/gems; pickaxes, axes, swords, and their recipe components. Ore-block items remain collectible even if a required tool tier controls whether they can be mined. |
| Farming and food | Grain/wheat item, seed item, bread, apple, pear, raw and cooked beef, raw and cooked pork, raw and cooked chicken, raw and cooked mutton, and any configured animal feed. Each edible definition declares hunger and saturation values; no food behavior is inferred from its display name. |
| Animal materials | Wool, leather, feathers, eggs, and species-specific raw meat items from configured sheep/cow/pig/chicken drop tables. Death/drop generation is one authoritative transaction; breeding consumes configured feed and produces an entity, not a duplicated inventory item. |
| Hostile mob loot | Rotten flesh, bones, gunpowder, string, and spider eyes from configured zombie/skeleton/creeper/spider tables. Each table defines bounded chance and quantity ranges and uses the persisted death-event identity for deterministic, exactly-once delivery. |
| Tools, weapons, and armor | Wooden hoe; pickaxe, axe, sword, bow, arrows, shield, staff, and cloth/leather/iron armor pieces for each configured equipment slot. Definitions carry mining tier, durability, attack/defense/style values, and stack/instance rules. Unique or durable equipment uses item instances; hoe use is validated by the farmland-tilling action. |
| Regeneration cards | The starter and later card definitions from Section 8 as unique card items. A card is an item while carried and transfers transactionally into or out of the separate World Loom deck; deck membership is not another copy. |
| Consumables and progression | Configured health-recovery and future focus consumables only when recipes and balance define them. Do not add placeholder items that have no gameplay use merely to pad the catalog. |

Any item that can exist in the world must have a stable ID, stack policy,
save/network representation, pickup/drop policy, and clear ownership rules.
Recipe outputs, block drops, mob drops, bucket transfers, and deck transfers
must all use the same item definitions. UI labels/icons are presentation;
they are never authoritative item identity.

#### Boats and water traversal

Boats are the first water vehicle and are required for ocean exploration,
including access to above-water instability towers. A boat has a
stable entity ID, owner/session state, position and orientation, passenger
slots, damage/destruction state, and a corresponding recoverable boat item
when the configured rules allow it to leave the world. Server authority owns
movement, collision, boarding, disembarking, water buoyancy, shore transitions,
and boat-item recovery. Clients may predict presentation but cannot duplicate
boats, teleport passengers, or enter protected structures without validation.

Boat placement requires a valid water surface and clearance for the hull and
passengers. Ordinary boat movement and disembark rules must provide a valid way
to reach the structure and must not trap or strand a boat at a chunk border.
Boats and passengers persist
through save/load, chunk unload/reload, disconnect/reconnect, and server
restart; an interrupted transfer either leaves the boat entity intact or
creates exactly one recoverable item. Required tests cover ocean travel,
shallow water, shore transitions, collision, chunk borders, passenger
replication, destruction/recovery, and retry-safe persistence.

#### Block breaking and ground drops

After an accepted block-break intent, the server validates reach, tool tier,
break permission, and the current chunk/block revision. It prepares the block
mutation and all resulting item-entity data first, then commits the block
change and its drop event together. If item-entity allocation or persistence
reservation fails, the block remains intact and the request reports failure;
there must never be a removed block with a lost drop or a retry that creates
two drops. A stable action/request ID makes retries idempotent.

The default rule is one item of the block's configured drop definition, with
quantity/tool/yield overrides in data. Thus breaking dirt yields dirt, stone
yields stone, each ore block yields its own ore-block item, and leaves or
plants yield their own block item in this initial rule set. Breaking
`voxel:grass` specifically yields one dirt item, not a grass-block item.
Air cannot be broken into an item. Water/lava source and flowing cells do not
spawn item entities. Normal survival cannot break bedrock unless a future
explicit game rule enables it, but its block-to-item mapping remains defined.

The dropped stack is then handled by the existing ground-item lifecycle:
bounded position/velocity, chunk spatial index, stack-compatible merging,
pickup lock if appropriate, persistence across unload/save, and server-side
pickup into available inventory space. Partial inventory capacity yields a
partial pickup and leaves the exact remainder in-world. No block break
directly teleports loot into the player's inventory.

#### Grass as a spreading surface block

`voxel:grass` is a turf/surface block distinct from `voxel:tall_grass`. It
drops dirt as specified above. It may spread by converting nearby eligible
dirt blocks into grass; it does not create dirt or increase the total number
of terrain blocks. Spread is an authoritative deterministic world update.

Eligibility is configurable but should initially require a loaded target
cell, exposed surface, sufficient light, and no disqualifying fluid or solid
cover. Evaluate a small local neighborhood from a grass-spread event/tick,
not a periodic scan of every block or chunk. Use a deterministic seed derived
from world seed, position, and simulation tick for any chance-based choice.
Deduplicate queued targets, cap work per server tick, and preserve old terrain
until the change is committed. If a valid target crosses a chunk edge, enqueue
the neighboring chunk only when it is loaded; otherwise persist a compact,
bounded pending edge update or re-evaluate the local edge when that chunk
loads. Tests must prove no double conversion, no spreading through full
blocks, no update into water/lava, and deterministic save/reload behavior.

#### Water and lava exist as world blocks but are carried in buckets

Water remains the water-source block ID 8; lava uses source block ID 64.
Flowing-water and flowing-lava IDs 65 and 66 are now registered separately,
with liquid metadata and no direct item-drop rule. The Voxel generator version
is now 8 so the changed registry manifest is explicit. The item catalog maps
the two source IDs to their filled buckets and declares validated
fluid-container rules: empty
bucket capacity 1 with no fluid, water bucket capacity 1 with water, and lava
bucket capacity 1 with lava. Bucket state is represented by the registered
item definition; the session planner stages fill/pour conversions, preserves
existing physical IDs, and mints an ID for a bucket split from a stack. The
allocator advances above current inventory, ground-item, and imported-card IDs.
Flowing IDs render with their
matching fluid tiles, but no flow simulation emits them yet. Surface Magma
generation now places bounded lava pools. Bucket transfers are available through
`GameSession::transfer_fluid_bucket`, which checks the camera ray, target cell,
chunk voxel revision, inventory revision, reach, and World Revision protection,
stages the bucket conversion with the catalog's immutable-snapshot planner, and
saves the resulting inventory/world snapshot. The item-catalog validator source
covers stack splitting, capacity failure, preserved instance identity, and pour
conversion. The API is not wired to a slot-selection control, and runtime
validators have not been run.
Neither source nor
flowing fluid is obtainable as a normal block item; only source blocks map to
their matching bucket transfer. Their legal portable forms are the water
bucket and lava bucket.
The generic selected-block interaction rejects direct water and lava placement;
generic delete and authoritative block-change paths also reject every liquid.
`World::replace_fluid_source_at` now permits only source-to-air or air-to-source
transitions when the expected block and chunk voxel revision still match. The
session transaction uses this primitive, records request parameters in the
shared journal, applies inventory and world changes, then saves them together
in snapshot v11. In-memory failures roll both participants and the allocator
back. The latest exact retry is cached; older transfer requests are pruned and
fail against their stale inventory revision. Multiplayer claim authorization,
a UI slot-selection route, process-crash action recovery, runtime action
validation and a host-wide allocator shared by every creation/import path
remain open. Recipe allocator reservations are serialized within one catalog;
multi-catalog allocation and coordination with concurrent container mutation
remain open.
GameSession snapshots now use version 11 and carry a bounded fluid-state
section. `World` serializes source-cell overrides with a version and checksum,
records successful revision-checked source transitions, and reapplies them to
newly streamed chunks before publishing their read state and remesh. The session
validator covers v11 opaque-section roundtrip/corruption, and the camera
validator covers World fluid-state roundtrip/corruption rollback. The session
bucket transaction and snapshot planner are implemented; runtime action
validation and process-crash recovery across the world/inventory journal remain
open.

For the first implementation, a bucket picks up only a valid source cell,
never a flowing cell. The server checks the player's reach, source state,
bucket contents, target block/chunk revisions, and inventory capacity. It
atomically replaces the source with the configured empty state and replaces
the empty bucket with the matching filled bucket. If either side cannot be
committed, neither changes. Pouring a filled bucket atomically validates the
target and replaces it with one source cell, then returns an empty bucket.
The bucket cannot create extra fluid: there is no infinite-source rule unless
one is later added explicitly to the world configuration. Flowing fluid is
derived from source state under bounded server simulation and never creates
bucket drops. Water/lava interaction, damage, burning, and fluid mixing are
explicit rules; do not let visual client prediction decide them.

Fluids keep their block rendering/collision/lighting properties separate from
solid full cubes. Water buckets and lava buckets are stack-size 1 and have
distinct item IDs. A filled bucket's save and network data includes only a
validated fluid enum and capacity, not a client-provided block ID. Creative
mode or administrative grants may provide filled buckets, but ordinary
survival obtains them only by collecting a valid source. The direct fluid
block items remain marked unobtainable even though the fluid blocks themselves
exist in the world.

#### Required item/drop/fluid tests

- Enumerate all 67 current built-in IDs and every registered runtime block. Confirm
  air's no-drop policy, grass-to-dirt, water's bucket-only policy, and a
  valid explicit drop policy for every other block. Confirm missing policies
  fail content validation rather than silently defaulting to no drop.
- For each self-dropping block, break it in a server test and assert the
  spawned item ID maps back to exactly that block ID. Assert grass produces
  dirt, never grass; ore blocks produce their own block items; bedrock follows
  the configured survival break permission; technical/air/fluid exceptions
  never create accidental item entities.
- Inject failure while preparing the block mutation, drop entity, persistence
  reservation, and replication event. Verify either both block and drop
  commit or neither does; retrying the same request cannot duplicate either.
- Test ground pickup with empty, partially full, and full inventory; stack
  limits; concurrent/replayed pickup requests; chunk unload/reload; and save
  restart. Verify no item is lost or duplicated.
- Grass-spread tests cover each allowed face/neighbor, insufficient light,
  covered dirt, fluid, non-dirt targets, unloaded chunks, chunk borders,
  duplicate queued events, deterministic seeds, and save/reload. Work stays
  bounded and does not scan whole chunks.
- Bucket tests cover empty/full bucket state, source versus flowing cells,
  out-of-reach requests, stale revisions, full inventory, invalid target,
  water and lava, concurrent pickup/pour, request retries, and persistence.
  Failed pickup/pour preserves both bucket and world; successful pickup/pour
  changes both exactly once. Direct water/lava block-item grants are rejected
  in survival, while configured creative/admin grants are separately tested.
- Verify fluid blocks survive chunk serialization and server/client deltas,
  and that their visual state cannot overwrite a newer block revision. Assert
  a bucket transfer causes only bounded local fluid/light/mesh invalidation.

## 21. Health, hunger, and optional thirst

### 21.1 Health is mandatory

The player needs an authoritative health value, maximum health, damage/healing
events, death state, and respawn flow. The current serialized `EntityState`
health field is only a data field; it is not a complete health/damage system.
Define all damage sources and validation on the server. Damage events have
unique IDs so retries cannot apply damage twice. Health is bounded to
`0..maximum_health`; healing cannot exceed the maximum. Death transitions
exactly once and applies a configured consequence policy. The first release
should not silently drop the entire inventory; define any death drops as an
explicit game rule and use the world-item system.

The player's baseline maximum health is **100 HP**. Store health as bounded
integer/fixed-point state and make maximum health configurable for future
effects, but 100 is the default combat tuning baseline. Armor, shield actions,
and skill effects modify validated damage through the ordered pipeline in
section 25; client-reported health is presentation only. Hunger-based natural
healing is scaled to this health range (the initial tuning unit is 5 HP per
vanilla-like heal event, subject to balance tests).

### 21.2 Hunger is mandatory

Implement the familiar Minecraft-style model rather than a single food meter.
The player has:

- **Health:** visible current/max health, initially proposed as 20 health
  points (10 hearts).
- **Food level (hunger):** visible 0–20 points (10 food icons).
- **Saturation:** hidden fractional reserve, initially 5 points in a newly
  created survival character, bounded by the current food level and 20.
- **Exhaustion:** hidden fractional activity cost. Once it reaches 4 points,
  consume one saturation point; when saturation is zero, consume one food
  point. Preserve any remainder correctly and clamp to valid ranges.

Store fractional values in fixed-point integers, not floats, so server/client
replay and saves are deterministic. Eating first increases food up to 20, then
adds the food's configured saturation, capped by the new food level. This makes
saturation matter separately: a food can fill the visible bar but provide a
larger or smaller reserve before hunger starts falling again.

Use the following Java-Minecraft-like defaults as starting balance data, not
scattered constants. –Saturation modifier– is the food quality multiplier;
the raw saturation addition is `nutrition * modifier * 2`, then capped by the
player's resulting food level. Food values may be changed by the world config.

| Food | Hunger restored | Saturation modifier | Saturation added before cap | Notes |
|---|---:|---:|---:|---|
| Bread | 5 | 0.6 | 6.0 | Made from 3 grain/wheat |
| Apple | 4 | 0.3 | 2.4 | Harvest from apple trees |
| Pear | 5 | 0.3 | 3.0 | Proposed Minecraft-specific balance |
| Raw beef/pork | 3 | 0.3 | 1.8 | Early fallback food |
| Cooked beef/pork | 8 | 0.8 | 12.8 | Furnace-cooked, high reserve |
| Raw mutton | 2 | 0.3 | 1.2 | Sheep drop |
| Cooked mutton | 6 | 0.8 | 9.6 | Furnace-cooked |
| Raw chicken | 2 | 0.3 | 1.2 | Chicken drop; omit random food poisoning in the first version |
| Cooked chicken | 6 | 0.6 | 7.2 | Furnace-cooked |

For example, bread always restores up to 5 visible food points, but its 6
potential saturation points can be capped when the resulting food level is
only 5. The food config should store integer nutrition plus a fixed-point
saturation modifier (or a precomputed fixed-point saturation amount), not
binary floating-point values.

Exhaustion comes from configured actions such as sprinting, jumping, mining,
attacking, and natural healing. Accumulate it from authoritative events/ticks;
do not update hunger once per render frame. Convert large exhaustion additions
with bounded integer arithmetic rather than a loop whose work scales without
limit. Eating consumes one food item and updates health/food/saturation in one
idempotent server transaction.

Natural regeneration follows two familiar thresholds, both controlled by a
world rule:

1. At full food (20) with saturation remaining, use the fast, saturation-fed
   regeneration path (baseline cadence: 10 simulation ticks); restore up to
   5 HP per vanilla-like heal event, scaled by available saturation, and
   charge exhaustion so recovery spends food reserve.
2. At food level 18 or greater without the fast path, use slower regeneration
   (baseline cadence: 80 simulation ticks per 5-HP heal event).
3. Below 18 food, natural regeneration stops. At zero food, starvation damage
   occurs only on its configured interval and difficulty-specific health
   floor; no food/health value may underflow.

Scale the classic difficulty floors to the 100-HP baseline if matching that
behavior: Easy stops starvation at 50 HP, Normal at 5 HP, Hard can starve to 0,
and Peaceful disables hunger drain/starvation. These are configurable defaults
and must be covered by tests. Default sprint eligibility may require food
above 6, matching familiar behavior. Damage, eating, healing, starvation, and
respawn are server-authoritative, revisioned events; retries must not apply
them twice. The UI shows current health/food and makes low-food or starvation
causes clear.

The numerical starting points above are based on the Minecraft hunger model;
see [Hunger management](https://minecraft.wiki/w/Tutorial:Hunger_management),
[Bread](https://minecraft.wiki/w/Bread), and
[Crop farming](https://minecraft.wiki/w/Tutorial:Crop_farming). Treat these as
design references, not a requirement to reproduce every version-specific
Minecraft edge case.

### 21.3 Breath and underwater survival

Breath is separate from hunger, thirst, and health. A living character has a
server-authoritative breath value with a configurable maximum. The default
maximum is **60 seconds** of underwater time, measured in simulation ticks
rather than render frames. The player can breathe normally while their head is
in air or at the water surface. Returning to the surface restores breath to
full at a configured recovery rate; the first implementation may refill it
immediately once the player is clearly breathing air.

While the character's breathing point is submerged, breath decreases only on
the authoritative simulation clock. A short grace period may cover surface
transitions and water-volume boundary jitter, but it must be bounded and
deterministic. When breath reaches zero, the character takes recurring
underwater suffocation damage at a configured interval until they reach the
surface or die. Suffocation damage cannot underflow health, bypass armor rules
unless explicitly configured, or be applied twice after a retry. The player
cannot remain underwater indefinitely by rendering, reconnecting, or changing
chunks.

The server must define and validate:

- the breathing-point test for water, air pockets, flowing water, and partial
  submersion;
- maximum breath, recovery rate, grace period, damage interval, and damage
  amount as versioned world rules;
- whether approved equipment, effects, or vehicles extend breath without
  bypassing the zero-breath damage rule;
- persistence of current breath, health, death, and respawn state across save,
  unload/reload, reconnect, and cross-chunk movement; and
- replication of breath and damage events without trusting client timers.

The UI must show current breath while submerged, warn before depletion, and
make suffocation damage and surface recovery understandable. Required tests
cover exactly-at-surface behavior, air pockets, one-minute depletion,
post-depletion damage until death, surface recovery, chunk borders, water
flow/partial submersion, save/load, reconnect, duplicate damage requests, and
respawn reset. Breath processing must be bounded per simulation tick and must
not run in the render loop.

### 21.4 Thirst is optional and gated

Do not add thirst to the first playable survival pass. Hunger, health, cooking,
and item progression already create a complete basic loop. Add thirst only if
the game has a distinct water/hydration mechanic that is more than another
meter demanding routine clicks. If approved later, it must be independently
configurable, have clear sources and consequences, persist/replay
deterministically, and be disableable without changing health/hunger behavior.

## 22. Economy and balance configuration

The item economy is intentionally an early tuning target, not a set of
permanent numbers. Keep item definitions, stack limits, recipe ingredients and
outputs, station requirements, furnace times/fuel values, food nutrition,
tool tiers/durability, card costs, dust sources, custom-feature footprints,
progression/unlock definitions, villager job/reputation rules, and deck rules
in versioned game configs. Configs are validated before a world/session starts.
Missing item IDs, invalid stack limits, cycles that generate items without
cost, impossible station requirements, invalid structure sizes, and card
recipes that reference unavailable materials fail validation rather than
silently producing free items.

For each recipe chain, add a balance report that states acquisition sources,
expected items/minute, station bottlenecks, output storage pressure, and how
many sessions/deck upgrades it supports. Analytics can measure crafting and
gathering duration, but must not silently tune gameplay or expose private
inventory data in shared traces.

### 22.1 Regeneration as the progression path

Chunk regeneration is not only a repeatable terrain-edit tool: it is one of
the main ways the player discovers new regions, materials, cards, settlements,
and higher equipment tiers. Keep progression rules in versioned configuration
with stable milestone/unlock IDs rather than scattering `if biome == ...`
checks through the renderer, inventory, or chunk generator.

A progression definition names its prerequisites, authoritative completion
event, unlocks, and one-time reward policy. Valid triggers may include a
first-time biome or resource discovery from a committed regeneration result,
crafting/adding a newly learned card, reaching a village reputation threshold,
or defeating a specific structure boss. A chunk commit alone is not a repeatable
reward trigger: track unique discovery/structure IDs so regenerating the same
area cannot repeatedly grant materials, card recipes, reputation, or gear
unlocks.

Use configurable equipment/card tiers. As the player regenerates into new
terrain and follows card effects, discoveries unlock the materials and recipes
for progressively stronger armor and weapons. A village can provide trade,
resource exchange, or research clues; giving dust can improve its relationship
with the player and expose configured rewards. The regeneration-only volcano
is a late milestone: its unlocked 9-by-9-chunk feature card opens a high-cost
encounter, and its defeated boss grants a unique progression reward/recipe for
the highest configured equipment tier. These are examples of progression
content, not hard-coded engine rules; worlds may configure alternative paths.

Each unlock/reward is committed idempotently with its source event and saved
player/world progression revision. A failed chunk commit cannot unlock a
discovery based on terrain that was not published. A failed inventory or
reward write cannot record the boss/discovery as completed while losing its
reward. The UI explains the next known milestone and the relevant card,
resource, structure, or crafting requirements without revealing intentionally
hidden content.

## 23. Updated dependency order

The minimum World Loom slice intentionally comes before the item/economy
foundation. It reuses the current world-revision path and may use test-seeded
dust/card fixtures; do not implement the entire inventory to prove one chunk
can be selected, regenerated, and published correctly. Regeneration 1.0 then
adds its real resource ledger, deck, and persistent replication. The broader
Game 1.0 systems depend on the item/inventory and entity foundations.

```text
existing revision preview + async generation + atomic commit
        -> Pre-Phase 1 one-chunk World Loom slice (minimal dust/card fixture)
             -> Libft CardGame adapter + one regeneration profile/session
        -> Regeneration 1.0 resource ledger + engine-backed 20..100 deck
        -> versioned custom-feature planner + cross-chunk fragment metadata
             -> Game 1.0 villages/villagers
             -> regeneration-only 9x9+ volcano/boss -> top equipment tier

item definitions + inventory transactions
        -> all-block drops/pickups + buckets + dust deposit
        -> recipe book + stations
             -> furnace/smelting/cooking + tools/armor/weapons
             -> farming/food + health/hunger
             -> paper/ink/card stock + Card Press/discovery
                    -> Libft CardGame adapter + Card Table two-player profile
                       -> 25..500-card active match capacity prerequisite
        -> customizable player avatar and equipment presentation
entity lifecycle + item drops + entity renderer
        -> sheep vertical slice -> cows/pigs/chickens + breeding
```

Build each edge with end-to-end tests before depending on it. Do not claim
Regeneration 1.0 requires Game 1.0's complete item, crafting, survival, avatar
customization, or fauna branches. A Game 1.0 release must pass all applicable
branches and the Regeneration 1.0 gate together.

## 24. Performance and stability implementation contract

This feature adds work in nearly every hot path–world updates, items, entities,
inventory, crafting, lighting, networking, and rendering. Performance is an
acceptance criterion, not a later optimization task. Implement the ownership,
queue, and scheduling limits below before adding large amounts of content.

### 24.1 Ownership and thread boundaries

Use one authoritative owner for mutable gameplay state. In the final topology,
that owner is the server process/world thread. During any transitional
single-process mode, preserve the same command/result boundary; do not let
render workers become alternate world writers.

| Work | Owner | Allowed handoff |
|---|---|---|
| Player input and drawing | Client/render thread | Small immutable command or read-only replica snapshot |
| Inventory, deck, resource, station, health/hunger, and entity authority | Server/world owner thread | Validated commands and immutable results |
| Terrain generation, card-policy evaluation for a chunk, lighting solve, mesh build | Persistent bounded worker pool | Immutable input snapshot in; owned candidate result out |
| GPU upload and visible mesh selection | Renderer | At most the configured publication budget; never mutable worker memory |
| Compression, persistence, analytics formatting/export | Background I/O/Libft-owned exporter paths | Versioned immutable payloads or completed Analytics buffers |

Workers must not receive `World`, live chunk/entity/inventory pointers, renderer
objects, or mutable server registries. Capture only the minimum immutable data
needed to do their operation. Results carry stable IDs plus world/chunk/entity
revision, request ID, and generation epoch so stale work can be rejected
without inspecting or mutating worker-owned state.

Do not create and join threads per chunk, item, recipe, or frame. Use existing
persistent sleeping workers and condition-variable/event notification. Idle
workers sleep; there is no busy polling. Worker count and queue capacity are
configuration values bounded by available hardware and memory. Avoid CPU
oversubscription: reserve capacity for the render/client and authoritative
world threads, especially on low-core systems.

### 24.2 Hard frame and publication budgets

The render frame must never synchronously perform terrain generation, a full
light solve, full mesh creation, inventory-wide recipe scans, compression,
filesystem I/O, analytics serialization, or an unbounded result-queue drain.
Normal frame work should read a stable client snapshot and submit bounded
commands only.

Preserve the existing gameplay requirement that **no more than one newly
generated or regenerated chunk mesh is published/uploaded per frame**. Make
the limit explicit and measurable. Also cap mesh-upload bytes and CPU time per
frame; if the byte/time budget is exhausted, defer the next item without
blocking. Urgent edits may invalidate a mesh immediately, but they must not
force a synchronous whole-chunk rebuild. Keep the old complete drawable state
until its replacement is ready.

The world owner processes a bounded number of small state commits per tick.
Prefer an immutable candidate pointer/snapshot swap over copying a complete
chunk, scanning all 4,096 cells, or rebuilding indexes in the commit phase.
Use an explicit time budget as well as a job-count budget; a single unusually
large result must not defeat a count-only cap. Measure commit queue age so
bounded work does not become permanently starved work.

Do not mark lighting invalid or replace it with zeros when generation or
recalculation begins. Candidate block/light/mesh data is assembled off-thread;
the prior tuple remains renderable until a revision-checked publication swaps
the complete replacement. A failed or stale job discards only its candidate.

### 24.3 Priorities, deduplication, and backpressure

Use bounded queues with explicit priority and reason-coded admission failure.
Suggested scheduling order:

1. input, network control, accepted block edits, and collision-critical state;
2. bounded light repair and mesh invalidation caused by those edits;
3. visible chunk result publication and nearby remesh;
4. nearby chunk streaming needed to keep the playable world expanding;
5. selected regeneration jobs near the player/station;
6. distant regeneration, entity low-detail simulation, persistence compression,
   and analytics/export work.

Age lower-priority jobs to prevent starvation, but never allow distant
regeneration to delay an accepted block edit indefinitely. Reserve queue slots
or separate lanes for edit/control work. When a queue is full, coalesce
replaceable work (for example, multiple pending remesh requests for the same
chunk/revision), reject or defer new low-priority work, and report the outcome.
Never silently drop authoritative edits, resource transactions, or committed
chunk state.

Deduplicate generation/mesh requests by `(world, chunk coordinate, operation,
source revision, config digest, request generation)`. A newer request replaces
or cancels obsolete queued work where safe. Check staleness before expensive
lighting/mesh stages when possible and again at commit. Do not allow one chunk
to accumulate a long list of identical remeshes; retain only the latest
required target revision plus explicit dependent work.

Cancellation is cooperative at stage boundaries. On cancellation, free the
candidate buffers promptly, preserve live state, and settle/refund the
corresponding resource reservation exactly once.

### 24.4 Synchronization and lock duration

Prefer owner-thread message passing for mutable gameplay state. Where a mutex
is unavoidable, hold it only long enough to copy or swap a small bounded
snapshot. Never hold a world/chunk/inventory/entity lock while:

- generating, propagating light, building a mesh, or waiting for a worker;
- sending/receiving network data or invoking callbacks;
- compressing, serializing large data, performing disk I/O, or flushing logs;
- searching the complete world/item/entity/deck/recipe catalog.

Document one lock order for the remaining shared structures and test it under
ThreadSanitizer. Do not nest a renderer lock under a world lock. A worker must
not need a world mutex to make progress while the world owner waits for that
worker. Destruction/shutdown first stops admission, cancels or drains bounded
work, joins persistent workers without holding world locks, then releases
world-owned state.

For inventory crafting, construct a compact transaction plan against one
inventory revision, reserve exact input/output slots, then commit once on the
owner thread. Do not lock inventory separately for each ingredient. For deck
editing, draw, discard, and card transfer, operate on one validated deck
transaction rather than repeated list scans and per-card lock/unlock cycles.

### 24.5 Subsystem-specific hot-path requirements

**World items and pickup**

- Index dropped stacks by chunk/spatial cell. Query only cells near the player;
  never scan all loaded or saved drops each frame.
- Run item simulation on a fixed server tick. Use a nearby-candidate cap,
  merge compatible stacks early, and apply a far-distance sleep/low-detail
  policy. Do not run full rigid-body physics for stationary drops.
- Batch nearby pickup candidates into one authoritative validation pass. A
  pickup is a small ledger/inventory transaction, not a rescan of every
  inventory item and every world entity.
- Bound per-chunk dropped-item count and payload size. If the cap is reached,
  merge, defer, or reject the drop with an explicit result; never overwrite a
  different item.

**Inventory and recipe book**

- Keep the recipe catalog immutable after config validation. Build indexes
  from required item IDs/tags to candidate recipes once at load time. Updating
  one inventory slot should not scan every recipe or reopen the recipe file.
- Cache recipe-book availability against inventory revision and station
  revision. Recompute only when a relevant revision changes or when the UI
  opens; do not recompute every rendered frame.
- Craft-maximum calculation uses bounded integer arithmetic and an indexed
  ingredient count. Reserve outputs before subtracting ingredients. Batch
  stack changes and publish one inventory revision.
- Keep inventory UI rendering separate from authority. The client may display
  a cached availability estimate, but the server validates the recipe.

**Furnaces and stations**

- Do not tick every furnace every render frame. Store `next_process_tick` and
  advance only active furnaces whose deadline is due; pause/invalidate the
  schedule when input, fuel, output capacity, or station state changes.
- Group due station work and cap station transitions per server tick. Persist
  compact progress and fuel state, not per-frame timers.
- Station UI receives changed slot/progress revisions, not repeated full
  inventory/station snapshots every frame.

**Health, hunger, food, and creatures**

- Update hunger and creature behavior on the fixed simulation clock, never on
  render frequency. Accumulate elapsed simulation ticks rather than taking a
  timer/lock per frame.
- Use a configurable AI think interval (initially a few simulation ticks) for
  decisions such as choosing a wander goal; movement/collision still uses the
  authoritative simulation tick. Recompute paths only after meaningful
  invalidation or a bounded retry interval.
- Use chunk/spatial indexes to find neighbors, flock members, food, and nearby
  players. Cap active entities per area and use lower-frequency distant
  simulation. Keep species behavior small and data-driven; do not run a
  behavior-tree traversal for every mob every display frame.
- Batch entity replication by nearby-client interest and only send changed
  state. Quantize/interpolate presentation data without letting it alter
  server-authoritative state.
- Eating, damage, drops, and healing are event transactions. Do not poll
  inventory or update all needs when nothing changed.

**Cards and regeneration**

- Resolve/validate the selected cards once per session. Produce one immutable
  canonical policy/digest and reuse it for all selected chunks; do not parse
  configuration or call arbitrary callbacks per block.
- Precompute biome/ore/feature lookup tables at config load. Per-chunk
  generation reads compact immutable tables and deterministic seeds.
- Work on at most the configured number of in-flight chunk snapshots. Bound
  selection size, queued candidate bytes, temporary light buffers, and result
  bytes. Release a snapshot immediately after its result is committed/discarded.
- Do not generate the same chunk/config/revision twice concurrently. If a
  request changes, cancel/coalesce stale work before starting another copy.

**Compression and persistence**

- Compress complete immutable snapshots on a background path after canonical
  serialization. Never compress while holding the live chunk lock, and never
  compress data merely to copy it between threads in one process.
- Use a bounded decompression result size and bounded temporary buffer pool.
  Keep a per-world limit on queued compressed bytes and save tasks.
- Batch small metadata writes when safe, but retain atomic replace semantics
  and correct failure reporting. Save failure keeps the authoritative state
  dirty/retryable; it does not block the render frame or discard edits.
- Measure compression ratio and CPU time. If a small delta costs more CPU than
  it saves in bandwidth, send/store it uncompressed.

### 24.6 Memory and network budgets

Define and expose hard caps for selected chunks per regeneration session,
in-flight generation jobs, queued result count/bytes, snapshot halo size,
visible ground items, active nearby creatures, per-chunk item/entity records,
recipe catalog size, inventory slots, deck size (20–100), card draw count, and
network message size. Validate the cap before allocation. Estimate worst-case
temporary memory as:

```text
in_flight_chunks * (block_snapshot + light_snapshot + mesh_candidate
                    + bounded_worker_scratch)
+ queued immutable results
+ serialized/compressed output buffers
```

The configured maximum must fit inside a documented process memory budget
with headroom for renderer/client data and the operating system. Use one
snapshot per in-flight job; do not copy the entire world for preview or
regeneration. Map summaries and progress messages are compact and paginated.

Network updates are interest-managed and batched. Control, block-edit, and
inventory-transaction results outrank bulk snapshots and distant entity
updates. A slow client cannot make the server retain unbounded chunks or entity
history: cap per-client queued bytes, then request a fresh snapshot or
disconnect according to policy.

### 24.7 Analytics must not become the bottleneck

Use the Libft Analytics buffers/exporter from the selected branch. Do not add
another Minecraft exporter thread, duplicate every scope into two sessions,
read the clock multiple times for one event, or share mutable export buffers
with rendering. Capture bounded counters and stage timings; Libft owns export
and file formatting.

Use detailed trace sampling during diagnosis, but use low-overhead region and
flow summaries for routine runs. Export frame summaries at the existing
configured interval (normally every 120 frames), with world-active and menu
periods kept distinct. Include exporter queue age/depth and dropped-event
counters in diagnostics. If analytics is disabled or saturated, skip telemetry
without blocking gameplay. Confirm the normal binary does not start analytics
workers or carry expensive instrumentation unless explicitly configured.

### 24.8 Performance test protocol and release gates

Every performance test uses fixed seeds, configs, chunk selections, player
actions, item/mob counts, and client count. Warm up separately; then run at
least ten repeated trials for comparative benchmarks. Record hardware/OS/build
mode, worker count, analytics state, and competing CPU load. Report each
trial plus p50/p95/p99 and worst observed latency; never report only a single
average.

Run two levels:

- **CI regression test:** a short deterministic scenario that measures frame
  duration, player-edit response, queue growth, publication count, and
  deterministic hashes. It fails on correctness regressions and on a generous
  but explicit performance ceiling.
- **Nightly/local stress test:** sustained regeneration plus streaming,
  lighting, block edits, item pickup/crafting, active furnaces, and nearby
  creatures; repeat ten trials and capture Analytics traces in a separate
  analytics build. Include a CPU-contention run to represent real gameplay.

The release is blocked if any of these occur: unbounded queue/memory growth;
more than one generated/regenerated mesh publication in a frame; render-thread
generation/light solve/compression/I/O; block edits miss the section 13 p95/p99
targets under regeneration load; old chunk light/mesh is cleared before a
replacement is ready; dropped items/cards/resources are duplicated or lost;
or identical seeded normal/analytics runs diverge.

For frame pacing, target p95 <= 16.7 ms and p99 <= 33.3 ms on the documented
reference 60-Hz machine under the standard nearby-world workload, then publish
results for minimum-spec hardware separately. Keep the existing stricter
edit/request/publication latency targets in section 13. If a target is missed,
use the stage/queue/lock measurements to identify the responsible component;
do not widen deadlines or hide the regression by lowering render distance
without recording that behavior change.

## 25. Gear, armor, combat styles, and basic magic

### 25.1 Shared combat model

The player's default maximum health is **100 HP**. Weapon damage and armor
reduction use the same integer HP units. Damage sources, range, attack
cooldowns, line of sight, ammunition, spell costs, target state, and equipment
are validated by the authoritative server. Client animation/prediction is
presentation only; it cannot commit damage or drops.

For every positive hit, apply attacker bonuses first, then subtract flat
defense. The first version intentionally has no percentage-based armor curve:

```text
raw_damage = validated_weapon_or_spell_damage_after_attacker_bonuses
flat_reduction = sum(equipped_armor_piece_reductions)
if shield_guard_is_active_and_attack_is_in_front:
    flat_reduction += shield_guard_reduction
final_damage = max(1, raw_damage - flat_reduction)
target_health = max(0, target_health - final_damage)
```

Zero/invalid attacks are rejected before this formula; any registered positive
hit that reaches armor deals at least 1 HP. Armor never makes an attack deal
zero. Apply damage once per unique server event ID. Clamp all values and use
checked fixed-point/integer math for skill multipliers and resource costs.

### 25.2 Provisional armor profiles

The values below are a starting balance fixture for a 100-HP character and
typical early attacks around 20–35 raw damage. They are config data and must be
adjusted from combat tests, not constants spread across attack code.

| Set | Head / chest / legs / boots flat reduction | Full-set identity | Tradeoff |
|---|---:|---|---|
| Cloth | 1 / 2 / 1 / 1 (5 total) | +20% spell power and 10% lower configured spell cost | Lowest protection; no movement penalty; flexible utility |
| Leather | 2 / 4 / 3 / 1 (10 total) | +15% bow damage and +8% movement speed | Medium protection; strongest mobility/ranged profile |
| Iron | 4 / 8 / 6 / 2 (20 total) | +10% shield guard reduction while actively guarding | Highest protection; -10% movement speed |

The bonuses activate only while the full matching set is equipped in the
required slots. Mixed armor remains valid and gives the flat reduction of each
piece; it does not receive an accidental full-set bonus. A later design may
add two-piece bonuses if testing shows that they improve choices without
creating dominant mixed sets. No armor set locks a player out of using a bow,
sword, shield, or staff.

For clarity, a 30-HP hit against the example full sets becomes 25 HP in cloth,
20 HP in leather, and 10 HP in iron before any active shield reduction. If
reduction would reach or exceed the attack, the hit still deals 1 HP. This
example must be part of deterministic combat tests and the UI should show the
actual defense total and movement/style bonus.

### 25.3 Shield, bow, sword, and equipment

- **Sword:** close-range weapon with configured base damage, attack interval,
  reach, and durability. A faster/lighter sword can trade raw damage for
  quicker attacks; do not encode this as a hidden armor effect.
- **Shield:** off-hand/guard equipment with a server-validated guard action.
  Guard covers a documented forward angle, has a short recovery/cooldown, and
  adds a configured flat reduction only against attacks from that direction.
  It never bypasses the 1-HP minimum for a hit that connects. While guarding,
  movement can be reduced by a clear configured amount. Do not add a stamina
  bar solely for shields in the first pass.
- **Bow and arrows:** bow attacks consume one arrow only after the server
  accepts the shot. Projectile position, collision, damage, lifetime, and
  pickup/drop are authoritative and bounded. Leather's full-set bonus
  increases bow damage; it does not create free arrows or bypass armor.
- **Gear durability:** if included in the first pass, damage/repair is a
  server-side item-instance transaction. Do not update durability per rendered
  frame. Broken equipment loses its defense/bonus exactly once and is visibly
  represented as broken; repair recipes reserve materials before restoring
  durability.

Equipment recipes use the existing progression chain: leather from configured
cow drops, cloth from configured wool/fiber processing, and iron bars from
furnace-smelted ore. A workbench recipe book lists the ingredients, station,
material tier, and resulting stats before crafting. Higher mining tiers gate
which ores a pickaxe can harvest; a client cannot claim a better tool than its
authoritative equipped item.

Equipment implementation note (2026-10-14): Libft's generic character
equipment component has been extended from head/chest/weapon to include legs,
feet, and off hand, with those extra slots represented in its legacy character
save/load format. The following implementation entries add Minecraft
catalog-validated session equip/unequip actions and physical inventory
ownership transfer. Cached derived combat stats, durability loss from combat,
and user-facing loadout presentation remain open. Source changes are not
compiled or tested yet.

Libft now also exposes `game_inventory::copy_item_at_revision`, which returns
a shared handle to the exact item object only when the expected inventory
revision still matches under both inventory and item-map locks. This preserves
instance identity and modifiers for the GLEQ loadout transaction. Source-only,
uncompiled, and untested.

GameSession now exposes journalled equip/unequip actions through GLEQ v1.
Equip validates the catalogued equipment slot and durability, removes the
physical instance from inventory, and returns any replaced instance to the
inventory; unequip returns the same instance. Prepared actions recover at
startup, and session identity checks include equipped IDs. Both inventory and
loadout persist in the same session snapshot. Hostile physical hits now apply
flat armor points from equipped armor, excluding broken pieces and retaining a
1 HP minimum. Main-hand attack stats, equipment wear in combat, shield guard,
and runtime verification remain open. Source-only; builds and tests are
deferred until the implementation pass is complete.

Equipped armor integration (2026-10-14): GameSession now derives flat armor
reduction from equipped head/chest/legs/feet catalog entries during hostile
damage resolution. Broken pieces contribute no armor and connected hits still
deal at least 1 HP. Shield guard is separate and remains unimplemented.
Catalog armor values now match the roadmap's provisional cloth/leather/iron
full-set totals of 5/10/20, and the session's initial/respawn health is 100 HP.
These changes are source-only; no compilation or runtime checks have run.

Shield guard implementation pass (2026-10-14): GameSession now accepts a
bounded server-side guard action only while a non-broken off-hand shield is
equipped. Each guard lasts at most 40 simulation ticks, covers a 120-degree
forward arc, and enters a 20-tick recovery after release or expiry. Front
attacks receive the catalogued flat shield reduction; blocked hits wear the
shield and checkpoint rollback restores its prior item modifiers. Guard state
does not survive reload. Source-only; the full combat action pipeline,
projectiles, style bonuses, and validation remain open.

### 25.4 Staffs and first magic slice

Staffs are crafted catalysts, not a new class lock. A staff can supply a
configured spell focus, range, or power bonus; the equipped armor determines
the cloth-set benefit. Add a small, understandable first spell list:

| Spell | Effect | Example cost/cooldown |
|---|---|---|
| Arcane Bolt | Single-target ranged magic damage; armor still applies | 20 focus, 1-second cooldown |
| Mending Pulse | Restore a bounded amount of health to self or valid ally | 25 focus, 5-second cooldown |
| Ward | Grant a short, capped flat-defense effect | 20 focus, 8-second cooldown |

These costs and values are balance examples. A separate configurable `focus`
resource (candidate 0–100 with slow server-tick regeneration) pays for spells;
it is not hunger or saturation. Every spell has stable ID, validated target
rules, range, cost, cooldown, effect duration, and maximum stack policy. The
server atomically checks/spends focus and applies the effect. Repeated cast
requests with one ID do not double-spend or double-heal. The first release
should favor a few useful spells over a large spell scripting system.

### 25.5 Playstyles are loadouts, not classes

The initial gear triangle is:

- **Cloth + staff – flexible magic:** lower flat defense, broader utility,
  healing/ward options, and configurable spell efficiency.
- **Leather + bow – mobile ranged:** medium defense, movement advantage, and
  stronger bow damage, balanced by ammunition and close-range vulnerability.
- **Iron + sword + shield – tanky melee:** highest flat defense and guard
  potential, balanced by slower movement and close-range positioning.

These are recommended builds and tutorial presets, not exclusive classes.
Players may mix pieces and weapons; the stat sheet explains exactly which
bonuses are active and why. Avoid permanent class selection until the combat
and item economy has been play-tested.

### 25.6 Derived stats and performance

Recompute a compact derived-combat-stat snapshot only when equipment, relevant
buffs, skill levels, or set membership changes. Cache total flat armor, active
set bonuses, movement modifier, and weapon/spell modifiers. An attack then
reads that snapshot and performs a small fixed amount of arithmetic; it does
not scan the whole inventory, all recipes, or all equipment definitions.

Keep cooldowns and focus regeneration on the fixed simulation tick. Replicate
only changed health/focus/equipment revisions and accepted combat events. Do
not broadcast the entire inventory or stat sheet every frame. Projectile
simulation uses a spatial index and configured maximum active projectile
count/lifetime; reject or defer excess low-priority projectiles rather than
letting them grow without bound.

### 25.7 Combat and balance tests

- Armor pieces add exactly their configured flat reductions; mixed sets get no
  full-set effect; all full sets activate only with the required items/slots.
- For every armor/shield combination, positive incoming damage is at least 1
  HP and never exceeds validated raw damage absent an explicit vulnerability
  rule. Test raw damage below, equal to, and above total reduction.
- Verify 100 HP initialization, max-health clamping, armor unequip/break,
  healing, death, respawn, damage-event replay, and save/load.
- Verify shield direction, guard duration/cooldown, blocked angle, and minimum
  damage at front/back/edge angles. Invalid or repeated guard events cannot
  create permanent defense.
- Bow shots consume exactly one valid arrow on acceptance; rejected shots
  consume none. Projectile impacts, terrain collision, expiry, drops, and
  multiplayer replication are deterministic and bounded.
- Cloth spell bonuses, leather bow/movement bonuses, and iron slow/defense
  bonuses recompute on equip changes and remain identical after reconnect.
- Spell range, target permissions, focus spend, cooldown, healing cap, ward
  duration/stacking, and repeated request IDs are tested under allocation and
  network failure injection.
- Benchmark attack resolution with full inventory and many nearby entities;
  cost should remain constant with inventory size because combat uses cached
  derived stats. Verify no per-frame scan is introduced.

## 26. Player character model and customization

The current player movement/collision geometry is not a character model. Add
an actual rendered player avatar and an intentional appearance-customization
flow as Game 1.0 work. Regeneration 1.0 only needs a valid default avatar for
other players; the pre-Phase 1 slice may use a temporary placeholder so model
production does not delay the core regeneration test.

### 26.1 Model and rig

Use a stylized, block-compatible 3D character with a stable shared rig for
head, torso, arms, and legs. The model's proportions and animations must fit
the existing player movement and collision contract. Rendering geometry is
never the collision/hitbox source of truth: customization must not change
reach, movement speed, collision, health, damage, mining rate, or network
authority. Support a default body/model and the same bounded rig for every
appearance profile; avoid arbitrary per-player bone scaling in the first
version.

Other clients render the complete avatar with equipped gear and held items.
In first-person, the local client may hide the body and render arms/held items
to avoid camera clipping, but this must use the same selected appearance and
equipment. A missing/unknown appearance or asset version resolves to a safe
default model, not an invisible player or a load failure that blocks joining.

### 26.2 Customization profile and player flow

Provide a character setup/customization screen at initial creation and a
reachable edit flow later. Offer a controlled set of compatible options, such
as:

- skin/body palette;
- face/eye style and color;
- hair style and color;
- clothing/outfit style and palette;
- a small set of cosmetic accessory slots.

Options are selected from versioned, whitelisted content definitions. The
profile stores stable IDs and palette values, not pointers or arbitrary asset
paths. Cosmetic unlocks may come from gameplay progression later, but the
initial appearance choices must not require rare resources or grant combat
advantages. Armor/equipment renders as a layer or compatible model attachment;
it does not overwrite the saved base appearance. UI previews the exact selected
model and safely falls back if a combination is unsupported.

Persist the profile against the stable player identity, independently of
chunk state and inventory. A customization update has a profile revision,
validates every selected option, and is saved atomically. Replicate only the
compact validated appearance IDs/colors and revision; clients resolve assets
from the matching local content pack. Never send arbitrary mesh/texture bytes
as routine player-state updates. Unknown IDs or content-version mismatches use
the default profile and produce a diagnostic.

### 26.3 Animation and presentation

Implement a small first animation set: idle, walk/run, jump/fall/land, and
common tool-use/attack poses. Animation state is derived from authoritative
movement/action state and smoothly interpolated on clients. The renderer may
predict local cosmetic pose, but it cannot use pose data to validate a hit,
movement, or interaction. Equipment visibility follows the validated loadout:
armor, shield, bow, sword, staff, and held block/item should attach to defined
rig slots without bespoke per-player mesh construction.

Load and prepare shared model assets off the render hot path. Cache rig,
mesh/material combinations, and animation data; changing a palette should
update a small material/appearance handle rather than rebuild the world or
recreate all player meshes. Use distance-based visibility/LOD for remote
avatars and bound animation/update work by visible nearby players.

### 26.4 Avatar tests

- Every supported appearance combination resolves to a valid rig/material;
  invalid IDs, missing files, unsupported combinations, and content-version
  mismatches select the documented default without crashing or hiding players.
- Save/load and reconnect preserve the exact profile revision. Repeated or
  stale customization requests cannot overwrite a newer profile.
- Replication sends only validated compact appearance state; clients never
  select arbitrary filesystem paths or upload unbounded assets.
- Before/after customization, assert identical hitbox dimensions, collision,
  movement, reach, combat values, and server validation results.
- Verify first-person and remote third-person presentation, all base
  animations, held-item/armor attachments, and graceful asset fallback.
- Stress a representative maximum number of nearby avatars. Asset loading,
  palette changes, and animation must not cause per-frame allocations,
  unbounded mesh rebuilds, or violations of the frame-time budgets in Section
  24.


Progress update (2026-10-11): Card transfer application now restores the inventory, card-session and journal snapshots when terminal persistence fails. The world-item validator injects terminal-save failure and checks rollback to PREPARED followed by a successful exact retry; its source compiles with strict MinGW warnings, but runtime execution remains unverified because bundled Git Bash fails to create its MSYS named object (NtCreateDirectoryObject, 0xC0000022). GameSession card-intent compensation now reports errors from chunk removal and card/rules snapshot restoration instead of masking them with the initial failure. GameSession.cpp passes strict Windows MinGW syntax compilation. Durable host-intent transaction coordination and the atomic World Loom start remain unfinished.


Progress update (2026-10-11): WorldRevisionServer confirm now requires a configured host committer instead of directly starting generation. It forwards actor/Loom identity, request ID, both expected revisions, and a 4–7 opening-card count; cached exact retries call the host once, while changed request payloads are rejected. The World Revision validator covers the delegation boundary, exact retry, changed-payload rejection, and fail-closed confirmation without a configured committer; strict MinGW syntax compilation passes. The production GameSession coordinator is not wired yet, so resource debit, opening deal, durable chunk reservation/barriers, and recoverable generation commit remain open.


Progress update (2026-10-11): RegenerationCardSession now has a persisted, request-idempotent opening-deal operation that extends the four-card bootstrap hand to the quoted 4–7 cards before any card action or transfer. The validator checks draw/hand counts and retry across snapshot restore. Strict MinGW compilation passes; runtime execution remains unverified through the current Make runner. Resource debit and World Loom transaction wiring remain open.


Progress update (2026-10-11): GameSession now provides a bindable World Revision commit callback. It rechecks quoted revisions/cost, checkpoints a prepared shared-journal record, debits currency, applies the persisted opening deal, starts generation and checkpoints the terminal record; pre-generation failures restore inventory/cards and abort. Startup aborts prepared starts because the draft/selection is not persisted. Strict MinGW syntax compilation passes. The app does not yet construct/bind a production WorldRevisionServer, and durable draft, chunk reservation, barriers, and crash-consistent generation recovery remain open.


World Loom follow-up (2026-10-11): Start commit and terrain generation are now separate host operations. A successful commit charges the quote and deals the opening hand; the host calls finalize_regeneration_start at its authoritative deadline to resolve expired choices, apply pending intents, and begin generation. Runtime timer wiring and durable recovery of the draft and decision window remain open.

World Loom follow-up (2026-10-11): Finalization now requires a successful committed start checkpoint; a selection-only draft cannot call the generation finalizer. This gate resets with a new draft/session and still needs durable restart recovery.

CardGame host notices (2026-10-11): GameSession now exposes durable consumption of the registered empty-draw and hand-overflow notices. The returned typed record is checkpointed with its acknowledgement; host delivery to clients is still outstanding.
RegenerationCardValidator now checks the draw-notice schemas and wrong-actor rejection. The edited validator compiled with strict warnings; rebuilt runtime validation remains outstanding.

Regeneration card actions now require an active committed World Loom decision window; play/draw/discard and intent/expiry consumers fail outside that state. Strict syntax compilation passes. Durable deadline recovery and multiplayer actor authorization remain open.

World Loom finalization now checks the persisted mandatory-discard counter and refuses generation while the Double Draft obligation remains. The regeneration validator checks the 1-to-0 obligation transition; runtime run awaits a working Make/MSYS runner.

World Loom finalization now refuses to generate while unresolved CardGame choices or mandatory discards remain after expiry processing. Strict syntax compilation passes; the Make runner still fails before build during Git Bash/MSYS initialization.

World Loom finalization now retries the terminal session checkpoint if generation is already active after a prior save failure. Runtime and crash recovery of world generation remain open.

Furnace advancement now checkpoints inventory and job state each active tick batch and restores both when advance/save fails. Empty queues skip the write. Strict syntax compilation passes; per-advance journal records and station reach/claim checks remain unimplemented.

Furnace tick advancement now uses a prepared shared-journal transaction with restart replay and exact retry; terminal records are pruned while the private request-ID high-water mark persists. Validators cover rollback/replay/retry but could not be run because Make fails during MSYS startup. Station reach/claim checks remain outstanding.
The edited journal validator passed in a direct MinGW build. The processing-advance integration validator compiles, but the full direct link against the stale Oct-10 Libft archive lacks newer current symbols, and Make fails before rebuilding them; processing runtime validation remains open.


Validation update (2026-10-11): The journaled furnace-advance integration now passes its direct Windows MinGW runtime validator after the test fixture was corrected to locate the retry card by physical instance ID. It covers prepared checkpoint, terminal-save rollback, restart-style recovery, exact retry, and request-ID high-water behavior. This does not provide placed-furnace ownership/reach checks, station persistence, or broader station crafting; those roadmap requirements remain open.

Validation update (2026-10-11): Item-catalog validation now explicitly checks one recipe output for each of the 18 enabled card items. It also verifies that the seven starter-card recipes are known by default and the eleven rare-card recipes remain discovery-locked. Direct MinGW runtime result: item-catalog: ok builtin=64 runtime=0 content=103. Rare-card effects and their data-driven discovery sources remain unimplemented.

Recipe-discovery update (2026-10-11): The recipe catalog now provides trusted data-driven source bindings and idempotent discovery grants, with direct validator coverage for valid grants, exact retries, conflicting source definitions, and unknown sources. Runtime item-catalog validation passes. Loot, exploration, and research source bindings still need content data and authoritative event consumers.
Recipe-discovery persistence update (2026-10-11): GameSession now exposes a checkpointed discovery-grant boundary. It snapshots the recipe book, applies a registered stable source grant, saves the session, and restores book plus crafting unlock state if saving fails. Recipe-book import now relocks recipes that are absent from the imported book. Direct strict MinGW ItemCatalogValidator runtime result: item-catalog: ok builtin=64 runtime=0 content=103. The mechanism has no authored loot/exploration/research bindings or live authoritative consumers yet; save-failure injection and event-source integration remain outstanding.

Recipe discovery transaction update (2026-10-11): Checkpoint/rollback is factored into `RecipeDiscoveryTransaction` and wired into `GameSession`. The direct item-catalog validator injects disk-full, asserts both recipe-book knowledge and the crafting runtime lock roll back, then retries successfully and confirms duplicate retry avoids another save. Result: `item-catalog: ok builtin=64 runtime=0 content=103`.

The Libft locked-recipe transaction regression now also covers relocking after an unlock and verifies a rejected craft preserves its inventory. Strict MinGW syntax compilation passes; the full Libft runner was not rebuilt in this slice.

Script catalogue update (2026-10-11): CardGame now supports checksummed `CGSC` v1 script catalogues, validation before registration, and trusted-manifest comparison across script/native ABI, sorted host-intent/draw schemas, context keys, event bindings/programs, plus a host-supplied policy hash. The targeted Libft bridge runtime test passed corruption rejection, hash-mismatch rollback, successful load and execution; strict MinGW compilation passed. Authenticated manifest provisioning and Minecraft session-import/policy wiring remain open.

The script bridge manifest also binds the configured operation limit; focused runtime coverage confirms mismatched limits reject and roll back catalogue loading.

Script environment hashing is now runtime-checked for registration-order independence. The focused script-catalog harness also passes corruption rejection, operation-limit and policy-hash mismatch rollback, successful load, and script execution.

World Loom authorization boundary (2026-10-12): World Revision quotes and all draft actions now pass through a required host callback that receives the authenticated actor/Loom, configured station chunk, and request payload. Server initialization now fails unless both client identity and station-access callbacks are installed, so an unprotected server cannot start accepting traffic. Missing callback or station anchor also fails closed at action time. The server validator uses an authorizer constrained to its expected actor, Loom, and station. Strict MinGW syntax compilation passes. Production callback binding, exact station block position/identity, live claim/reach checks, session revocation, and network runtime validation remain on the roadmap.

World Loom draft shutdown cleanup (2026-10-12): Destroying the World Revision server now cancels its owned, uncommitted pending draft before clearing ownership. The API returns network teardown/cancellation errors and retains the draft state if cleanup fails so the caller can observe and retry. A draft whose host commit succeeded stays pending for the GameSession decision window. Strict MinGW syntax compilation passes; disconnect/restart runtime coverage and crash recovery of draft state remain open.

World Loom exact station anchor (2026-10-12): Hosts can now save exact signed block coordinates and expected block ID with the Loom anchor; the configuration call verifies the current loaded block first. Metadata v4 stores the identity while retaining v1-v3 readers; network access checks compare the current block at that position before evaluating host reach/claim policy. Local breaks and authoritative edits that remove/replace the anchored block invalidate it and cancel an uncommitted draft. Strict MinGW syntax compilation passes. Authoritative Loom block placement/configuration, claim/reach provider binding, and runtime migration checks remain outstanding.

World Revision metadata save wiring (2026-10-12): GameSession initially used a seed-specific `.wrv` sidecar. The follow-up below supersedes sidecar writes for new saves.

World Revision metadata in GameSession snapshots (2026-10-12): GameSession snapshot v10 now embeds the bounded, checksummed World Revision metadata payload in the same `.wisp` snapshot as inventory, cards, rules, instability and the shared journal. Startup restores embedded metadata and reads `.wrv` only as a fallback for older snapshots. This removes the cross-file replacement gap for these metadata values. Strict MinGW warning-as-error syntax compilation passes for the metadata store, manager, facade, persistence and GameSession units. Runtime v10 round-trip/legacy compatibility and failure-injection checks remain outstanding; draft selection, chunk reservations/barriers and generation publication are still not crash-recovered.

Cross-container physical-ID save gate (2026-10-12): Combined session saves now validate uniqueness across the character inventory and ground-item store before encoding. The WorldItemStore validator checks the duplicate-ID failure and verifies that a pre-existing save is unchanged. Strict MinGW warning-as-error syntax compilation passes; runtime validation remains outstanding. A shared allocator and synchronization across concurrent item mutations remain open.

Craft allocator synchronization (2026-10-12): Journaled hand craft and prepared-craft recovery now advance the recipe catalog's next unique output ID beyond IDs in current inventory and ground-item state before applying. Validator source injects a ground item with ID 12000 after restoring a prepared craft and expects the catalog counter to recover as 12001. Strict MinGW warning-as-error syntax compilation passes; runtime validation remains outstanding. A host-wide allocator for all mint paths and synchronization against concurrent mutations are still needed.

Snapshot offset review (2026-10-12): Corrected the combined snapshot loader to compute journal, card, rules, and instability ranges while accounting for later trailing payloads and to copy their declared lengths exactly. This also repairs v9 instability payload boundaries. Strict MinGW syntax compilation passes; runtime round-trip coverage is still outstanding.

Fortune's Draw card (2026-10-12): Enabled the later card as definition 14 with a one-card draw routed through the existing policy and exact host-intent retry path. Added the discovery-locked Card Press recipe 2019 and item/recipe coverage for 19 card definitions. Card recipes now use the physical ID allocator at commit, making each crafted card a unique instance. The adapter's mandatory discard count now follows the card's declared obligation. Strict MinGW warning-as-error syntax compilation passes for all touched card, catalog, and session translation units; runtime integration validation remains outstanding.

Ultimate Greed implementation update (2026-10-12): Rare definition 12 can be imported and played through the authenticated card intent path. It requests exactly three cards with no compulsory discard; existing empty-deck and overflow policies remain active. Its draw request is tied to the card host-intent sequence, exact retries replay, and mismatched draw counts are rejected. Finalization pauses for queued draw notices so the host can deliver overflow/empty-deck costs first. The separately configured instability delta is checked against each selected chunk and applied with transaction rollback. Strict MinGW syntax compilation passes for the touched implementation and validator units. Runtime validation and player-facing card text/UI remain open.

Biome Rupture implementation update (2026-10-12): Rare definition 13 now accepts two distinct biome choices, removes both through one idempotent World Rules intent, and rejects removal of the last viable biome. Choices already excluded remain no-ops and are never replaced; the play result carries a cached two-bit no-effect mask persisted in RCS1 v5, with v1-v4 compatibility. It uses its own configured instability delta, included in the rules manifest and applied transactionally. Validator source covers effect, no-op behavior, response-metadata rejection on authoritative intents, and minimum-biome constraints; strict MinGW syntax compilation passes. Runtime validation and UI rendering of the no-effect mask remain open.

World Loom station prerequisite update (2026-10-12): Network draft creation now requires the host-configured station chunk anchor, failing closed when no safe-zone location exists. Validator source checks the missing-anchor rejection and begins after configuration. Station block identity, reach/claim authorization, and runtime validator execution remain open.

GameSession future-version rejection check (2026-10-12): `WorldItemStoreValidator` now creates a v10 snapshot, changes its header to version 11, recomputes the valid outer checksum, and verifies the loader rejects it without changing the live character or any opaque subsystem payload output. Strict MinGW warning-as-error syntax compilation passes, and a directly linked runtime smoke against current persistence sources passes (`session-future-version: valid-checksum rejection and rollback ok`). The full project validator remains unrun; the Libft submodule Git metadata is inaccessible to `git diff` in this workspace.

Volcano Mark implementation update (2026-10-12): The 256-chunk World Revision hard ceiling and default generation queue permit the minimum footprint. Before card consumption, GameSession now requires enough distinct selected chunks, a contiguous selected 9-by-9 square made entirely of paid chunks, and capacity for volcano and Village Charter reservations. World exposes a paid-selection snapshot so free bonus chunks cannot reduce the volcano footprint's normal dust price; the existing read-only start quote reports the paid count/cost. Revision start deterministically chooses the first unreserved paid square and attaches the same anchor/9-chunk width to all 81 chunk requests. The worker derives terrain from global coordinates and builds a volcanic cone, crater lake with rock containment, an arena shelf, and a connecting passage, preserving the profile across chunk seams. Playing the card now requires the rare Volcano Mark recipe to be known; the catalog exposes a host source-registration boundary, and ItemCatalogValidator source checks the blueprint recipe's locked state and checkpointed discovery grant/rollback. `WorldRevisionValidator` source checks the lake boundary, passage, arena support and slope continuity across three generated chunks. Strict MinGW warning-as-error syntax compilation passes for the affected application, recipe, revision-selection, pipeline, worker, and validator units. Runtime validation could not run: `mingw32-make -j1 debug` fails when Git Bash cannot create its MSYS named object (`NtCreateDirectoryObject ... 0xC0000022`), and WSL launch is denied (`Wsl/E_ACCESSDENIED`). The exploration/research source binding, configured larger footprints and feature-highlight preview, full support/lighting/committed-manifest validation, authoritative boss gating and lifecycle, production instability configuration, and runtime validation remain open; the volcano is still incomplete.

Fluid container metadata (2026-10-12): `MinecraftItemCatalog` now resolves capacity and validated fluid kind for empty, water-filled, and lava-filled buckets, and catalog validation ties those rules to the registered item definitions and stack limits. `ItemCatalogValidator` source checks each mapping; strict MinGW warning-as-error syntax compilation passes. Bucket item-instance persistence, fluid source/flow state, transfer interactions, revision/reach validation, and durable cross-world/inventory transactions remain outstanding.

Playable-card previews (2026-10-12): `RegenerationCardSession::get_card_preview`
provides a title, effect summary, choice kind, and number of choices for all
currently supported card definitions. Validator source checks every supported
entry, including the two-biome Rupture choice, and rejects an unsupported ID
without modifying the output. Strict MinGW warning-as-error
syntax compilation passes; runtime validation remains unverified. Seven rare
card items still have recipes but no supported card definitions/effects, so
the full card catalogue and preview requirement remain incomplete.

Session item-ID synchronization (2026-10-12): `GameSession` now uses one
high-water synchronization helper before hand-craft and split-bucket ID
minting. It advances beyond IDs present in the character inventory, ground
items, and imported-card links; transaction-level inventory/ground checks stay
in place. `MinecraftRecipeCatalog` serializes allocator and unique-craft
operations with a mutex. ItemCatalogValidator source exercises 512 parallel
reservations and checks for nonzero, unique IDs. GameSession serializes
hand-craft, fluid-transfer, card-import/export, and ground-pickup paths across
their ID scans and container mutations. Strict MinGW warning-as-error syntax
compilation passes; runtime validation, shared allocation across multiple
catalogs/hosts, and mutations that bypass the session APIs remain open.

World Revision identifier exhaustion (2026-10-12): Both begin overloads now
reject `UINT32_MAX` before changing draft or selection state, so a completed
regeneration cannot wrap the revision identifier to zero. The World Revision
validator source checks both overloads preserve the non-pending state and
selection revision on rejection. Strict MinGW warning-as-error syntax
compilation passes; runtime validation remains blocked by Git Bash's MSYS
named-object permission failure (`NtCreateDirectoryObject ... 0xC0000022`).

Volcano Mark instability application (2026-10-12): GameSession now applies
the configured Volcano Mark instability delta to selected chunks and includes
that mutation in the existing instability snapshot rollback path. Previously
the action validated and snapshotted the delta but omitted the actual update.
Strict MinGW warning-as-error syntax compilation passes for GameSession;
session-level runtime validation and the remaining Volcano lifecycle gates
are still open.

Recipe discovery authority boundary (2026-10-12): GameSession now fails
closed on recipe discovery grants unless a host-installed authorization
callback approves the exact source type, source ID and event ID. The callback
must be configured before the session starts; mapping registration is no
longer exposed as a GameSession client action. ItemCatalogValidator source
checks missing authority, zero IDs, mismatched events and exact approval.
The mutable GameSession recipe-catalog accessor has been removed; callers now
receive a const view, so direct `unlock_recipe()` cannot bypass the transaction.
Trusted hosts can now register discovery source-to-recipe mappings through a
bounded pre-start GameSession configuration API. Startup installs the mappings
before saved recipe-book restore, and already-known recipes retain their source
bindings after load. ItemCatalogValidator source covers registering a new source
for an already-unlocked recipe and idempotently granting it, zero-ID rejection,
duplicate/conflicting mappings, and the 4096-entry cap.
Strict MinGW syntax compilation passes; runtime validation, discovery source
consumers and authenticated multiplayer callback wiring remain open.
ItemCatalogValidator cross-checks all 19 card recipe outputs against card
definition mappings, acquisition/knowledge policy, and supported effect/preview
metadata. Unsupported rare items must stay unmapped and discovery-locked.
Strict syntax compilation passes, but validator runtime remains blocked by the
build runner.

Script catalogue host authorization (2026-10-12): GameSession now rejects a
script catalogue unless a separately configured host authorizer approves its
exact bytes and expected manifest hash. It must be configured before session
start; absent authorization fails closed. ItemCatalogValidator source covers
missing, malformed, mismatched and approved catalogues. Strict MinGW syntax
compilation passes; runtime validation and production host manifest
provisioning remain open.

Timed processing station authority boundary (2026-10-12): Starting or replaying
a timed furnace job now requires a pre-start host callback to approve the exact
station coordinates and captured station revision against the current player
position. The version-2 transaction journal binds that context to the request
and retains a reader for version-1 prepared entries. Missing authority or a
rejected station fails before journal or inventory changes. The local
application installs a single-player callback before world startup; it checks
the Furnace voxel, current chunk revision, protected-zone status and six-block
reach. The camera-interaction validator source covers accepted and rejected
authorization cases, including denial inside a protected chunk. Multiplayer
hosts still need authenticated claim and permission providers, and there is no station UI. Strict warning-as-error
compilation passes; runtime authorization and v1/v2 recovery coverage remain
open.

Furnace block identity (2026-10-12): Appended built-in voxel ID 67 as
`voxel:furnace`, added solid/breakable metadata and a stone atlas mapping, and
mapped its block-acquisition result to the existing Furnace item. The voxel
generator version advances to 9. The item catalog now resolves the Furnace
item to this exact placed block and resolves generic self-item blocks by their
stable ID. The item-catalog validator source checks the Furnace mapping and
physical properties; strict warning-as-error syntax compilation passes for
registry, data, item catalog, texture mapping, and validator units.
Revision-checked inventory consumption with world placement, processing UI,
production station authorization wiring, and runtime validation remain open.

Persistent world-cell override foundation (2026-10-12): Session world-cell
state writes format v2 and retains a v1 fluid-state reader. V2 stores known
non-flowing blocks, and the revision-checked replacement stages the override
before publishing a live edit. A recovery setter records a cell before its
chunk streams in; generation reapplies the override and remeshes the chunk.
CameraInteractionValidator source now checks a furnace block roundtrip through
v2 and restoration to the prior air override. Strict warning-as-error syntax
compilation passes for validator, editor, world facade, generation committer,
and GameSession. Ordinary edits do not all persist through the override table,
and migration/recovery runtime validation remains open.

Inventory-backed block placement (2026-10-12): Added a GameSession action that
validates raycast target, range, collision, inventory revision, chunk revision,
and World Revision protection. It journals exact item/block/cell context,
consumes one matching inventory item, updates the persistent block override,
and checkpoints the commit as one recoverable action. Startup recovery restores
prepared placements before chunk streaming. Strict warning-as-error syntax
compilation passes for session, editor, world facade and generation committer.
Secondary mouse input now routes through the transaction and uses the first
matching inventory stack for the selected block. The local application also
configures single-player furnace station authorization before startup; network
hosts still need authenticated claim and permission providers. A
hotbar/inventory-slot UI, persistence for all ordinary block edits, and runtime
retry/failure/recovery validation remain open.

Processing jobs now persist the exact furnace coordinates and captured station
revision in processing-state v2. The v1 reader retains legacy jobs without
station evidence, and tick authorization fails closed for those jobs. Starting
a job uses the reach-aware authorizer; tick progression uses a separate station
continuity validator so players can leave interaction range. Before any batch
advances, GameSession checks all station contexts; a rejected station pauses
that shared batch and discards elapsed processing time. The local continuity
check uses station block identity and protection state without invalidating a
job for unrelated edits elsewhere in the same chunk. Runtime migration and
roundtrip coverage, per-job scheduling, and production multiplayer continuity
policy remain open.

`GameSession::query_station_recipe` now provides an authorized station-aware
recipe query for registered workstations. The host validates exact station
coordinates, revision, and player reach before the catalogue returns recipe
availability and ingredient deficits. At this slice, station craft execution,
local blocks for workstations beyond Furnace, and a crafting UI remained open;
the follow-up entries below resolve the first two foundations.

`GameSession::craft_station_recipe` now runs authorized workstation crafts
through the shared journal and save path. Craft journal v2 binds station
identity and context; v1 hand-craft records still recover. Craft action cache
v2 binds station context for exact retries and reads v1 cache snapshots. Runtime
recovery/cache migration and the crafting UI remain open.

Added built-in Workbench, Paper Mill and Card Press blocks with item drops,
placement mappings and local station-ID validation. The registry now has 71
built-in blocks and generation version 10. Strict syntax compilation passes;
runtime catalog and workstation transaction validation remain open.

Volcano footprint configuration (2026-10-12): GameSession now accepts a
pre-start 9–16 chunk footprint setting. The setting flows through the
generation manifest, paid-square preflight, revision reservation and worker
terrain scaling. Width 9 retains the previous manifest hash; non-default widths
are bound into the manifest. WorldRevisionValidator source now includes a
12-by-12 generation case. Strict MinGW warning-as-error syntax compilation
passes for all touched units. Runtime validation is unverified because the
Git Bash Make runner fails with `NtCreateDirectoryObject ... 0xC0000022` before
compilation. Authoritative boss gating, feature preview, full committed
lighting/support checks and durable lifecycle work remain open.

Volcano Mark progression authorization (2026-10-12): GameSession now requires
host approval for the exact card instance and active revision before the card
is consumed. Authorization is bound by the host to the authenticated session
owner and fails closed when absent or denied. The item-catalog validator source
covers missing/denied/mismatched/approved callbacks; strict MinGW syntax
compilation passes for the session and validator. Runtime validation remains
unverified because the Make runner fails during Git Bash startup. The game still
needs a persistent authoritative boss-completion source and production callback
wiring before this gate can be approved in gameplay.


Volcano Mark configured preview (2026-10-12): RegenerationCardPreview now includes reserved chunk width, and GameSession supplies the active configured 9-16 width for Volcano Mark through a session-aware preview query. Other cards report no footprint, and the static copy describes a configured square instead of always promising 9x9. Strict MinGW warning-as-error syntax compilation passes for RegenerationCardSession.cpp and GameSession.cpp. Runtime preview and feature-highlight rendering remain open, along with the host progression provider, world-generation validation and durable lifecycle contracts.
The ItemCatalogValidator source now covers session preview widths 9, 12 and 16, rejects 8 and 17, and checks that a non-footprint card reports zero. Strict MinGW warning-as-error syntax compilation passes for the session, card adapter and validator units; runtime validator execution remains unverified.
Preview validation now checks the default static preview's footprint metadata, in addition to session-configured widths and invalid boundaries. Strict MinGW syntax compilation passes for the two validators, GameSession and RegenerationCardSession. Retrying mingw32-make through cmd.exe still fails before compilation when Git Bash cannot create its MSYS named object (NtCreateDirectoryObject 0xC0000022); runtime validators did not run.
WorldRevisionValidator now checks the post-feature light buffer is valid and current, and the chunk is render-ready, in both the default 9x9 and configured 12x12 volcano generation cases. Strict MinGW warning-as-error syntax compilation passes. Runtime execution remains unverified; Make cannot start Git Bash because MSYS named-object creation fails with 0xC0000022.
Volcano feature-map preview (2026-10-12): The revision debug map highlights in cyan the exact paid square reserved for Volcano Mark. Preview and generation now share one deterministic anchor helper, including village reservations, paid-chunk selection and configured 9-16 width. WorldRevisionValidator source covers anchor ordering, reservation and bonus-chunk rejection. Strict syntax compilation and a direct MinGW anchor smoke executable pass. Full application runtime/UI validation remains open; the Make runner still fails during Git Bash startup.
The Volcano preview map now spans 17x17 chunks and centers on the pending reserved footprint, so 9-16 chunk widths fit in full. Preflight, debug preview and generation all call the shared paid-square anchor selector; allocation failures from preflight are surfaced instead of being reported as a missing footprint. Strict MinGW warning-as-error syntax compilation and diff checks pass.
The 17x17 revision preview now computes chunk offsets in 64 bits and skips out-of-range coordinates, avoiding wraparound at int32 limits. WorldRevisionValidator source checks previews centered at both int32 extremes, and GameSession validates its map indices before access. Strict MinGW syntax compilation and diff checks pass; runtime validator execution remains unverified due the Git Bash Make startup failure.
The anchor validator now covers maximum 16x16 footprints, deterministic ordering, village reservations and bonus-chunk rejection; a direct native MinGW smoke executable passed those cases. Strict compilation also passes for the int32-boundary preview checks. Full application validators still cannot run through the Make runner.
WorldRevisionValidator now checks that RenderDebug copies preserve feature highlights and clear resets the full 17x17 mask. Strict MinGW compilation and a standalone native smoke executable pass. Full application validator execution remains unavailable through the failing Git Bash Make runner.

World Loom retry behavior (2026-10-12): committed start requests are now resolved from the transaction journal before checking the live revision stage, preserving exact retry success after regeneration starts. The GameSession unit passes strict MinGW syntax compilation; runtime validator execution remains unverified. Durable restoration of World Revision selections and barriers remains incomplete.

World Loom cancellation gate (2026-10-12): WorldRevisionValidator now covers rejection of cancellation after the host start commit succeeds. Strict syntax compilation passes; runtime validator execution is blocked before compilation by Git Bash MSYS object creation failure (0xC0000022). Pending selection/barrier restoration on restart remains incomplete.

World Loom commit idempotency coverage (2026-10-12): The WorldRevisionValidator now replays an accepted confirm request, rejects the same request ID with a changed opening-hand count, and verifies that cancellation remains rejected afterward. Strict warning-as-error syntax compilation passes. Runtime execution remains unverified because Git Bash cannot create its MSYS named object (0xC0000022).

World Loom occupancy feed (2026-10-12): GameSession now accepts a pre-start host provider for all occupied player chunks, refreshes it during world ticks and immediately before a new start commit, and fails closed on provider errors. Binding the network WorldRevisionServer requires that provider; single-player sessions retain camera-derived occupancy. The item-catalog validator checks provider configuration rules. Strict warning-as-error syntax compilation passes. Runtime coverage of provider refresh/commit interaction and the full server validator remain unverified because the Make runner cannot launch Git Bash (NtCreateDirectoryObject, 0xC0000022). Pending selection/barrier restart persistence remains open.

World Loom movement barrier (2026-10-12): WorldRevisionManager now activates a reserved-chunk barrier after the GameSession start commit. PlayerCollision treats reserved chunks as solid for movement into them, permits escape for a player already overlapping the reserved area, and prevents cancellation while the barrier is active. Successful regeneration clears the barrier; generation failure retains the selected coordinates and barrier for safety. WorldRevisionValidator covers entry, escape, cancel rejection, and explicit clearing. All affected units pass strict MinGW warning-as-error syntax compilation. Runtime execution remains unavailable because Git Bash startup fails with NtCreateDirectoryObject 0xC0000022. Barrier visual rendering, network teleport/vehicle hooks, durable restart state, and repair workflow after generation failure remain open.

World Loom generation repair path (2026-10-12): Failed regeneration now retains its selected coordinates, feature plans, and active movement barrier. A trusted host can call World::retry_failed_revision_regeneration or GameSession::retry_failed_regeneration to reset the failed progress record and resubmit the same plan without another opening-hand deal or currency debit. Successful completion clears the barrier. Strict warning-as-error syntax compilation passes for the manager, regenerator, facade, GameSession, movement collision, and WorldRevisionValidator. Runtime recovery remains unverified; visible barrier rendering, durable barrier/plan restoration, and failure repair after process restart remain open.

Barrier status in revision map (2026-10-12): RenderDebug carries and copies the active barrier state, and DebugOverlayRenderer outlines selected cells in bright cyan and labels the map while the barrier is active. The validator checks that this state survives RenderDebug copying and clear resets it. Strict MinGW warning-as-error syntax compilation passes. This is a screen-space map indication only; a world-space glowing boundary mesh and renderer/runtime visual validation remain open.

Barrier map visibility (2026-10-12): GameSession now forces the revision map into the optional overlay while a committed barrier is active, even if the user had hidden the preview. It returns to the user's preview preference once the barrier clears. Strict syntax compilation and diff checks pass; this remains a screen-space cue, not the world-space glowing wall required by the design.

World-space barrier renderer (2026-10-12): The GPU renderer draws additive cyan translucent panels along only the outer perimeter of selected chunks while the committed movement barrier is active. Shared edges are omitted; vertices use the existing camera MVP and bounded dynamic buffer. GpuWorldRenderer.cpp passes strict MinGW warning-as-error syntax compilation. Runtime shader compilation and visual inspection remain unverified, as does a software-renderer equivalent. Teleport/vehicle interception and durable restart recovery remain open.

World Loom committed-plan snapshot recovery (2026-10-12): The embedded World Revision metadata now stores the committed selection, paid/bonus flags, full-generation mode/stage and exact selection revision with a checksum. GameSession restores the rules before reconstituting this plan and enabling its barrier, then refreshes occupancy; an occupied reserved area fails closed. WorldRevisionValidator covers the restore path. Strict MinGW warning-as-error syntax compilation passes for the affected session, manager, facade and validators. Runtime recovery is unverified. Durable deadline/server-draft identity, generation-publication recovery and standalone legacy .wrv lifecycle recovery remain open.

Standalone World Revision lifecycle persistence (2026-10-12): Standalone .wrv save/load now writes and reads the same checksummed committed-selection and barrier envelope as the session snapshot, using atomic replacement. Legacy metadata still loads. The WorldRevisionValidator restore scenario exercises this file path. Strict MinGW syntax compilation passes for all affected units; runtime execution remains unverified. Five-minute deadline/network draft recovery and crash-safe generation publication remain open.


World Loom decision-window deadline persistence (2026-10-13): World Revision lifecycle metadata version 2 adds a host epoch-millisecond deadline while retaining version-1 reads. GameSession requires a pre-start restart-stable authoritative clock provider, sets a five-minute deadline at the committed start boundary, rechecks host time before finalization, and restores the deadline with the saved barrier. The WorldRevisionValidator restore case asserts exact deadline round-trip. Affected manager, facade, GameSession and validator units pass strict MinGW warning-as-error syntax compilation; runtime validator execution remains unverified. Legacy version-1 committed metadata has no deadline and currently follows immediate-finalization compatibility behavior. Network draft identity, atomic generation publication recovery, and other design sections remain open.

World Loom deadline scheduler clarification (2026-10-13): The active GameSession update loop now polls the configured host clock and starts finalization once the persisted deadline is reached, including after restart. Early attempts remain rejected; pending choices defer completion until deterministic expiry handling resolves them. The host must install a restart-stable monotonic clock provider before a committed session is accepted. Strict syntax checks and diff checks pass; runtime behavior is not yet verified.

World Loom deadline choice resolution (2026-10-13): At the five-minute session deadline, GameSession now resolves every still-pending CardGame choice to its validated deterministic default, regardless of a later per-choice deadline. The Libft choice ledger validates all defaults before mutating any choice, so a missing/invalid default leaves the whole pending set unchanged. Focused Libft tests cover early session-deadline defaults, idempotent repeat resolution, and all-or-nothing failure. Strict MinGW warning-as-error syntax compilation passes for the Libft engine/ledger, new test source, RegenerationCardSession, GameSession, WorldRevisionManager, and WorldRevisionValidator; tests were not run.

World Loom deadline mandatory-discard recovery (2026-10-13): Once pending CardGame effects and typed host notices have been handled, finalization now resolves any mandatory hand-discard obligations automatically. It chooses the lowest physical card instance ID, generates a deterministic collision-checked internal request ID in the deadline namespace, checkpoints the change, and rolls back the card snapshot if the checkpoint fails. The deadline path still waits for typed empty-draw/overflow notices to be handled by the host so their configured instability effects are not silently lost; integrating those notice costs into deadline recovery remains open. Strict MinGW syntax compilation passes for the changed GameSession and card-session units; runtime validator coverage is not yet added.

World Loom deadline discard validator coverage (2026-10-13): RegenerationCardValidator now restores a Double Draft snapshot with an outstanding mandatory discard and checks deadline resolution discards exactly the lowest physical instance, clears the obligation, and preserves expected hand/discard counts. Strict MinGW warning-as-error syntax compilation passes for the validator and affected card/session units. Runtime execution remains unverified.

Clock-provider startup gate (2026-10-13): Startup now fails configuration validation when it restores an active committed World Loom barrier without a host clock provider, avoiding a session that could never enforce its persisted deadline. The provider's restart-stability remains a host integration contract; no production multiplayer clock source is wired yet.

Draw notice instability integration (2026-10-13): RegenerationWorldRulesPolicy now carries optional empty-draw and hand-overflow instability deltas, and both nonzero values participate in the content manifest hash. GameSession exposes configure_draw_instability_deltas, bounds the amounts by the chunk-instability maximum, and includes them in restored rules validation. At the deadline, typed draw notices are now processed before acknowledgement: each configured event applies a saturating delta to every reserved chunk, then saves the updated card intent ledger and instability store together. Failure rolls both in-memory stores back; an unset/zero delta still acknowledges the event without changing instability. Strict MinGW warning-as-error syntax compilation passes for GameSession, rules and WorldRevisionValidator. End-to-end notice transaction runtime coverage remains outstanding.

Draw-instability policy validation (2026-10-13): ItemCatalogValidator now checks draw-delta configuration fails before the chunk policy exists, accepts bounded empty/overflow values, rejects negative and over-maximum values, and rejects lowering the instability maximum beneath configured deltas. WorldRevisionValidator confirms both draw deltas change the regeneration manifest hash. The validator sources pass strict syntax compilation; runtime execution has not been performed.

World Loom deadline immutability (2026-10-13): WorldRevisionManager now permits setting the committed deadline once and accepts only an exact idempotent repeat; attempts to extend or shorten it fail with FT_ERR_INVALID_STATE. WorldRevisionValidator source checks that changed deadlines are rejected before saving/restoring the active plan. Strict MinGW warning-as-error syntax compilation passes; runtime execution remains blocked by Git Bash MSYS named-object creation failure.

Item acquisition-policy exhaustiveness (2026-10-13): MinecraftItemCatalog::validate_content_coverage now counts explicit special-item policies plus self/admin block-item policies and requires that count to equal the complete registered item-definition count. It rejects content rules that collide with IDs reserved for built-in/runtime block items, preventing an item from acquiring overlapping policy sources. Catalog initialization already runs this validator, and ItemCatalogValidator invokes it again after runtime registration. Strict warning-as-error syntax compilation passes for the catalog and validator; runtime validation remains blocked by the Git Bash MSYS startup error.

Item acquisition-policy runtime smoke (2026-10-13): A temporary direct MinGW harness linked the current MinecraftItemCatalog.cpp against the existing matching full Libft archive, initialized the catalog, and ran validate_content_coverage successfully (item-catalog-init=0, content-rules=104). This verifies the exact-count exhaustiveness check against the current catalog at runtime; it does not replace the broader item/recipe or full application validators, which remain blocked by the Make runner.

Validator freshness recheck (2026-10-13): The workspace executable was invoked with --validate-item-catalog and reported item-catalog: ok builtin=64 runtime=0, but current source declares 71 built-in policies. This proves the checked-in binary is stale and provides no runtime evidence for current catalog coverage. A fresh mingw32-make -j1 normal attempt failed before compilation because Git Bash repeatedly could not create its MSYS named object (NtCreateDirectoryObject, 0xC0000022); the attempt was interrupted. Continue to treat current item/recipe and World Revision runtime validators as unverified until a fresh build is produced. The direct catalog harness recorded above remains valid only for the catalog object it compiled against its stated Libft archive.

Deck-wide copy-limit enforcement (2026-10-13): card_game_format::validate_deck now totals each card definition across every submitted deck zone before applying its configured copy limit. The main and side zones can no longer each independently consume the full allowance. Added test_card_game_format_enforces_copy_limit_across_deck_zones for 4 allowed / 5 rejected, and a direct MinGW runtime harness linked the edited source and current Libft archive; it passed (cross-zone-copy-cap=ok). Both implementation and test pass strict warning-as-error syntax compilation. The full Libft test runner is still unverified because the workspace Make runner fails during Git Bash startup.

Minecraft recipe-policy runtime coverage (2026-10-13): A focused direct MinGW executable compiled the current MinecraftItemCatalog.cpp and MinecraftRecipeCatalog.cpp plus current Libft game_crafting.cpp, game_inventory.cpp, and game_recipe_book.cpp, linked against the existing support archive, initialized both catalogs, and ran validate_recipe_coverage successfully (recipe-coverage=0 outputs=93 missing=0). This verifies that every current content item whose acquisition policy is recipe-backed has a catalogued recipe output, and every tracked card output satisfies its unique recipe/discovery policy checks. It does not prove all proposed recipes/features in Sections 10-12 exist or that the application-wide validators pass. The regression test enforcing deck-wide copy limits also passes a direct runtime harness (cross-zone-copy-cap=ok).

Spawn paths respect active World Loom barriers (2026-10-13): PlayerCollision::player_box_intersects_world now includes the selected-chunk barrier, so PlayerSpawner's normal candidate check rejects reserved chunks. If no legal spawn candidate exists during an active barrier, the fallback no longer drops the player into the reserved region; it leaves the existing position so an already-present player can escape through normal movement rules. WorldRevisionValidator now checks a vertical-free point inside the selected chunk is blocked and a point outside is not. Strict warning-as-error syntax compilation passes for PlayerCollision, PlayerSpawner and WorldRevisionValidator; the runtime validator has not been run. No authoritative teleport or vehicle movement API exists in this client/server code yet, so those hooks remain open for future movement entrypoints.

Barrier-aware spawn regression (2026-10-13): WorldRevisionValidator now exercises PlayerSpawner while the committed barrier is active. It accepts either a spawn outside all barrier geometry or an unchanged in-barrier position if no safe candidate is available; it rejects a relocated spawn still intersecting the barrier and rejects the unsafe in-place drop-to-ground fallback. Strict warning-as-error syntax compilation passes for the validator, PlayerCollision and PlayerSpawner. Runtime execution remains unverified because the application build cannot start Git Bash in this workspace.

Build-path recheck (2026-10-13): Running mingw32-make -j1 normal via cmd.exe reproduces Git Bash NtCreateDirectoryObject access denied (0xC0000022); invoking WSL for the same workspace is denied by the environment (Wsl/E_ACCESSDENIED). Neither alternate shell reaches compilation. Continue using direct MinGW compilation and focused harnesses for source-level slices, while treating application validators as unverified until the workspace can launch its supported build runner.


Validator sweep and stale expectations (2026-10-13): A direct MinGW link assembled a fresh application from current Minecraft objects, all current CardGame objects, current voxel registry/data, and targeted current Libft Game units. `--validate-item-catalog` passes (71 built-ins, 0 runtime, 104 content policies); `--validate-transaction-journal` passes. The catalog validator now runs its synthetic runtime-block mutation last, after production coverage checks, and its Shipwreck Chart craft fixture grants the rare recipe through discovery first. RegenerationCardValidator now locates imported rare items by physical inventory slot and searches the discard pile for the lowest-ID automatic discard. Its remaining snapshot check detects non-identical RCS bytes after restore/replay; the physical mapping and exact request replay succeed, but persistence round-trip still needs investigation. WorldRevisionValidator fixtures were corrected for variable biome counts and zero-based new-record revisions; the sweep now advances to a separate magma-feature generation failure. `--validate-world-items` still terminates with an access violation. Strict warning-as-error compilation passes for the three edited validator sources. The full Libft suite and application-wide `--validate-all` remain unverified.

RegenerationCard validator stack and snapshot verification (2026-10-13): `RegenerationCardSession` is 502,176 bytes on this build; the physical-import validator held five sessions locally, exceeding the Windows default thread stack. It now allocates those fixtures on the heap with explicit allocation-failure handling. Strict MinGW warning-as-error syntax compilation passes. A focused executable built from current session, CardGame, inventory and validator sources ran the exact physical-import validator successfully, including three physical-card imports, script context, restored mapping, exact retry, and byte-identical RCS snapshot replay. This resolves the reported RCS byte mismatch for that path. The full Libft suite, other application validators and `--validate-all` remain unverified.

Regeneration Card validator recovery and Volcano Mark preflight (2026-10-13): The physical-import validator now heap-allocates its five 502 KB session fixtures, preventing Windows stack overflow; its RCS serialize/restore/retry byte comparison passes. The World Rules validator’s second Surveyor’s Reach fixture now uses a distinct chunk coordinate so it exercises the configured cap, and `RegenerationWorldRules::preflight_card_effect` admits the special Volcano Mark definition (15) through its ID guard. The current RegenerationCardValidator executable passes end to end (`regeneration-cards: ok`, result 0); both changed sources pass strict MinGW warning-as-error compilation. Full Libft tests and application-wide validators remain unverified.

CardGame host-intent snapshot and replay coverage (2026-10-13): The byte snapshot test now restores into an engine with the same registered callback/rules manifest as the source; the previous fixture omitted a callback whose presence is intentionally included in the rules hash. Its direct MinGW runtime harness passes checksum rejection, snapshot restore, pending records, hash equality and no-mutation-on-invalid-snapshot checks. The multi-draw serialized replay test now compares each pending host-intent sequence, schema ID/version, field type and field value after replay deserialization; its direct MinGW runtime harness passes. Both test files pass strict warning-as-error syntax compilation. The full Libft suite remains unverified; direct linking its existing response file fails because its object/archive set has mismatched sections and missing renderer symbols.

Libft integration update (2026-10-13): sequenced CardGame commands now include choice selection with the server-provided epoch, idempotent retry semantics, and versioned command/replay persistence. Focused direct runtime validation passed; standard test-runner execution remains blocked by Git Bash named-object startup failure. See the detailed evidence and remaining action-contract gaps in the linked integration design ledger.

CardGame choice actions now take their deadline epoch from the trusted host API rather than the client payload; exact retries preserve the original commit. Authenticated host wiring and logged expiry handling remain open. Details are in the Libft integration ledger.

Logged deadline resolution now has an engine command with server actor identity, authoritative epoch, idempotent request IDs, result counts, and versioned replay. Minecraft's World Loom deadline scheduler still needs to issue that action and persist its request lifecycle.

World Loom sequenced choice expiry (2026-10-13): RegenerationCardSession routes expired-choice resolution through the replayable host-only CardGame command, retaining its request ID and original command sequence across retries. GameSession checkpoints the command, and deadline finalization now uses the same logged path. The internal deadline request ID is reserved from the public expiry API. Strict MinGW warning-as-error syntax compilation passes for the changed units. Runtime validation and authenticated network scheduler wiring remain open.

World Loom expiry cache follow-up (2026-10-13): Runtime testing showed the CardGame engine snapshot does not retain command records, so engine-log lookup alone could not provide expiry retries after restore. RegenerationCardSession now persists a bounded expiry result cache in RCS1 v6 (v1-v5 remain readable), returns cached results across later host epochs and after restore, and rejects request ID collisions with play/draw/discard action IDs. The independent direct MinGW session harness passes first submission, exact retry, restore, retry after restore, and unchanged post-retry session snapshots. Strict warning-as-error compilation passes. The supported Make build still fails before compilation because Git Bash cannot create its MSYS named object (NtCreateDirectoryObject 0xC0000022).

World Loom host clock boundary (2026-10-13): The public GameSession expired-choice API no longer accepts a caller-supplied epoch. It reads from the configured authoritative clock provider and persists the expiry result even when zero choices expired. Deadline finalization uses that same provider and transaction path. Strict MinGW warning-as-error compilation passes; runtime coverage of GameSession clock-provider rejection is still outstanding.

World Loom expiry compatibility (2026-10-13): The focused direct MinGW harness also downgrades an RCS1 v6 snapshot by removing its expiry-cache section, changes the format version to v5, recomputes the checksum, and confirms the v5 reader restores it. The GameSession public expiry API reads only the configured host clock and snapshots even zero-result actions; strict syntax compilation passes. The full application validator remains blocked at the Make shell startup stage.

Direct regeneration validator after RCS1 v6 (2026-10-13): A fresh direct MinGW link using the current GameSession, adapter, persistence and validator objects plus the matching CardGame command object passes --validate-regeneration-cards end to end, including the new retry/cache and v5 compatibility cases. The fixture now removes the v6 empty-cache count when deriving a v1 legacy snapshot. The same direct-linked binary exits with Windows stack-overflow status during --validate-world-revision; that validator remains unverified. The supported Make runner still fails before compilation at Git Bash MSYS object creation.

World Revision metadata decoder and stack-frame fix (2026-10-13): The committed-barrier restore fixture had a 2.5 MB stack frame from two local World instances; heap allocation reduces the measured frame to 1.9 KB. Its next run found the v2 lifecycle metadata decoder subtracting the already-consumed deadline field twice, rejecting the saved extension. Removing the extra subtraction allows the barrier-restore test to pass. The broader direct-linked World Revision validator now progresses beyond that point and fails a later server helper with FT_ERR_INVALID_STATE. Strict MinGW warning-as-error compilation passes; full validator remains open.

World Revision validator follow-up (2026-10-13): Corrected stale authorization, bonus-pricing, occupancy-revision, cost-policy revision, and cumulative Deep Seam expectations in the validator fixtures. Current WorldRevisionManager, WorldRevisionServer, and WorldRevisionValidator objects were directly linked into a fresh application; --validate-world-revision passes (world-revision: ok regenerated=1 skipped=0). The supported Make runner still fails before compilation because Git Bash cannot create its MSYS named object. Full --validate-all, production multiplayer scheduler/authentication wiring, persistent reservations, and atomic resource/CardGame/world recovery remain unfinished.

World Revision validator follow-up (2026-10-13): Corrected stale authorization, bonus-pricing, occupancy-revision, cost-policy revision, and cumulative Deep Seam expectations in the validator fixtures. Current WorldRevisionManager, WorldRevisionServer, and WorldRevisionValidator objects were directly linked into a fresh application; --validate-world-revision passes (world-revision: ok regenerated=1 skipped=0). A second fresh link with current CardGame commands, GameSession, adapter, and both validators passes --validate-regeneration-cards and --validate-world-revision. The supported Make runner still fails before compilation because Git Bash cannot create its MSYS named object. Full --validate-all, production multiplayer scheduler/authentication wiring, persistent reservations, and atomic resource/CardGame/world recovery remain unfinished.

World-item validator stack follow-up (2026-10-13): Heap allocated the three large card-session fixtures in WorldItemStoreValidator::validate and two nested card-transfer fixtures. Measured validator frames fell from 1,532,096 + 1,010,720 bytes to 25,424 + 6,272 bytes. A fresh direct-linked --validate-world-items run now avoids stack overflow but fails the v2 recipe-book roundtrip: recipe 1001 is locked after restore. The direct-linked --validate-all sweep also stops early at block-edit emissive-delete convergence with timeout 59. These gates remain open while the recipe-book persistence failure and global validator timeout are investigated.

World-item persistence validation update (2026-10-13): Corrected the snapshot validator to expect the MCSP game-session magic; v2 recipe-book save/load now passes. Fixed GameSessionPersistence v11 metadata slicing so fluid-state bytes are not appended to revision metadata. Updated the metadata validator for the current stored-section flags and improved snapshot stage diagnostics. A fresh direct MinGW link passes --validate-world-items end to end, including session snapshots through v11 and pickup, craft, processing, and card-transfer recovery. Changed persistence and validator sources pass strict warning-as-error compilation. Application-wide --validate-all and the full Libft test suite remain unverified; the prior --validate-all attempt timed out in block-edit emissive-delete convergence.

World Loom committed retry guard (2026-10-13): A committed World Loom start retry now requires an active barrier and nonzero stored deadline while the revision remains pending before it checkpoints and returns success. The session decision-window flag is restored only while the revision is pending and not regenerating, preventing a stale committed retry from reactivating it after generation begins or completes. GameSession.cpp passes strict MinGW warning-as-error compilation; current direct-linked --validate-world-revision and --validate-regeneration-cards pass. `WorldRevisionValidator` now starts a headless `GameSession`, seeds a committed start record, retries it with no pending revision, and checks the decision flag and barriers remain inactive. `GameSession.cpp` and `WorldRevisionValidator.cpp` pass strict MinGW warning-as-error compilation. Runtime execution remains unverified because `mingw32-make -j1 normal` fails before compilation when Git Bash cannot create its MSYS named object (`NtCreateDirectoryObject ... 0xC0000022`). Prepared starts remain safely aborted from the pre-mutation snapshot; draft recovery and crash-durable generation publication remain open.

Entity lifecycle foundation (2026-10-13): Added `WorldEntityStore` for monotonic entity IDs, atomic group spawning, revision-checked updates/despawn, bounded snapshots, and checksummed WENS v1 serialize/restore. The `--validate-world-items` path checks entity snapshot roundtrip, successful batch IDs, invalid-batch no-partial-mutation, stale revision rejection, checksum rejection, and no mutation on corrupt restore. Strict MinGW compilation passes; the direct-linked runtime validator passes these cases. The store is now connected to GameSession snapshots in MCSP v12, with older snapshots restoring an empty entity store. World/chunk lifecycle, mob AI, spawners, and loot remain unimplemented and required for the rare-card effects.

GameSession entity snapshot integration (2026-10-13): Added MCSP v12 with a bounded entity-state section after fluid state, backward-compatible loading for versions 1-11, GameSession startup restore, and snapshot serialization. The world-item validator covers a v12 entity roundtrip and v11 loading through the extended API with an empty entity section. All changed translation units pass strict MinGW warning-as-error syntax compilation. A fresh direct-linked application using current Minecraft and Libft objects passes `--validate-world-items` end to end (`world-items: ok`), including the v12 test, and also passes `--validate-regeneration-cards`, `--validate-world-revision`, and `--validate-item-catalog` (71 built-in, 104 content policies). The standard Make runner still cannot launch because Git Bash fails to create its MSYS named object. World/chunk lifecycle, mob AI, spawners, and loot remain open.

Hostile spawner lifecycle foundation (2026-10-13): Added a bounded, checksummed WSP1 schedule store with per-spawner alive caps and durable pending-death events. GameSession advances schedules on its tick and stores the entity and spawner snapshots together in MCSP v13/WEC1; MCSP v1-v12 and legacy WENS entity payloads remain readable. A World Revision validator restores a non-empty spawner, confirms it remains idle before its deadline, and confirms a hostile entity appears at the saved deadline. Strict MinGW warning-as-error compilation and a fresh direct-linked `--validate-world-revision` pass. Mob AI, loot-table resolution and atomic death-to-drop delivery, spawner feature generation, and rare-card planners remain open.

Spawner death-to-drop integration update (2026-10-13): GameSession resolves persistent death events into ground items for loot-table IDs 1, 2, and 3 (zombie rotten flesh, skeleton bone, creeper gunpowder), removes the tracked corpse when present, and acknowledges the event in the same MCSP v13 checkpoint. Failed processing or saving rolls the in-memory stores back so the pending event can retry. Loot tables currently emit one deterministic item; mob AI, weighted loot, player attribution, and rare-card structure planners remain incomplete. Strict MinGW warning-as-error syntax compilation passed for touched units. Runtime integration validation remains outstanding because the standard Make runner fails during Git Bash/MSYS startup (`NtCreateDirectoryObject ... 0xC0000022`).


Mob loot catalog update (2026-10-13): Extracted mob drops into a bounded MinecraftLootCatalog. Table IDs resolve deterministically by entity ID; the skeleton table drops bones plus string and zombie/creeper tables drop their registered world-acquisition items. Results are bounded, repeatable after death-event restore, and reject unknown tables and zero IDs. Strict warning-as-error syntax compilation passes for the changed units, and a focused executable smoke passes deterministic, retry, and rejection checks. Full GameSession checkpoint runtime coverage remains open.


Death-delivery validator update (2026-10-13): Added a GameSession regression path that stages a skeleton death, checks the bone/string ground items, corpse removal and event acknowledgement, then restarts the session and verifies the drops restore with no pending duplicate event. The changed validator and session pass strict warning-as-error compilation. The app runner still fails during Git Bash/MSYS startup, so this full integration validator is not yet executed.


Direct-link validation diagnosis (2026-10-13): Current strict warning-as-error syntax compilation passes for the death/loot implementation and validator. The focused loot-catalog smoke passes. The manually assembled World Revision executable fails before the new death test because startup reports MinecraftItemCatalog::initialize() error 7. A fresh catalog harness linked with the available full Libft archive reproduces the error; the older catalog-count executable succeeds with prior sources, so the mixed/stale link cannot validate current catalog integration. Full session restart/death-path runtime evidence still requires a consistent current Libft build.


Death checkpoint rollback coverage update (2026-10-13): Expanded the GameSession regression fixture to force a failed checkpoint, verify item/entity rollback and retained death-event retry, then retry successfully and check the persisted drops after restart. Strict warning-as-error syntax compilation passes. Runtime execution remains blocked before this test by the manually linked binary's catalog-initialization failure; Make still fails during Git Bash/MSYS startup.

Loot chance correction (2026-10-13): Skeletons always drop one to two bones and have a deterministic 50% chance to drop one to two string. Validators now cover both the string and no-string outcomes and compare death-delivery/restart results with the loot catalog output. Runtime integration remains unverified pending a consistent Libft build.

Loot failure-output contract validation (2026-10-13): WorldItemStoreValidator now verifies unknown loot-table rejection preserves the caller's existing drop vector and zero entity IDs are rejected. The focused loot executable passes both deterministic skeleton outcomes and output preservation; strict warning-as-error syntax compilation passes for the catalog and affected validators. Full GameSession death-checkpoint runtime validation remains unverified.

Spawner loot integration runtime follow-up (2026-10-13): The direct-linked World Revision validator now passes the death-delivery path, including injected checkpoint failure, item/corpse rollback, pending-event retry, successful checkpoint and restart restore. This exposed and fixed two defects: mob-drop IDs collided with shield/staff/armor item IDs (moved to 210130-210133), and committed World Revision restore rejected the reconstruction-time selection revision instead of restoring the saved authoritative revision after matching selected chunks. The same fresh executable passes `--validate-item-catalog` (71 built-ins, 108 content policies) and `--validate-regeneration-cards`. Its separate `--validate-world-items` path still fails in card-transfer journal recovery with FT_ERR_NOT_FOUND against the available Libft link.

World-item recovery link diagnosis (2026-10-13): Added stage reporting to the card-transfer recovery validator. With the current direct-link response and Libft archive, the first prepared card-transfer checkpoint returns success but loading its snapshot reports FT_ERR_NOT_FOUND. This isolates the remaining validator failure to the save/load artifact path before recovery logic; a fresh complete Libft build is still needed to establish whether the cause is stale linked file support or another fixture issue.

Snapshot-loader and world-item runtime correction (2026-10-13): MCSP loading retained FT_ERR_NOT_FOUND from its expected legacy-format probe, then returned it for version-9 snapshots that do not enter the later entity-validation branch. Reset the probe result before parsing MCSP. The card-transfer validator now confirms the prepared snapshot exists before load. A fresh direct-linked executable passes `--validate-world-items`, including card-transfer exact retry, and also passes `--validate-world-revision`, `--validate-item-catalog` (71 built-ins, 108 content policies), and `--validate-regeneration-cards`. The supported Make runner remains unable to start its bundled Git Bash/MSYS shell.

Regeneration result metadata rollback (2026-10-13): WorldRegenerationResultApplier now preserves the previous live chunk while publishing a replacement and its per-chunk generation/feature-outcome record. Read-state or metadata publication failure restores the prior chunk; geometry-change notification occurs after metadata commits. Strict MinGW warning-as-error compilation passes, and a fresh direct-linked --validate-world-revision passes (world-revision: ok regenerated=1 skipped=0). This is in-memory rollback only; crash-atomic cross-store publication, full validator sweep, and the remaining rare-card planners are still open.

Surface Magma capacity and placement reservation (2026-10-13): Each planned magma pool now gets its own selected chunk after village reservations and before the paid Volcano square is chosen. GameSession preflights applied plans, pending feature-card intents and the card being played against selected capacity before it is consumed; feature preview reserves magma chunks from Volcano footprints too. Strict MinGW warning-as-error compilation passes, and direct-linked --validate-world-revision and --validate-regeneration-cards both pass. Rare-card structures, full app-wide validation and durable multi-system recovery remain incomplete.
The focused validator now directly exercises the shared distinct-chunk reservation primitive, including skipping a village-reserved chunk for magma and failing cleanly when all selected chunks are reserved. The rebuilt --validate-world-revision passes.

Livestock wandering slice (2026-10-14): Added a bounded server-side animal
simulation for cows, pigs, chickens and sheep. Every 10 ticks, nearby animals
choose deterministic wander directions from entity ID and tick epoch, step only
onto loaded supported space with clear body/head cells, and update persisted
position, velocity, yaw and action state through revision-checked entity-store
updates. Simulation is capped at 256 nearby entities and saves changed animal
state on the existing 600-tick session checkpoint. The world-item validator
checks the tick interval, repeatable movement across identical stores, and
state preservation after entity snapshot restore; the full direct-linked
`--validate-world-items` run passes. This is wandering only: follow/flee and
flocking behavior, natural habitat population, breeding/age, player interaction,
and animal rendering/replication remain open.

Chicken egg acquisition slice (2026-10-14): Chickens now produce one egg on a
deterministic per-entity interval from 6,000 to 11,999 server ticks (five to
ten minutes at 20 ticks per second). The persisted animal tick prevents a
second egg on retry; the egg and updated entity snapshot commit through the
same session save. Failed item creation or checkpoint restores the ground-item
store and rewinds the chicken tick so production can retry. The World Revision
session validator injects a checkpoint failure, verifies rollback and retry,
then restarts and confirms exactly one egg is present. Strict MinGW compilation
and direct-linked `--validate-world-revision` pass. Egg timing is fixed for now;
configuration and client presentation remain open.

Boat entity identity validation hardening (2026-10-13): Boat-only item identity is now enforced by spawn, revisioned update and WENS deserialization. The fresh direct-linked `--validate-world-items` run passes malformed identity, no-mutation, and legacy snapshot checks. Cross-store boat transfer and movement remain open.

Physical item allocator coverage for entities (2026-10-13): The host's next-ID synchronization now includes physical IDs held by item-backed world entities, preventing a newly minted craft item from reusing a placed boat's ID. Direct-linked world-item, World Revision, regeneration-card, and item-catalog validators pass against the updated session and entity-store objects. Boat transfer actions and movement remain open.

Entity/inventory/ground identity uniqueness (2026-10-13): Session restore and checkpoints now reject a physical item ID duplicated between inventory, ground items, and item-backed entities. The direct-linked world-item, World Revision, regeneration-card and item-catalog validators pass. Card-session identity cross-checking and the boat transfer actions remain open.

Global physical-item identity check (2026-10-13): Session restore and checkpoints now include the CardGame physical-item mapping in duplicate checks alongside inventory, dropped items and item-backed entities. Direct-linked world-item, World Revision, regeneration-card and item-catalog validators pass. Boat placement/retrieval and movement remain open.

Physical item identity in entity snapshots (2026-10-13): WENS v2 persists optional entity-linked physical item IDs, with WENS v1 backward-compatible loading. Boat entities use an item-bearing spawn API, and duplicate physical IDs are rejected. The world-item validator proves the ID survives MCSP save/load and old WENS v1 records load as non-item entities. Strict warning-as-error compilation and direct-linked --validate-world-items pass. Boat inventory/entity transfer actions and movement remain open.

Boat inventory/entity transfer (2026-10-13): GameSession now places an identified boat item from inventory onto validated water and retrieves that same item from a reachable boat entity. Transfers use expected revisions, a journal, exact request retries, checkpoint rollback, and startup recovery. Strict compilation and direct-linked world-item, world-revision, regeneration-card, and item-catalog validators pass. Client interaction/rendering, steering/movement, and multiplayer ownership/authorization remain open.

Livestock death-drop acquisition foundation (2026-10-13): Persistent scheduled entities now support cows, pigs, chickens, and sheep. Their deterministic loot tables produce raw meat and the relevant leather, feather, or wool drops, and a GameSession integration validator confirms cow drops pass through the death checkpoint into the ground-item store. Strict compilation plus direct-linked world-item, World Revision, regeneration-card, and item-catalog validators pass. Animal AI/natural spawning/breeding and crop growth/harvest remain open.

Crop and fruit acquisition foundation (2026-10-14): The Voxel registry now includes immature/mature wheat and world-only farmland. A recipe-acquired wood hoe invokes revision-checked, journaled dirt/grass tilling from secondary click; the session checks inventory ownership, target reach, protection and revisions, and delegates prepared-record recovery/rollback to `WorldFarmlandTransaction`. `--validate-wheat-crop` passes world tilling, stale revision, dry/hydrated farmland checks, wheat/seed harvest, fruit drops, exact transaction retry, payload mismatch rejection, hoe preservation, failed-checkpoint rollback and prepared-record recovery; `--validate-item-catalog` passes with 74 built-ins/109 content policies. The full `--validate-world-items` suite now passes end-to-end, including mining integration and persistence/recovery cases. Strict warning-as-error compilation and `git diff --check` pass. The validator covers the transaction helper but not the live mouse/session authorization path. The bounded near-player growth tick now requires a water source within four horizontal blocks and at soil level or an adjacent vertical level. Configurable hydration/growth parameters, light thresholds, persisted per-chunk growth deadlines/offline policy, paper plant acquisition and gameplay/render validation remain open; the broader block-edit suite still times out earlier in emissive-delete convergence.
Paper-reed renewable growth slice (2026-10-14): Added append-only `voxel:paper_reed` (ID 74) and `voxel:paper_reed_immature` (ID 75), metadata and texture/color entries, plus fiber/no-drop policies. Deterministic decoration generation places immature reeds on suitable shoreline ground within four horizontal and one vertical block of surface water; generator version 12 records the terrain change. The bounded server crop tick matures supported reeds near water. Harvesting a mature reed yields fiber and returns the plant to its immature state; failed drop publication restores maturity. `--validate-wheat-crop` covers harvest-to-immature and runs the actual `GameSession` growth routine to verify dry reeds stay immature and nearby water allows maturation. Item-catalog validation passes with 76 built-ins/109 content policies, and terrain determinism validation finds a natural reed in seeded wet-world samples. Strict warning-as-error compilation passes. Paper-reed planting, persisted growth deadlines/offline policy, light thresholds and gameplay/render validation remain open.

Wheat seed planting integration (2026-10-14): The gameplay-selected seed path
now has runtime coverage in the World Revision session validator. It tills a
real supported dirt/grass cell, aims the live camera interaction at the cell,
places immature wheat through `try_place_selected_inventory_block`, and checks
that the seed is consumed and the block appears. This exposed a protection
interaction: tilling marks the edited chunk generation-protected, after which
the generic placement guard rejected planting there. Placement now permits
immature wheat on farmland in generation-protected chunks while retaining
Loom, manual and occupied-chunk denials. Strict MinGW compilation and the
direct-linked `--validate-world-revision` pass. Paper-reed planting, crop
deadline/offline persistence, light thresholds, and gameplay/render validation
remain open.

Paper-reed planting integration (2026-10-14): Paper-reed fiber now resolves to
the immature plant. The selected-block placement transaction checks suitable
grass/dirt/mud/sand support and nearby loaded water, consumes fiber atomically
with the block placement, and permits generation-protected crop placement while
retaining Loom, manual, and occupied-player protection. Catalog mapping checks
and a live selected-item fixture verify dry-site rejection, water-source
acceptance, placement and fiber consumption. Fresh direct-linked item-catalog
and World Revision validators pass. Growth deadline/offline persistence,
configurable crop policy, and broader gameplay/render validation remain open.

Authoritative crop light gating (2026-10-14): Added `World::light_level_at`,
which reads combined sky/block light only when the loaded chunk's light buffer
matches its content and lighting revisions. Wheat and paper reed require level
9 to mature; stale or missing light data prevents growth. The crop validator
checks stale, dark and bright lighting states and passes, as does the fresh
World Revision validator. Strict warning-as-error compilation passes for the
changed production and validator units. Configurable crop growth/light policy,
persisted deadlines/offline behavior, and gameplay/render coverage remain open.

Configurable crop growth policy (2026-10-14): Added a pre-start host setting
for crop tick interval, deterministic maturity-roll divisor, and separate
wheat/paper-reed minimum light levels. Defaults retain 200 ticks, one-in-three
growth rolls, and level 9 light. The crop validator rejects invalid settings
and verifies a custom reed threshold of 12 using dark, level-11, and level-15
conditions. Fresh direct-linked crop and World Revision validators pass, as
does strict warning-as-error compilation. Host config identity on session
restore, growth deadlines/offline behavior, hydration-radius settings, and
gameplay/render coverage remain open.

Crop policy persistence (2026-10-14): MCSP v14 persists the validated crop growth interval, roll divisor and species light thresholds. Existing MCSP v1-v13 loads retain default policy values. A v14 non-default roundtrip and v15 rejection fixture are in the World Item Store validator; strict syntax compilation passes. A standalone direct-linked v14 save/load smoke test passes with non-default values. It caught and fixed an entity-section boundary bug. The broader --validate-world-items run has not returned an exit result from the runner. Crop deadline/offline behavior and configurable hydration radius remain open.

Configurable crop hydration radius (2026-10-14): Crop policy now controls water radius 0–8 for wheat farmland hydration and paper-reed growth. MCSP v15 saves it; v14 loads with radius 4. Focused crop tests reject invalid radius and confirm radius-dependent water eligibility and growth; the standalone v15 save/load smoke test and strict production syntax checks pass. Per-chunk growth deadlines/offline catch-up and gameplay/render validation remain open.

Crop growth scheduling (2026-10-14): Added per-chunk due ticks and persisted crop policy/world simulation time in MCSP v16. The scheduler evaluates only due nearby chunks, checkpoints deadline and crop changes together, and pauses simulation-time growth while offline (no offline wall-clock catch-up). A stale deadline causes one growth attempt before being rescheduled. Focused crop validation, v14/v15/v16 persistence smoke, strict validator compilation, and git diff --check evidence recorded; full world-item suite was not observed to finish through the current launcher. Gameplay/render validation remains open.

Validation follow-up (2026-10-14): The v16 direct-linked binary passes item-catalog and regeneration-card validators, but --validate-world-revision now fails when the Volatile Stabilizer case restores a session (start returns FT_ERR_INVALID_STATE). This is a possible session-restore regression requiring isolation; broad world-item invocation still does not return. Keep crop scheduling integration validation open.

Restore validator link correction (2026-10-14): Rebuilding WorldRevisionValidator.cpp against current headers and relinking makes --validate-world-revision pass. The prior Volatile Stabilizer error came from a stale validator object. The fresh --validate-world-items invocation currently exits with xC0000374 and no diagnostics, so that suite remains unresolved.

Validation repair (2026-10-14): Closed the v15 snapshot input stream before the v15-to-v14 atomic compatibility rewrite. A fresh, serial direct-linked --validate-world-items run now passes end to end with the MCSP v16 roundtrip, v17 rejection and recovery cases. --validate-world-revision, --validate-wheat-crop, item catalog, regeneration cards, and v14/v15/v16 persistence smoke also pass. The previous restore failure used a stale validator object; concurrent test attempts had also collided on fixed temporary files. The design's remaining multiplayer, rare-card, catalogue, UI, and performance requirements are still open.
Rare-card source audit correction (2026-10-14): The earlier gap statement below is superseded by current source. The six rare items now map to stable card definitions and previews; GameSession and persisted rules handle the rare effect intents; the generation worker contains Shipwreck, Desert Pyramid, Ancient Canopy and Spider Nest structures, and WorldRevisionRegenerator handles the added feature and modifier paths. These source slices are not yet evidence of complete runtime publication, restore behavior, economy, or validation. Keep those acceptance requirements open until the deferred implementation verification pass is complete.

Spawner batch registration and rollback foundation (2026-10-14): Added bounded WorldSpawnerStore add_batch and remove_batch operations for multi-spawner structures. Removal is all-or-nothing and rejects live or pending-death spawners. Validator source covers input rejection and rollback; strict warning-as-error syntax compilation passes. Runtime validation remains open and no rare-card planner uses the APIs yet.

World regeneration store attachment (2026-10-14): Active World now borrows the session-owned entity and spawner stores through an explicit attach/detach lifecycle so generation publication can use the authoritative state. Strict warning-as-error syntax compilation passes. Generated spawner publication/rollback and rare-card planner integration remain open.

Generation-result spawner payload path (2026-10-14): Feature plans carry value-only spawner definitions through worker results. The result applier filters them by chunk and batch-registers after chunk publication; instability metadata failure removes the batch and restores the prior chunk. Plans are capped at 16 spawners and 4096 feature plans per request. A validator fixture covers payload transport. Strict syntax compilation passes; live publication/rollback validation and rare-card planner use remain open.

Ruined Village Relic rules slice (2026-10-14): Added the manifest-bound instability cost, bounded plan count, accepted intent and save/restore plan reconstruction for definition 17. Focused runtime rules smoke and strict syntax compilation pass. The card remains unavailable; generation reservation, ruined structure and spawner payload work remain open.

Ruined Village Relic vertical-slice progress (2026-10-14): Definition 17 is now registered and previewable, mapped from its physical card item, and included in card preview/catalog validator fixtures. GameSession reserves a distinct selected chunk, accounts for pending features, and applies the configured manifest-bound instability cost. Regeneration generates a damaged structure/gold cache and transports hostile zombie/skeleton spawner definitions through chunk publication; a dedicated deterministic loot table gives the skeleton spawner rare emerald/diamond drops. The cache's emerald reward is staged as an invisible value item in the session WorldItemStore and published with the chunk/spawners, with compensating removal on commit failure. Loot payload size and worker structure/transport have validator fixtures. Strict warning-as-error syntax compilation passes. Runtime generation/publication, loot distribution, and rollback remain unverified: make crashes during process initialization through both PowerShell and cmd.exe in this workspace. The feature remains an incomplete slice and the other five rare cards remain untouched.

Per-recipe furnace policy implementation (2026-10-14): `MinecraftRecipeCatalog` and `GameSession` now expose pre-start configuration for each of the seven smelting/cooking recipe IDs: fuel item, fuel units per output, and processing duration. Defaults remain coal x1 and 200 ticks. Processing-state v4 persists the full policy set and rejects restore under mismatched host configuration; legacy formats retain default-policy compatibility. Implementation is not yet compiled or runtime-validated; verification remains deferred until the implementation pass is complete.

Durable block mining/drop implementation pass (2026-10-14): GameSession
mining now records a prepared `WorldBlockMiningTransaction` before editing the
voxel or publishing its item drops, then checkpoints the committed block,
world-item and journal state. The record carries both block states, coordinates,
revision and exact drop stacks; startup recovery replays prepared operations.
Ordinary mined block changes now use the saved block-override path. This source
change is not compiled or runtime-validated; verification remains deferred
until the implementation pass is complete.

Hostile attack implementation pass (2026-10-14): WorldAnimalSimulation now
emits deterministic attack events for its four hostile kinds when they have
line of sight to the player. GameSession applies damage to the character and
checkpoints the health change; new characters start with 20 hit points and
restored characters retain saved health. Attack cadence derives from the
persisted world tick and entity ID. Death/respawn rules, combat balance,
compilation, and runtime validation remain open. This supersedes the earlier
entry that deferred all hostile attacks.

Durable block mining/drop implementation pass (2026-10-14): GameSession mining
now records a prepared `WorldBlockMiningTransaction` before editing the voxel
or publishing its item drops, then checkpoints the committed block, world-item
and journal state. The record carries both block states, coordinates, revision
and exact drop stacks; startup recovery replays prepared operations. Ordinary
mined block changes now use the saved block-override path. This source change
is not compiled or runtime-validated; verification remains deferred until the
implementation pass is complete.

CardGame action identity follow-up (2026-10-14): Schema v3 scopes idempotent
action requests to the persisted GameSession ID. Command-log v10 and replay
format v9 preserve that identity, and the Minecraft adapter binds host expiry
actions to it before restoring card state. Existing replay/action test source
now covers ID roundtrip. Compilation and runtime checks remain deferred until
the implementation pass is complete.

Regeneration action routing follow-up (2026-10-14): Session-bound draw and
discard actions now use CardGame's idempotent action API and retain their
session/request IDs in command history. Standalone adapter callers without a
GameSession binding keep the prior command route. Card-play choice data still
needs a replayable command representation; this work remains uncompiled and
untested.

Generic crafting-tag implementation pass (2026-10-14): Libft `game_crafting`
now registers bounded stable item tags before recipes and expands tagged
ingredients into immutable alternatives at registration. Minecraft registers
its logs, planks, crystal, ore-sample and flower-pigment tags; recipes use the
plank, crystal-sample and flower-pigment tags. This source change has not been
compiled or runtime-validated; verification remains deferred until the
implementation pass is complete.

Pending-choice query implementation pass (2026-10-14): Libft now exposes
unresolved choices only to their owning player through a read-only query. The
view carries stable option IDs, option values, kind and deadline, and omits
timeout defaults and resolved state. The API preserves the caller's prior
output on allocation failure. The source change is not compiled or
runtime-validated; verification remains deferred until the implementation
pass is complete.

Legal-action query implementation pass (2026-10-14): Libft now reports
phase-allowed command kinds and pending mandatory-discard/choice state for a
player without executing effects or advancing RNG. Action submission pauses
other commands during a pending choice and allows only its owner or trusted
expiry; a pending discard takes priority. Card-specific target legality and
unavailable/no-effect explanations remain open for the host effect-status
view. This source change is not compiled or runtime-validated.

Physical-card action identity implementation pass (2026-10-14): CardGame
action schema v2 requires the exact physical hand instance for PLAY_CARD;
schema v1 and earlier command/replay records remain readable. Command-log v9
and replay codec v8 persist the source instance ID for deterministic replay
and exact retry comparison. This implementation is not compiled or
runtime-validated.

Card effect status query implementation pass (2026-10-14): GameSession now
provides a read-only preflight result for a specific physical card in hand and
the supplied choice. The query leaves card/rules state unchanged and reports
inactive Loom-phase status through its result value and includes no-effect
choice bits for biome removal. Legal-target enumeration is covered by the
following implementation entry; card-specific reason text and full parity with
card-play authorization/recipe discovery still need implementation.
Compilation/runtime validation remain deferred until the implementation pass
is complete.

Legal card-choice query implementation pass (2026-10-14): RegenerationWorldRules
enumerates valid biome/ore choices and no-choice plays from current rule state.
Surveyor's Reach choices are filtered from host-provided candidates, which
GameSession rechecks against current loaded, protected, selected and capacity
state. GameSession also applies Volcano Mark's discovery and authorization
requirements. Per-card reason text, compilation and runtime validation remain
open for the implementation pass.

Draw-notice write-ahead transaction implementation pass (2026-10-14):
Positive empty-draw and hand-overflow instability penalties now use the shared
transaction journal, keyed by stable CardGame host-intent sequence. The prepared
record stores exact per-chunk before/after values and revisions; recovery
finishes a partially applied event idempotently and acknowledges a still-pending
notice. The transaction retains only its latest terminal high-water record.
This closes the local session snapshot/journal crash window; compilation and
runtime validation remain deferred until implementation is done.

Surveyor's Reach status parity (2026-10-14): The proposed-choice status API now
uses World Revision's read-only bonus-chunk validator for the supplied target,
so current loaded/protected/selected/capacity state and stale scope affect the
result as they do in play preflight. Full structure-site and instability
preflight parity, compilation and runtime validation remain open.

Rare-card status feasibility pass (2026-10-14): Read-only card status now checks
configured instability headroom on affected chunks, stabilizer surge/modifier
capacity, an eligible Catalyst target, and site availability for Shipwreck,
Pyramid, Canopy and Spider Nest. Compound feature reservation feasibility,
Volcano footprint preflight, remaining authorization gates and reason text
remain open. This source change is not compiled or runtime-validated.

Generic host-output delivery surface (2026-10-14): GameSession now exposes a
host-only ordered copy of pending typed outputs and a snapshot-checkpointed
acknowledgement path for generic outputs. Policy effects and draw-penalty
notices remain on their dedicated handlers. The sequence is retained until the
host accepts it, allowing retry after a failed checkpoint. Global session/batch
identity and a durable prepare/commit boundary with external consumers remain
open; source is not compiled or runtime-validated.

Host output identity follow-up (2026-10-14): Added an explicit host-configured
session ID envelope to pending regeneration outputs and a session-checked
acknowledgement entry point. MCSP v17 now persists that ID with the pending
CardGame output queue; a preconfigured host ID is checked on restore, and an
unset ID adopts the snapshot value. The sequence-only acknowledgement is
private. Durable external-consumer coordination and compile/runtime validation
remain open.

Card status reason text (2026-10-14): Added a read-only status overload that
returns user-facing reasons for phase, authorization, configuration,
instability, capacity and structure-site failures, and identifies already
excluded biomes as no-effect choices. Full authorization and reservation
parity, compilation and runtime validation remain open.

Generic host-output transaction implementation pass (2026-10-14): GameSession
now offers a pre-start prepare/commit/abort consumer for non-policy and
non-draw typed CardGame outputs. Each output is recorded under a
session-namespaced journal scope and checkpointed before invoking the consumer;
startup recovery drains journaled pending outputs in order, and public ack
requires a committed transaction record. Consumer callbacks must be
idempotent by `(session_id, sequence)`. This is post-action durable delivery;
it does not make an external consumer part of the original CardGame action's
atomic commit. Consumer backend provisioning and failure/crash-boundary
validation remain open. A new session using the consumer must be given a
stable nonzero session ID before startup; restored sessions can adopt their
saved ID. These source changes are not compiled or runtime validated, per the
implementation-first instruction.

Read-only compound feature capacity preflight (2026-10-14): The status query
now accounts for already accepted feature plans and queued feature intents,
Magma's configured footprint, Volcano's square, and the available ocean chunks
for Shipwrecks. It invokes the same Volcano placement feasibility check as
play acceptance. This avoids reporting capacity that is already reserved;
worker/assignment checks remain authoritative at generation time, and
compile/runtime validation remains open.

MCSP output-session persistence integration (2026-10-14): Snapshot v17 now
stores the output session ID after crop-growth state, and GameSession writes,
restores and checks it against an optional host override. If none is supplied,
a new world receives a deterministic seed-scoped ID before its first save. The world-item
validator source includes a v17 roundtrip and advances unsupported-version
rejection to v18. A getter exposes the restored ID to host transport setup.
Starting another world resets the previous restored identity and reapplies only
the configured override; configuring zero clears that override. Legacy readers
remain in place; compilation and execution are deferred until the
implementation pass is complete.

Adapter session persistence follow-up (2026-10-14): RCS1 v7 stores the bound
action-session ID and echoes it in cached draw/discard results. Restore adopts
the saved value or rejects a conflicting host value; legacy v1-v6 snapshots
remain readable. Compatibility fixture source was updated to remove the new
header field when constructing v1 and v5 examples. Compilation and runtime
verification remain deferred.

Rare-card recipe knowledge parity (2026-10-14): GameSession now checks each
craft-only rare card's stable recipe-book entry on play and status queries, and
hides its choices until unlocked. Volcano Mark continues to require its
separate host authorization. Compilation and runtime verification remain
deferred until the implementation pass is complete.

Structure-site status capacity (2026-10-14): Card status now checks that all
queued Shipwreck, Pyramid, Canopy and Spider Nest plans can be assigned to
distinct selected chunks with matching ocean, desert, forest or cave sites.
This includes accepted plans, pending intents and the proposed card. The
source has not been compiled or runtime-validated.

Generation-time distinct site assignment (2026-10-14): WorldRevisionRegenerator
jointly matches Shipwreck, Pyramid, Canopy and Spider Nest to distinct eligible
chunks before assigning Shipwreck payloads. After village reservations, it
rematches the remaining structure types and uses those matched chunk indices.
This prevents greedy false failures when a chunk fits multiple types. Villages
can still consume the only remaining site after preflight, so generation
fails closed in that case. Build and runtime validation are deferred until the
implementation pass is complete.

Boat ownership persistence foundation (2026-10-14): WENS v3 now persists an
owner actor ID for entities and reads legacy v1/v2 snapshots as owner zero.
WorldEntityStore returns the owner in snapshots and offers an item-entity spawn
overload with an owner ID. Actor-aware GameSession and transfer-transaction
entry points now persist that ID with placed boats, record it in WBT journal v2,
and reject retrieval by a different actor. Legacy WBT v1 records are accepted
for owner zero. GameSession now requires a pre-start actor authorizer for
nonzero actor IDs and fails closed when it is absent or denies the actor. The
host must bind the callback to authenticated transport state; production
transport binding, boat steering and movement remain open. Validation is
deferred until the implementation pass is complete.

Boat placement now also rejects a target chunk blocked by an active World Loom
barrier, closing the inventory-placement route for introducing a vehicle into
the regeneration boundary. This source change is not compiled or runtime
validated. Boat steering, movement, and network transport still need
implementation.

The actor-aware boat placement and retrieval entrypoints reject zero IDs and
always call the configured host authorizer. The actor-less public wrappers
have now been removed, leaving no public GameSession boat-transfer path that
can omit actor identity. This change has not been compiled or runtime
validated.

An actor-aware boat movement step now checks ownership, reach, entity revision,
loaded water support, protected chunks, and active World Loom barriers. It
records the exact endpoint in the shared transaction journal, checkpoints the
prepared state, updates the entity, and commits or restores on checkpoint
failure. The endpoint is bounded to one block per action. Steering controls,
client presentation, and authenticated transport integration remain open. This
implementation has not been compiled or runtime validated.

GameSession now exposes entity storage through a const accessor so application
callers cannot bypass the authoritative movement and ownership entrypoints with
direct entity updates. Internal simulation retains private mutation access.
This change remains uncompiled and unvalidated.

World regeneration now plans up to three deterministic instability sites per
chunk only when both reward and danger bands have been earned. Each site
couples bounded hostile spawners to a one-time tiered reward; persisted feature
outcomes suppress duplicate drops and spawner creation while preserving the
site structure across later regeneration. Sites are separate and the count is
capped at three. Balance tuning and runtime coverage remain open; this source
change has not been compiled or validated.

Opportunity sites are now assigned after the worker reserves card feature
footprints. Chunks containing those footprints or an existing permanent
instability overlay are excluded, preventing these generic structures from
overlapping accepted features. This source change remains uncompiled and
unvalidated.

The opportunity planner now requires a flat, dry, open surface site and places
the structure, spawners, and reward relative to its detected ground height.
An unavailable site skips that opportunity tier without failing regeneration.
This implementation has not been compiled or runtime validated.

GameSession now configures a bounded sorted script policy-context schema before
loading a fresh catalog, applies host values after startup or snapshot restore,
and exposes live set/clear calls that update the adapter bridge. Snapshot
values remain adapter-owned; active updates checkpoint immediately and roll
back in memory if saving fails. A separate host-installed authorizer approves
each initial or live context set/clear. Wiring it to authenticated identity and
authoritative generation-policy provisioning remains open. This source change
has not been compiled or runtime validated.

Host-output acknowledgement retries now use CardGame's persisted next-sequence
high-water mark. An already acknowledged sequence returns success after
checkpoint or restore; an unissued or out-of-order sequence still fails. This
removes the lost-response retry failure from the generic durable outbox path.
Source has not been compiled or runtime validated.

Persistent block-edit API completion (2026-10-14): The public delete/place
wrappers now stage persisted block overrides around their legacy edit commands,
and undo/redo stage the restored override along with the chunk edit. This closes
the remaining direct-edit paths found during source inspection. Compilation and
runtime validation remain intentionally deferred until implementation work is
complete.

Undo/redo publication follow-up (2026-10-14): History edits now publish the
updated immutable chunk read state and invalidate a World Loom station when its
block changes. If read-state publication fails, the previous voxel is restored
and the persistent override is not committed. Compilation and runtime
validation remain deferred until implementation work is complete.

World Loom owner checkpoint implementation pass (2026-10-14):
`WorldRevisionServer` exposes a checksummed WRS1 serializer/restorer for the
active actor/Loom owner, committed-start state, and bound world selection
revision. The host can checkpoint this alongside its durable session and must
restore it before server initialization; mismatched world revision state is
rejected. The network host still needs to wire this state to durable storage,
and request-cache persistence plus crash-safe generation publication remain
open. No compile or runtime validation was performed.

World Loom request-cache checkpoint implementation pass (2026-10-14): Added
bounded WRC1 serialization for completed request payloads/results and wire retry
identity, including quotes, instability views, and revision summaries. Restore
validates all lengths, entry counts and checksum before replacing either cache.
The World Revision validator source includes roundtrip and corruption cases.
Host wiring to save after each action and atomically couple the cache with world
mutations remains open; unfinished requests are not restored and their action
owners still need transaction recovery. This has not been compiled or run.

Physical item identity policy (2026-10-14): MinecraftItemCatalog marks boats, tools/armor, physical CardGame items, and filled water/lava buckets as unique physical instances. Player inventory and ground-item entity snapshots are checked for nonzero IDs and unit stacks as part of GameSession cross-store identity validation. ItemCatalogValidator source covers representative definitions and invalid missing-ID/stacked cases. Implementation and validator source are present; compiling and running validation are deferred until the remaining implementation gaps are complete. A world-wide multiplayer identity registry and generic registration for every future physical-item store remain open.

World Loom combined server checkpoint (2026-10-14): WRP1 now packages the server owner draft and bounded request/replay caches into one checksummed checkpoint. Restore validates both into temporary state before publishing and requires the corresponding World revision to be loaded first. Validator source covers roundtrip and corruption rollback. The host still needs to persist this payload atomically with World/session state; compilation and validation are deferred until implementation gaps are complete.

World Loom checkpoint file helpers (2026-10-14): the combined server checkpoint now has bounded load and atomic-replacement save helpers, with validator source for disk roundtrip. The helper guarantees file visibility semantics only; durable cross-system atomicity with the world/session snapshot remains a host integration task. No compile or validator run yet.

World Loom checkpoint integration (2026-10-14): GameSession snapshot v18 now stores the combined WRP1 WorldRevisionServer checkpoint beside World Revision metadata. GameSession restores the bundle before binding/initializing the network server, and the bound server checkpoints completed action and query caches through GameSession save. The persistence validator source includes v18 roundtrip/v19 rejection. Confirm-start avoids an intermediate save before the server records its completed replay result, so the final MCSP replacement captures both. It activates barriers and sets the deadline before committing the journal; setup failures restore inventory/cards, clear barriers, and abort the prepared record. Callback-error recovery and crash-boundary validation remain open; builds/tests are deferred until implementation gaps are complete.

Checkpoint save retry correction (2026-10-14): the GameSession callback keeps the newest serialized WRP1 in memory when MCSP replacement fails, preventing a later ordinary session save from pairing changed World/session data with an older server replay cache. Exact request retry attempts persistence again. Process-crash durability remains open until a successful MCSP replacement; implementation remains uncompiled and untested.

Recipe output coverage gate (2026-10-14): `MinecraftRecipeCatalog::initialize` now validates that every recipe-acquired catalog item has a registered output and that each card item has exactly one recipe plus the required starter/rare knowledge policy. Startup fails closed and tears down the catalogue on a coverage mismatch. This closes the catalog registration invariant only; world acquisition and discovery-event completeness remain open. No compile or validation has been run.

Durable hoe tilling implementation pass (2026-10-14): Live GameSession tilling now writes GTIL v2 with the hoe's physical instance ID and applies one durability point through the same prepared transaction as the soil change. Checkpoint rollback restores both inventory and soil. Recovery resolves v2 wear from the saved slot/instance, while v1 prepared records remain valid without retroactive damage. Committed retries consult the journal before requiring the original inventory revision. Source-only; compilation and runtime verification remain deferred until the full implementation pass is complete.

Broken-equipment state and repair staging (2026-10-14): The catalog's damage path now keeps a broken tool as its original physical item instance with durability zero, and exposes a staged full-repair snapshot API. This satisfies identity retention and prepares repair integration, but a material-cost policy, station authorization, durable repair transaction, and UI representation remain open. No compilation or validation has been run.

Journalled equipment repair action (2026-10-14): GameSession now exposes an authorized workbench repair action backed by GRPR v1. Repair consumes one tier-matched material and restores the same physical item instance to maximum durability through a single staged inventory update. Prepared repair records recover at startup, while committed retries are idempotent. This uses provisional material costs and does not yet provide UI presentation or derived equipped combat stats. No compilation or validation has been run.

Equipment repair gameplay integration follow-up (2026-10-14): `GameSession::repair_equipment` now checks the host-authorized Workbench, and `WorldEquipmentRepairTransaction` persists exact station/tool/cost identity in GRPR v1. Repair cost is derived from the material tier; material debit and durability restoration commit in one inventory snapshot. Prepared records recover at session startup, and retries remain idempotent if the station is later removed. UI rendering, derived combat stats, and balancing the provisional one-unit material costs remain open. This implementation is source-only and unverified.

Equipment repair quote API (2026-10-14): Added a non-mutating authorized Workbench query for equipment durability, tier-derived repair materials, available quantity, and current repairability. This supplies the data contract for a future UI; no rendering surface is wired. Source-only, unverified.

Repair rollback recovery hardening (2026-10-14): The repair transaction now preserves its prepared journal image across participant failure and propagates inventory, journal, and compensating-save rollback errors. This leaves a recoverable intent if inventory compensation fails. No compile or runtime check has been run.

Equipment repair material coverage gate (2026-10-14): Startup equipment coverage now validates the durability representation and confirms every repair material resolves to a stackable catalog item. This prevents malformed repair policies from entering a session. Source-only, unverified.

World Loom publication checkpoint pass (2026-10-14): World regeneration now
invokes the GameSession atomic snapshot callback after publishing a chunk's
instability provenance, spawners and loot. A failed snapshot restores the
previous live chunk/read state and instability record and removes the new
spawners; staged loot is rolled back. Generated voxel arrays are not stored in
the session snapshot. Matching persisted feature-outcome IDs, rules hash and
generation epoch now suppress duplicate spawner/loot publication when the
pending revision replays after restart. Restart voxel replay and crash-boundary
validation remain open. Source-only; compilation and runtime verification are
deferred until implementation is complete.

Authenticated inventory query boundary (2026-10-26): the public
`GameSession::query_inventory_view` now requires a nonzero authenticated actor
and a configured host authorizer; the actor-free entry point was removed. It
returns immutable inventory and equipment item values under the item
transaction lock and includes a deterministic equipment content revision.
Recipe query rows carry the same equipment revision captured under the session
transaction lock. Actor-aware overloads also guard catalog-wide and
single-recipe snapshot queries plus revision-bound hand/station craft requests.
Actor-bound station queries/crafts require a station callback that receives the
actor ID and rechecks reach/claims/ownership around the read or immediately
before execution; `configure_actor_processing_station_authorizer` now installs
that required host policy before session start. Actor-bound craft requests bind
the equipment content revision and authenticated actor ID into version-5
journal identity and require the equipment token to match on new execution and
prepared recovery; exact terminal retries remain
tied to the persisted revision after equipment changes. Production callback
wiring, legacy actor-free recipe/craft APIs, and cross-system acceptance
evidence remain open. This is a content fingerprint, not a monotonically
allocated character revision; no builds or tests have run.
Source-only; tests and builds remain deferred until implementation is
complete.

Card scripting context restore correction (2026-10-14): GameSession applies
configured script-context defaults only to a fresh card session. Restoring an
existing RCS1 snapshot now preserves authorized runtime context values instead
of replacing them with startup defaults. Startup also rejects a restored
context-key schema that differs from host configuration. A persistence
regression and runtime verification are still pending; tests and builds remain
deferred until the implementation phase is complete.

Choice-expiry retry binding (2026-10-14): RegenerationCardSession now rejects
an expiry retry if the request ID is reused with a different authoritative
epoch. Exact retries return the original resolution count. Regression
execution and compilation remain deferred until implementation work is
complete.

Mining transaction rollback error propagation (2026-10-14): Failed drop or
tool-wear staging now checks all compensating despawns and restoration of the
block or unloaded-cell override. A failed compensation is returned while the
prepared journal intent remains available for startup recovery. Source-only;
compilation and runtime verification remain deferred.

Revision-checked entity damage primitive (2026-10-14): `WorldEntityStore` now
applies positive damage to a live non-player, non-boat entity under an expected
revision. Lethal damage changes health and action state together. This is a
storage primitive; the authenticated player attack action, range and line of
sight, weapon power and wear, durable request replay, and coordinated death loot
remain open. Source-only; implementation-first sequencing still defers tests
and builds.

Journalled player melee action (2026-10-14): `GameSession::attack_entity` now
validates target revision and liveness, 3.5-block reach, a 55-degree forward
arc, and solid-block obstruction. A catalogued main-hand item supplies damage
and loses one durability point; entity, equipment and committed request result
are saved together. Exact retries replay the stored result and changed target
or revision payloads fail. Network hosts can use a fail-closed actor-authorized
entrypoint with per-actor request scopes; the direct entrypoint is for the
local authoritative host. Successful attacks now use a configurable
simulation-tick interval (default 10, range 1–200); exact retries replay during
cooldown. The action record persists the accepted tick and restore derives the
remaining cooldown from the saved world tick. Projectiles, armor-set
bonuses, broader death-loot verification and runtime coverage remain open.
The local primary-click path selects the nearest live entity on a narrow camera
ray before falling back to block mining, and rejects over-capacity nearby
queries rather than acting on a truncated set. Source-only; testing and builds
remain deferred.

Equipped-tool mining correction (2026-10-14): Active GameSession mining now
checks only the catalogued main-hand tool's class, power and durability. GMIn
v3 records its physical identity and uses the same equipped instance during
prepared-action recovery; compatibility callers retain the inventory-tool v2
path. This enforces the roadmap rule that a stronger tool in inventory cannot
authorize mining. Source-only; testing and builds remain deferred.

Recipe catalogue snapshot query (2026-10-22): `MinecraftRecipeCatalog::query_recipes`
now captures one Libft inventory snapshot and evaluates every requested recipe
against that same inventory revision. Libft owns the reusable
`game_crafting::query_recipe_at_station_snapshot` matcher and validates the
snapshot's stack and aggregate invariants. The ItemCatalog validator checks a
craftable filtered query; normal and analytics builds and both
`--validate-item-catalog` runs pass. Broader catalogue/recipe-book revision
consistency is still open. The attempted `make validate-all` does not pass yet:
it times out during emissive block-delete convergence with the target chunk
still dirty and a remesh in flight. That lighting/remesh scheduling failure
is separate from the recipe query and remains an active backend issue.

Regeneration deferred-edit commit allocation guard (2026-10-26): The
regeneration applier reserves checked deferred-edit capacity before mutating
chunk or session state, avoiding a post-checkpoint allocation failure in its
`noexcept` commit tail. This does not close crash-atomic voxel persistence or
cross-store generation replay. Source-only; implementation, test authoring,
and validation remain in progress.

CardGame host-output failure policy (2026-10-26): Permanent prepare errors
(permission, invalid payload, or configuration) now persist an aborted output
record after consumer abort succeeds; transient prepare errors and uncertain
commit failures stay retryable. Added an explicit sequence-scoped retry API
that checkpoints reopening before callback replay. Consumer storage still
requires its own atomic idempotency. Source-only; test authoring and validation
remain deferred until implementation is complete.

World Loom occupancy retry ordering (2026-10-26): Exact cached draft-action
and quote retries now replay before refreshing multiplayer occupancy. New
mutations still refresh and fail closed on stale/unavailable occupancy, while
station access remains rechecked for mutating retries. This preserves durable
idempotency during provider outages. Source-only; test authoring and validation
remain deferred.

World Loom occupancy sample validation (2026-10-26): The occupancy-provider
contract now returns one chunk per active player plus a sample epoch. The
GameSession binding checks completeness and rejects future or older-than-one-
second samples against the configured authoritative clock before publishing
the occupied set. A zero-player sample is accepted only when the count is
explicitly zero. Direct WorldRevisionServer providers also require a complete
timestamped sample and authoritative epoch provider; the server applies the
same freshness bound. Provider implementations and validator fixtures still
need migration; source-only, with tests and builds deferred.

World Revision status transport (2026-10-26): The version-1 protocol now has an
authenticated, read-only `status` action for the owning actor/Loom. Its bounded
response reports lifecycle flags, deadline and revision tokens, plus each
selected chunk's revision state and whether publication is committed or
pending. Durable per-chunk failure outcomes are not yet represented. Source-
only; tests and builds remain deferred.

Script manifest trust boundary audit (2026-10-26): GameSession source already
requires host authorization of catalogue bytes against a nonzero trusted hash,
then validates that hash again on fresh load and restore alongside the
configured generation manifest. Production host provisioning and durable
authenticated recovery of the expected hash remain open. Source audit only;
no tests or builds were run.

Runtime block acquisition policies (2026-10-26): GameSession can now receive
runtime block policies before start, applies them to MinecraftItemCatalog, and
fails startup if any registered runtime block lacks a policy. An active-session
registration API covers newly added runtime blocks and retains policy for the
next start of that GameSession. The policy list remains host-provisioned
configuration rather than world-persisted data. Source-only; tests/builds
remain deferred.

Prepared furnace advance recovery (2026-10-26): Timed-processing tick recovery
now completes an already-prepared advance before checking current furnace
continuity, including frames with zero new elapsed ticks. New advances still
require currently valid station contexts. Independent per-job advancement
remains open. Source-only; tests and builds are deferred.

Per-job furnace continuity (2026-10-26): New timed-processing advance records
persist the sorted authorized job IDs, so each furnace is checked and advanced
independently; a removed station pauses only its own job. Recovery replays the
same set, while legacy advance records retain all-job replay behavior. Source-
only; tests and builds remain deferred.

World Revision protocol envelope version and length bound (2026-10-26): The
server now requires `world_revision.protocol_version: 1`, returns version 1 in
all World Revision action/quote/instability response envelopes, and checks
request length with a bounded scan capped at 4096 bytes. Cost quotes reject
zero request IDs and opening-card counts outside 4–7 before reserving replay
cache capacity. Client and validator fixtures still need migration; no
compatibility negotiation exists yet. Source-only; tests and builds are
deferred until implementation work is complete.

GameSession mutation boundary (2026-10-26): Public GameSession accessors for
the item catalog, ground-item store, spawner store, and transaction journal
now return const references. This prevents external callers from bypassing
the session's authorization, identity checks, transaction journaling, and
checkpoints through those owned objects. Source-only; the broader cross-store
recovery work and validation remain open.

GameSession card/start serialization (2026-10-26): The per-session transaction
lock now also covers CardGame queries and mutations, host-intent delivery and
acknowledgement, World Loom start/finalization, lifecycle transitions, and
snapshot creation. Recursive locking supports internal recovery/finalization
calls without exposing partial transitions to concurrent backend callers.
Cross-process reservation ownership and crash-recovery validation remain open;
this source-only change has not been tested or built.

World persistence audit (2026-10-26): MCSP carries sorted checksummed fluid
overrides and WLR carries accepted-generation requests and per-chunk
publication receipts. WVC1 separately persists per-world chunk voxel payloads,
including user edits and regeneration output, and startup loads those payloads
before procedural generation. The files still lack one crash-atomic commit
across terrain, MCSP, entities, spawners, instability and resource effects.
Backup/restore also does not yet include WVC1 terrain. Source audit only; tests
and builds remain deferred.

Committed World Loom generation restart (2026-10-26): WLR metadata v4 now
stores the accepted generation request and per-chunk publication receipts;
session start checkpoints the request before scheduling, incrementally loads
reserved chunks after restore, and resumes or retries generation with the
committed rules and barrier. Each receipt is saved atomically with chunk-related
items, entities/spawners, instability, and session state. Replay rebuilds chunk
data and skips side effects already recorded as committed. A failed checkpoint
removes its receipt and rolls in-memory participants back; with instability
enabled, replay rejects a receipt whose rules hash or feature outcomes disagree.
Completion writes a
final checkpoint after clearing the request and receipt set. This closes lost
start intent and duplicate per-chunk output on the GameSession snapshot path;
backup/restore, corruption recovery, multi-world isolation and full crash
validation remain open. Source-only; tests, builds and runtime validation are
deferred until implementation is complete.

Actor-bound furnace start and recovery (2026-10-26): Recipe-processing start
now requires a nonzero authenticated actor, the session actor authorizer, and
the actor-aware furnace station policy; the actor identity is persisted in
new processing journal records for retry identity. Startup recovery revalidates
the persisted furnace coordinates and station revision through the configured
continuity validator before applying a prepared start, leaving it pending when
the station cannot be verified. Production wiring, post-prepare authorization
revalidation now occurs after the PREPARED checkpoint and before replay or
mutation. Production wiring and deferred test/build evidence remain open.
Actor-scoped processing requests also carry the inventory, catalog-content,
and recipe-book revision tokens returned by the recipe query. Processing cache
v5 persists catalog/book tokens (v1-v4 remain readable), and processing
journal v4 binds those tokens with the actor ID (v1-v3 remain readable).
Recovery reuses the persisted tuple so it cannot start a job against changed
recipe content or knowledge. Runtime verification remains deferred.

Permanent CardGame host-output commit rejection (2026-10-26): a permanent
consumer commit error now invokes the host abort callback, persists the
transaction as ABORTED, and waits for explicit operator retry after repair.
Transient commit errors remain PREPARED and recoverable. The host callback
contract requires permanent rejection to leave no committed effect. Crash
injection, runtime validation, and external-consumer conformance remain open.

Actor-bound equipment actions (2026-10-26): equip, unequip, repair, and repair
quote APIs now require the authenticated actor authorizer. Workbench repair
also requires actor-scoped station access and rechecks the station revision
after assembling the quote. Loadout and repair journal v2 payloads persist the
actor ID, retaining v1 readers; prepared recovery preserves that identity.
Terminal replay/high-water behavior, production host wiring, and tests remain
open.
Equip and unequip now revalidate the actor after the prepared checkpoint and
before inventory/equipment mutation. Repair rechecks actor and workbench access
at that same boundary; startup recovery validates saved station continuity and
leaves an unverifiable prepared repair pending.
Public combat entry points now require an authenticated actor for attacks,
arrows, and shield guarding; actor-free helpers are private to local simulation.
Multiplayer transport integration and action authorization/replay tests remain
open. Loadout and repair scopes retain the latest committed request as a
durable high-water marker, reject missing older IDs, and keep exact actor-bound
payload replay for the retained request. This bounds their journal history;
broader result retention and acceptance tests remain open.

Runtime block acquisition rules and the trusted regeneration script manifest
now contribute to a checksummed host-policy hash in MCSP v19. Restore compares
the saved value with host-provisioned rules and manifest before resuming, and a
mismatch fails configuration validation. Registering a runtime rule during an
active session checkpoints the updated hash; retrying an identical
registration retries the save after a previous write failure.

World Loom lifecycle metadata now uses WLR v6 to retain per-chunk failures and
skips during a retryable run and the last completed revision's selected,
published, failed, and skipped chunks after completion and restart. Status
reports committed, failed, skipped, and pending publication separately and
includes an error code for failed or skipped chunks. WLR v1 through v5 remain
readable. Test authoring and build verification remain deferred until
implementation work is complete.

The World Loom start transaction now requires the configured authenticated
actor authorizer and rechecks the actor before preparing, before resource
mutation, and immediately before commit. Revocation after the prepared state
or resource changes aborts and restores the transaction. Production identity
provider wiring and test coverage remain open.

MCSP snapshots now rotate a checksum-valid previous version to a `.bak` file
before atomic replacement. Loading falls back to that backup when the primary
snapshot is unreadable or has a damaged outer checksum, then atomically repairs
the primary path when possible. Semantic validation failures with a valid outer
checksum still fail closed; broader corruption recovery remains open.

Backend follow-up (2026-10-26): Authenticated regeneration-card play and
required-card discard now have actor-bound GameSession entry points. They
recheck the configured actor authorizer around the CardGame mutation and
restore the prior CardGame snapshot if authorization is revoked before the
action is accepted. Actor-free entry points reject calls when a host actor
authorizer is configured. CardGame snapshot version 9 persists actor identity
in play and discard retry records; exact retries from another actor fail, and
older snapshots restore these entries with actor ID zero. Required discard
requests must match the actor on their source draw.

World Revision protocol routes authenticated `play_card`, `draw_cards`,
`discard_card`, and `resolve_expired_choices` actions for the owning actor and
Loom through GameSession. It rechecks Loom access on exact retries, persists
structured results through WRC1 v6, and omits private play-result fields from
wire responses; readers retain WRC1 v1-v5 compatibility. Owner-bound
`choose_card_option` is sequenced through the host clock and exact choice
retries persist in RCS v10. Choice expiry uses
the GameSession host clock, rejects early polls without consuming the request
ID, and can recover the original result if the session save preceded the
World Revision cache save. An owner-only `card_hand` query now returns physical
instance and definition IDs, pending discard count, and redacted pending
choice options/deadlines through an immutable GameSession snapshot. Scheduled
timeout automation, disconnect/reconnect ownership, and broader multiplayer
CardGame recovery remain open. Source-only; no tests or builds have run.

Legacy World Loom start recovery (2026-10-26): A live retry of a prepared WLS1
record is now durably aborted because that format did not retain its absolute
decision deadline. Retries requiring a decision window must prepare a WLS2
record with the deadline persisted; committed WLS1 records remain readable.
Both changes are source-only; tests and builds remain deferred.

Scheduled CardGame choice expiry (2026-10-26): GameSession's authoritative
world tick now checks pending choices against the configured host clock and
submits deadline resolution through the existing snapshot/checkpoint path.
The request ID is derived from the persistent CardGame session and earliest
due choice deadline/ID, preserving retry identity across restart. Provider and
checkpoint failures fail the tick without silently dropping the timeout.
Disconnected-session scheduling and crash/runtime verification remain open;
tests and builds stay deferred until implementation and test authoring are
complete.

Host-facing choice deadline scheduler API (2026-10-26): Exposed
`GameSession::process_due_regeneration_choice_expiry` for trusted server
schedulers. It reads the host clock, resolves due choices with stable persisted
request IDs, and checkpoints before returning. The application session tick
calls the same backend entry point. Hosting timeouts while a session is
unloaded/disconnected remains open; tests and builds remain deferred.

Mode-specific World Loom decision windows (2026-10-26): Start commit now uses
the refreshed authoritative player count to choose a solo window (15-minute
default) or multiplayer window (5-minute default). Both are configurable before
session start. WLS3 persists the selected mode, player count, timeout and
absolute deadline. Prepared retries preserve that policy and abort if player
count changed; incomplete prepared WLS1/WLS2 records abort instead of resuming
under an implicit policy. Committed legacy records remain readable. Source-only;
test authoring and builds remain deferred.

World Loom pre-debit revalidation (2026-10-26): After the WLS3 prepared
checkpoint, start commit refreshes occupancy, rechecks the selection and cost
policy revisions, compares every quote field, verifies the player count still
matches the persisted mode, and checks that the decision deadline has not
elapsed before charging or dealing. Stale inputs durably abort the request.
Crash/fault tests and builds remain deferred.

World Loom status policy (2026-10-26): Owner-only status now includes the
committed start mode, active-player count and selected decision timeout from
the durable WLS3 journal, alongside the deadline. Legacy WLS2 reports its
persisted timeout as legacy mode; missing policy is reported as unknown. No
mode is inferred from current occupancy. Source-only; tests and builds remain
deferred.

World Loom lifecycle status (2026-10-26): Authenticated status now reports the
logical lifecycle state plus whether player cancellation, selection editing,
or trusted retry is allowed. It combines those values with the durable start
mode and decision timeout. Protocol acceptance and restart-transition tests
remain deferred.

Terminal World Loom status ownership (2026-10-26): The backend retains the
owner identity after pre-start cancellation and generation completion so the
same authenticated actor/Loom can query `NO_DRAFT` or `COMPLETE` after
reconnect. A later successful begin replaces the terminal owner. Existing WRS1
checkpoint carries the identity; no new format is needed. Shutdown/restart
transition validation remains deferred.

World Loom status after cancel and historical completion (2026-10-26):
`NO_DRAFT` now takes precedence over a prior completed revision when the
retained owner canceled a newer uncommitted draft. Its response clears stale
chunk results and old policy fields; a committed owner still sees `COMPLETE`.
Source-only; restart transition tests remain deferred.

World Loom per-chunk outcome durability (2026-10-26): The lifecycle checkpoint
stores failed/skipped outcomes for active selected chunks and preserves them
with the completed revision. Restore validation ensures each outcome belongs
to a selected chunk and does not conflict with a published chunk. The
authenticated status response reports `committed`, `pending`, `failed`, or
`skipped` per selected chunk, including its error code when present. Restart,
crash, and serialization fault coverage remains to be authored; tests/builds
remain deferred.

World Loom request serialization audit (2026-10-26): Libft's
`game_server::run_once()` serializes callbacks under its server lock, so one
normal network loop dispatches one request at a time. Multiple server loops or
direct concurrent calls into `WorldRevisionServer` are not covered by that
lock. The host must still guarantee single ownership/serialization for a world
or add a lock design that preserves the GameSession checkpoint lock order.
Concurrency coverage remains open; tests/builds remain deferred.

Mining policy enforcement (2026-10-26): `WorldBlockMiningTransaction` now
checks the current original block's acquisition policy and breakability before
mutation in both new execution and prepared recovery. Admin-only or
non-breakable blocks fail closed even when a caller bypasses `BlockInteractor`.
The code has no authenticated privileged mining route yet; complete block/drop
coverage, test authoring, and builds remain open.

Spawner loot save boundary (2026-10-26): Source inspection confirms pending
spawner deaths are handled under the GameSession transaction lock: deterministic
loot and recipe discoveries are added, the corpse is removed, and death is
acknowledged before one session snapshot. If saving fails, the prior item,
entity/spawner, and recipe-book snapshots are restored. Crash injection at the
save replacement boundary and broader acquisition coverage remain open;
tests/builds are deferred.

Melee mob loot transaction (2026-10-26): Lethal melee hits now resolve the
matching supported mob loot table from the stable target ID and stage drops
with the combat journal, entity removal, and weapon durability before one snapshot.
Multiplayer drops retain a short killer pickup lock. Failed drops or checkpoint
saving restore the prior item store and the entity/spawner snapshot, preserving
the killed entity's original ID and state, so exact retries cannot repeat the
loot. A spawner membership query leaves spawner-owned animals to the existing
pending-death transaction, preventing duplicate loot. Coverage and builds
remain deferred until the implementation pass ends.

Projectile mob loot and attribution (2026-10-26): Lethal arrow hits on
non-spawner mobs now stage deterministic loot with projectile removal and mob
removal in the simulation checkpoint. The shooter actor ID is persisted on
projectiles through projectile request payload v2; WENS v5 now persists
projectile attribution and the last authenticated damage actor for mobs while
reading WENS v1-v4. Spawner death records carry that actor into loot creation
and apply the short killer pickup lock. Projectile payload v1 remains readable.
The loot table mapping is shared with melee handling. Migration and crash
coverage plus builds remain deferred.

World Loom final-checkpoint recovery (2026-10-26): A failed final generation
checkpoint now restores the live manager to its previous revision and
barriered committed request, including selected chunks, publication receipts,
and deadline. Explicit and startup-resume retries checkpoint that committed
request before scheduling workers. Per-chunk receipts continue to prevent
replayed side effects. Occupancy provider samples now document one entry per
active player, preserving duplicates for co-located players until the manager
deduplicates positions. Source-only; crash/retry coverage remains deferred.

Starter deck startup invariant (2026-10-26): Session setup checks nonzero
starter copies, the per-definition copy cap, accumulation overflow, and the
configured 20-card total before shuffle/deal, then confirms the CardGame engine
contains all 20 cards after insertion and inspects each definition count and
unique nonzero physical instance ID against the expected multiset. Regression
coverage remains for the test-authoring pass. Source-only; tests and builds
remain deferred.

Barrier-aware survival respawn (2026-10-26): Death recovery checks the
configured spawn chunk against active World Loom barriers before changing the
player's horizontal position. If the spawn is reserved, it keeps the current
horizontal position and performs the usual ground adjustment there. This
closes a local position-change path around the movement barrier; authoritative
multiplayer reconnect positioning remains a host integration requirement.
Source-only; tests and builds remain deferred.

World Revision cache side-effect guard (2026-10-26): Quote and draft mutation
paths check capacity and allocate the replay entry before refreshing live
occupancy. Cache-full/allocation failures cannot prune selection state or
advance its revision; failed occupancy refreshes remove the placeholder.
Cache-saturation/fault-injection coverage remains deferred. Source-only; tests
and builds remain deferred.

Barrier-aware committed-session restore spawn (2026-10-26): Startup checks the
default spawn after restoring barriers, resumes at the saved player
coordinates if that spawn is reserved, and fails closed if the saved position
is reserved too. This prevents a committed regeneration from being bypassed
by the local reconnect spawn path. Host live-position mapping and restart
transition coverage remain open. Source-only; tests and builds remain deferred.

Barrier-aware spawn search (2026-10-26): The shared player spawn search now
rejects terrain-safe candidates inside active World Loom barriers, covering
startup and death recovery when a nearby safe surface lies in another chunk.
Invalid coordinates stop the bounded search safely. Spawn/barrier coverage
remains open. Source-only; tests and builds remain deferred.

Barrier-aware animal effects (2026-10-26): Animal movement rejects safe-step
destinations inside active World Loom barriers, while allowing an animal
already there to step out. Hostiles inside a barrier cannot attack through it,
and chickens there do not add eggs during regeneration. Existing entity
occupancy and publication recovery remain open. Source-only; tests and builds
remain deferred.

Barrier-aware player footprint (2026-10-26): Horizontal movement now checks
the player's full collision width against reserved chunks, preventing the body
from overlapping a barrier before the camera center crosses the chunk edge.
Chunks already intersecting the player's starting footprint remain traversable
so an already-overlapping player can move out. This only covers the local
movement path; authoritative multiplayer movement admission and teleport
transitions remain open. Source-only; tests and builds remain deferred.

Completed terrain recovery implementation (2026-10-26): Startup can reconstruct
the most recent completed World Loom revision as an in-place deterministic
replay, restoring its saved selection, publication receipts, and original
regeneration mode while keeping the externally visible revision identifier.
Replay lifecycle metadata accepts the saved stage mask for that mode.
After selected chunks load, the session rehydrates saved regeneration rules
and refuses a replay whose selected footprint changes. The replay rebuilds
per-chunk feature assignments. While the process remains live, completed
assignments are retained and passed through the normal chunk streaming request
so an evicted selected chunk regains its terrain features. Existing publication
receipts suppress duplicate spawner and loot publication during replay; their
stores remain separately persisted. WVC1 now persists voxel bytes and mutable
edits separately, so normal startup can reuse saved terrain. WBK v3 now includes
WVC1 terrain with WBJ1 rollback on restore. Cross-file crash consistency and
replay/restart acceptance remain open. Source-only; tests and builds remain
deferred.

Regeneration outcome allocation failure (2026-10-26): Per-chunk failure and
skip outcome recording now returns allocation errors instead of swallowing
them. A failed outcome allocation is also recorded as a generation error, so
the revision cannot finish as successful with missing outcome state. The
manager pre-reserves outcome capacity for the selected set before scheduling;
this error path remains covered by the later fault-injection test pass.
Source-only; tests and builds remain deferred.

World Loom prepare-checkpoint retry (2026-10-26): If the initial PREPARED
session snapshot fails, `GameSession::commit_regeneration_start` now restores
the transaction journal to its pre-request image. The server drops the new
confirm request-cache entry on transient I/O or allocation errors from that
callback, allowing the same request ID to retry preparation. This applies only
before the debit/deal boundary; committed starts continue to retain their
cached success for checkpoint retry. Crash/fault coverage and builds remain
deferred.

World Revision server durability gate (2026-10-26): Server initialization now
requires both the host start transaction callback and checkpoint writer in
addition to its authentication, station, occupancy, and epoch providers. A
configured network server therefore cannot accept lifecycle mutations while
silently omitting its durable state checkpoint. Existing validator fixtures
need migration to the required callbacks during the later test-authoring pass.
Source-only; builds remain deferred.

World Loom charge settlement and participant binding (2026-10-26): The start
journal now writes WLS6 with the quoted revision identifier, sorted
authenticated participant IDs, card content version, generation and trusted
script manifest hashes, currency
identity/amount, paid and bonus chunk counts, and an explicit retain-after-commit
policy. Fresh occupancy identities and the opening-deal preflight must pass
before debit. Before commit, rejection and checkpoint failure
abort and restore the pre-debit state. After commit, timeout, disconnect or
restart, partial chunk publication, and trusted same-plan retry retain the
upfront cost; an exceptional repair refund requires a separate audited
transaction. The exact card instance zones remain in the atomic session
snapshot. Owner-only status exposes participant, content, and settlement data only
when the record matches the active/completed revision and requesting owner.
WLS1-WLS5 committed records remain readable; legacy prepared records abort
safely from their pre-mutation checkpoint. Runtime checks and builds are deferred until
backend implementation and test authoring are complete.

World backup/restore policy consistency (2026-10-26): Catalog maintenance now
blocks owner edits to access, ownership, allowlist, ban/approval, admission,
compatibility, and character-import policy while a restore holds the world
maintenance lease. Admission-capacity changes also fail while existing join
leases would exceed the new capacity, and admission-mode changes fail while
join leases are active. This closes process-local policy races during restore
and prevents live reservations from becoming invalid through owner edits.
Policy mutations that can change a live session's authorization or world
configuration also fail while that catalog instance has active join leases.
Only one live `WorldSaveCatalog` can now initialize a normalized catalog path
within a process, so separate catalog objects cannot maintain independent
capacity and character-lease tables for the same save. Cross-process
coordination remains open. Source-only; tests/builds remain deferred until
implementation and test authoring are complete.

Catalog world creation orchestration (2026-10-26): `GameSession` now exposes a
backend create-and-start entry point that persists the world definition,
generates a fresh world-bound character identity and retry request ID, then
starts through `start_from_catalog_admitted`. It returns the created
definition even if session startup fails, so callers can retry opening the
persisted world. The session identity is cleared after a failed inactive
start. Catalog creation and first-session startup are not a single durable
transaction; the world entry intentionally remains available after a startup
failure. Source-only; tests/builds remain deferred until implementation and
test authoring are complete.

Direct world-ID admission boundary (2026-10-26): The public
`start_with_world_id` overload now fails closed because it lacks an
authenticated actor, character, and request identity. Its implementation is
private and is reached only by the catalog-admitted start path and the legacy
seed-derived compatibility path. Network publication wiring, durable leases,
and the legacy seed-only compatibility boundary remain open. Source-only;
tests/builds remain deferred.

Owner-confirmed world deletion (2026-10-26): `GameSessionPersistence` now
deletes the ID-derived snapshot and revision sidecar only under catalog and
process-local session maintenance locks. It journals their prior bytes in WBJ1
before removal, then atomically removes the catalog entry only after both save
files are gone. A failed pre-commit operation invokes the existing WBJ1
rollback; startup recovery can restore files if the process stops before the
catalog removal. The owner must type the exact world name, and active join
leases prevent deletion. A WDL1 marker lets admitted world-open recovery
finish cleanup when the catalog entry is already gone, without scanning or
deleting legacy saves that lack a deletion marker. Hosts can also run the
catalog deletion recovery API during startup; `initialize_catalog` combines
catalog loading with that reconciliation before session admission. Cross-
process locking remains open. Source-only; tests/builds remain deferred.

Offline ownership-mode migration (2026-10-26): Owner-authorized
`update_policy` can now change a world between local and shared ownership
modes, using the existing atomic catalog replacement. The operation rejects
while any local join lease is active, so live participants are not silently
carried across an ownership-policy transition. Cross-process lease coordination
and player-profile migration between separately owned worlds remain open.
Source-only; tests/builds remain deferred.

Terrain chunk-store integration (2026-10-26): `WorldTerrainChunkStore` writes
and reads versioned, coordinate-bound, checksummed voxel chunk payloads under
a world-ID directory using atomic replacement and the existing
`game_voxel_chunk` serializer. Catalog-backed GameSession startup initializes
the store. Initial and streamed chunk loads prefer saved payloads; newly
generated chunks, direct placement/deletion/state replacement, undo/redo,
deferred generated edits, and regeneration results update the store. The
regeneration applier restores the prior payload if a later in-process commit
step rolls back. Remaining gaps include crash-atomic coordination with the
GameSession snapshot and entity/spawner/instability stores, cross-process
access, and complete recovery/acceptance coverage. This does not yet constitute
a complete world-save transaction. Source-only; tests/builds remain deferred
until implementation and test authoring are complete.

Terrain-aware world bundles (2026-10-26): WBK1 v3 now includes a bounded,
checksummed WTA1 archive of the world's WVC1 chunk files. Export and import
validate each WVC1 header, coordinate-bound filename, payload length, record
checksum, and complete voxel payload. Import validates the archive before
changing state, keeps WBK1 v2 compatibility without touching terrain, and uses
WBJ1 v2 to retain
the pre-import terrain archive alongside the prior MCSP and revision sidecar.
Recovery restores all previous data after
an interrupted v3 install, including the prior absence of a terrain directory.
Cross-file crash durability and concurrency across separate stores remain
open. Source review only; tests and builds remain deferred until test
authoring is complete.
