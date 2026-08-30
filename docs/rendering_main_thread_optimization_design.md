# Rendering and Main-Thread Optimization Design

Status: design only; no runtime changes implemented

Reviewed branch: `agent/libft-hardening-update`

Reviewed Minecraft commit: `68cb9ef`

Reviewed Libft commit: `182f9a5c`

Date: 2026-08-28

## 1. Purpose

This document verifies the rendering and main-thread performance review and
defines a careful implementation and validation plan. The target is smoother
interactive rendering, especially lower p95/p99 frame times, without changing
world behavior, graphics output, chunk fairness, or thread ownership.

The first implementation pass should optimize confirmed unnecessary work. More
invasive renderer changes, especially geometry batching, must be justified by
measurements from the current GPU path.

## 2. Current frame ownership

The interactive game loop is single-threaded for input, world-result commits,
render submission, and presentation:

```text
poll input
    -> update player
    -> update world / commit worker results
    -> build optional diagnostics
    -> collect visible chunks
    -> submit GPU draws
    -> present
    -> sleep to the 120 Hz deadline
```

Terrain generation and remeshing can execute in the generation pipeline, but
completed results are committed into `World` on the main thread. OpenGL work
also remains on the main thread that owns the graphics context.

The interactive loop targets 120 frames per second in
`Application::run_game_loop()`. This is an 8.33 ms CPU and GPU frame budget,
not a 60 FPS lock. `present()` may impose an additional platform-dependent
swap interval, so actual pacing must be measured rather than inferred from the
sleep deadline alone.

## 3. Verification of the review

### 3.1 Confirmed: render diagnostics are built even when hidden

`GameSession::render()` calls `build_render_debug()` before deciding whether
the debug pointer should be null. Therefore the following work happens every
rendered in-game frame even when the FPS overlay and revision preview are both
disabled:

- Linux opens and parses `/proc/self/status` to obtain resident memory;
- macOS calls `task_info()`;
- GPU memory accounting scans every `GpuChunkMesh` slot;
- biome information is recomputed;
- seed and biome strings are copied or formatted.

This is a stronger and more actionable finding than the original description,
which treated debug cost as limited to visible overlays. The 15-frame throttle
in `VoxelRenderer::render_gpu_overlay()` only throttles rasterization and
texture upload; it does not throttle `build_render_debug()`.

When the revision preview is visible, an additional temporary vector is
created, reserved, populated, and traversed every frame.

Verdict: confirmed, high-confidence main-thread waste, low-risk optimization.

### 3.2 Confirmed: redundant resize and viewport calls

`Application::render_frame()` reads the GPU window dimensions and calls
`VoxelRenderer::resize_gpu()` every frame. `GpuWorldRenderer::resize()` ignores
unchanged dimensions, but `GpuRenderer::resize()` still calls `glViewport()`.
`GpuWorldRenderer::render()` calls `glViewport()` again during the same frame.

Verdict: confirmed duplicate work. The likely gain is small, but the change is
simple and should remove redundant driver calls without affecting rendering.

### 3.3 Confirmed: every frame scans and culls all chunk slots

`GpuGeometryBatch::collect()` iterates from zero to `world.chunk_count`, checks
initialization and mesh state, performs chunk AABB visibility testing, calls
`GpuChunkMesh::sync()`, and reconstructs `_visible_chunk_slots` each frame.

`GpuChunkMesh::sync()` correctly avoids uploads when the mesh revision is
unchanged, so the review must not characterize geometry as being uploaded every
frame. The recurring CPU work is the full slot scan, eight-corner visibility
test, visible-list reconstruction, and subsequent draw submission.

Verdict: confirmed recurring CPU work. Its materiality depends on chunk count
and must be measured before introducing visibility caching.

### 3.4 Confirmed: one or two draw submissions per visible chunk

The solid pass loops over `_visible_chunk_slots`, updates the chunk-offset
uniform, binds the chunk's solid VAO, and calls `glDrawElements()`. The water
pass loops over the same list and attempts a corresponding water draw. Empty
water or solid geometry returns before drawing, but the loop and method call
still occur.

At high render distance this can produce many driver submissions and VAO
bindings. The cost is likely to become more important as lighting, shadows,
SSAO, fog, and additional render passes are added.

Verdict: confirmed architecture and credible scaling risk. Actual CPU/GPU cost
requires draw-count and timing instrumentation.

### 3.5 Confirmed: the apparent mega-batch path is inactive

`GpuGeometryBatch` contains mega-buffer vectors, buffer objects, a geometry
signature, dirty flags, and upload helpers. The active path does not invoke
`geometry_signature()`, `add_chunk()`, `upload_buffers_if_dirty()`, or the
mega-buffer setup functions. `initialize()` currently ignores its argument and
does not initialize those buffers. Rendering instead uses one `GpuChunkMesh`
per chunk.

This code is incomplete/dead infrastructure, not a ready optimization that can
simply be enabled. Its `Vertex` layout also differs from the compact
`chunk_mesh_vertex` format used by the active path. It must either be removed
or deliberately redesigned and tested.

Verdict: confirmed. Do not activate the old path opportunistically.

### 3.6 Confirmed: software rendering traverses every chunk and triangle

The software path scans all chunk slots. Visible chunks traverse mesh indices
triangle by triangle and perform face checks, transformation, projection,
clipping, and rasterization on the main thread. Chunk visibility currently
transforms all eight AABB corners.

This is expected to dominate software rendering. It is separate from the
normal GPU path and should not take priority unless software mode is a supported
performance target.

Correction to the earlier review: `RenderTarget::prepare()` normally points
the render target directly at the destination framebuffer. In that case
`RenderTarget::blit_to()` detects identical pixel pointers and returns without
copying. A mandatory full-frame copy is therefore not present in the usual
software path.

Verdict: traversal cost confirmed; unconditional final copy disproved.

### 3.7 Confirmed: result commits can consume or exceed the frame budget

`World::drain_generation_results()` checks a two-result and two-millisecond
limit before polling each result. A result commit may replace mesh storage,
update chunk state, register a chunk, queue neighbor remeshes, and append
deferred edits.

The time check is not a hard upper bound:

- one commit starts before the deadline and may finish after it;
- `apply_deferred_edits()` runs after the timed loop;
- edit sorting, voxel writes, snapshots, and remesh submission are not covered
  by the two-millisecond condition.

Verdict: confirmed frame-spike risk. Do not assume this path is bounded to 2 ms.

### 3.8 Confirmed with qualification: deferred-edit sorting and allocation

`apply_deferred_edits()` sorts the edit vector and creates a local `pending`
vector every call. An empty vector does not allocate storage, and sorting an
empty range is trivial, so this is not an important idle-frame cost by itself.
When edits are present, `pending.push_back()` can allocate, and repeated
`queue_chunk_remesh()` calls can capture snapshots during main-thread commit
work.

Verdict: burst/frame-spike concern, not a proven steady-state bottleneck.

### 3.9 Confirmed: menu canvases are regenerated and uploaded every frame

GPU menu rendering clears the canvas, rerenders the scene, converts every
pixel to RGBA, and uploads the full texture each frame. The same happens for
the in-game settings overlay while it is open.

Verdict: confirmed. It does not affect ordinary gameplay frames, but dirty
tracking can reduce menu and paused-overlay CPU/upload work.

### 3.10 Confirmed: RW locks are not the render hot path

The new RW-lock integration protects voxel reads and bulk snapshot capture for
generation/remeshing. The renderer consumes committed `WorldChunk::mesh` data
owned by the main-thread world state and does not repeatedly lock voxel storage
while drawing.

Adding RW locks around rendering would add synchronization overhead and would
not address the confirmed render costs. Keep OpenGL context ownership and mesh
commit ownership on the main thread.

## 4. Optimization goals and non-goals

### Goals

- remove hidden diagnostics work when the overlay is disabled;
- reduce driver calls and CPU submission work without changing pixels;
- reduce p95/p99 frame spikes caused by result commits and edit bursts;
- preserve incremental mesh uploads based on `mesh_revision`;
- establish measurements that distinguish update, commit, culling, upload,
  draw submission, GPU execution, presentation, and sleep;
- keep the design suitable for future lighting and shadow passes.

### Non-goals

- changing RW-lock scheduling or voxel semantics;
- moving OpenGL calls to arbitrary worker threads;
- changing chunk generation order or world determinism;
- weakening frustum correctness to gain misleading benchmark results;
- optimizing only average FPS while allowing worse tail latency;
- enabling the incomplete mega-buffer code without redesign.

## 5. Proposed implementation phases

### Phase 0: establish trustworthy measurements

Add low-overhead timing counters around these main-thread phases:

1. event and input processing;
2. player update and collision queries;
3. `World::update_around()`;
4. result polling and individual result commits;
5. deferred-edit application;
6. debug-data collection;
7. visibility collection and mesh synchronization;
8. solid submission;
9. water submission;
10. overlay/menu rendering;
11. `present()`;
12. explicit frame sleep.

Record count, total time, maximum time, and histogram/percentile-compatible
samples. Also record per frame:

- loaded and visible chunks;
- solid and water draw calls;
- triangles submitted;
- mesh uploads and bytes uploaded;
- generation results committed;
- deferred edits processed;
- time spent over the intended commit budget.

CPU timers do not measure asynchronous GPU completion. Add optional GPU timer
queries around world, water, and overlay passes where supported. Never call
`glFinish()` in normal benchmarking because it changes pipeline behavior.

### Phase 1: remove unconditional diagnostics work

Change `GameSession::render()` so it determines whether diagnostics are needed
before building them.

Required behavior:

```text
overlay disabled and revision preview hidden
    -> pass null debug pointer
    -> perform no RAM query, biome query, string formatting, or GPU-byte scan

overlay enabled
    -> build cheap per-frame fields
    -> refresh expensive system fields on a slower cadence

revision preview visible
    -> refresh preview only when its inputs change or at a bounded cadence
```

Recommended split:

- `build_render_debug_frame_fields()` for FPS, camera, selected block, and
  loaded/render-distance counters;
- `refresh_render_debug_system_fields()` at approximately 2-4 Hz for RAM and
  approximate VRAM;
- `refresh_revision_preview()` when center chunk, revision state, or preview
  visibility changes.

Cache the revision preview vector as a `GameSession` member to reuse capacity.
Do not cache raw pointers into world/chunk storage.

### Phase 2: eliminate redundant resize and viewport work

Make the outer GPU resize path return immediately when width and height are
unchanged. Keep one authoritative `glViewport()` update location:

- either set it only when dimensions change if no render target changes the
  viewport; or
- set it at render start and remove the additional resize-time call.

The second option is safer if future shadow maps or offscreen passes change the
viewport. The invariant should be: the world pass explicitly establishes its
required viewport once, not twice.

### Phase 3: improve visibility collection carefully

First replace the eight-corner camera-space test with precomputed world-space
frustum planes or another conservative AABB/frustum test. Construct the frustum
once per frame, then test each chunk AABB against its planes.

Maintain conservative behavior near the camera and far plane. A visible chunk
may be accepted unnecessarily, but a genuinely visible chunk must never be
rejected.

Only add cross-frame visible-list caching if Phase 0 shows collection is
material. Cache invalidation must include:

- camera position, yaw, and pitch;
- viewport/FOV changes;
- active render distance;
- chunk initialization and eviction;
- mesh transitions from empty to non-empty;
- world-center/index rebuilds.

A movement threshold is only safe with an expanded guard frustum. Without that
guard region, reusing visibility after camera movement can create popping.

### Phase 4: reduce draw submissions

If driver submission remains material, introduce deliberate batching rather
than reviving the incomplete mega-buffer path unchanged.

Preferred design:

- retain CPU chunk meshes and `mesh_revision` as the source of truth;
- allocate chunk geometry ranges in one or a small number of large GPU buffers;
- update only ranges belonging to changed chunks;
- retain separate solid and water command/range metadata;
- issue grouped draws using the best portable baseline supported by the
  project, with an optional multi-draw path when available;
- encode world position in vertices or per-draw data so one uniform update is
  not required for every chunk;
- compact or recycle ranges without rebuilding all geometry every frame.

Do not merge transparent water into the opaque pass. The current water pass
disables depth writes and enables alpha blending. Future correctness may
require back-to-front chunk or surface ordering, so batching metadata must keep
water independently sortable.

The implementation must handle chunk slot reuse. A GPU range cannot be treated
as valid solely because the array slot is the same; coordinate and mesh
revision/generation identity must match.

### Phase 5: bound world-result commit spikes

Separate result polling from commit scheduling and attach an estimated work
class to each result:

- generated chunk commit;
- remesh replacement;
- regeneration replacement;
- deferred edit batch;
- neighbor-remesh scheduling.

Use both a result-count limit and a time budget, but treat elapsed time as an
observation rather than a guarantee. Before expensive follow-up work, check
whether it can be deferred safely to the next frame.

Recommended rules:

- commit at least one ready result when progress is needed;
- stop after the first over-budget commit;
- never partially expose a chunk or mesh;
- coalesce dirty/remesh requests by chunk coordinate;
- avoid capturing multiple snapshots for the same chunk in one frame;
- retain queue backpressure so deferred work cannot grow without bound;
- expose queue depth and oldest-result age in diagnostics.

Do not move final `WorldChunk` mesh replacement onto a worker without designing
a safe ownership transfer. The main thread must never draw a mesh while another
thread mutates or destroys its containers.

### Phase 6: optimize deferred edits and menus

For deferred edits:

- return immediately when no edits exist;
- reuse pending storage;
- group edits by chunk;
- apply all edits for a chunk under one write phase;
- increment revisions and queue remesh once per affected chunk;
- preserve deterministic conflict ordering by request and sequence.

For menus:

- mark the canvas dirty on scene entry, selection/input changes, settings
  changes, and animation ticks;
- rerasterize and upload only when dirty;
- continue drawing the cached texture each frame when presentation requires it.

### Phase 7: software renderer, only if required

If software mode has a performance requirement, profile it separately. Likely
work includes conservative frustum planes, face-group rejection, reduced
per-triangle copying, tiled rasterization, and possibly worker-owned tiles.

Parallel software rendering must partition color/depth ownership by tile or
use another race-free design. Multiple threads must not write the same depth
and color pixels without coordination.

## 6. Performance traps

- Do not cache visibility without complete invalidation; stale lists cause
  missing chunks or slot-reuse corruption.
- Do not rebuild a single mega-buffer whenever one chunk changes; that trades
  draw-call cost for large CPU copies and GPU uploads.
- Do not call `glBufferData()` for unchanged geometry.
- Do not use `glFinish()` to obtain convenient timings in production.
- Do not perform occlusion queries per chunk without considering query latency
  and CPU/GPU synchronization.
- Do not combine water with opaque geometry or discard water ordering needs.
- Do not update the debug overlay less frequently while still collecting all
  expensive source data every frame.
- Do not make the generation result budget so strict that visible chunks never
  become ready or worker queues remain permanently full.
- Do not count sleeping or swap-interval blocking as renderer CPU cost.
- Do not compare headless software FPS directly with interactive GPU FPS.
- Do not optimize the software fallback at the expense of the normal GPU path
  unless both are explicit product requirements.

## 7. Validation plan

### 7.1 Correctness checks

For every phase run:

```sh
make -j2 all
make -j2 tests
make test
```

Add focused validation for:

- resizing repeatedly and rendering after zero/minimized dimensions;
- camera rotation at chunk/frustum boundaries with no popping;
- chunk eviction and slot reuse while moving quickly;
- mesh revision upload after block edits and generation results;
- solid-only, water-only, and mixed chunks;
- underwater tint and overlay behavior;
- settings/menu redraw after every input-driven visual change;
- revision preview refresh after selection and regeneration changes;
- deterministic world/edit results before and after commit scheduling changes.

Use framebuffer or screenshot comparisons for stable scenes. Allow no pixel
difference for diagnostic gating, resize cleanup, and equivalent culling.
Batching may produce harmless floating-point differences only if vertex world
position representation changes; define and justify any tolerance before
accepting it.

### 7.2 Performance scenarios

Measure release/optimized builds after a warm-up period. Run each scenario
multiple times and report median plus variation:

1. stationary camera, overlay off;
2. stationary camera, FPS overlay on;
3. revision preview visible;
4. slow movement through already-loaded chunks;
5. boosted movement causing generation, eviction, and uploads;
6. repeated block edits causing neighbor remeshes;
7. water-heavy view;
8. maximum supported render distance;
9. menu and in-game settings overlay;
10. software fallback at fixed resolution.

For interactive GPU runs record CPU frame time and GPU timer results separately.
Report average, median, p95, p99, maximum, one-percent-low FPS, draw counts,
mesh upload bytes, and commit overruns. Averages alone are insufficient.

### 7.3 Acceptance criteria

Phase 1 and Phase 2 are accepted when:

- hidden overlays perform no system-memory query or GPU-byte scan;
- visible diagnostics remain correct at their documented refresh cadence;
- unchanged window dimensions do not enter resize work;
- only one required world-pass viewport setup occurs per frame;
- no framebuffer differences or validator failures occur.

Visibility/batching work is accepted when:

- no visible chunk popping occurs in boundary and rapid-rotation tests;
- mesh updates and slot reuse never display stale geometry;
- draw calls or measured render CPU time decrease materially in high-distance
  scenarios;
- p95 and p99 frame times do not regress;
- GPU memory remains bounded during long movement tests.

Commit-scheduling work is accepted when:

- p99/worst update spikes improve under generation and edit load;
- queue depth and oldest-result age remain bounded;
- loading makes continuous progress;
- deterministic world and edit validators remain unchanged.

## 8. Recommended order

1. Add phase timing and counters.
2. Gate and throttle debug-data collection.
3. Remove duplicate resize/viewport work.
4. Measure again.
5. Improve the conservative frustum test if collection is material.
6. Implement GPU range batching only if draw submission is still material.
7. Bound/coalesce result-commit and edit work.
8. Add menu dirty tracking.
9. Profile and optimize software rendering only if required.

This order produces useful low-risk gains first and prevents a large renderer
rewrite from being justified by assumptions instead of measurements.
