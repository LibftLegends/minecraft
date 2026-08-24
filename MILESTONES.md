# ft_minecraft Milestones

Subject: `ft_minecraft` v2.2 ("Pimp My World"). Target score: **125**
(100 mandatory + full bonus). We already delivered full bonus on `ft_vox`
(see [remaining.md](remaining.md), archived) — same ambition here.

Architecture decision: **server-authoritative generation.** The server
procedurally generates the world and dispatches chunks to clients; clients
are thin renderers + input. This gives one source of truth for persistence
and entity state, and avoids cross-platform determinism bugs that
client-side generation would require.

Team: **bvangene** and **rperez-t**. Split below follows the same shape
that worked on `ft_vox` (bvangene owned gameplay/content, rperez-t owned
platform/infra/testing) — swap freely if it doesn't fit this project.

---

## Ground Rules

1. **Mandatory must be fully done and verified before any bonus work is
   evaluated** (subject rule, not our preference). Both tracks finish all
   mandatory milestones before starting their bonus milestones.
2. **M0 is joint and comes first.** It freezes the shared data contracts
   (persistence format, network protocol, entity struct) so the two tracks
   can build against a fixed interface instead of blocking on each other.
3. Work in separate feature branches per milestone (`world/biomes`,
   `net/server-core`, etc.) and merge at milestone boundaries — the split
   below is designed so the two tracks rarely touch the same files.
4. If time runs short, cut bonus in this order (heaviest/least-value first):
   Nether portal → Villages → Bow & arrow → Crafting → Water simulation →
   Growing plants → Cross-platform packaging (cross-platform is cheapest to
   partially claim: even "builds and runs on Linux + macOS" is worth
   claiming if Windows slips).

---

## M0 — Shared Contracts (joint, do first, ~1-2 days)

Both of you sit on this together — it's small but everything else depends
on getting it right once instead of renegotiating it mid-project.

- [x] Chunk/edit persistence format: what a saved chunk looks like on disk
      (block array + biome + ore markers + dirty-edit list). This format is
      reused verbatim as the network wire format for chunk dispatch.
      `game_voxel_chunk` (Libft/Modules/Game) now carries `_biome_id` and a
      bounded dirty-edit list on top of the existing section/palette block
      array; `serialize()`/`deserialize()` (format version 4) cover all of
      it in one shot, so the same bytes are the save format and the chunk
      wire format.
- [x] Block-edit op format: `{x, y, z, block_type, tick}` — used for save
      file, network broadcast, and undo/redo already in `src/edits/`.
      `game_block_edit_op` (Libft/Modules/Game/game_block_edit_op.hpp) is
      the single struct reused by the chunk dirty-edit list, by
      `ProtocolEditBroadcastMessage`, and by `WorldEditHistory` undo/redo.
- [x] Entity state struct: id, type (player/monster kind), position,
      velocity, orientation, health/action state — used by both the
      monster AI (Track A) and entity sync (Track B).
      `EntityState` (src/entities/EntityState.hpp) + binary
      serialize/deserialize; `EntityKind`/`EntityActionState` enums cover
      player/monster kind and action state.
- [x] Extend `terrain_biome` enum in `Libft/Modules/Voxel/voxel.hpp` with
      placeholder entries for the new biomes so both tracks can compile
      against final names immediately.
      Added `TERRAIN_BIOME_CANYON/SWAMP/SEQUOIA_FOREST/SAVANNA/ISLAND` (5-9)
      as reserved placeholders; biome selection/generation logic is
      untouched until Track A1 wires each one in.
- [x] Network message schema: join/leave, chunk request/response, edit
      broadcast, entity update, chat/ping — just the message shapes, not
      the implementation.
      `src/network/ProtocolMessages.hpp` defines the header framing plus
      one struct + serialize/deserialize pair per message type (join,
      leave, chunk request/response, edit broadcast, entity update, chat,
      ping/pong). No socket loop — that's Track B's B3 milestone.

---

## Track A — World, Gameplay & Entities (bvangene)

Builds on the existing `Libft/Modules/Voxel/voxel_generation.cpp` terrain
pipeline and `src/physics/` / `src/movement/` player mechanics.

### Mandatory

- [ ] **A1 — Biome overhaul.** Expand past the 5 `ft_vox` biomes (Plains,
      Hills, Desert, Snow, Mountains) to ≥5 biomes with genuinely distinct
      geography/elevation/vegetation (e.g. swap in Canyon, Swamp, Sequoia
      Forest, Savanna, Island) and smooth blended transitions at borders —
      no hard biome-edge cliffs. Add meandering rivers.
- [ ] **A2 — Caves & ores.** Natural cave entrances visible from the
      surface (not just underground carve). Ore clusters (gold, diamond,
      etc.) placed as vein/cluster structures, not flat per-block
      probability.
- [ ] **A3 — Vegetation.** Procedural trees with per-instance shape/height/
      width/leaf-density variation per biome (extend the existing
      `terrain_tree_template` system rather than replacing it), plus
      scattered flowers/mushrooms/small plants.
- [ ] **A4 — Player mechanics.** Sprint (2 cubes/s vs 1 cube/s walk),
      fly-mode toggle (×20 speed while flying), swim/dive with slowed
      underwater movement — extends the existing gravity/water code in
      `src/movement/PlayerMovement.cpp`.
- [ ] **A5 — Monsters.** Spawn logic + chase-when-near AI, basic
      walk/attack animation states (simple, Minecraft-grade, not
      full skeletal animation).

### Bonus

- [ ] **A6 — Growing plants.** Seed → maturity tick-based growth.
- [ ] **A7 — Crafting system.** Recipes + inventory state (coordinate the
      HUD/inventory rendering with Track A4/Interface work in B4).
- [ ] **A8 — Bow & arrow.** Projectile physics + damage, hooks into A5
      monster health.
- [ ] **A9 — Villages.** Structure generation (place after biomes in A1
      are stable, since villages need flat-ish biome-aware siting).
- [ ] **A10 — Nether portal.** Portal structure detection + a second
      generated dimension + teleport; coordinate with Track B on a
      distinct sky/fog treatment for the nether (B2).
- [ ] **A11 — Water simulation.** Upgrade the existing placement-based
      water spread (`src/edits/`) to dynamic flow/spreading; coordinate
      rendering with Track B's transparent-water pass (B2).

---

## Track B — Engine: Rendering, Persistence, Netcode, Interface & Audio (rperez-t)

Builds on `src/gpur/` (renderer), `src/chunks/` (chunk store/loader),
and the unused `Libft/Modules/Networking` / `Libft/Modules/Storage` modules.

### Mandatory

- [ ] **B1 — Persistence.** Save/load chunk + edit data to disk using the
      M0 format, wired into `WorldChunkStore`/`WorldChunkLoader` so edits
      survive chunk unload → reload and process restart.
- [ ] **B2 — Rendering upgrades.** Directional lighting, shadows, SSAO,
      far-distance fog, 3D clouds (block-based or shader), render distance
      floor raised 160 → 260, optional sky shader to replace the current
      gradient skybox. New shader files go in `src/gpur/shaders/`.
- [ ] **B3 — Server & netcode.** Server-authoritative generation process
      (reuses Track A's Voxel generation code, run server-side), TCP/UDP
      protocol per M0 schema, ≥4 concurrent players, block-edit and entity
      broadcast, join-in-progress chunk dispatch. Built on
      `Libft/Modules/Networking`.
- [ ] **B4 — Interface & audio.** HUD toggle for FPS/triangle/cube/chunk
      counts + connected-player list (extends `src/debug/`); per-biome
      ambient music with smooth crossfade; walk/attack/swim sounds for
      players and monsters with distance-based volume falloff (wires up
      the currently-unused `Libft/Modules/DUMB` sound backends).

### Bonus

- [ ] **B5 — Cross-platform.** Verified build + run on Windows, Linux, and
      macOS; install/build scripts for third-party libs already follow
      this pattern (`scripts/install_*.sh`) — extend, don't reinvent.
- [ ] **B6 — Final perf pass.** 25+ fps floor confirmed with lighting +
      shadows + SSAO + fog + 4-player sync + monster AI all running
      simultaneously; profile and rebalance CPU/GPU workload split.

---

## Joint — Integration & Defense Prep

Do together once both tracks clear their mandatory milestones, and again
after bonus:

- [ ] Merge both tracks, run a full mandatory-requirements pass (world
      scale ≥5,000,000 cubes XZ, 5+ biomes, persistent edits, 4-player
      session, no crashes, ≥25 fps, 1080p+, full-window framebuffer).
- [ ] Confirm asset payload ≤42MB or add a download script (subject
      submission rule).
- [ ] Rehearse the defense/demo walkthrough together.
