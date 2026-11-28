# Source Layout

This folder is organized by file responsibility using shallow, single-word folder names. Each folder lives directly under `src`, and class filenames should make it obvious why they belong there.

## Runtime Flow

`main.cpp` calls `Application::run()`. From there, the application owns the window, input, player controller, world, render policy, and renderer. A frame is roughly:

1. `app` runs the high-level loop.
2. `config` parses CLI options.
3. `platform` talks to the DUMB window/input/framebuffer layer.
4. `player`, `movement`, `physics`, and `spawn` update the camera/player state.
5. `interaction` converts player actions into block edits.
6. `validators` contains all command-line and integration checks.
7. `policy` decides render distance and generation budget.
8. `world`, `chunks`, `coordinates`, and `queries` provide terrain data.
9. `edits` applies command-pattern block changes.
10. `render` coordinates the render pass through the render helper folders.

## Runtime terrain customization

The application can configure terrain before starting a `GameSession`; the
configuration is copied into `World` and passed to every generated chunk.
Libft keeps the low-level generator and built-in templates, while the game
chooses the policy:

```cpp
terrain_generation_config terrain;
terrain_default_generation_config(terrain);
terrain.set_sea_level(68);
terrain.set_water_chance_percent(55);
terrain.set_biome_height_profile(0U, 76, 10, 3); // uneven plains
terrain.set_biome_decoration_policy(0U, FT_TRUE, FT_TRUE, 6U, 8U);
terrain.set_biome_tree_template_override(0U,
    &terrain_large_oak_tree_template());

GameSession session;
session.set_terrain_generation_config(terrain);
session.start(seed, window, renderer);
```

For saved worlds, pass the world-owned terrain policy path to
`World::initialize(seed, terrain_config_file_path)`. `World` loads the policy
through Libft before the first chunk is generated, and Libft owns the binary
format and file I/O. Once generation starts, `World` keeps a private Libft
generation context and ignores later terrain-policy changes until the world is
restarted. Use `World::save_terrain_config(...)` to persist the active policy.

Custom biome slots are selected with `biome_selector`; custom tree/object
templates are supplied through `terrain_feature_rule`. Validate a policy with
`terrain_generation_config_is_valid` before starting generation. The
integration check is available through `--validate-terrain-configuration`.

Biome sizing is explicit and can be tuned globally or per biome. Set
`enable_biome_size_control` to `FT_FALSE` to use the fixed legacy zone size,
or enable it and set the global fallback range with
`set_biome_size_range(minimum, maximum)`. Individual biomes can opt into their
own range with `set_biome_size_range_for_biome(index, minimum, maximum)` and
can be disabled again with `set_biome_size_override_enabled(index, FT_FALSE)`.
The same fields can be supplied to a regeneration request, so a small,
temporary mountain range does not alter the saved baseline policy:

```cpp
terrain_generation_config revision_config;
revision_config.initialize(world.terrain_generation_settings());
revision_config.set_biome_size_control_enabled(FT_TRUE);
revision_config.set_biome_size_range_for_biome(
    TERRAIN_BIOME_MOUNTAINS, 16, 64);
request.config.initialize(revision_config);
world.apply_revision_request(request, &result);
```

## World revisions

`World` exposes server-side revision primitives for applying a new terrain
policy to selected chunks. Call `begin_world_revision` with a regeneration mode,
protect edited or important chunks with `set_chunk_protected`, select eligible
coordinates with `select_revision_chunk`, and call
`regenerate_selected_chunks` only after the preview is confirmed. Edited chunks
are automatically protected, manual protection includes a one-chunk safety
ring, and `build_revision_preview` supplies the protected/selected/transition/
unchanged categories for a map client. Revision identifiers and manual
protection can be persisted with `save_revision_metadata` and restored with
`load_revision_metadata`.

GameSession saves also embed World Revision metadata in snapshot version 10,
so the station anchor and revision policy are replaced atomically with the
session inventory, CardGame state, rules, instability records, and transaction
journal. Older session snapshots without the embedded payload can still load
the legacy seed-specific `.wrv` sidecar. New saves no longer update that
sidecar. This preserves metadata across ordinary session saves; it does not
persist an active selection draft or make chunk publication crash-recoverable.

`unselect_revision_chunk` explicitly removes one selected coordinate, and
`update_revision_chunk_selection` is the compare-and-set API: it requires the
64-bit expected selection revision and returns the current revision, rejecting
stale selection updates without applying them. The revision is visible through
`World::world_revision()`.
`query_revision_start_cost` returns a read-only quote from the authoritative
selected set and a caller-supplied `WorldRevisionCostPolicy`. The quote keeps
paid chunk count, currency item ID, chunk cost, opening-card count/cost, total
currency cost, regeneration ID and selection revision in separate fields. The
policy supplies a nonzero currency item ID, per-chunk units, opening-deal base
price and total cap. Any actual selection
change advances the selection revision; callers can pass that revision to the
query overload to reject a stale quote request. Opening deals accept 4–7 cards;
if `B4` is the
configured four-card price, their total prices are `B4`, `2*B4`, `4*B4`, and
`8*B4`. Arithmetic and total-cost caps are checked. These APIs do not debit a
player balance, start a CardGame session, reserve chunks, or specify a client
UI; those belong to the future authoritative regeneration-session adapter.

Network or multiplayer adapters should submit a `World::RevisionRequest` to
`World::apply_revision_request`; it performs the server-side validation and
returns a `RevisionRequestResult` rather than requiring clients to mutate the
world directly. The revision server's `select`, `unselect`, and `protect`
actions require the current decimal `selection_revision`; selection edits use
compare-and-set semantics and enforce the hard selected-chunk limit.

In the interactive client, press `M` to show the read-only revision preview in
the HUD. It is centered on the player and uses red/protected, green/selected,
gold/transition, and gray/unchanged cells.

The selection API uses a 64-bit compare-and-set revision and enforces the
caller-provided cap beneath a 64-chunk hard limit. The revision manager also
stores a server-owned `WorldRevisionCostPolicy`; policy changes use their own
compare-and-set revision. Start-cost quotes require both current revisions and
return them with the checked 4–7 card opening price and selected-chunk cost.
Quotes return paid and bonus chunk counts separately and charge only paid
chunks. Bonus selection is host-authorized, still counts toward the selection
cap, and uses the same loaded-chunk and protection checks.
The host can also update the occupied chunk set; each occupied chunk and its
eight neighbors are protected from selection, and changes invalidate quotes.
The single-player simulation tick feeds its current chunk into this API.
Multiplayer aggregation and atomic refresh at start commit are not yet
connected.

The revision server accepts an authorized `world_revision` `quote` action and
sends a direct `world_revision_quote` response to that client, including a
result code and the complete quote on success. Reusing the request ID returns
the cached quote/error, while changing quote inputs under that ID is rejected.
Mutation actions return `world_revision_result` with the resulting revisions
and selected count. Exact retries return the same cached state snapshot.
The owner may cancel a draft through the `cancel` action; this releases the
actor/Loom binding and exact retries replay the cached cancellation result.
These APIs do not reserve chunks, debit inventory, create a CardGame session,
or persist a World Loom transaction.
World revision metadata version 2 stores the cost policy and revision with a
checksum through Libft's atomic replacement API. Version-1 protection metadata
still loads and preserves an already configured runtime pricing policy.

`GameTransactionJournal` stores scoped request identity, transaction type,
payload and prepared/committed/aborted state in a bounded checksummed snapshot.
Its save path uses atomic replacement; loading validates into staged state and
keeps the live journal unchanged on malformed input. The
`--validate-transaction-journal` validator covers exact retries, changed-payload
rejection, terminal-state
transitions, pending recovery queries, checksum rollback and file round-trip.
GameSession owns the journal and includes it in version-6 combined session
saves; versions 1–5 still load, with an empty journal. Ground-item pickup and hand crafting now
persist prepared requests before changing inventory, then save
the terminal journal state with character, world-item and recipe data. Startup
replays prepared pickup and hand-craft requests before play. Other inventory changes, timed processing, CardGame and World Loom operations are not yet connected
to this recovery path.

The terrain library supports the same stage masks used by the four modes, so
decoration and underground refreshes operate on the existing chunk while full
regeneration rebuilds it. Selected boundaries adjacent to loaded protected
chunks receive a height blend before their meshes are rebuilt.

## Regeneration Card Backend

`RegenerationCardSession` registers the seven versioned starter definitions
and the implemented rare definitions: Ore Overgrowth, Ultimate Greed, Biome
Rupture, Fortune's Draw, Volcano Mark, Volatile Stabilizer, and Ruined Village
Relic. It seeds the canonical 20-card
starter multiset in the engine's native draw pile and hand.
Accepted plays emit typed host intents; biome, ore, chunk and no-choice payloads
are checked against the supplied active generation config before the physical
card is consumed. Biomes currently use one-based config indices, while ore
targets use enabled ore block IDs. Double Draft resolves
only from its acknowledged host intent, submits two draws and a one-card
mandatory discard through Libft commands, and caches exact retries. Played,
drawn, discarded and burned cards retain one physical instance across the
engine's native zones and configured discard/burn zones. The in-memory request
cache retains accepted actions until session destruction, accepts at most
4,096 card-play/draw/discard requests total, and rejects new requests without
mutation once full. The adapter blocks further card plays while Double Draft
resolution or its discard obligation is pending. GameSession preflights
supported card effects against current regeneration rules before consumption, so effects
that would exceed guaranteed plan caps fail without removing the card. Cached
request retries skip preflight and retain their original exact result.
Surveyor's Reach also uses a read-only world selection check before card
consumption: the target must be loaded, unprotected, unoccupied, unselected,
and within the selection cap. Selection and server-side preflight share the
same eligibility rules.
Revision-checked transfers now move the seven starter physical card items and
the supported Ore Overgrowth, Ultimate Greed, Biome Rupture, Fortune's Draw,
Volcano Mark, Volatile Stabilizer, and Ruined Village Relic items between inventory
and the draw pile,
preserving their 64-bit identity through a session-persisted mapping to the
engine instance ID. Both directions
cache exact request results; inventory-full returns do not remove the deck
card. GameSession routes transfers through the shared prepared transaction
journal and replays pending records during startup recovery. Runtime journal
recovery and exact retry are covered by `WorldItemStoreValidator`, but that
validator has not yet been executed with this change; save-failure injection
and the other rare card definitions/use remain open. Validation runs with
`--validate-regeneration-cards` and from `--validate-all`.

Fortune's Draw is discovery-locked at recipe 2019 and requests one card through
the same policy and retry cache as the other card draws. Card recipes commit
through the physical-ID allocator so each crafted card remains a unique item.

Volcano Mark is definition 15 and remains recipe-gated by its existing physical
item recipe. It now requires a paid, selected contiguous 9-by-9 chunk square
before card consumption and distributes one anchored global plan across those 81
chunks. The worker builds the same volcano profile from global coordinates in
each chunk, including a crater lake, supported arena shelf, and passage; the
validator source checks lava containment and geometry across chunk seams.
World Revision permits up to 256 selected chunks. Playing the card also
requires its rare blueprint recipe to be known. Hosts can register a trusted
discovery source and grant that recipe through the checkpointed discovery API;
the exploration/research source itself is not authored yet. Configured larger
footprints, full lighting/support and structure-manifest validation, boss
gating/lifecycle, and runtime validator execution remain incomplete.

This remains adapter groundwork: Voxel config has no stable biome ID field, so
the current biome choice mapping is config-local and must be versioned with
the generation manifest. Minecraft has not yet applied all terrain host intents
transactionally or connected the session to a committed World Loom start.
Card session and action replay state are included in the versioned session
snapshot. Session checkpoints flush the temporary file before replacement;
POSIX builds sync the containing directory, and Windows replacement requests
write-through semantics.
The RCS1 version 4 adapter snapshot also stores the script bridge manifest
hash. Restore can load a bounded script catalog only when the caller supplies
its independently trusted manifest and policy hashes; the catalog, context
key schema and saved context values are installed before the engine snapshot
is accepted. Version 1–3 snapshots remain readable for scriptless sessions.
Hosts configure the catalog and authenticated content hash on `GameSession`
before `start()`; startup binds it to the generation-policy manifest before
restoring saved card state.

`RegenerationWorldRules` folds accepted card intents into a session-local
overlay: biome exclusion, checked cumulative ore scaling, village and magma
plan counts, configured biome-weight adjustments, and additional target scope.
`consume_pending_card_intent` decodes the engine's actual pending card intent,
applies it, then acknowledges that queue entry. The rules reject removal of
the final active biome, enforce configured caps, and deduplicate accepted
host-intent sequences so a retry cannot apply a card twice. `apply_ore_scales`
materializes checked ore frequencies into a caller-owned staged generation
config without changing the base config. The rules-aware
`World::begin_world_revision` overload freezes eligible-biome state and ore
scales into the manager-owned config before async jobs start. It retains the
original selector for eligible biomes and deterministically remaps excluded
selections within their biome zone. With Compass adjustments, it samples
`max(1, 1 + adjustment)` weights among eligible biomes using a stable zone
hash; excluded biomes stay out of that set. Because rule changes alter
generated terrain, this overload requires full stages for each selected
chunk. While the draft remains pending, accepted card intents now refresh the
manager-owned generation config from the immutable base plus the latest rules.
Surveyor's Reach requires a pending draft; the rules overlay now materializes
its typed target as a bonus chunk during the same transactional manager update,
respecting loaded, protected, occupied and selected-cap checks. Duplicate
targets are rejected, and rules-owned targets cannot be manually unselected.
The session rolls back card/rule state and the bonus selection if checkpointing
fails. A target made unsafe by a later occupancy update is removed and blocks
regeneration until it becomes eligible again. The
session rejects card intents once generation is active, and the revision
manager rejects pending-rule updates after the start boundary. The
`GameSession` card action entry points now checkpoint card play, draw, and
mandatory discard together with rules and the transaction journal, restoring
the adapter snapshot if persistence fails. Its CardGame and rules accessors
are read-only; callers use those session entry points for these actions.
Physical card imports/exports checkpoint both the adapter and character
inventory and restore both on save failure. The WorldItemStore validator now
checks in-place inventory snapshot restore and cached transfer retry behavior;
it has compiled but has not run since this addition.
`World` owns a configurable per-chunk instability store with policy-bound,
checksummed serialization. `GameSession` can accept the matching policy before
startup and stores the encoded records in combined snapshot versions 9 and 10.
Versions 1–8 remain readable; version 10 appends World Revision metadata after
instability, and versions 1–9 remain readable. Host card actions and generation
publication still need to update instability records.
Ore Overgrowth now requires an explicit positive instability delta configured
before startup. The amount is bound into the generation manifest, preflighted
against selected chunks, and applied in the same checkpoint as its card intent.
Before World Revision queues generation, it materializes selected-chunk
instability records; successful chunk publication updates the record with a
per-chunk monotonic generation revision and a domain-separated hash of the
serialized accepted-rules overlay. Revisions without a card overlay hash the
base generation configuration signature.
The public view exposes only chunk coordinates and reward/danger bands; server
records and hidden modifier/outcome IDs stay out of that view.
World destruction clears all per-chunk records but retains the configured
instability policy so a reused World cannot leak one seed's state into another.
World Revision validator checks that a Deep Seam effect accepted after draft
creation changes the pending manager config. The card session is still not
connected to the World Loom server action; structure plans and full scope
application remain open.

The overlay now has a bounded version-2 byte codec and file save/load methods.
It stores accepted typed intents with a checksum and the server-supplied
generation/content manifest hash. Restore requires that same nonzero hash,
then replays intents against the caller-supplied base generation config and
policy. Overlay and revision-policy file saves now use the safe replacement
path used by session checkpoints; their independent restart lifecycle and
atomic coordination with CardGame session history remain required.

## Folders

- `app`: `Application`, the application facade and main loop entry point.
- `config`: `ApplicationOptions` and `CommandLine` parsing.
- `platform`: window and input integration.
- `diagnostics`: errors, crash handling, and framebuffer hashing.
- `camera`: camera state and raw camera input data.
- `player`: player controller facade.
- `movement`: player movement intent and camera motion.
- `physics`: collision, ground checks, and player geometry.
- `spawn`: spawn placement rules.
- `interaction`: block selection, placement, and deletion from player actions.
- `inventory`: Minecraft item definitions for built-in block drops, ordinary
  materials, food, gear, and the current card definitions. Each catalogued
  item has a stack limit and one primary acquisition category; full recipe
  coverage and persisted recipe-book progression are still being added.
- `crafting`: 88 stable-ID bootstrap recipes covering the current card outputs,
  starter materials, structures, and configured gear. Workstation constraints,
  default and locked recipe-book states, and output/byproduct transactions are
  enforced through Libft Game. Recipe unlocks and the unique output ID allocator
  persist in session save version 5; load advances the allocator past IDs in
  character and ground-item inventories and restores the bounded craft-action
  request cache (version 1 through 4 saves remain readable). `GameSession` exposes
  hand-station recipe queries and revision-checked craft actions with exact
  retry replay; real workstation discovery/access, UI/network action transport,
  CardGame request persistence and processing-job runtime validation remain open.
  Recipe discovery grants now run through `RecipeDiscoveryTransaction`: the
  recipe book is snapshotted, a trusted registered-source grant is applied,
  and the session checkpoint must succeed before acknowledgement. Failed saves
  restore both book knowledge and runtime crafting locks; the item-catalog
  validator injects a disk-full result and verifies the grant can be retried.
  The session exposes the recipe catalog read-only, so callers cannot bypass
  the authorized, checkpointed discovery path through `unlock_recipe()`.
  The item-catalog validator cross-checks all 19 card items against their
  definition mapping, acquisition and knowledge policy; every supported card
  must also provide an effect and preview metadata.
  Trusted hosts bind discovery source IDs to recipes before startup; GameSession
  installs those mappings before restoring saved recipe knowledge. Catalog
  mappings remain available when a save already contains the discovered recipe.
  The item-catalog validator checks zero IDs, exact duplicate/conflicting
  mappings, and the 4096-source configuration bound.
  Furnace recipes consume one coal per input and use a bounded authoritative
  20 Hz tick queue in `GameSession`; each input takes 200 ticks and
  full-inventory outputs remain pending for later delivery.
- `validators`: validation facade plus camera, collision, block edit, and visible-distance checks.
- `policy`: render-distance strategy and budget policy classes.
- `render`: `VoxelRenderer`, the renderer facade.
- `frame`: render target, frame cache, and color helpers.
- `geometry`: render vertex and triangle texture data.
- `assets`: texture atlas ownership.
- `debug`: debug view, overlay, and debug payloads.
- `sky`: skybox rendering.
- `meshes`: chunk mesh facade, culling, clipping, projection, and rasterizer strategies.
- `world`: `World`, the world facade.
- `chunks`: world chunk data, storage, and loading.
- `coordinates`: world/chunk coordinate conversion helpers.
- `queries`: block lookup and raycasting.
- `edits`: command-pattern block edit operations.

## Folder Rule

Folder names are single words. Group files by the responsibility visible in the class/file name first, then by the domain facade they support. For example, `ChunkMeshRenderer` belongs in `meshes`, while `WorldChunkLoader` belongs in `chunks`.

World Loom start retries now consult the persisted transaction result before checking live revision state. This keeps an exact committed retry idempotent after regeneration begins; conflicting request payloads remain rejected. Strict syntax compilation passes; runtime retry coverage and durable restoration of selected chunks/barriers remain open.

The World Revision validator now includes an accepted start commit followed by a rejected cancellation attempt. Strict syntax compilation passes; runtime execution remains blocked because the Make runner cannot launch Git Bash in this environment (NtCreateDirectoryObject ... 0xC0000022).

World Loom commit idempotency coverage (2026-10-12): The WorldRevisionValidator now replays an accepted confirm request, rejects the same request ID with a changed opening-hand count, and verifies that cancellation remains rejected afterward. Strict warning-as-error syntax compilation passes. Runtime execution remains unverified because Git Bash cannot create its MSYS named object (0xC0000022).

World Loom occupancy feed (2026-10-12): GameSession now accepts a pre-start host provider for all occupied player chunks, refreshes it during world ticks and immediately before a new start commit, and fails closed on provider errors. Binding the network WorldRevisionServer requires that provider; single-player sessions retain camera-derived occupancy. The item-catalog validator checks provider configuration rules. Strict warning-as-error syntax compilation passes. Runtime coverage of provider refresh/commit interaction and the full server validator remain unverified because the Make runner cannot launch Git Bash (NtCreateDirectoryObject, 0xC0000022). Pending selection/barrier restart persistence remains open.

World Loom movement barrier (2026-10-12): WorldRevisionManager now activates a reserved-chunk barrier after the GameSession start commit. PlayerCollision treats reserved chunks as solid for movement into them, permits escape for a player already overlapping the reserved area, and prevents cancellation while the barrier is active. Successful regeneration clears the barrier; generation failure retains the selected coordinates and barrier for safety. WorldRevisionValidator covers entry, escape, cancel rejection, and explicit clearing. All affected units pass strict MinGW warning-as-error syntax compilation. Runtime execution remains unavailable because Git Bash startup fails with NtCreateDirectoryObject 0xC0000022. Barrier visual rendering, network teleport/vehicle hooks, durable restart state, and repair workflow after generation failure remain open.

World Loom generation repair path (2026-10-12): Failed regeneration now retains its selected coordinates, feature plans, and active movement barrier. A trusted host can call World::retry_failed_revision_regeneration or GameSession::retry_failed_regeneration to reset the failed progress record and resubmit the same plan without another opening-hand deal or currency debit. Successful completion clears the barrier. Strict warning-as-error syntax compilation passes for the manager, regenerator, facade, GameSession, movement collision, and WorldRevisionValidator. Runtime recovery remains unverified; visible barrier rendering, durable barrier/plan restoration, and failure repair after process restart remain open.

Barrier status in revision map (2026-10-12): RenderDebug carries and copies the active barrier state, and DebugOverlayRenderer outlines selected cells in bright cyan and labels the map while the barrier is active. The validator checks that this state survives RenderDebug copying and clear resets it. Strict MinGW warning-as-error syntax compilation passes. This is a screen-space map indication only; a world-space glowing boundary mesh and renderer/runtime visual validation remain open.

Barrier map visibility (2026-10-12): GameSession now forces the revision map into the optional overlay while a committed barrier is active, even if the user had hidden the preview. It returns to the user's preview preference once the barrier clears. Strict syntax compilation and diff checks pass; this remains a screen-space cue, not the world-space glowing wall required by the design.

World-space barrier renderer (2026-10-12): The GPU path now draws additive cyan translucent panels around the selected chunk-set perimeter during an active committed barrier, omitting shared internal edges. It uses the normal camera MVP and does not modify camera state. Strict MinGW warning-as-error syntax compilation passes for GpuWorldRenderer.cpp; runtime shader and visual validation remain open, along with software rendering and restart recovery.

World Loom committed-plan snapshot recovery (2026-10-12): The atomic GameSession snapshot now includes committed selected chunks, paid/bonus flags, mode/stage, selection revision and active barrier state. Startup rebuilds the plan after restoring card rules and refreshes occupancy; conflicts fail closed. The WorldRevisionValidator includes a metadata restore scenario, and affected units pass strict MinGW syntax compilation. Runtime remains unverified; deadline and network draft identity, generation publication recovery, and the old standalone .wrv fallback remain incomplete.

Standalone World Revision lifecycle persistence (2026-10-12): .wrv metadata save/load now includes committed selected chunks, paid/bonus flags, revision mode/stage, selection revision and barrier state in the checksummed envelope, with atomic replacement. Legacy metadata remains readable. The restore validator exercises the standalone file path. Strict MinGW compilation passes; runtime validation remains unverified. Deadline/network draft restoration and generation publication recovery are still incomplete.


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

Swept World Loom barrier collision (2026-10-13): PlayerCollision now clips horizontal movement at the selected chunk's actual edge, including large steps that previously collided against an interior block and stopped the player inside the reserved chunk. The WorldRevisionValidator adds checks for exact entry clipping, reverse entry, and entry on the Z axis. The collision and validator sources pass strict warning-as-error syntax compilation. A temporary MinGW runtime harness linked the actual PlayerCollision source to a stub World and passed forward entry, reverse entry, Z-axis entry, and exit from inside. The full World Revision validator remains unverified because the direct application currently fails with Windows heap/stack corruption in world-dependent validation.

Timed furnace enforcement (2026-10-13): Furnace recipe IDs 1029–1035 now reject generic immediate crafting and can only be started through the persisted processing queue. The rejected result is retained for exact request retries; reusing its request ID with a changed batch is rejected. WorldItemStoreValidator checks that the attempt preserves input, fuel and inventory revision. Strict warning-as-error syntax compilation passes, and a focused MinGW runtime harness confirmed rejection, exact retry, changed-payload rejection, and a 200-tick processing job that consumes inputs once. The full world-item validator remains affected by the direct application runtime failure recorded above.

Build-path recheck (2026-10-13): Running mingw32-make -j1 normal via cmd.exe reproduces Git Bash NtCreateDirectoryObject access denied (0xC0000022); invoking WSL for the same workspace is denied by the environment (Wsl/E_ACCESSDENIED). Neither alternate shell reaches compilation. Continue using direct MinGW compilation and focused harnesses for source-level slices, while treating application validators as unverified until the workspace can launch its supported build runner.


Validator sweep and stale expectations (2026-10-13): A direct MinGW link assembled a fresh application from current Minecraft objects, all current CardGame objects, current voxel registry/data, and targeted current Libft Game units. `--validate-item-catalog` passes (71 built-ins, 0 runtime, 104 content policies); `--validate-transaction-journal` passes. The catalog validator now runs its synthetic runtime-block mutation last, after production coverage checks, and its Shipwreck Chart craft fixture grants the rare recipe through discovery first. RegenerationCardValidator now locates imported rare items by physical inventory slot and searches the discard pile for the lowest-ID automatic discard. Its remaining snapshot check detects non-identical RCS bytes after restore/replay; the physical mapping and exact request replay succeed, but persistence round-trip still needs investigation. WorldRevisionValidator fixtures were corrected for variable biome counts and zero-based new-record revisions; the sweep now advances to a separate magma-feature generation failure. `--validate-world-items` still terminates with an access violation. Strict warning-as-error compilation passes for the three edited validator sources. The full Libft suite and application-wide `--validate-all` remain unverified.

RegenerationCard validator stack and snapshot verification (2026-10-13): `RegenerationCardSession` is 502,176 bytes on this build; the physical-import validator held five sessions locally, exceeding the Windows default thread stack. It now allocates those fixtures on the heap with explicit allocation-failure handling. Strict MinGW warning-as-error syntax compilation passes. A focused executable built from current session, CardGame, inventory and validator sources ran the exact physical-import validator successfully, including three physical-card imports, script context, restored mapping, exact retry, and byte-identical RCS snapshot replay. This resolves the reported RCS byte mismatch for that path. The full Libft suite, other application validators and `--validate-all` remain unverified.

Regeneration Card validator recovery and Volcano Mark preflight (2026-10-13): The physical-import validator now heap-allocates its five 502 KB session fixtures, preventing Windows stack overflow; its RCS serialize/restore/retry byte comparison passes. The World Rules validator?s second Surveyor?s Reach fixture now uses a distinct chunk coordinate so it exercises the configured cap, and `RegenerationWorldRules::preflight_card_effect` admits the special Volcano Mark definition (15) through its ID guard. The current RegenerationCardValidator executable passes end to end (`regeneration-cards: ok`, result 0); both changed sources pass strict MinGW warning-as-error compilation. Full Libft tests and application-wide validators remain unverified.

CardGame host-intent snapshot and replay coverage (2026-10-13): The byte snapshot test now restores into an engine with the same registered callback/rules manifest as the source; the previous fixture omitted a callback whose presence is intentionally included in the rules hash. Its direct MinGW runtime harness passes checksum rejection, snapshot restore, pending records, hash equality and no-mutation-on-invalid-snapshot checks. The multi-draw serialized replay test now compares each pending host-intent sequence, schema ID/version, field type and field value after replay deserialization; its direct MinGW runtime harness passes. Both test files pass strict warning-as-error syntax compilation. The full Libft suite remains unverified; direct linking its existing response file fails because its object/archive set has mismatched sections and missing renderer symbols.


### Hostile spawner loot

GameSession retains hostile spawner deaths until loot delivery is checkpointed. MinecraftLootCatalog resolves bounded deterministic loot by table and entity ID; zombies drop rotten flesh, skeletons always drop bones and have a 50% string chance, and creepers drop gunpowder. Drop quantities range from one to two. Ground-item creation, tracked corpse removal, and event acknowledgement are saved in one session snapshot. If the checkpoint fails, the affected item/entity/spawner state rolls back so the death can retry. Mob AI, weighted/progression loot, player attribution, and rare-card structure planners are not implemented yet.


The spawner death validator also injects a snapshot-write failure and checks item rollback, corpse retention, and retry of the pending event before verifying the successful checkpoint and restart restore. The current expanded validator compiles strictly and passes in a fresh direct-linked runtime build. The standard Make runner still cannot launch its bundled Git Bash/MSYS shell in this environment.

Regeneration publication update (2026-10-13): The earlier note above that generation publication still needed to update instability records is superseded. WorldRegenerationResultApplier now retains the previous chunk while the new terrain and its generation/feature-outcome metadata are committed, restoring the previous chunk if that metadata commit fails. This is in-memory rollback and does not provide crash-atomic publication across world/session journals.

Surface Magma reservation update (2026-10-13): Each planned pool is assigned a distinct selected chunk after village reservations; Volcano placement and its preview exclude those chunks. GameSession capacity preflight includes applied plans and pending feature-card intents before accepting the play.

World entity item identity (2026-10-13): WENS v2 stores a physical item instance ID for item-backed entities such as boats; v1 snapshots remain readable. This lets future placement and retrieval transactions preserve the same boat identity through session saves. Those item/entity actions and boat movement are still unimplemented.


Ruined Village Relic is definition 17. Before startup, the host must configure
its positive instability cost with `GameSession::configure_ruined_village_instability_delta`.
GameSession reserves one distinct selected chunk before consuming the card. The
worker builds a damaged, chunk-bounded village and gold cache; the plan carries
hostile spawner definitions and a value-only emerald reward. World borrows
GameSession's entity, spawner, ground-item, and item-catalog stores during its active
lifecycle. The result applier stages the reward invisibly, publishes spawners and
the reward with the replacement chunk, and removes staged items if generation or
the instability-record update fails. The skeleton spawner uses a dedicated
deterministic loot table with low-probability emerald and diamond drops. Loot
payloads are bounded per feature plan and transported as values through worker
results. Validator source covers card mapping/preview, plan transport and size
limits, and generated ruin/cache geometry. Runtime generation publication and
compensation remain unverified while the local Make/Git Bash process fails to
initialize; five rare card definitions and crash-atomic cross-store publication
remain open.

Regeneration host output identity (2026-10-14): Pending generic outputs are exposed only through the host-only GameSession query. Each output is wrapped with a session ID and CardGame queue sequence; consumers acknowledge with both values after durable acceptance. The session ID can be configured before start and is persisted in MCSP v17 beside the card queue. Restore without a host override adopts the saved ID; a mismatching override fails closed. Snapshot versions 1-16 remain readable. External consumer side effects still need their own prepare/commit recovery because they are not atomic with the local session file.
Session IDs are generated deterministically from the world seed when no host override is supplied; network hosts should provide their durable authoritative ID to avoid treating a shared save as a separate session.

World Loom server draft checkpoint (2026-10-14): `WorldRevisionServer` now
exports/imports a checksummed WRS1 record for the active draft owner and binds
it to the exact World revision and selection revision. Hosts must persist and
restore it before server initialization. The World Revision validator source
covers owner roundtrip and checksum rejection; request-result cache durability
and automatic host storage integration remain open. These changes are not
compiled or runtime-validated yet.

World Loom request retry cache: `WorldRevisionServer` also exposes a WRC1
bounded request-cache checkpoint (4,096 entries per cache, 64 MiB maximum),
which retains exact payloads and cached responses across a host restore. The
host must checkpoint it alongside WRS1 before acknowledging the network action;
this API does not itself provide atomic durable storage.

Minecraft item identity policy (2026-10-14): the catalog now requires a physical ID and unit stack for boats, tools/armor, physical CardGame items and filled water/lava buckets. Session integrity checks apply the rule to player inventory and dropped-item entities. ItemCatalogValidator source covers example identities and rejects missing IDs or stacked unique items. Builds and validator runs are intentionally deferred until implementation gaps are closed. Other/future item stores and multiplayer-wide uniqueness still need a common owner registry.

World Loom combined host checkpoint (2026-10-14): WorldRevisionServer::serialize_checkpoint and
estore_checkpoint wrap WRS1 draft ownership and WRC1 replay caches in WRP1, validating the complete bundle before restoring any of it. The matching World state must already be restored, and host initialization must follow checkpoint restore. The host still owns durable storage and cross-file/cross-system atomicity. Validator source checks combined restore and corruption rollback; no build/run has been performed in this implementation pass.

World Loom checkpoint files (2026-10-14): save_checkpoint_file uses Libft atomic replacement and load_checkpoint_file bounds input before transactional restore. This helps host persistence but does not provide crash durability or atomic coordination with the world/session save. The validator source now covers file roundtrip; implementation builds and runtime verification remain deferred.

World Loom checkpoint integration (2026-10-14): MCSP v18 stores the combined server checkpoint next to World Revision metadata. GameSession loads that state and restores it into the server before server initialization; binding also installs a checkpoint callback so completed action, quote, and instability-view request state is persisted through the GameSession snapshot writer. The legacy persistence API may intentionally ignore this optional section. WorldItemStoreValidator source includes v18 roundtrip and v19 rejection. Failure recovery when checkpoint persistence fails after an in-memory World change remains open; no build or runtime validation has run.

## World item replication

Chunk snapshots include published ground items and update the client-owned WorldItemReplicaStore. Visible item-store changes trigger bounded, round-robin chunk snapshot refreshes from the replication server pump. The callback API remains available for custom clients. The wire snapshot preserves item instance identity and modifiers but does not expose pickup-lock owner data. This implementation uses chunk refreshes instead of item-specific delta messages.
