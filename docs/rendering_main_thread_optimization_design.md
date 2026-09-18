# Rendering and Main-Thread Optimization Design

Status: implemented through the currently justified optimization phases;
renderer identity, mesh partition changes, visibility caching, analytics-only
upload/commit diagnostics, bounded persistent mesh-upload scheduling, startup
backpressure handling, and conservative frustum-plane culling are implemented.
GPU draw batching remains measurement-gated because current draw-submission
cost is not material.

Reviewed branch: `agent/libft-hardening-update`

Reviewed Minecraft commit: `68cb9ef`

Reviewed Libft commit: `182f9a5c`

Date: 2026-08-28

## 1. Purpose

### Manual world-generation stall capture

Debug builds support an opt-in manual abort for a run that appears stuck during
world loading. Start the debug executable with `FT_VOX_ABORT_ON_SIGINT=1`, then
send `Ctrl+C` to its console. The handler converts that deliberate interrupt to
`abort()`, allowing the platform debugger/core-dump configuration to capture all
threads, including workers waiting on or executing generation. This is manual
only: there is no automatic loading timeout and the handler is not compiled into
release builds. The dump must be inspected together with the startup stage
markers and periodic stream diagnostics; a dump without those state lines cannot
distinguish expensive generation from a lock or result-commit stall.

This document verifies the rendering and main-thread performance review and
defines a careful implementation and validation plan. The target is smoother
interactive rendering, especially lower p95/p99 frame times, without changing
world behavior, graphics output, chunk fairness, or thread ownership.

The first implementation pass should optimize confirmed unnecessary work. More
invasive renderer changes, especially geometry batching, must be justified by
measurements from the current GPU path.

### 1.1 GPU startup/render audit (2026-09-03)

The interactive analytics executable was launched with `--auto-start` and
allowed to run through the loading transition and first gameplay frames. The
audit reached gameplay, completed the 625-slot visibility scan, listed 13
visible chunks, synchronized four meshes, issued 13 solid draws, completed the
water pass, and continued through cached visibility samples. The normal
executable also remained responsive during the same startup window. This is
the required graphics-context evidence for the startup fix; the headless
world-generation probe alone would not have detected the renderer loop hang.

The hang was caused by the empty/uninitialized-slot branch in
`GpuGeometryBatch::collect()` skipping the scan-index increment. The scan is
now a structured bounded loop, and all skip branches advance through the loop
control while updating the visited count. The first post-fix GPU audit showed
`visible=13`, `uploads=4`, `draws=13`, and completed solid and water passes.

### 1.2 Current analytics evidence (2026-09-01)

The first captured in-world session recorded ordinary frames in approximately
the 1--6 ms range, but one frame contained a roughly 2.67 second
`gpu_batch_collect` interval. A separate `world_update` sample reached roughly
3.82 seconds. The analytics bookkeeping visible in the same capture was only
approximately 0.2--0.6 ms per frame, so the capture does not support blaming
the analytics exporter for the multi-second stalls.

The follow-up capture must be interpreted carefully: frame 64 contains a
`world_update` interval of approximately 6,423 seconds while the recorded
render interval is only approximately 1.3 ms. That is consistent with the
application being paused or inactive between frames, not a valid renderer
lag sample. Analytics now supports `trace_frame_interval`; Minecraft keeps
frame summaries and region aggregates on every frame while exporting detailed
trace events and frame summaries every 120th frame. This reduces trace
formatting, file I/O, and buffer pressure without removing the data needed to
identify frame-level bottlenecks. A one-frame export/trace interval remains
available for focused capture.

The old `minecraft_analytics.jsonl` capture predates frame-export sampling and
also exposed a format defect: records were concatenated without newline
delimiters, so it was not valid JSONL and could not be parsed as a stream. Libft
now appends a newline to every JSON frame and trace record. Old logs must not be
used for quantitative aggregation unless they are split between top-level JSON
objects first.

The producer still writes only to the active in-memory export buffer. Once a
buffer is handed off, the persistent exporter thread consumes that separate
buffer and performs serialization/file I/O. Any remaining frame-time impact
must therefore be measured as producer-side bookkeeping or lock contention;
the next capture should include the duration of `analytics_end_frame()` and
the dropped-event counters. The Minecraft analytics build now emits this
diagnostic only when finalization exceeds 1 ms or an event/frame is dropped.

Detailed stderr diagnostics use an 8 ms threshold, matching the 120 Hz frame
budget, rather than printing for every ordinary 2 ms commit or render pass.
The analytics scopes retain all timings; stderr is reserved for work capable
of consuming most or all of a frame budget.

Per-cull timing and GPU chunk identity validation are sampled once every 120
visibility-collection calls in analytics builds. Timing every cull and
rechecking every uploaded chunk added repeated clock reads and validation work
to the render thread; the enclosing visibility-scan timer remains continuous.

Controlled 10-second headless checks after these changes produced 20.22 FPS
with analytics enabled and 21.62 FPS without analytics in separate trials.
Earlier trials were approximately 20.16 and 20.07 FPS respectively. Because
the generation workload varies between launches, these numbers are not a
conclusive speedup; they do show that the analytics path is no longer causing
the previously suspected catastrophic throughput collapse. Interactive tests
must still compare matched repeated trials. The analytics output from the
latest headless run contained frame summaries without diagnostic stderr spam.

The latest in-world capture provides the first useful GPU-path breakdown. It
contains 24 sampled frame summaries from frames 120 through 2880. The sampled
`world_update` region averaged approximately 1.78 ms and peaked at 4.94 ms;
`world_stream_update` averaged 1.47 ms and peaked at 4.22 ms. The nested
deferred-edit region averaged 1.06 ms and peaked at 3.09 ms. By comparison,
GPU batch collection averaged 0.05 ms, solid submission averaged 0.25 ms,
and water submission averaged 0.009 ms, with respective maxima of 0.49 ms,
0.51 ms, and 0.018 ms. This capture does not justify Phase 4 draw batching
yet. The next performance work should first separate stream drain, deferred
edits, and input/world-update spikes while retaining the GPU draw counts as a
baseline.

A fresh 10-second headless analytics sample must not be used as GPU evidence:
it selected the software renderer and reported 17.24 FPS, 58.015 ms average
frame time, 103.685 ms p95, and 153.087 ms p99. The sampled software pass saw
121 visible chunks and 197,144 visible triangles. This is useful evidence for
the conditional software-renderer phase, but it says nothing about OpenGL
draw-submission cost and therefore does not independently justify Phase 4.
GPU batching still requires an interactive GPU capture with draw counts and
GPU-path timings.

A fresh matched headless check after frame-export sampling produced 30.90 FPS
normal versus 27.87 FPS analytics without movement. A movement-enabled pair
produced 28.47 FPS normal versus 28.84 FPS analytics. The disagreement, along
with the different p99 values, shows that the workload is not deterministic
enough for one pair to identify the culprit. It does establish that reducing
file export alone does not consistently remove the observed tail latency.
The next diagnostic experiment must run the same seed, movement script,
render-distance schedule, and duration while independently toggling producer
instrumentation and the exporter thread.

The analytics executable now accepts `--analytics-no-exporter` for this
controlled experiment. It keeps producer instrumentation and in-memory
statistics enabled, but performs the final export during shutdown instead of
starting Libft's persistent exporter thread. Normal analytics runs retain the
sleeping exporter by default; this flag is diagnostic-only and must not be used
for ordinary interactive captures.

The analytics executable also accepts `--analytics-no-instrumentation`. This
leaves the analytics-capable binary and all compile-time integration present,
but does not initialize Libft's recording session, so the runtime calls return
through the disabled path. Compare normal analytics, no-exporter, and
no-instrumentation runs with the same world seed and movement. If only the
third run recovers performance, producer-side recording is responsible; if it
does not, the regression is elsewhere in the analytics build or in another
compile-gated diagnostic path. This switch is an isolation tool, not a normal
profiling mode.

The analytics file from the sampled run contained two frame summaries (frames
120 and 240) and valid newline-delimited records. Those summaries attributed
approximately 69 ms total to `software_meshes` across the two samples, while
the analytics frame-finalization and queue diagnostics showed no reported
backpressure. This makes software mesh traversal/rasterization the current
measured hotspot, but it does not prove that analytics causes the hotspot.

Three additional movement trials with the exporter disabled averaged 24.15 FPS
(24.26, 24.02, and 24.17). Three trials with the persistent exporter enabled
averaged 23.95 FPS (23.95, 24.00, and 23.91). The approximately 0.8% average
difference is within the run-to-run variation, so the sleeping exporter thread
is not the source of the catastrophic slowdown. One enabled trial reported a
9.329 ms world-commit operation for request 424, which is a more actionable
frame-budget violation than the exporter path. The next work should focus on
commit/generation spikes and software mesh traversal while retaining the
exporter switch as a repeatable isolation tool.

A post-upload-policy headless pair also produced identical framebuffer hashes:
the normal binary measured 18.98 FPS (p95 78.09 ms, p99 91.70 ms), while the
analytics-capable binary with instrumentation disabled measured 14.99 FPS
(p95 150.25 ms, p99 202.58 ms). This is not a controlled conclusion about
analytics overhead because the runs did not share a synchronized workload
schedule and the analytics-disabled binary still uses a separate build
configuration. Retain it as evidence that repeated matched trials are needed
before attributing render-distance or frame-tail changes to the upload policy.

The full Minecraft validator target has since passed after the upload-queue and
inactive-buffer cleanup: camera speed, collision, block editing,
visible-distance, terrain determinism, world scale, caves, terrain
configuration, world revision, and asynchronous world generation all report
success. This verifies the optimization changes did not alter the tested world
or movement invariants, but it does not replace matched GPU performance runs.

The validator executable now has an explicit `--validate-all` mode. It runs the
validators sequentially, reports the named validator and error code for every
failure, and exits nonzero if any validator fails. Individual flags remain
available for focused runs. No-argument execution continues to launch the game;
the validator binary must never be mistaken for the Libft test runner or for a
request to run every check implicitly. The all-validator mode is intended for
CI and repeatable local regression checks, while long world-streaming checks
should still retain their phase-progress diagnostics.
The Minecraft Makefile exposes the same workflow as `make validate-all`, and
the ordinary `make test` target uses it so new validators are not silently
omitted when another check is added.

Analytics-only commit phase diagnostics now split a slow streamed-chunk commit
into payload transfer, spatial-index registration, deferred-edit append, and
neighbor-remesh scheduling. They are emitted only when the measured phase sum
reaches 8 ms, so normal builds and ordinary analytics frames do not pay for
formatted diagnostics. A fix should target the phase that actually dominates;
the commit's overall two-result/two-millisecond admission budget is not enough
to protect against one indivisible chunk transfer exceeding a frame budget.

The first software-render breakdown implementation revealed an instrumentation
failure: the center chunk routinely exceeded 8 ms, so printing that condition
every frame flooded stderr and itself destroyed FPS. The diagnostic now samples
one frame every 120 frames, performs per-chunk timing only on that sampled
frame, and emits at most one summary. Profiling diagnostics must never
synchronously log from a hot loop on every frame.

The software mesh loop now performs the cheap face-orientation test before
transforming vertices. This avoids three camera-space transforms for every
back-facing triangle without changing the existing face-selection rule. A
follow-up analytics sample still measured approximately 119,792 visible
triangles across 69 visible chunks, with the slowest central chunk taking
approximately 10.1 ms. Therefore triangle rasterization remains the dominant
software cost; further changes should target measured raster work rather than
adding more per-triangle diagnostics.

The software depth buffer now stores interpolated inverse depth rather than
forward depth. Since the rasterizer already interpolates inverse depth for
perspective-correct texture coordinates, the per-pixel depth test no longer
performs a reciprocal division. The clear value and comparison direction were
changed together; this remains private to the software renderer and does not
affect the GPU depth path.

The follow-up renderer diagnostic pass also moved GPU cull timing and uploaded
chunk identity/Y-bound validation to one visibility collection every 120
collections. Five repeated three-second headless analytics trials averaged
approximately 26.76 FPS, while five producer-disabled trials averaged
approximately 26.57 FPS; all framebuffer hashes matched. This does not
reproduce the reported interactive slowdown and indicates that these
diagnostics are not its cause in the headless workload. The producer isolation
switch remains necessary for an interactive capture because menu/input and
window-present behavior are not exercised by this benchmark.

The analytics producer no longer rotates and wakes the exporter for every
frame. It retains multiple frame summaries in the active buffer and hands the
buffer off only when its configured capacity is reached or during shutdown.
Region statistics are merged as one batch per completed frame, and the
export-error value is read atomically. This keeps the measured thread away
from per-event mutex churn while retaining bounded memory and explicit drop
accounting.

The producer's ordinary scope path also keeps a validated thread-local pointer
to the active frame state. Scope begin/end operations therefore do not scan the
four-entry fallback state table for every nested scope. World/menu
classification is still read atomically at scope start, because the public API
allows a transition within a frame and each scope must retain the state it
started in. The fallback lookup remains available for worker and standalone
APIs, and the cached pointer is cleared at frame completion and validated
against the session before use. This is bookkeeping-only: it does not change
scope timestamps, nesting, classification, aggregation, or export buffer
ownership.

The frame publisher also previously rebuilt the rolling mean and sorted the
entire 120-frame window for p95 and p99 on every frame while holding the
analytics mutex. That made the sampled exporter policy ineffective: file
serialization was deferred, but percentile calculation still ran at frame
rate. The publisher now maintains the rolling sum incrementally and performs
percentile sorting only when the frame is selected for export. This preserves
the exact sampled percentiles while removing avoidable O(window-squared) work
from ordinary frames. Any future aggregate must follow the same rule: use an
O(1) producer update and defer sorting, formatting, and reporting to the
exporter or sampled path.

The active renderer performs a synchronous `GpuChunkMesh::sync()` and OpenGL
buffer upload while collecting visible chunks. A newly completed world result
can also perform main-thread chunk insertion, full snapshot capture for a
neighbor remesh, and deferred-edit work. These are now separately instrumented
in analytics builds. The next measurement must record the affected chunk and
mesh byte/index counts before choosing between upload chunking, a stricter
commit budget, or both. A fix is not considered validated until the p95/p99
frame data and the worst upload/commit samples are captured independently.

The collector now keeps a persistent upload cursor and admits at most two
pending visible mesh uploads per frame, with a four-megabyte aggregate transfer
budget. The first pending upload is always admitted so a large individual mesh
cannot starve indefinitely, while later uploads are deferred when either limit
is reached. This is deliberately a small fixed policy rather than a new thread
or a GPU synchronization point: it reduces the visible-world fill-in delay
without allowing a burst of generated meshes to consume an unbounded frame.
The upload count and byte total remain available to analytics diagnostics so
the policy can be tuned from matched captures. A slot-reuse identity mismatch
still invalidates the entry and cannot be bypassed by the budget.

The scheduler is shared by cache-hit and cache-miss paths. This matters while
streaming: generation and remeshing can invalidate the visibility cache on
consecutive frames, so fairness implemented only in the cache-hit path would
repeatedly upload the first entries in the spatial index and starve distant
visible chunks. The scheduler walks the complete visible list from a
persistent cursor before advancing it, so repeated full scans cannot reset
upload priority. If the cursor's chunk was evicted during recentering, the
next traversal starts at the visible chunk nearest the camera rather than at
an arbitrary storage/index entry. This makes newly streamed terrain appear
around the player first while the remainder of the visible list continues to
drain fairly.

The scheduler additionally selects the nearest pending visible chunk as the
start of each traversal. This handles the case where the cursor remains valid
but older remesh work keeps arriving ahead of a newly streamed chunk. The
nearest-first choice is only a priority hint; the complete circular traversal
and bounded upload policy remain in force, so the system does not create a
thread per upload or discard distant work.

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

### 3.1 Resolved: render diagnostics are built only when enabled

`GameSession::render()` now checks whether an overlay or revision preview is
enabled before calling `build_render_debug()`. Therefore the following work is
skipped on ordinary in-game frames when diagnostics are disabled:

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

Verdict: resolved. Keep the early check in place and retain a regression test
or profile comparison for overlay-disabled gameplay.

### 3.2 Resolved/qualified: resize and viewport calls

`ApplicationPhaseController::render_gpu_frame()` reads the GPU window
dimensions and calls `VoxelRenderer::resize_gpu()` every frame. The underlying
`GpuRenderer::resize()` returns immediately when dimensions are unchanged, so
that path does not repeatedly invoke resize work. `GpuWorldRenderer::render()`
sets `glViewport()` as part of establishing the world render state; it is not a
second resize operation and should remain unless viewport state is managed by a
strict frame-level render-state cache.

Verdict: the previously suspected duplicate resize work is not present in the
current implementation. A render-state cache may be considered later, but it
is not a current performance fix.

### 3.3 Confirmed: every frame scans and culls all chunk slots

`GpuGeometryBatch::collect()` discovers drawable chunks from the bounded world
chunk storage, checks initialization and mesh state, performs chunk AABB
visibility testing against six precomputed frustum planes, calls
`GpuChunkMesh::sync()`, and reconstructs `_visible_chunk_slots` when the
visibility cache is invalidated.

`GpuChunkMesh::sync()` correctly avoids uploads when the mesh revision is
unchanged, so the review must not characterize geometry as being uploaded every
frame. The recurring CPU work is the bounded drawable-slot scan, six-plane AABB
test, visible-list reconstruction, and subsequent draw submission.

Analytics builds additionally time the individual mesh upload nested inside
this scope. A slow-upload diagnostic reports the chunk coordinate, byte count,
vertex count, solid-index count, water-index count, and duration. World-result
commit and deferred-edit application are separate analytics scopes as well.
The collector also reports its scanned-slot count, visible-slot count, upload
count, and total duration when it exceeds two milliseconds. This distinguishes
a blocking OpenGL upload from an expensive main-thread world commit or an
unexpected collection-loop size. All of these diagnostics are compiled out of
normal builds.

On the same sampled collection cadence, the solid and water flushes now report
their effective draw counts and pass duration in analytics builds. This is the
measurement gate for Phase 4: range or multi-draw batching should only be
implemented if these counts and timings are material on the supported GPU path.

The analytics build separates cached-list synchronization and full visibility
scanning regions. Geometry change detection is now an O(1) revision read rather
than a second storage walk; this measurement is not present in normal builds.

The hot `WorldChunk` metadata is stored before the large voxel and mesh
payloads within each object. This reduces the offset to metadata when a
known chunk is visited, but does not make an array walk cache-friendly because
the object stride is still large. The fixed storage bound keeps the renderer’s
authoritative fallback bounded; the spatial index remains the important
optimization for world queries. The payload layout and rendering semantics are
unchanged.

The world spatial index remains the authoritative query acceleration structure,
but the GPU visibility collector does not rely on it as the sole drawable
enumeration. A streamed slot can become initialized between index-maintenance
steps, and making rendering depend on a later block edit to repair that index
causes visible pop-in. The collector therefore scans the bounded chunk-storage
array for drawable meshes and converts each storage slot directly to its stable
GPU slot. The analytics sample reports index validity separately from the
storage scan; the fixed 625-slot bound makes this correctness fallback explicit
until a publication-safe drawable list is implemented.

Visibility invalidation now uses a monotonic world geometry revision instead of
recomputing a hash over the loaded index every frame. World insertion, eviction,
remeshing, regeneration, and block edits advance that revision; unchanged frames
therefore perform an O(1) cache-key read while retaining explicit invalidation
for geometry changes. The old hash-based cache path has been removed; diagnostics
and validation use the monotonic geometry revision directly.

The latest capture still showed multi-second `gpu_batch_visibility_scan`
intervals even though upload and signature regions were sub-millisecond. This
change reduces index-publication correctness risk; the next analytics capture
must distinguish actual culling cost from an externally descheduled process.
The diagnostics must report index validity, storage slots visited, visible
chunks, and the slowest culling operation for any scan exceeding the threshold.

Sampled GPU identity and vertical-bound checks are only evaluated after a
mesh is current for the world revision. A mesh deferred by the upload budget
is reported through the pending-upload counters instead of being misreported
as stale geometry. This keeps diagnostics actionable without changing the
upload policy.

The subsequent capture moved the dominant spike to `world_update`: one frame
took approximately 3.53 seconds while rendering itself remained about 1 ms.
The suspected recenter path (evicting out-of-range chunks and rebuilding the
spatial index) previously sat outside the stream sub-regions, so it could not
be distinguished from the rest of the update. An analytics-only
`world_stream_recenter` region now measures that path and reports loaded
counts before and after it. This must be used to determine whether chunk
destruction is the real stall before changing eviction semantics.

The next capture recorded a 3.97-second `world_update` interval, but its
exclusive time was entirely in the outer update scope: no `game_update`,
`player_motion`, stream, commit, or deferred-edit child region was recorded.
This places the stall before `GameSession::tick_world()`, where input-device
polling was previously uninstrumented. An analytics-only `input_device_poll`
region now isolates that call. Do not change world-generation or eviction
behavior based on the earlier attribution until this region is measured.

Verdict: substantially optimized. The per-slot mutex-taking empty check has
been removed, the loaded spatial index avoids scanning unused storage slots,
and the AABB test uses precomputed frustum planes. A persistent visible-list
cache skips the cull scan and list rebuild when camera pose, viewport, render
distance, and the geometry revision are unchanged. A cache miss still performs
the full scan, and runtime captures must continue to measure its candidate
count, duration, and tail latency. Upload selection is independent of cache
validity and remains fair during streaming invalidations.

The cached path previously uploaded only one pending visible mesh per frame.
That protected frame time but could make newly generated terrain appear to have
a shorter render distance while the upload backlog drained. It now uses the
same bounded count/byte policy as the cache-miss path. The current policy is a
maximum of four meshes and 8 MiB per frame. This improves catch-up without
removing the frame-time guard; it must be validated with rapid movement and
large-mesh scenes for both pop-in distance and p95/p99 frame time.

The cache also retains every visible non-empty world mesh whose upload is still
pending. Previously, a deferred mesh could be omitted from the cached visible
list during the initial scan and then never revisited while the camera and
geometry signature remained unchanged. Retaining the pending entry makes the
upload budget a true queue instead of a one-time visibility decision; draw
submission still skips entries that have no completed GPU geometry.

Every 120th full collection now emits an analytics-only sample containing the
active distance, chunk radius, index validity, storage slots visited, visible
and listed counts, pending upload count, uploaded count, uploaded bytes, and
total collection time. These fields distinguish a genuinely short loaded world from
GPU upload backlog or an overly aggressive visibility result. The sample is
not emitted by normal builds and is not printed for ordinary collections.

The empty-mesh check in this loop must not call `ft_vector::empty()` for every
slot: that accessor takes the vector mutex. The mesh’s
`has_occupied_bounds` flag is maintained with the mesh lifecycle and is a
lock-free equivalent for this renderer-level check. The active GPU collector
and `MeshCuller` now use that flag, avoiding one synchronization operation per
chunk per frame while preserving the empty-mesh behavior.

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
now has analytics-only draw-count and slow-pass diagnostics. A future capture
must compare the solid/water draw counts and pass durations before introducing
range batching.

### 3.5 Confirmed: the apparent mega-batch path is inactive

The former `GpuGeometryBatch` implementation contained mega-buffer vectors,
buffer objects, a geometry signature, dirty flags, and upload helpers. The
active path never invoked those helpers; rendering uses one `GpuChunkMesh` per
chunk. That unused implementation has now been removed.

This code is incomplete/dead infrastructure, not a ready optimization that can
simply be enabled. Its `Vertex` layout also differs from the compact
`chunk_mesh_vertex` format used by the active path. It must either be removed
or deliberately redesigned and tested.

Verdict: resolved by removal. The unused mega-buffer state, vertex type,
geometry-signature helper, shared-water-buffer setup/upload code, and stale
compatibility aliases were removed. The active per-chunk GPU mesh path remains
the only implementation; future batching must be designed as a new path with
its own range ownership, slot-reuse identity, water ordering, and validation.

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

The deferred-edit phase now has its own one-millisecond observation budget and
retains the existing maximum of 64 edits per call. It always attempts the
first pending edit so a busy queue continues making progress, then stops at the
time or count limit. The budget is deliberately separate from the result-drain
budget; it prevents a large deferred-edit burst from silently consuming the
rest of the frame while preserving deterministic ordering of the edits that
are applied.

Verdict: result commits are bounded by the existing two-result/two-millisecond
policy, and deferred edits are now separately bounded, but a single edit can
still exceed its budget. Continue recording count, queue depth, and worst
individual operation time before tightening the policy further.

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

Public `World` voxel-query wrappers now acquire a shared lock, while internal
`WorldBlockQuery` helpers remain explicitly unlocked for callers that already
own the world lock, including the raycaster worker pool.

Adding RW locks around rendering would add synchronization overhead and would
not address the confirmed render costs. Keep OpenGL context ownership and mesh
commit ownership on the main thread.

The persistent raycast worker pool now takes one shared world read lock at the
public raycast boundary while its range jobs execute. Worker lookups continue
to use the internal unlocked query path, so the pool does not recursively lock
once per voxel. World streaming or edits therefore cannot mutate chunk storage
while a multi-range raycast is reading it.

World mutation entry points now take the matching exclusive lock while
updating chunk storage, revisions, edit history, or streaming state. The lock
is not taken by internal query helpers, so a public raycast acquires one read
lock and its persistent worker ranges remain lock-free at the voxel-lookup
level.

### 3.11 Resolved: GPU mesh identity when chunk slots are reused

The active renderer stores one `GpuChunkMesh` per fixed world-array slot and
currently decides whether to upload with the stored mesh identity. A newly
streamed chunk can reuse a slot whose previous chunk was evicted. The stream
commit path assigns the current world geometry revision to the replacement, so
the renderer cannot retain the previous chunk's VAO/EBO contents when the
coordinates and voxel revision happen to be equal.

The renderer then updates the slot's world X/Z offset to the replacement
coordinate. This can display valid geometry at the wrong location and can look
like a height error while collision still reads the correct live chunk.

The implemented fix is:

- make the upload key include chunk identity, such as
  `(chunk_x, chunk_z, voxel_revision, mesh_revision)`;
- store the uploaded identity in `GpuChunkMesh`;
- force an upload when coordinate or world-generation identity changes, even
  when the local mesh revision is numerically equal;
- invalidate the GPU entry on eviction and empty-mesh transitions;
- publish/update the world offset only with matching geometry.

The slot-reuse regression now evicts and regenerates the same coordinates and
verifies that the replacement has a new mesh identity and valid geometry before
any block edit occurs. Add a
cross-layer check comparing source mesh Y, collision surface Y, and rendered
vertex Y for the same world column. Do not add a global Y offset: chunks use a
documented local Y range starting at zero.

The Libft-side portion of this invariant is now covered by
`test_chunk_mesh_height_matches_generated_chunk_height`, which compares the
highest generated solid local Y with the mesh occupied maximum Y on a
deterministic flat chunk. The remaining graphics-context check must continue
to compare the uploaded chunk identity and rendered vertex Y at runtime in the
analytics/diagnostics build only.

The analytics renderer now performs that runtime check without adding work to
normal builds: it reports only chunk X/Z offset mismatches, uploaded identity
mismatches, or uploaded-versus-source Y-bound mismatches. The upload identity
contains chunk coordinates, voxel revision, and mesh revision. Normal builds
do not compile these diagnostics or their reporting code.

GPU memory accounting also now distinguishes invalidation from destruction:
evicting a chunk invalidates its uploaded identity while retaining the byte
count for still-allocated GL buffers; destruction clears that count after the
buffers are released.

The existing `--validate-visible-distance` validator now also checks the live
loaded chunks' world origins, highest solid local Y, mesh occupied maximum Y,
and representative collision surface. A successful run reported
`visible-distance: ok` with 377 loaded chunks; any mismatch prints the slot,
chunk identity, and measured values before failing.

### 3.12 Confirmed: generated, collision, and rendered height need one invariant

Generation, `WorldBlockQuery`, player grounding, collision, mesh vertices, and
GPU offsets must use the same vertical convention. Add diagnostics that report
the chunk coordinate, local/world column, top solid block Y, mesh occupied Y
range, player feet/eye Y, collision surface Y, mesh/voxel revisions, and GPU
uploaded identity. Diagnostics must distinguish stale geometry, wrong X/Z
offsets, and actual generation errors and must remain diagnostics-build only.

The collision validator now covers all tied-boundary combinations: X/Y, X/Z,
Y/Z, and the three-axis X/Y/Z corner. In each case an edge-touching voxel is
not reported as traversed, while the voxel entered after the tied boundary is
still detected. The no-hit assertion uses the raycast API's existing
`FT_ERR_INVALID_ARGUMENT` no-hit result and initializes output coordinates
before the call so a failure cannot be mistaken for a hit. The fixtures place
the test blocks above the generated terrain and choose the origin so the
edge-touching block is never the initial cell.

Fluid generation now performs a post-terrain support check before filling a
candidate pond column. The heightfield is only a candidate: caves, terrain
layers, and clamping can change the actual surface afterward. Water and
aquatic decorations are therefore skipped when the actual column-height block
is not solid, preventing floating water over an opening and decorations in an
unsupported column.

## 4. Optimization goals and non-goals

### Goals

- remove hidden diagnostics work when the overlay is disabled;
- reduce driver calls and CPU submission work without changing pixels;
- reduce p95/p99 frame spikes caused by result commits and edit bursts;
- preserve incremental mesh uploads based on `mesh_revision`;
- preserve chunk identity across slot reuse so geometry cannot be associated
  with the wrong world coordinate;
- prove that generated, collision, and rendered Y coordinates share one
  documented convention;
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

The active implementation now uses precomputed world-space frustum planes and
tests each chunk AABB against them. Keep the six-plane construction in
`RenderCache` covered by orientation, edge, and far-distance tests.

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

The generation pipeline now timestamps results when workers publish them and
reports the age of the oldest completed result still waiting for main-thread
commit. The async validator prints this value at its progress checkpoints.
This is distinct from candidate age: a high result age with low candidate age
identifies a commit/drain bottleneck, while a high candidate age with no
completed results points toward worker throughput or submission pressure.

Do not move final `WorldChunk` mesh replacement onto a worker without designing
a safe ownership transfer. The main thread must never draw a mesh while another
thread mutates or destroys its containers.

### Phase 6: optimize deferred edits and menus

For deferred edits:

- return immediately when no edits exist;
- reuse pending storage rather than allocating a temporary vector on every
  frame;
- group edits by chunk;
- apply all edits for a chunk under one write phase;
- increment revisions and queue remesh once per affected chunk;
- preserve deterministic conflict ordering by request and sequence.

The deferred applier now retains a touched-chunk list, applies the edits in its
existing deterministic order, and schedules at most one remesh request for
each affected loaded chunk after the batch. This avoids repeated snapshot
capture and remesh submission when a generated result contains several edits
for the same chunk. The list is cleared and reused between calls; it is not
part of the serialized world state.

Streamed and regenerated chunk commits now mark the affected chunk and its
loaded neighbors dirty without capturing their snapshots during the commit.
The existing bounded dirty-remesh submitter performs those captures and queues
the remesh work afterward, preserving the one-remesh-in-flight and queue-depth
limits. Player edits retain the immediate border-remesh path because their
interactive result must remain synchronous. This keeps the expensive snapshot
copy out of the generation-result commit while preserving ownership and
deterministic remesh ordering.

The dirty-remesh submitter now propagates snapshot and pipeline errors to the
stream update instead of discarding them. Queue saturation remains an expected
backpressure result, but allocation, invalid-state, and other failures are
reported so a chunk cannot silently remain dirty forever.

The queue also retains a processing cursor. A partial budgeted pass no longer
copies the entire unprocessed suffix into scratch storage. Newly appended
edits are sorted as an incremental suffix and merged with the already ordered
remainder; processed prefixes are compacted only after they become large
enough to justify the copy. This keeps backlog handling bounded without
changing the deterministic comparator or the serialized edit data.

For menus:

- mark the canvas dirty on scene entry, selection/input changes, settings
  changes, and animation ticks;
- rerasterize and upload only when dirty;
- continue drawing the cached texture each frame when presentation requires it.

The first-launch settings menu now follows this policy as well. It compares
the selected row and the three settings values after input handling, redraws
and uploads only when one changed (or on the initial frame), and presents the
cached overlay texture on unchanged frames. This removes repeated full-canvas
rasterization and texture upload while keeping input and window presentation
responsive.

### Phase 7: software renderer, only if required

If software mode has a performance requirement, profile it separately. Likely
work includes conservative frustum planes, face-group rejection, reduced
per-triangle copying, tiled rasterization, and possibly worker-owned tiles.

Parallel software rendering must partition color/depth ownership by tile or
use another race-free design. Multiple threads must not write the same depth
and color pixels without coordination.

The first software-renderer optimization is now implemented without changing
pixel ownership: the renderer caches the visible chunk-slot list while camera
pose, render target, render distance, loaded count, stream center, and world
geometry revision remain unchanged. Cached slots use a visible-only mesh path,
so chunk frustum culling is not repeated once by the outer renderer and again
by `ChunkMeshRenderer`. Any tracked input change rebuilds the list, preserving
the conservative culling and slot-reuse invalidation rules. This is a
single-threaded optimization; tile parallelism remains a separate change that
must first define race-free depth/color ownership.

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
- Do not use a slot-local mesh revision as a complete identity; slots are
  reusable and revisions may restart at the same value.
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
- pond columns with caves, clamped low terrain, and nearby vegetation;
- water support invariants: every generated fluid column has a solid block
  immediately below it, unless an explicitly configured fluid source rule says
  otherwise;
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
- newly generated visible meshes catch up within the bounded upload policy
  without unbounded frame spikes or persistent missing terrain;
- p95 and p99 frame times do not regress;
- GPU memory remains bounded during long movement tests.

Commit-scheduling work is accepted when:

- p99/worst update spikes improve under generation and edit load;
- queue depth and oldest-result age remain bounded;
- loading makes continuous progress;
- deterministic world and edit validators remain unchanged.

Loading readiness is intentionally a small playable-area gate rather than a
barrier on the complete render-distance envelope. The center chunk and the
four directly adjacent chunks must be committed before gameplay starts;
remaining chunks continue through the persistent generation pipeline while
the player is already in the world. Readiness must count those exact
coordinates, not only `loaded_chunk_count`, because the latter can include
chunks retained from a previous stream center. A generation failure in the
playable area must remain observable as a loading error; distant failures are
retried and reported without blocking entry into the world.

Startup seeding follows the same rule: `World::initialize()` seeds only the
minimum playable radius. The configured render distance is activated when the
loading gate transitions to gameplay. Seeding the complete configured radius
before the gate was a startup backpressure bug: it filled the persistent
worker queue with distant chunks and delayed the chunks required to enter the
world. The normal and analytics GUI paths now reach the gameplay transition
without that full-distance startup burst; the headless world-generation probe
and the full validator suite remain regression checks for the same handoff.

The first post-loading GPU render audit found a separate hard hang in
`GpuGeometryBatch::collect()`: the storage-scan branch for an uninitialized or
empty chunk invalidated its GPU slot and continued without advancing
`scan_index`. Since unused storage follows the initial loaded chunks, the first
gameplay render revisited that slot forever. The scan is now a structured
bounded `for` loop, so loop progress cannot be omitted by a skip branch; every
skipped slot also updates the visited counter. A diagnostics-build startup
audit verifies that the scan reaches all 625 bounded slots, reports the visible
list, completes mesh synchronization, and completes both solid and water
passes. This is a correctness fix, not merely a timeout or a relaxed loading
gate.

The loading update uses the selected `RenderDistanceStrategy` to choose its
per-frame generation budget. It must not hard-code a larger loading budget than
the interactive policy: adaptive strategies reduce submissions when measured
frame time is high, while fixed strategies retain their deliberately
conservative budget. This keeps loading responsive on slower systems without
changing the persistent worker ownership model.

Each worker result now carries separate monotonic-clock durations for voxel
generation and mesh construction. These values stay attached to the result
through queueing and main-thread commit, so analytics can distinguish a slow
worker from a slow result transfer or commit. Analytics builds report the
request ID, chunk coordinate, and both durations when their combined time is
at least 8 ms; normal builds retain the fields for ABI-consistent result
ownership but emit no timing diagnostics. A queue-age sample and a worker
duration sample must be interpreted together: high queue age with low worker
time indicates commit/backpressure, while high worker time with an empty
completed queue identifies generation or meshing throughput as the bottleneck.

Verification on 2026-09-03 passed the rebuilt asynchronous-generation and
visible-distance validators after this loading change. The asynchronous test
requires the complete playable startup area and validates that every required
chunk is occupied, drawable, and has valid partition indices, including a
genuinely worker-generated neighbor; the visible-distance test accepted the full 160-block
envelope with 400 loaded chunks. Terrain determinism, cave generation, and
terrain-configuration checks also passed. These results validate the loading
handoff and geometry invariants, but do not yet establish a throughput target
for filling the entire distant render envelope.

The normal `ft_vox.exe` target also remained up-to-date after these validator
changes, and `git diff --check` reported no whitespace errors. This confirms
that the stronger readiness coverage and its diagnostics remain isolated from
the release runtime path.

The follow-up regression pass also independently completed the terrain
determinism, cave-generation, terrain-configuration, and strengthened
playable-area async checks. These are separate invocations, so a passing
async-generation result is not being used as evidence for the deterministic
terrain or cave invariants.

A matched-duration headless sample on the same date measured 21.88 FPS for the
normal executable and 19.28 FPS for the analytics executable with its exporter
disabled. The framebuffer hashes matched; p95 frame times were 76.40 ms and
75.20 ms, and p99 frame times were 86.20 ms and 82.55 ms respectively. This
single trial is evidence that the analytics-capable path still needs repeated
matched trials, not proof that analytics or rendering is the cause of the FPS
difference. Draw batching remains gated until repeated GPU-path measurements
show material submission cost and no tail-latency regression.

The required three-trial, ten-second follow-up was also completed with the same
headless workload. Normal trials measured 22.45, 22.42, and 22.19 FPS, for an
average of 22.35 FPS; analytics-without-exporter trials measured 15.44, 18.71,
and 18.41 FPS, for an average of 17.52 FPS. The corresponding average frame
times were 44.73 ms and 57.50 ms, average p95 times were 72.92 ms and 86.35
ms, and average p99 times were 81.36 ms and 108.20 ms. All six framebuffer
hashes were `f530308712e4b5c2`, so the workloads produced identical output.
This confirms a reproducible analytics-build overhead in this headless path;
it does not identify which instrumentation region causes it, and therefore the
next profiling pass must measure producer-side bookkeeping and lock contention
before any renderer rewrite.

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

## 9. Analytics-versus-renderer investigation gate

An analytics run is not comparable with a normal run until both executables
report the same rendering backend. The analytics build must emit a one-time
startup diagnostic containing:

- selected backend (`gpu` or `software`);
- effective window dimensions;
- whether GPU initialization succeeded or the application fell back to the
  software renderer.

This diagnostic is intentionally emitted once during window setup, never from
the frame loop. A software fallback can explain a very large FPS difference by
itself and must not be attributed to analytics instrumentation.

The startup line is emitted after GPU initialization succeeds, and includes
both the selected backend and the initialization result. This prevents a
requested GPU mode from being reported as GPU before initialization has
actually completed.

The current capture demonstrates why this gate is required: the largest region
was `software_meshes`, approximately 50--70 ms per frame, while the GPU
rendering regions were absent. That is evidence of a software-renderer capture,
not evidence that the exporter thread is consuming 50--70 ms. Future captures
must compare GPU-to-GPU or software-to-software runs at the same resolution.

Each comparison should also record, separately from game timings:

- analytics frame-finalization duration;
- number of pending scopes and trace events;
- dropped scope, frame, and trace counters;
- exporter queue depth and oldest queued buffer age;
- total frame time and renderer backend.

The analytics session now exposes completed-buffer queue depth and active
frame/trace counts. Minecraft prints these only when frame finalization exceeds
1 ms, alongside the existing drop counters. This makes exporter backpressure
visible without adding a lock or formatter call to ordinary frames. Libft now
timestamps each completed buffer at handoff and exposes the age of the oldest
queued buffer. Minecraft includes that value in slow-finalization diagnostics.
A non-zero age with a growing queue identifies exporter backpressure; a zero
age and no drops keeps the investigation focused on the renderer or
world-generation path.

If frame-finalization stays below the agreed budget and no drops or queue
backpressure occur, investigate the renderer or world-generation phase rather
than adding more analytics buffering. If finalization or queue pressure spikes,
capture a matched run with trace export disabled and then with instrumentation
disabled to isolate sampling, aggregation, and file-export costs.

The isolation sequence must additionally verify the backend line printed at
startup. Run the same executable and scene in these three modes:

1. normal analytics: producer recording and persistent exporter enabled;
2. `--analytics-no-exporter`: producer recording enabled, exporter disabled;
3. `--analytics-no-instrumentation`: analytics-capable executable with runtime
   recording disabled.

Compare only runs with the same `renderer_backend`, resolution, seed, input
schedule, render-distance schedule, and duration. A software capture compared
with a GPU capture is invalid evidence. If mode 3 remains slow while mode 1
and mode 2 are similar, the cause is in analytics-build-only code outside the
Libft producer/exporter path. If mode 2 is materially faster than mode 1,
investigate exporter queue pressure and file I/O. If mode 3 recovers the frame
rate, investigate producer scope volume or aggregation cost and retain the
thread-local fast path above.

## 10. Streamed-chunk and fluid-generation correctness follow-up

World storage slots are recycled during recentering. A pending GPU upload must
not leave the previous slot's geometry drawable at the new chunk coordinates.
The geometry batch therefore invalidates stale drawable counts when a chunk
identity or revision changes, and validates the persistent upload cursor using
chunk coordinates as well as the slot index. The visibility cache also tracks
loaded-count, stream-center, and index-validity changes so a newly committed
chunk cannot be hidden behind a stale visibility snapshot.

Procedural lake candidates are tied to terrain: a lake candidate below sea
level requires a deterministic above-sea-level neighbour rim. Explicit
lowland-flood configurations retain their existing behavior. Aquatic
decoration is checked against generated blocks after fluid placement: lily
pads require water below a replaceable target and seagrass requires an actual
water target. Ordinary shrubs and trees continue to require dry support.

Required regression coverage includes slot reuse while uploads are throttled,
streaming beyond the initial area, visibility-cache topology changes, lake
rim and unsupported-water checks, vegetation exclusion from water, and
deterministic results when neighbouring chunks arrive in different orders.
The water-column regression must inspect every water voxel and require its
immediate lower voxel to be water or solid, except for the intentional rooted
aquatic-plant case: if the lower voxel is seagrass, the voxel below the
seagrass must be solid. Checking only the first water voxel in a column is
insufficient to detect internal floating sections. Lily pads must remain
surface decorations over water, while ordinary shrubs and trees must continue
to require dry solid support.

The streamed-result commit path must transfer the complete mesh payload:
vertices, the compatibility index list, solid index ranges, water index ranges,
and occupied bounds. Dropping either partition during the worker-to-world move
causes a chunk to be present for collision and generation while its GPU pass
has no corresponding draw range; an edit can then appear to “fix” it by
triggering a later remesh. This is now enforced in the result mesh move helper.

The streamed commit also assigns the world geometry revision to the mesh
revision (with a non-zero fallback). Revisions must not reset to `1` whenever a
storage slot is reused: otherwise the same chunk coordinates can be paired
with the same voxel/mesh revisions as an older occupant, causing the GPU cache
to retain stale geometry until a block edit changes the revision. Renderer
identity tests must therefore cover eviction, regeneration of the same
coordinates, and upload before and after an edit. Using the world revision
rather than a request-local counter also remains safe across generation-pipeline
shutdown and reinitialization.

The initial and synchronous stream paths now use that same world-wide geometry
revision when publishing a chunk. They no longer depend on a storage slot's
local `mesh_revision` increment. This keeps synchronous loading, asynchronous
loading, regeneration, and slot reuse on one identity contract: every
published geometry state receives a new monotonic world revision, even when a
slot is reused for the same chunk coordinates before a renderer collection.

Remesh commits now prepare and validate a complete replacement mesh before
destroying the live mesh. Allocation or payload-transfer failure therefore
leaves the previously drawable geometry intact and returns the original error
to the result drain. The final main-thread replacement remains the only point
where the chunk mesh revision advances, so the GPU identity/upload path sees a
single coherent transition.

The async streaming validator must be invoked with
`--validate-async-generation`. An unrecognized validation flag falls through to
the normal application path and can look like a stalled validator. The async
validator now reports progress every 100 frames, logs update calls exceeding
one second, and reports stream queue counts when an update returns an error.
This distinguishes a real generation/commit stall from an incorrectly invoked
test or a process that is merely waiting for a window/application loop.

The Minecraft `automated_tests.exe` target is the application binary with
validator flags, not the Libft test executable. Running it without a recognized
`--validate-*` flag launches the normal window/application loop and can appear
to hang during loading. CI and local validation must pass an explicit validator
flag; the Libft module suite must be run from its own `Test/libft_tests` target.

The validator waits briefly between polls instead of busy-spinning. This is
important because the test itself must leave scheduling time for the persistent
generation workers; otherwise a fast host can make the worker-backed test look
like a world-generation hang. The wait is test-only and does not change the
runtime streaming loop.

Debug and analytics builds also emit a sampled commit-pipeline line every 120
stream frames. It records queued requests, completed results, results committed
on the main thread, and the age of the oldest completed result. A growing queue
identifies worker throughput or scheduling pressure; a growing completed count
with no commits identifies a main-thread drain or lock problem; an empty queue
with no playable progress points to candidate admission or worker failure. The
line is compile-gated and is absent from release builds. Loading diagnostics
also report the number of actively processing generation requests, so an empty
queue is not mistaken for idle workers that are still computing a chunk.

While the loading screen is active, its periodic stream line also reports the
drawable playable-area count as `drawable=current/required`. This separates
“the chunk objects exist” from “the renderer has valid geometry” and makes a
stall at the end of loading actionable: a fixed pending count with a growing
completed-result age indicates commit pressure, while a fixed drawable count
with no pending/completed progress indicates candidate or worker failure.
The same line includes the stream `progress` frame, which advances whenever a
chunk is published; an unchanged progress value across successive reports
distinguishes a stalled pipeline from a backlog that is still making progress.
Diagnostics builds also print up to four exact playable-area coordinates that
are still missing or non-drawable, plus the remaining gap count. This keeps
loading investigation independent of a debugger: missing coordinates point to
candidate admission/publication, while non-drawable coordinates point to mesh
construction or payload transfer.

For repeatable local diagnosis, both normal and analytics executables expose
`--worldgen-probe`. It starts the real persistent generation pipeline without a
window, advances it from the origin, prints loaded/pending/ready/drawable/
active-worker counters, and exits zero only after the complete minimum
playable ring has drawable meshes. It has a bounded iteration timeout and
returns nonzero with the last queue/error counters if progress stops. The
analytics probe opens a frame and world session around each update so its
measurements exercise the same valid analytics lifecycle as the game loop;
this avoids turning invalid-scope error logging into a false world-generation
stall. Run `ft_vox.exe --worldgen-probe` and
`ft_vox_analytics.exe --worldgen-probe --analytics-no-exporter` before using
GUI reproduction or a debugger.

The loading telemetry contract is now preserved through the complete API path:
the stream-diagnostics builder computes playable-required, playable-drawable,
and active-worker counts; `World::stream_diagnostics()` copies those fields
explicitly; and the loading screen reports them only in diagnostics builds.
Leaving fields out of this forwarding step would expose uninitialized values
and could falsely suggest that generation has stopped or completed. The
diagnostics mesh test also checks partition index bounds, matching the
interactive readiness gate.

The revision validator waits for its selected loaded chunk through the normal
streaming path, then cancels stale background requests and drains completed
stale results before submitting the revision job. This prevents unrelated
startup queue saturation from producing nondeterministic revision-test
failures.

The async-generation validation also checks the committed mesh payload itself:
the compatibility index list must equal the combined solid and water ranges,
and every transferred partition index must reference a transferred vertex.
This prevents a voxel-only test from accepting a chunk that is valid for
collision but has no drawable GPU ranges.
Its progress, slow-update, and failure lines also include the active worker
count, so a nonzero in-flight count remains visible even when the queued and
completed-result counts are both zero.

The synchronous visible-distance validator now prints phase checkpoints with
elapsed time. Its full-radius stream and slot-reuse passes intentionally create
large workloads; a quiet period before `full-stream-complete` or
`slot-reuse-complete` is therefore not sufficient evidence of a deadlock. The
phase markers distinguish slow synchronous generation from failures in culling,
mesh validation, or recentering. This instrumentation is test-only and does not
run in the game executable.

The visible-distance validator applies the same drawable-payload invariant
after initial streaming and after a one-chunk recenter. An occupied streamed
chunk must contain vertices and at least one solid or water index range; an
empty payload is reported with its storage slot and chunk coordinates.

The interactive loading gate now uses that same minimum invariant for the
playable area. A chunk counts toward entering gameplay only after it has
occupied bounds, vertices, and at least one valid solid or water index range;
the compatibility index count must match the two partition counts. This keeps
the loading phase from ending in the interval where collision can see a
published chunk but the renderer still has no drawable geometry.

`WorldChunk::mesh_is_drawable()` is now the shared implementation of this
predicate for the loading gate, stream telemetry, and asynchronous validation.
The validator retains its additional failure-specific messages, but the
accept/reject decision is no longer duplicated across those paths. This is
important for diagnosing an apparent end-of-generation stall: a reported
non-drawable chunk now means the same thing everywhere, including an invalid
partition index rather than only an empty mesh.

Analytics captures now also report, once per diagnostic sample, the identities
of up to four visible chunks whose GPU upload remains deferred after the
bounded upload pass. Each record includes the storage slot, chunk coordinates,
voxel and mesh revisions, vertex count, and solid/water index counts. A
non-zero `deferred_visible` count with stable revisions identifies upload
backlog; a missing chunk from this list points instead to streaming/index or
culling admission and must be investigated in those phases. The GPU visibility
collector treats the fixed chunk storage array as the authoritative drawable
set. The spatial index remains useful for world queries, but a streamed chunk
can be published between index-maintenance steps; rendering must not require a
later block edit to make that chunk drawable.

The same sample reports up to four occupied chunks that are within the active
render-distance envelope but are rejected by the culler, together with the
`nearby_culled` count. This is separate from upload backlog: a non-zero value
identifies a false-negative visibility decision, while a zero value combined
with missing geometry points to stream indexing or GPU upload state instead.

Interactive acceptance for streamed rendering requires a GPU analytics capture
with a fixed seed and movement across at least one cache-radius boundary. The
capture must record, for the same frame, the stream center, loaded/pending
counts, committed chunk coordinates, visible-list count, deferred-visible
count, upload count/bytes, and the framebuffer/backend label. Interpret the
result in this order:

1. A committed chunk absent from the visible list is a storage, coordinate, or
   culling admission failure.
2. A visible chunk reported as deferred across successive frames is an upload
   scheduler or GPU-transfer failure.
3. A visible chunk whose upload identity matches but is absent from the image
   is a draw-state, shader, depth, or camera-transform failure.
4. Water-only anomalies with valid chunk identity and draw ranges belong to
   water mesh ordering/material or generation validation, not streaming.

The headless validators prove the first category's data invariants and the
slot-reuse identity rule, but cannot prove the second or third category
without an active graphics context. Do not mark the interactive rendering
issue resolved from headless success alone.

The visible-distance validator now performs the same admission check without
an OpenGL context. After initial streaming and after a one-chunk recenter, it
builds the production `RenderCache` and verifies that every occupied chunk in
the horizontal render envelope is accepted by `MeshCuller`. A failure reports
the storage slot, chunk coordinates, world offset, camera position, and active
render distance. This catches coordinate and frustum regressions in CI; it
does not replace an interactive GPU capture, which is still required to prove
that an admitted chunk was uploaded and drawn.
