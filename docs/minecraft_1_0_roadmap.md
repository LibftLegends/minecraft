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

This roadmap distinguishes two milestones. **Regeneration 1.0** is the first
complete, playable World Loom/card-driven regeneration release. **Game 1.0**
is the broader survival release that builds on it with the full item,
crafting, survival, combat, farming, fauna, customizable player-avatar, and
custom-structure progression package. This includes villages and villagers,
and a late progression volcano/boss feature. Before either release, build a
deliberately tiny end-to-end playable vertical slice to validate the core
interaction and atomic world update.

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

At the time of writing, the Minecraft submodule is on
`agent/compression-analytics-cardgame-scripting` at `4922eb88`. The fetched
`very-real-engine-checkout` branch diverges from it and is not merged into it.
Implementation must target the compression/analytics branch named above and
must not assume that changes found only on `very-real-engine-checkout` are
available.

That branch provides useful Compression and Analytics APIs. Its CardGame work
is design groundwork, not a dependency that this first playable feature may
assume is already implemented. The first implementation should use a narrow
Minecraft-owned regeneration-deck adapter with stable IDs and declarative
effects. Add a Libft CardGame adapter later when the branch has a reviewed,
usable deck/effect API. Keep the adapter boundary so that migration does not
change saved deck data or gameplay semantics.

Any future Libft code changes must follow `Libft/AGENTS.md`; in particular,
test hooks stay test-only, APIs use stable IDs rather than serialized function
pointers, and fallible lifecycle operations are explicit. This task itself
adds only a Minecraft documentation file.

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

The supporting progression loop is: gather or receive world item stacks, put
them in a deliberately sized inventory, use a recipe book at the required
station, smelt ores and cook food in a furnace, craft equipment and physical
cards, then move eligible physical cards into the separate regeneration deck.
This is a recipe-selection interface, not a shaped crafting grid.

The station is an interface and an ownership/permission anchor, not the owner
of terrain data. Opening the UI must not pause the world or block ordinary
movement, rendering, edits, or networking.

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

## 6. Map, selection, and preview

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
as a whole or consumed by ordinary gameplay, and it is not the future
general-purpose CardGame deck. Individual crafted card items may be carried in
inventory before being inserted into the regeneration deck, or after being
removed from it. Moving one into the deck transfers that unique instance; it
must never exist in both places at once.

The configured deck contains **at least 20 and at most 100 card instances**.
The initial starter deck contains 20 deliberately low-power cards. Deck edits
are transactional: a card cannot be removed if that would leave fewer than 20
cards, and a card cannot be inserted if the total would exceed 100. Deck size
counts all owned instances across draw pile, operation hand, and discard pile;
normal draws/plays move an instance between those zones and do not change deck
size. The user can craft/obtain cards, insert them through the World Loom deck
screen, and remove them back to inventory subject to those bounds and available
inventory space.

Each card instance has a unique ID even when several copies share one card
definition. The deck persists its ordered draw pile, hand, discard pile,
configuration version, and deterministic shuffle state. Progression may
unlock definitions, but unlocking alone does not insert a copy. When the draw
pile is empty, reshuffle the discard pile using the server's deterministic,
persisted shuffle stream. If no drawable cards exist, return a clear no-cards
result; never silently create a card. Hand size, simultaneous sessions, and
cards played per session are separately bounded by configuration.

### 8.2 Effect representation

Card definitions are configuration-driven and refer to stable IDs. Prefer a
small declarative set of validated regeneration operations in the first
implementation. Example operations include:

- add/remove a biome from an allowed set;
- adjust biome weights within configured bounds;
- enable/disable an ore or alter its configured distribution profile;
- add a bounded water, lava, cave, structure, or surface-feature profile;
- modify feature density within world-defined safe limits;
- draw/discard cards through the regeneration deck API;
- grant a bounded number of extra chunk targets;
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
4. Played card effects in a documented order (the default is the order played
   by the player).
5. Deterministic generation using the resulting immutable policy.

Cards may modify values only inside configured ranges and may not remove
mandatory safety constraints. Conflicting cards either compose by an explicit
operation (`add`, `multiply`, `replace`, `intersect`, or `exclude`) or are
rejected as incompatible during preview. Never rely on container iteration
order to resolve conflicts.

The server canonicalizes the resolved policy and computes a digest. The digest
includes generator version, terrain config, mode/stage mask, card definition
versions and ordered instances, and relevant world policy. Persist it with
the revision and chunk metadata.

### 8.4 Starter deck example

The following is illustrative balance content, not fixed implementation data:

| Card | Example effect | Purpose |
|---|---|---|
| Biome Compass (5 copies) | Adds a small configured set of eligible biome weights | Teaches biome selection without forcing an unsafe hard replacement |
| Deep Seam (4 copies) | Increases one selected ore profile within world caps | Simple underground modification |
| Springseed (3 copies) | Enables a bounded freshwater feature profile | Introduces surface water with containment checks |
| Gentle Ridges (4 copies) | Adds a modest height-variation profile | Makes terrain changes legible and low risk |
| Surveyor's Reach (2 copies) | Grants one additional target chunk for this session | Demonstrates bonus scope and its explicit accounting |
| Recycle Draft (2 copies) | Discards one card from the regeneration hand and draws one replacement under the draw-cost rules | Demonstrates deck manipulation without touching player inventory |

These example counts total the required 20-card starter deck. Card definitions
are configuration content and can change without changing the deck-size
invariant.

Starter effects should be weak, transparent, and deterministic. Avoid cards
that force lava, erase edits, override protection, create unbounded structures,
or guarantee rare loot. Progression can add stronger combinations after the
preview and rollback systems are reliable.

### 8.5 Card recipe discovery and Card Press crafting

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
all game configuration. Libft's card/recipe machinery may validate and execute
configured operations, but Minecraft owns exploration rewards, Card Press
access, material economy, and the mapping from recipe outcomes to generation
effects. Do not put raw callbacks or mutable world pointers in a card or recipe.

Tests must verify exact known-recipe output, blueprint unlocks, ingredient
influence on experiment eligibility/weights, tier caps for every candidate,
discovery bias, no-result/fully-discovered policy, and deterministic replay of
the committed outcome. Inject failures at material reservation, random-result
selection, output creation, discovery persistence, and commit; either all
effects commit once or none do. Duplicate request IDs, disconnects, save
reloads, and deliberately repeated experiment requests must not duplicate
materials/cards or reroll a completed experiment. Confirm the normal Card Press
UI remains a recipe book and never becomes an ingredient grid.

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
5. Verify the exact Compression and Analytics APIs on the selected Libft
   branch; do not change the submodule branch/pointer as part of this roadmap.

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
4. Use one configured regeneration card/effect and a minimal test deck; the
   full 20-card starter deck and deck editing are later Regeneration 1.0 work.
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
   resource reservations/refunds, and a 20-card starter deck with unique card
   IDs, draw/hand/discard state, and bounded deck editing.
3. Add the starter card set, effect precedence, deterministic resolved-policy
   digest, and safe preview/confirmation workflow. Card manufacturing can
   remain configured starter content at this milestone.
4. Harden bounded priority scheduling, per-chunk prepare/commit, cancellation,
   persistence/compression, and authoritative client replication.
5. Meet atomic block/light/mesh publication and strict frame/edit-latency
   gates. Analytics-on/off runs must yield the same world result.
6. Add the versioned custom-feature/structure planning interface, including
   default-world versus regeneration-only policy, complete chunk footprints,
   stable structure-instance IDs, and renderer-visible cross-chunk fragments.
   Production villages/volcano content comes later.
7. Provide a valid default player avatar for other clients; the full appearance
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
3. Connect crafted unique card items to World Loom deck transfers without
   changing the Regeneration 1.0 card identity/save contract.

### Phase 5 — Game 1.0 player avatars and customization

1. Add the final block-style player rig/model, default appearance, and
   authoritative pose/animation replication.
2. Add the player appearance UI and saved, validated customization profiles.
3. Confirm armor, tools, shields, bows, and staffs display on the model without
   changing gameplay collision or combat authority.

### Phase 6 — Game 1.0 neutral fauna

1. Complete authoritative entity ownership, persistence, spawn/despawn, and
   renderer submission; implement sheep as the first species vertical slice.
2. Add sheep drops/breeding/maturation, then cows, pigs, and chickens with
   configured behavior, feed, cooldowns, and drop tables.
3. Verify regeneration cannot duplicate, erase, or relocate protected
   creatures, eggs, drops, or breeding state.

### Phase 7 — Game 1.0 custom structures and regeneration progression

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

### Phase 8 — broader content and extension

1. Add richer draw/discard, bonus-scope, feature, and combination cards.
2. Add further structure definitions and progression content through config,
   without adding per-structure special cases to the chunk renderer.
3. If Libft's CardGame module is implemented and reviewed on the target branch,
   add an adapter without changing persistent card IDs or replay semantics.
4. Add optional animal species only after entity and performance gates pass.

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

The slice may seed dust and a test deck through explicit development/test
fixtures. It does not require the general item economy, card discovery, or a
final character model.

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
- the recipe book, workbench, furnace, fuel, smelting, cooking, inventory,
  paper/card materials, Card Press recipes, and deck transfer preserve item
  counts transactionally;
- exploration unlocks and ingredient-constrained Research Synthesis discover
  cards; known recipes craft exact cards, while random results are bounded by
  their disclosed tier, committed once, persisted, and replay-safe;
- health and hunger are authoritative and persistent; farming, grain/bread,
  fruit, raw/cooked foods, and optional thirst follow configured rules;
- starter tools, armor, weapons, shields, staffs, and combat/magic profiles
  have explicit recipes, balance configuration, and server validation;
- players have a rendered character model and can customize an allowed,
  persistent appearance profile; appearance cannot change collision, reach,
  movement, damage, or other authoritative gameplay stats;
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
- Implementing a full general-purpose card-game engine in Minecraft.
- Allowing clients to regenerate terrain or choose their own seed, regenerating
  protected chunks, or silently deleting player content.
- Sending chunk meshes over the network or running analytics, compression,
  disk I/O, lighting propagation, or full mesh rebuilds in the render frame.
- Requiring final player-model assets/customization to pass the minimum slice;
  a temporary placeholder is acceptable there.

Regeneration 1.0 also does not require the complete survival economy, animal
breeding, combat progression, experimental card crafting, or customizable
avatars, nor does it require finished village/villager or volcano/boss content.
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
| Soil and terrain | `voxel:grass`, `voxel:dirt`, `voxel:sand`, `voxel:gravel`, `voxel:clay`, `voxel:coarse_dirt`, `voxel:podzol`, `voxel:mud`, `voxel:red_sand`, `voxel:terracotta`, `voxel:salt`, `voxel:wet_sand` | Same block item, except grass drops dirt |
| Common and regional stone | `voxel:stone`, `voxel:bedrock`, `voxel:permafrost`, `voxel:canyon_rock`, `voxel:slate`, `voxel:moss_rock`, `voxel:granite`, `voxel:andesite`, `voxel:diorite`, `voxel:obsidian`, `voxel:mossy_stone`, `voxel:cracked_stone`, `voxel:limestone`, `voxel:basalt`, `voxel:frozen_stone`, `voxel:chalk`, `voxel:volcanic_rock`, `voxel:quartz`, `voxel:amethyst`, `voxel:amber`, `voxel:frost_crystal`, `voxel:shimmer_stone` | Same block item |
| Ores | `voxel:coal_ore`, `voxel:iron_ore`, `voxel:gold_ore`, `voxel:diamond_ore`, `voxel:emerald_ore`, `voxel:copper_ore` | Same ore-block item; the furnace may consume that item to make its configured bar/material |
| Wood and plants | `voxel:shrub`, `voxel:oak_log`, `voxel:oak_leaves`, `voxel:cactus`, `voxel:pine_log`, `voxel:pine_leaves`, `voxel:birch_log`, `voxel:birch_leaves`, `voxel:red_flower`, `voxel:yellow_flower`, `voxel:tall_grass`, `voxel:fern`, `voxel:dead_bush`, `voxel:red_mushroom`, `voxel:brown_mushroom`, `voxel:mushroom_stem`, `voxel:lily_pad`, `voxel:seagrass` | Same block item |
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

### 21.3 Thirst is optional and gated

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
        -> Regeneration 1.0 resource ledger + 20..100 deck + replication
        -> versioned custom-feature planner + cross-chunk fragment metadata
             -> Game 1.0 villages/villagers
             -> regeneration-only 9x9+ volcano/boss -> top equipment tier

item definitions + inventory transactions
        -> all-block drops/pickups + buckets + dust deposit
        -> recipe book + stations
             -> furnace/smelting/cooking + tools/armor/weapons
             -> farming/food + health/hunger
             -> paper/ink/card stock + Card Press/discovery
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
