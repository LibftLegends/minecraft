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

Network or multiplayer adapters should submit a `World::RevisionRequest` to
`World::apply_revision_request`; it performs the server-side validation and
returns a `RevisionRequestResult` rather than requiring clients to mutate the
world directly.

In the interactive client, press `M` to show the read-only revision preview in
the HUD. It is centered on the player and uses red/protected, green/selected,
gold/transition, and gray/unchanged cells.

The terrain library supports the same stage masks used by the four modes, so
decoration and underground refreshes operate on the existing chunk while full
regeneration rebuilds it. Selected boundaries adjacent to loaded protected
chunks receive a height blend before their meshes are rebuilt.

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
