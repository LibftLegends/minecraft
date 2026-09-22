# Minecraft 1.0 Roadmap

**Status:** implementation design; no code changes are included in this document change.
**Audience:** Minecraft implementation agent (Luna), reviewers, and future maintainers.
**Date:** 2026-09-21

## 1. Goal

Turn world regeneration into an intentional, understandable game system rather
than an administrative revision tool. A player should be able to use a
dedicated in-world block to inspect a map, choose chunks, pay a configurable
world resource such as magical dust, draw and play cards from a regeneration-
only deck, preview the result, and ask the authoritative world service to
regenerate those chunks safely. Keep the broader survival foundations—world
items, inventory, crafting, smelting, gear, food, health, hunger, and fauna—in
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

The same design must leave room for basic non-hostile animals—sheep, cows,
pigs, and chickens—without allowing regeneration to duplicate, erase, or
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
4. The player spends magical dust (or the configured resource) to draw a
   chosen number of cards from the regeneration deck. The next draw costs more
   than the previous draw in the same draw action.
5. The player plays cards onto the selected chunk set, chooses valid targets,
   and may use configured draw/discard or scope-modifying effects.
6. The client requests a server preview. The preview includes the resulting
   biome/feature policy and a warning for any terrain, fluid, structure, or
   mob-state consequence the server can determine.
7. On confirmation, the server revalidates the request, reserves its resources,
   prepares per-chunk jobs, and returns a session ID. The UI shows accepted,
   queued, generating, ready-to-commit, committed, skipped, or failed status.
8. Each successful chunk is published atomically. The map and all clients
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

### 4.1 Multiplayer regeneration reservation and card window

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
ban a character ID.

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

### 8.4 Starter deck example

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
structure. A crossover entry may use concepts from a card feature—such as a
shipwreck, volcano, or ruined village—but it is an instability-driven variant,
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
more dangerous because of deliberate player choices—powerful cards,
overdrawing, empty-deck draws, and other accepted risk events—not because
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
recipe always produces that card definition—never a random substitute. Inputs,
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
the paid tier—no blank outcomes, safety-rule bypasses, or low-cost chance at a
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

Use the checked-out Libft `CardGame` module as the rules-neutral runtime for
card identity, ordered zones, draws/shuffles, effect dispatch, choices,
usage limits, and match/session state. Minecraft owns the world-specific
meaning of cards and every world mutation. Implement one thin
`MinecraftCardGameAdapter` boundary that translates versioned Minecraft
content into Libft definitions and translates validated engine results back
into Minecraft-owned intents. Do not build a second card-game runtime in
Minecraft.

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
edge is one continuous structure—not a completed structure ending at each
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
one. Based on the chunk-regeneration context, “minimum 9x9” means a minimum
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

### Phase 0 — validate the existing foundation

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

### Pre-Phase 1 — finish the minimum playable vertical slice

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

### Phase 1 — Regeneration 1.0

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

### Phase 2 — Game 1.0 item and inventory foundation

1. Audit Libft Game's item/inventory/recipe APIs on the target branch; define
   Minecraft stable item IDs, block-item mappings, stack rules, and adapters.
2. Implement ground item spawn, pickup, merge, persistence, and replication;
   cover all registered blocks with the documented drop rules.
3. Add bucket-based water/lava transfer, grass spread, and the remaining
   persistent item/world interactions.
4. Establish configurable inventory capacity and transactional item transfers.

### Phase 3 — Game 1.0 crafting and survival loop

1. Add recipe-book crafting (no grid), station checks, workbench, furnace,
   fuel, ore smelting, and cooking.
2. Add paper plants, farming/grain/bread, apple/pear harvesting, and the
   configured food catalog.
3. Implement authoritative health and hunger; keep thirst optional and
   independently configurable.
4. Add the initial tools, armor, weapons, shields, staffs, and combat/magic
   rules, with balance and performance tests.

### Phase 4 — Game 1.0 card manufacture and discovery

1. Add paper/card materials and the Card Press using the recipe-book model.
2. Add exploration blueprints, deterministic known recipes, and optional
   ingredient-constrained Research Synthesis with disclosed candidate pools
   and bounded effect tiers.
3. Connect crafted unique card items to both World Loom deck transfers and the
   card collection/minigame catalogue without changing stable definition,
   printing, or physical-instance IDs.

### Phase 5 — Game 1.0 Card Table minigame

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
   validators in section 6.5 before enabling public multiplayer worlds.

### Phase 6 — Game 1.0 player avatars and customization

1. Add the final block-style player rig/model, default appearance, and
   authoritative pose/animation replication.
2. Add the player appearance UI and saved, validated customization profiles.
3. Confirm armor, tools, shields, bows, and staffs display on the model without
   changing gameplay collision or combat authority.

### Phase 7 — Game 1.0 neutral fauna

1. Complete authoritative entity ownership, persistence, spawn/despawn, and
   renderer submission; implement sheep as the first species vertical slice.
2. Add sheep drops/breeding/maturation, then cows, pigs, and chickens with
   configured behavior, feed, cooldowns, and drop tables.
3. Verify regeneration cannot duplicate, erase, or relocate protected
   creatures, eggs, drops, or breeding state.

### Phase 8 — Game 1.0 custom structures and regeneration progression

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

### Phase 9 — broader content and extension

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
current source tree. Libft's Game module has item-definition, inventory, and
recipe/crafting building blocks, but these do not by themselves provide
Minecraft world entities, rendering, collision, station discovery, or the
server transaction workflow. Audit those APIs on the selected Libft branch
before deciding what to reuse; avoid creating a second incompatible item
catalog if the existing definitions meet the needs.

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

The current built-in Voxel registry contains 64 IDs including air (therefore
63 non-air built-ins). Audit and test against the canonical runtime registry,
not a hand-maintained partial list. As of the registry inspected for this
roadmap, the non-air names are:

| Group | Existing built-in blocks | Default drop item |
|---|---|---|
| Soil and terrain | `voxel:grass`, `voxel:dirt`, `voxel:sand`, `voxel:gravel`, `voxel:clay`, `voxel:coarse_dirt`, `voxel:podzol`, `voxel:mud`, `voxel:red_sand`, `voxel:sandstone`, `voxel:terracotta`, `voxel:salt`, `voxel:wet_sand` | Same block item, except grass drops dirt |
| Common and regional stone | `voxel:stone`, `voxel:bedrock`, `voxel:permafrost`, `voxel:canyon_rock`, `voxel:slate`, `voxel:moss_rock`, `voxel:granite`, `voxel:andesite`, `voxel:diorite`, `voxel:obsidian`, `voxel:mossy_stone`, `voxel:cracked_stone`, `voxel:limestone`, `voxel:basalt`, `voxel:frozen_stone`, `voxel:chalk`, `voxel:volcanic_rock`, `voxel:quartz`, `voxel:amethyst`, `voxel:amber`, `voxel:frost_crystal`, `voxel:shimmer_stone` | Same block item |
| Ores | `voxel:coal_ore`, `voxel:iron_ore`, `voxel:gold_ore`, `voxel:diamond_ore`, `voxel:emerald_ore`, `voxel:copper_ore` | Same ore-block item; the furnace may consume that item to make its configured bar/material |
| Wood and plants | `voxel:shrub`, `voxel:oak_log`, `voxel:oak_leaves`, `voxel:cactus`, `voxel:pine_log`, `voxel:pine_leaves`, `voxel:birch_log`, `voxel:birch_leaves`, `voxel:red_flower`, `voxel:yellow_flower`, `voxel:tall_grass`, `voxel:fern`, `voxel:dead_bush`, `voxel:red_mushroom`, `voxel:brown_mushroom`, `voxel:mushroom_stem`, `voxel:lily_pad`, `voxel:seagrass`, `voxel:ladder`, `voxel:trapdoor` | Same block item |
| Cold-region blocks | `voxel:snow`, `voxel:ice`, `voxel:packed_ice`, `voxel:packed_snow` | Same block item |
| Fluid | `voxel:water` | No direct fluid-block item; collect/place through a water bucket |

`voxel:air` never drops. The explicitly required exception is
`voxel:grass` → `voxel:dirt` item. Every other existing non-air, non-fluid
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
balance values—not hard-coded assumptions in the inventory or recipe engine.

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
| Tools, weapons, and armor | Pickaxe, axe, sword, bow, arrows, shield, staff, and cloth/leather/iron armor pieces for each configured equipment slot. Definitions carry mining tier, durability, attack/defense/style values, and stack/instance rules. Unique or durable equipment uses item instances. |
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

Water already has a built-in block ID. Lava is not present in the inspected
built-in registry and must be added as a fluid block definition before lava
generation, placement, or bucket behavior is implemented. Both fluids need
world-cell representations for terrain generation and simulation. Neither
fluid source nor flowing state is obtainable as a normal block item; the
catalog explicitly marks direct water/lava block-item acquisition as
unobtainable. Their legal portable forms are the water bucket and lava bucket.

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

- Enumerate all 64 built-in IDs and every registered runtime block. Confirm
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
scattered constants. “Saturation modifier” is the food quality multiplier;
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

This feature adds work in nearly every hot path—world updates, items, entities,
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

- **Cloth + staff — flexible magic:** lower flat defense, broader utility,
  healing/ward options, and configurable spell efficiency.
- **Leather + bow — mobile ranged:** medium defense, movement advantage, and
  stronger bow damage, balanced by ammunition and close-range vulnerability.
- **Iron + sword + shield — tanky melee:** highest flat defense and guard
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
