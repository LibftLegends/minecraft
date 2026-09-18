# Voxel lighting implementation handoff

Status: lighting test-harness implementation in progress; production lighting
gates remain failing and are intentionally reported by the harness.

Continuation note: this document is now the handoff point for feature work.
The next implementation commits should resolve the remaining lighting, water
generation, and block-break scheduling issues described below before adding
new rendering features.

Branches:

- Minecraft: `agent/analytics-performance`
- Libft: `agent/compression-analytics-cardgame-scripting`

## Required work priority order

All implementation and runtime scheduling work must use this order. Do not
start a lower-priority feature while the acceptance criteria for a higher
priority item are still failing. At runtime, a lower-priority class may run
only when the higher-priority class has no ready work, or when its bounded
starvation allowance expires.

### Priority 1 — loaded-chunk block changes and visible geometry

Apply the authoritative block edit, invalidate the edited chunk and its visible
border neighbours, and publish the replacement geometry as soon as possible.
Do not wait for the complete lighting solve before the removed/placed block
disappears from the renderer.

This includes the edited chunk, face-sharing chunk-border geometry, stale-result
rejection, geometry-only intermediate publication, and coalescing repeated
edits before scheduling duplicate remeshes.

**Exit criteria:** the edited block and affected visible border faces update
before lighting completes, no old mesh is uploaded under the new voxel
revision, and repeated edits do not synchronously block the render thread.

### Priority 2 — lighting updates caused by those changes

Process the bounded light frontier for the edited chunk and its cardinal
neighbours, then publish the light-aware remesh results in edit-distance order.
Coalesce repeated cells and sections, and keep each frame within the runtime
lighting budget.

This includes direct skylight and block-light invalidation, chunk-border
propagation, incremental node/region work across frames, and light-aware
remesh publication.

**Exit criteria:** nearby light converges correctly after edits, chunk seams do
not become black or falsely bright, stale lighting cannot overwrite newer
state, and block-break latency remains within the measured budget.

### Priority 3 — new-chunk generation and ordinary arrival remeshes

Only after priorities 1 and 2 have no ready interactive work may the engine
generate, light, and upload streamed chunks. New arrivals must not occupy the
interactive edit queue.

This includes terrain/biome generation, water and lake/river generation,
arrival lighting, ordinary neighbour remeshes, and distant chunk uploads.

**Exit criteria:** streaming continues without starving, new chunks do not delay
an active edit or its lighting, and the bounded fairness escape is observable
in analytics.

### Required scheduler sequence

For each frame, the scheduler must make this decision in order:

```text
1. apply/submit loaded-chunk block changes and visible border geometry
2. process and publish pending edit-caused light work
3. only then submit/process new chunk generation and arrival remeshes
```

The scheduler must expose queue depth, age, and starvation promotions for all
three classes. A test that repeatedly breaks/places blocks while the camera
streams new terrain must prove that the edited block is removed from the
visible mesh first, the nearby light converges second, and new chunks make
progress third without either starvation or an unbounded frame spike.

The worker arbitration follows the same order: remesh/light requests are
selected before generation requests, with one generation escape after at most
eight consecutive remesh selections. This is starvation prevention, not a
reversal of the priority order.

## Completed and committed

- Libft owns packed sky/block light, light metadata, light sections, the
  deterministic bounded solver, light-aware greedy meshing, and blob-shadow
  receiver helpers.
- Minecraft generation builds initial derived lighting off the render thread.
- GPU meshes upload packed light and the world shader applies the two channels,
  nonlinear brightness, and existing face shading.
- A GPU player blob-shadow quad was added with ground receiver detection and
  height fade.
- Neighbor remeshes preserve packed light instead of resetting it to zero.
- The software renderer now applies the same packed-light brightness curve as
  the GPU shader once per triangle, without per-pixel world/light lookups.
- Block edits are coalesced into a loaded 3x3 dirty region and submitted to the
  asynchronous remesh pipeline instead of synchronously rebuilding nine
  chunks in the edit call.
- Chunks now carry an explicit light revision. Remesh requests/results carry
  that revision, neighbor invalidation advances it, and stale worker results
  are rejected with diagnostics when newer lighting state exists.
- Remesh snapshots now include a 15-block horizontal lighting halo and workers
  consume immutable snapshot data. Missing streamed space is conservative
  solid space.
- Remesh snapshots now capture all eight neighboring chunks for the halo,
  including diagonal corners, instead of treating diagonal halo cells as
  conservative solid space when those chunks are loaded.
- Remesh lighting now reads the immutable snapshot callback instead of wrapping
  every halo coordinate back into the target chunk. The async-generation
  validator proves cardinal and diagonal skylight propagation across chunk
  seams and verifies that removing either neighboring source removes the
  contribution.
- GPU mesh publication now prioritizes visible chunks whose voxel revision is
  greater than the initial generated revision. This prevents an interactive
  block edit from waiting behind the one-background-upload-per-frame stream
  budget while newly generated chunks are being uploaded.
- GPU publication no longer uploads an old CPU mesh under a newly edited voxel
  revision. While an edited chunk is dirty, its last committed GPU mesh stays
  visible until the matching remesh is committed; new chunks and reused storage
  slots still invalidate normally. Local and authoritative edits also submit
  the target remesh immediately, with the priority queue retained for retry.
- Priority remesh requests are now deduplicated in a queue instead of a
  single overwritable coordinate. Stream recentering also clears pending
  remesh ownership markers for canceled requests and requeues those chunks,
  preventing a canceled request from leaving a neighbor permanently dirty.
- The asynchronous remesh window is bounded to two persistent worker jobs;
  one interactive edit can therefore proceed beside a background remesh while
  retaining the queue cap and revision guards.
- Remesh snapshot submissions are now cadence-limited across frames so the
  render-thread world-lock path does not repeatedly capture large halo buffers
  back-to-back.
- Chunk eviction during a stream recenter now invalidates the surrounding
  loaded chunks, so removal of a neighbor schedules replacement lighting and
  border remeshing instead of leaving stale seam data.
- Analytics/debug remesh and worker diagnostics are sampled by request ID
  instead of synchronously writing one `stderr` line for every completed job.
  This keeps instrumentation from becoming a console-I/O workload; detailed
  propagation counters remain available in the sampled records.
- Interactive remesh requests use a separate high-budget worker configuration
  and are selected ahead of background generation slices. This keeps the
  bounded lighting solve off the render thread while targeting sub-frame edit
  publication; background generation retains the conservative slice budget.
- Newly arrived synchronous/asynchronous and regenerated chunks invalidate
  their loaded neighbours and enter the bounded ordinary dirty-remesh scan.
  They deliberately do not enter the interactive front-priority queue: doing
  that for every startup arrival can fill the remesh window and starve the
  generation queue. Explicit player edits are still inserted at the front of
  the priority queue and keep the immediate submission path.
- The block-edit validator now runs sixteen consecutive place/delete remeshes
  and reports p50/p95/p99/max latency. Normal and analytics Windows builds
  remain below the one-second regression bound, with current samples around
  59/60/60 ms for p50/p95/p99.
- Remesh snapshot capture now uses the chunk bulk-copy APIs for the target,
  lighting halo sources, and border columns instead of taking a lock for every
  voxel read. Retired mesh buffers are transferred to the persistent worker
  cleanup path rather than being destroyed during frame-thread publication.
  Analytics-only snapshot and remesh-publication timing diagnostics separate
  capture cost from commit cost.
- Recenter eviction now transfers evicted chunk payloads into the persistent
  worker cleanup queue. Slot metadata and the chunk index are updated on the
  world thread, while voxel, light, and mesh deallocation is deferred. The
  Windows visibility validator confirms recenter time fell from roughly
  86--105 ms to approximately 8--9 ms in the measured run.
- Libft lighting scratch grids and propagation queues are reused per worker
  thread between builds. Their contents are reset for every build, preserving
  deterministic output while avoiding repeated large allocations in the
  remesh hot path.
- The lighting worker now also caches the block IDs for the scanned halo in
  its thread-local workspace. Propagation reads the cached IDs instead of
  calling the world lookup callback again for every propagated neighbor. This
  preserves the existing solver order and output while removing redundant
  snapshot/world lookups from the propagation phase.
- Remesh lighting now uses a persistent `voxel_light_build_operation` owned by
  the generation request. The worker executes bounded slices and requeues an
  incomplete request on the existing persistent worker queue. Scan,
  propagation, and finalization state survive between slices, and the
  runtime-configured minimum/target/maximum node counts and time budget are
  carried with each remesh request. The render thread still receives only a
  completed immutable result.
- CSV parsing preserves trailing/consecutive empty fields, rejects structural
  delimiters, and rolls back row metadata when the second vector append fails.
- The complete design document is promoted to
  `Libft/Docs/rendering_main_thread_optimization_design.md`.

## Still required before calling the design complete

### Verification update: 2026-09-08

The current `agent/analytics-performance` checkout was rebuilt after the
Libft submodule moved to `a9c66bdb` (`document voxel shadow APIs`). Both
variants passed the graphics-context publication check:

- `ft_vox.exe --validate-renderer-publication`
- `ft_vox_analytics.exe --validate-renderer-publication --analytics-no-exporter`

The normal `make validate-all` aggregate also passed with zero failures. Its
async workload grew from two initial chunks to fourteen loaded chunks, reached
the first visible mesh at frame 12, and completed the repeated edit workload.
The analytics world-generation probe independently reached a playable area and
expanded the stream before exiting successfully.

The Voxel README now documents the public `voxel_shadow.hpp` receiver and
height-fade APIs. The Voxel make graph and full Libft manifest already include
`voxel_shadow.cpp` and `voxel_shadow.hpp`; no dependency-graph change was
needed.

### Latest continuation result: edit-to-render publication

The direct player placement/deletion path was found to bypass the shared
remesh invalidation primitive. It manually changed revisions and cleared the
pending request, while stale worker results and failed remesh results could
fall back to the ordinary dirty-chunk scan. That made authoritative block
storage update before the replacement mesh was guaranteed to be scheduled.

The path now uses the same invalidation helper as authoritative and deferred
edits, advances the light revision when an in-flight request is invalidated,
prioritizes the edited chunk for the next scheduler pass, and re-prioritizes a
chunk when a stale or failed remesh result is discarded. Undo/redo uses the
same path as well. This keeps the worker snapshot immutable and does not move
lighting or mesh generation onto the render thread.

Focused Windows evidence from the current checkout:

- `ft_vox_analytics.exe --validate-block-edit` passed twice; the mesh revision
  and index counts changed for both placement and deletion.
- `ft_vox_analytics.exe --validate-async-generation` passed after the edit
  path changes.
- The async-generation validator now records the initial synchronous seed and
  requires the loaded chunk count to grow before the playable area is accepted;
  this prevents a test from passing while remesh work is active but generation
  has stopped expanding the world.
- The normal `make -j2` target rebuilt and linked the analytics executable
  successfully.
- The Windows `automated_tests.exe --validate-all` aggregate passed with zero
  failures after the scheduler-priority correction. The run reported 16
  repeated edit samples at p50=75 ms, p95=78 ms, p99=78 ms, max=79 ms, and
  async startup first-visible mesh at frame 8.
- Stale-result diagnostics are now split by source. Fresh async-generation
  runs completed successfully with 5--6 rejected results: four stale stream
  results and one or two stale remesh results. This confirms rejection is
  active and gives the next scheduler pass a concrete remesh-stale case to
  explain rather than hiding it in debug output.

This proves storage-to-CPU-mesh publication for the validator workload. The
edit-priority upload path is compiled into both normal and analytics builds.
The separate `--validate-renderer-publication` check now exercises a real GPU
context, edits a visible block, runs the normal world/renderer handoff, and
verifies that the uploaded chunk identity and voxel revision advance after the
edit. It passed in both normal and analytics Windows builds. It remains
separate from `--validate-all` so headless CI machines do not fail the
aggregate suite.

### Latest continuation verification: 2026-09-10

An edit that arrived while a background remesh already owned the chunk could
be accepted by `queue_chunk_remesh()` without carrying its incremental-light
classification into the next request. The edit commands now always promote
the edited chunk into the interactive priority queue; the queue preserves the
source coordinate and old/new block metadata until the existing request has
retired. This prevents an older full-light request from silently replacing the
edit-specific incremental request.

The normal build path also now guarantees an analytics executable. `make all`,
`make normal`, and the Windows `make ft_vox` alias build the normal executable
and then verify/build `ft_vox_analytics.exe`, including when the normal binary
is already up to date. The `all` target uses one serialized normal path so the
normal and analytics graphs cannot race over shared generated files.

Current Windows verification:

- `make -j2 all` produced both `ft_vox.exe` and `ft_vox_analytics.exe`.
- `automated_tests.exe --validate-block-edit` passed; repeated edits reported
  p50=30 ms and p95=47 ms, with incremental placement/deletion counters
  increasing and rapid supersession passing.
- `automated_tests.exe --validate-visible-distance` passed with 377 chunks
  loaded.
- `automated_tests.exe --validate-all` passed with zero failures.

This closes the edit-priority handoff and build-variant requirements. It does
not close the remaining coordinated cross-chunk removal frontier or
section-local mesh rebuild work; border source removal and mixed light changes
still require the full reference solver until differential tests prove a
multi-chunk frontier equivalent.

### Latest Libft lighting verification: 2026-09-09

The fetched Libft branch is now at `2f49c33d`. Its bounded lighting operation
was not equivalent to the synchronous builder: the scan phase seeded only
selected lateral skylight candidates instead of every direct-sky cell. That
could omit valid lateral sources and produced different propagation counts.
The operation now uses the same direct-sky seed contract as the full builder;
the temporary operation and full rebuild agree again.

Focused verification from `Libft\Test` passes 21/21 for the voxel lighting,
mesh-boundary, shadow, and biome-edge tests, including the sliced-operation
reconstruction test. The complete Libft run now reports 6,344/6,344 passes.
The generator-policy failures were fixed by publishing snow caps on border
columns, preventing beach replacement from overwriting surface-water beds,
and validating enclosed underground-lake geometry instead of applying an
unrelated coordinate-grid filter. The context-freeze fixture now explicitly
opts its custom biome into snow caps, matching the per-biome policy contract.

The remesh pipeline now has an explicit two-stage publication contract:
geometry-only results publish the edited block mesh as soon as the immutable
snapshot is available, while the same revision-checked request continues its
bounded light solve and later publishes the final light-aware mesh. GPU upload
accepts that intermediate mesh revision but still refuses to upload an old
mesh under a new voxel revision.

### Latest Windows continuation verification: 2026-09-09

The default world-generation worker count now leaves two hardware threads
available for the render loop, remesh snapshot capture, and analytics/export
work when the machine exposes more than two hardware threads. The previous
`hardware_threads - 1` policy allowed persistent generation workers to
preempt those systems and made stale-result commits appear as multi-frame
pauses on smaller Windows machines.

The async-generation validator also moved its `World` and expected voxel
chunk from automatic storage to heap storage. Their lighting-backed storage
exceeded the default Windows thread stack, which caused status `0xC00000FD`
during `--validate-all` and looked like a startup hang. The isolated normal
validator now passes with 14 loaded chunks and a visible mesh by frame 12;
the aggregate normal validator passes with zero failures.

The dedicated 64-edit workload was rerun after the worker reservation:

```text
analytics: p50=61444us p95=81581us p99=118504us
normal:    p50=62599us p95=141330us p99=179582us
```

The normal-versus-analytics spread is still too large to call the performance
work complete. These runs show that worker oversubscription was a real
contributor, but the remaining snapshot volume, lighting-node count, stale
result count, and whole-chunk publication cost still require the scheduler
and coalescing changes below.

The scheduler now also promotes background remesh notifications that have
waited at least 120 stream frames. The promotion is counted in
`remesh_starvation_promotions` and is included in the async-generation JSON
metrics. The validator exercises two aged background entries and requires both
to be promoted, while the worker's existing eight-remesh generation escape
remains active. A later matched validator run reports normal p50=61 ms,
p95=77 ms, p99=77 ms, max=89 ms; analytics reports p50=62 ms, p95=77 ms,
p99=77 ms, max=77 ms. The Libft solver now coalesces queued cell/channel
entries and seeds only direct-sky boundary neighbours. In the repeated-edit
workload the lighting queue peak fell from approximately 392,998 entries to
78. This removes the runaway open-air queue, but whole-chunk
snapshot/publication cost and section-level dirty coalescing remain open for
further optimization.

The remesh capture worker now performs a request-id and voxel/light-revision
freshness check under the world read lock before copying the target and its
lighting halo. A superseded request is discarded before snapshot allocation
and copy, and cleanup can clear only the matching request marker. The worker
also repeats the check after capture, so an edit that arrives during the copy
cannot be submitted as current work. `stale_capture` is exported separately
from stale worker results; the current normal validator passed with 2 skipped
stale captures, 0 failures, and a 46 ms p50 / 61 ms p95 repeated-edit
latency. This reduces obsolete snapshot work but does not yet replace the
whole-chunk mesh publication path or implement section-local mesh rebuilds.

The geometry-only remesh stage is now worker-internal. It still advances a
persistent request into the lighting solve, but its unlit mesh is not
committed to a chunk. The client keeps the existing/local-lit mesh until the
final light-aware mesh is ready, preventing black surface chunks and visible
lighting flicker during startup and neighbour arrival. The focused normal and
analytics renderer-publication validators both pass. Expected bounded-queue
`FT_ERR_FULL` and stale-request events are also no longer printed for every
occurrence in analytics/debug builds; they remain in diagnostic counters,
with sparse sampling reserved for investigation output.

### Immutable halo equivalence fix: 2026-09-11

The immutable remesh handoff initially treated an unavailable neighbouring
chunk as air. The established live capture path treats that same boundary as
solid stone while marking the lighting halo invalid. Those two representations
could therefore produce different halo snapshots at startup and at chunk
edges: the worker could see an artificial opening, discard/rebuild lighting,
and publish a result that did not match the live-world capture.

`WorldChunkSnapshotCapture::capture_read_states()` now preserves the live
capture fallback (`VOXEL_GENERATOR_STONE_BLOCK`) for missing or unavailable
neighbour halo cells. The halo remains invalid until all required neighbours
are present and current; the fallback is only a safe boundary value and is not
reported as converged lighting.

The immutable-vs-live equivalence validator compares target blocks, target
light, four borders, halo blocks, halo light, and halo validity. It passes
after this correction. The focused renderer-publication check also passes,
confirming that a block edit reaches a GPU-visible mesh without requiring the
remesh worker to read `World` or acquire the world mutex.

The capture worker also now checks the per-chunk cancellation token before
starting and after finishing immutable capture. A delete/place that supersedes
an in-flight capture therefore prevents that obsolete snapshot from entering
the lighting pipeline; the existing revision checks remain as the final
defence for work already submitted. The async startup validator observed
stale captures being rejected before submission while still reaching its first
visible mesh within the startup budget.

Latest verification after the cancellation and seed-coalescing changes:

```text
validate-all: passed failures=0
async-worldgen: final_loaded=13 first_visible_mesh_frame=6
async-worldgen: stale_capture=43 max_update_us=24732
block-edit: repeated p50=30 ms p95=31 ms p99=31 ms max=42 ms
block-edit: boundary place=95 ms delete=93 ms
```

### Border geometry/light separation: 2026-09-11

Face-neighbour invalidation previously used one predicate for two different
jobs: it only queued a neighbour when its edge block was transparent. That is
appropriate for deciding whether light can cross the edge, but it is wrong for
mesh visibility. Deleting a block can expose the face of a solid block in the
neighbouring chunk, so that neighbour must still receive a geometry update.

The edit path now always marks loaded face neighbours dirty for geometry. It
adds a light dependency and light frontier only when the neighbour edge is
transparent/light-relevant. Solid neighbours are submitted as a final
mesh-only request using their immutable previous-light snapshot; this avoids a
whole relight solely to expose a newly visible face. The result clears the
pending request immediately and does not replace the valid light buffer.

The focused block-edit validator now passes all four border faces, and the
async-generation metrics show geometry-only neighbour completions without
breaking startup convergence. The renderer publication validator passes when
run alone; its timing is kept separate from parallel-process measurements.

### Incremental light publication stability: 2026-09-12

The renderer still exposed an intermittent black/flickering replacement when
an incremental frontier result contained only a small subset of the complete
mesh light field. The worker now reconstructs a complete temporary mesh-light
field from the immutable snapshot, including the direct-sky fallback used by
generated meshes, before applying incremental deltas. The committer also
rejects a severe light regression instead of replacing a valid visible mesh;
it keeps the previous mesh and invalidates the light input so the normal full
solver retries the request.

Chunk-arrival invalidation was also tightened. A published neighbour is only
marked for a light remesh when its newly available shared face can increase a
transparent target boundary cell. This avoids repeatedly advancing light
revisions for arrivals that cannot change the target and prevents a startup
storm of stale remesh results. Geometry invalidation remains independent, so
border faces are still refreshed even when light is unchanged.

Verification from the current Windows checkout:

- analytics renderer-publication: 3/3 passes, 40--41 ms edit publication;
- analytics block-edit: 3/3 passes, border and cross-chunk checks passed;
- normal/analytics builds completed;
- standalone visible-distance validator passed with 377 loaded chunks;
- aggregate `automated_tests.exe --validate-all` passed with zero failures;
- no nearly-black intermediate upload was observed in the renderer trace after
  the complete-field reconstruction.

The normal `automated_tests.exe` was rebuilt after the worker diagnostics were
guarded out of release code. Both normal focused validators then passed:
`block-edit: ok` with boundary place/delete at 62/78 ms and
`renderer-publication: ok` at 51 ms. This also confirms that the diagnostic
helpers do not create release-build `-Werror` failures or release-only
instrumentation overhead.

### Interactive publication drain follow-up: 2026-09-12

One renderer-publication trial exposed a 4.2-second edit delay while 154
stream requests and 124 stale results were present. The result committer was
hard-limited to one result per frame, so stale generation publication could
delay an already queued interactive remesh even after the worker had finished
it. Normal frames still commit one result; frames with priority remesh work now
commit at most four results, retaining the existing one-millisecond wall-clock
budget and fixed upper bound.

Verification after the change:

- renderer publication: 5/5 trials passed, 52--65 ms edit publication;
- async generation: passed with `stale_remesh=0`;
- block edit: passed, including border and cross-chunk convergence;
- Libft voxel mesh light mapping test: passed.

This reduces publication starvation but does not prove that all long-run
interactive workloads remain below the latency target. Repeated edit bursts
and a loaded-world stress run remain required.

The subsequent aggregate validator completed with `validate-all: passed
failures=0`. Its async-generation metrics still recorded stale stream/remesh
results during intentional supersession (`stale_result_count=8`,
`stale_remesh=4` in that run). Those results were rejected and did not become
visible publications; the counters are useful evidence that cancellation is
active, but they are not a substitute for the pending long-run starvation
stress test.

A ten-process repeated block-edit stress pass then completed with zero test
failures. Across the ten runs, p50 latency was 30--45 ms, p95 was 47--156 ms,
and the worst observed sample was 466 ms against the 500 ms publication bound.
This establishes a bounded result under the current workload, but the wide
tail confirms that section-local remeshing and finer queue coalescing remain
performance work rather than completed requirements.

This closes the currently reproducible incremental-publication flicker path.
It does not close the larger handoff items below: section-local mesh rebuilds,
full node-level scheduler proofs, water-generation validation, entity shadows,
and the required Valgrind/cross-platform verification.

1. Finish the runtime lighting scheduler. The persistent bounded operation and
   queue-level slicing now exist, and dirty-remesh selection now ranks the
   bounded scan window by explicit edit priority and distance from the active
   world center. Dirty sections still need stronger coalescing and distance
   from the originating edit must be carried through the queue rather than
   approximated by chunk distance. Notification-level revision coalescing is
   now in place; section-level/per-cell coalescing and starvation proof are
   still required. Verify that repeated requeueing cannot starve generation
   or a nearby player edit. Minecraft now uses a separate bounded background
   configuration of 2048/8192/32768 nodes with an 8 ms slice budget; the generic
   Libft defaults remain unchanged. Dirty chunks in the visible 3x3
   neighborhood are deduplicated into the existing priority queue, with only
   the front request using the separate bounded 4096/65536/131072-node, 8 ms
   interactive configuration. Distant arrival work retains the background
   budget, so local convergence does not make every nearby solve compete at
   once.
   The edit path now promotes the edited chunk and its four loaded
   face-sharing neighbors into that bounded interactive queue. Diagonal
   neighbors remain background work. This keeps an edit's lighting seam ahead
   of unrelated arrival remeshes without submitting all nine chunks at once.
   Both the background and interactive node/time budgets are now exposed as
   runtime `World` configuration APIs and reject invalid min/target/max
   relationships; the async validator checks both round trips. Remaining
   scheduler work is the queue-order and section-coalescing stress proof.
   Priority entries now reject ambiguous same-cell seed histories. If two
   edits for one cell carry different old/new transitions before the earlier
   request is captured, the incremental seed list is discarded and the
   revision-gated full solver is used for the current state. Replaying those
   historical transitions against the latest snapshot would be incorrect and
   was a source of boundary flicker during rapid deletes/places. Identical
   duplicate seeds continue to coalesce without falling back. The normal
   block-edit and aggregate validators pass with this guard enabled.
2. Verify that neighbor arrival/removal enqueue bounded relighting as well as
   face remeshing, and that temporary conservative boundaries converge after
   the neighbor is published or evicted. Arrival and eviction invalidation are
   now wired and newly published chunks use the ordinary bounded dirty-remesh
   path so startup generation cannot be starved. The bounded propagation
   scheduler is still required. The visibility validator now waits for the
   local 3x3 neighborhood to become clean after initial arrival and after
   recenter, including nonzero light revisions and no pending remesh request,
   and reports per-chunk gaps if convergence times out. The Windows visibility
   validator now passes both convergence checks with this scheduling
   configuration, and the full aggregate validator passes as well.
   Libft's water regressions now also pass: connected columns, configured
   levels, chunk-border continuity, atomic surface columns, depth limits,
   enclosed underground lakes, and vegetation exclusion. Runtime visual
   water rendering and long-distance streaming remain separate checks.
3. Complete the Libft lighting tests. Foundational coverage now includes
   pack/unpack, combined darkening, update-configuration bounds, direct
   skylight, full roof occlusion, deterministic local builds, and build stats.
   Cave opening, roof occlusion, block falloff, and multiple-source maximum
   coverage now exist. `test_voxel_lighting.cpp` is now included in the Voxel
   test group and its lighting tests pass in the global Windows test
   executable, including metadata attenuation contracts, incremental
   operation versus clean-rebuild equivalence, and a per-step configured
   slice-bound assertion and sliced-operation versus clean-rebuild equality.
   Neighboring-chunk emitter regressions now verify contribution, removal, and
   deterministic ordering across the boundary; Minecraft-level edit-to-outside
   assertions remain.
4. Add Minecraft validators for edit-to-outside lighting, edit propagation
   across chunk boundaries, stale worker rejection, and the configured work
  budget. The block-edit validator now also waits for the loaded cardinal
  neighborhood to become clean after an edit, exposing stranded pending
  remesh requests. The repeated block-edit workload and p50/p95/p99 remesh
   timing are now present. The validator now places an emitting block on a
   chunk edge, verifies block-light propagation into the neighboring chunk,
   verifies restoration after deletion, and requires all four loaded cardinal
   neighbors to publish newer, clean meshes and newer light revisions after
   both placement and deletion. Queue peak, propagation count, snapshot
   bytes, and stale-job counts are now exported by the async-generation
   validator as a machine-readable JSON metrics line.
5. Add software-renderer lighting parity tests. The implementation now
   prepares the packed-light multiplier once per triangle and performs no
   per-pixel world/light lookup. The renderer validator now compares dark,
   intermediate, and full-light software color outputs against the GPU
   brightness contract, and those checks pass in normal and analytics builds.
   A future framebuffer-level raster comparison can still cover interpolation
   and texture sampling if the software and GPU paths diverge there.
6. Integrate shadows for mobs/entities when the Minecraft entity render list
   is available. The current blob submission is player-only because no entity
   draw submission path was present in the reviewed branch.
7. The headless `--validate-camera-interaction` validator now drives the public
   camera update path through the 89-degree clamp and checks near-vertical
   solid/edit raycasts plus the non-overlapping placement cell. A real input
   session remains useful as an additional manual graphics check.
8. Finish the CSV test run. The broad test build was initially blocked by an
    existing `-Werror` useless-cast failure in
    `Test/Test/test_compression_stream.cpp`; that warning was corrected. The
    full Windows Libft executable now passes all 6,321 tests when launched
    from `Libft\Test` (the fixture-relative working directory). Launching it
    from `Libft` produces false failures for path-dependent File/Voxel tests,
    so CI and local instructions must preserve the intended working directory.
9. Public API documentation is now present in `Libft/Modules/Voxel/README.md`.
   It documents focused header ownership, packed 0--15 lighting, resumable
   light builds, shadow callbacks, mesh replacement bounds, and the staged
   fluid contract. The dependency graph still needs a final CI check after any
   future header split.
10. Run final matched normal/analytics builds, `make validate-all`, and a
    graphics-context runtime test. Do not claim performance success without
    before/after measurements from repeated block-breaking workloads.

### Blob-shadow integration audit: 2026-09-08

The current branch was audited directly rather than inferred from the design
claims above. The result is intentionally split by ownership and renderer:

| Area | Current implementation | Design status |
| --- | --- | --- |
| Libft receiver query | `Modules/Voxel/voxel_shadow.hpp/.cpp` exposes `voxel_shadow_find_receiver(...)` with a caller-owned solid-block callback and a bounded downward search. | Implemented as the reusable primitive. |
| Libft height fade | `voxel_shadow_height_fade(...)` clamps the entity-to-receiver fade to `0..1` and handles non-positive maximum height. | Implemented as the reusable primitive. |
| Minecraft GPU player | `src/gpur/GpuWorldRenderer.cpp:231` submits one four-vertex quad (`GL_TRIANGLE_FAN`, two triangles) after solid geometry. It uses `World::solid_block_at`, an eight-block receiver search, a fixed radius of `0.38`, radial alpha in `shadow.frag.glsl`, and height fade. | Partial: player-only GPU coverage exists. |
| Minecraft GPU mobs/entities | No entity render list or per-entity GPU submission exists. `EntityState` is only a serialized/network state contract; it is not consumed by `GpuWorldRenderer` or an entity renderer. | Missing. Adding it here would require inventing entity ownership/submission architecture, so it is not implemented in this audit. |
| Minecraft software player | `VoxelRenderer::render_world_software(...)` submits only visible chunk meshes, post-processing, debug overlay, and crosshair. There is no blob-shadow geometry, receiver query, or shadow raster submission. | Missing. The current software path has no safe entity/shadow submission seam. |
| Minecraft software mobs/entities | No software entity renderer or entity collection is present. | Missing and blocked on the same architecture gap. |
| Receiver caching and eligibility | The GPU player path performs a fresh world lookup every frame, has no receiver/revision cache, and has no entity distance/population budget. The fixed player path is bounded and cheap, but does not satisfy the optional cache or entity-budget requirements. | Partial/optional work remains. |

The only production-safe missing piece that can be completed without touching
world generation or scheduling is focused contract coverage for the existing
Libft helpers. `Libft/Test/Test/test_voxel_shadow.cpp` now verifies nearest
receiver selection, maximum-distance behavior, invalid callback/output
arguments, and bounded height fading. It does not claim renderer integration.

#### Exact integration blockers

1. There is no authoritative Minecraft entity collection passed into either
   renderer. `src/entities/EntityState.hpp` contains position/type data for
   serialization, but no active entity storage, visibility selection, model
   geometry, or render submission API.
2. The GPU path has a player-specific method rather than a generic shadow
   submission interface. Reusing it for mobs would require defining entity
   lifetime, footprint/radius, active/near-camera filtering, and a stable
   per-frame submission contract first.
3. The software renderer has no entity draw path and no established way to
   project a receiver quad through its depth-tested rasterizer. Adding a
   second ad-hoc path would violate the design's ownership and consistency
   requirements.
4. There is no headless graphics validator that can prove a GPU blob is
   actually visible, correctly depth-tested, or faded. The new Libft tests
   validate only the renderer-independent mathematical contract.

#### Safe next implementation boundary

The next implementation must first introduce or identify the existing
Minecraft-owned active-entity/render-submission contract, then add one shared
shadow submission list consumed by both backends. That work should define
entity culling, footprint/radius, receiver-revision caching, and the fixed
two-triangle budget before wiring mobs. It must remain separate from the
lighting scheduler and world-generation files. Until that contract exists,
the current player-only GPU shadow is the maximum defensible integration and
the software/entity requirements remain open.

## Known issues and cautions

- The last Valgrind report showed an invalid worker-thread read at address
  `0xb0000` after the first halo implementation. A vector-size guard was added
  in the snapshot reader, but a Valgrind rerun is still mandatory.
- A complete Libft test build previously filled the workspace and failed at a
  pre-existing compression-test warning. Keep builds serial and monitor disk
  space.
- The synchronous full-halo helper remains in the legacy `WorldChunkLoader`
  initialization/remesh code for non-streaming callers. Audit those callers so
  interactive block edits always use the asynchronous path.
- The current solver is deterministic and bounded by the 15-level light
  radius, but its internal build is still a whole-region operation on a worker;
  it is not yet incrementally resumable at node granularity.

The focused Windows analytics block-edit validation after the bulk-copy and
retired-mesh changes completed successfully (`EXIT=0`). Its sampled worker
lighting solve remained a whole-region operation at approximately 10--19 ms
for 541,696 scanned cells; no snapshot-capture warning above 8 ms was emitted.
This confirms the frame-thread capture reduction for that workload, but does
not satisfy the persistent node-level scheduler requirement.

The later boundary-enabled analytics run also passed the clean-baseline,
cardinal-neighbour mesh/light-revision checks. It exposed remaining sampled
snapshot-capture spans of approximately 10--24 ms for a 65,536-cell target and
541,696-cell lighting halo. These spans are the next handoff-performance target:
the client must retain immutable ownership and revision validation, but the
render/update thread must not repeatedly pay this full copy cost during an
interactive edit burst. Future work should measure a reusable immutable chunk
snapshot or copy-on-write publication path against this baseline before moving
capture to any worker that could read live mutable chunks.

The next scheduler pass now ranks the bounded dirty-remesh scan by chunk
distance from the active world center, with explicit edit-priority requests
still handled first. For a short expiry window after an edit, the originating
chunk is used as the ranking anchor so neighboring dirty chunks converge
outward from the edit before unrelated work. The remesh counter also uses
saturating atomic release and cancellation accounting; the validator exposed that an earlier
cancel/release race could underflow the unsigned counter and permanently stop
all future remesh submissions. After rebuilding every affected world-worker
object together, the Windows analytics block-edit validator completed with
`block-edit: ok before=3834 place=3246 delete=3204` and the asynchronous
generation validator completed with `async-worldgen: ok frame=19`.

Repeated invalidations are now coalesced consistently across authoritative
edits, deferred generated edits, neighbor arrival/removal, and queued
neighbor remeshes. A dirty chunk without an in-flight request keeps its
current revision until it is submitted; an in-flight request is advanced so
its result is rejected and a fresh snapshot can be captured. This removes
revision churn without allowing an old worker result to overwrite newer world
data. The block-edit and async-generation validators still pass after this
change.

After block-ID caching, the Windows analytics executable passed the worldgen
probe and block-edit validator (`EXIT=0`). The probe completed in 1.34 seconds
with instrumentation enabled and 1.01 seconds with runtime instrumentation
disabled in the same checkout. The sampled lighting solves still took roughly
11--23 ms and scanned 541,696 cells, so the cache reduces redundant lookups but
does not replace the required persistent node-level scheduler.

After deferred eviction was added, both the normal and analytics Windows
visibility validators completed successfully (`EXIT=0`). The normal run took
approximately 12.4 seconds and the analytics run approximately 13.5 seconds;
   both reached the required 160-block visible distance and completed slot reuse
and recenter checks without synchronous full-stream generation.

## Newly confirmed issues for the Windows continuation

The next implementation pass must continue from the branches named above and
must be validated on Windows. Do not treat a successful Linux build as proof
that the Windows executable, analytics launcher, shader assets, or runtime
configuration are current. The Windows handoff must first verify the exact
Libft submodule commit recorded by Minecraft, then build both the normal voxel
executable and the analytics executable from that checkout.

### 1. Lighting is still incorrect in several situations

Observed symptoms include surface-side blocks remaining black when exposed to
daylight, light not reaching the correct area when a cave is opened to the
outside, and light levels not changing consistently after edits. These are
correctness failures, not merely tuning issues.

The continuation should check, in this order:

1. The snapshot contains every block needed by the bounded light radius,
   including diagonal neighboring chunks, and missing chunks use a conservative
   boundary rather than daylight.
2. Direct skylight is seeded from the top of the world and remains level 15 down
   open columns; only light spreading around an obstruction is attenuated.
3. Opaque and attenuating blocks are applied consistently for all six
   directions, including the cell immediately outside every visible face.
4. A block edit invalidates the complete bounded affected region, not only the
   six cells directly adjacent to the edited block. Opening a roof or tunnel to
   the surface must propagate the change until the queue reaches a stable
   boundary.
5. The light-aware mesher does not merge faces with different light signatures,
   and no path resets packed light to zero while copying or remeshing a chunk.
6. Worker results carry a light/voxel revision and stale results cannot replace
   newer lighting.

Required Windows validation cases:

- open sky column, roofed column, and cave with a side opening;
- a tunnel dug horizontally from inside terrain to outside;
- a surface block whose side faces are visible from lit air;
- an emitter at a chunk edge and at a diagonal chunk corner;
- repeated place/break edits followed by comparison with a clean rebuild.

Record the packed-light values and the affected chunk/section coordinates for
failures. A screenshot or “the chunk looks dark” alone is not sufficient to
locate the defect.

### 2. Surface and underground lakes are too deep or geometrically invalid

Water must be generated only after terrain and the relevant cave/solid-space
classification exist, and before shrubs and trees. The current generation
pass needs explicit depth and enclosure validation so it cannot create water
that is floating over air, cut off by impossible walls, or deeper than the
configured lake limit.

The continuation must:

- enforce a small, explicit maximum surface-lake depth rather than allowing
  noise or terrain depression depth to determine an unbounded fill;
- keep underground lakes within the configured minimum/maximum Y range and
  enforce the configured depth, floor thickness, roof thickness, and side-wall
  requirements;
- validate that every placed water cell has supported terrain below it or is a
  deliberately enclosed underground cell;
- validate the perimeter and roof so water never borders an unintended empty-air
  pocket or produces a detached floating sheet;
- ensure rivers follow a continuous terrain depression/valley and terminate or
  join another valid water body instead of ending in visibly impossible cutoffs;
- run water placement as a deterministic post-terrain stage, with vegetation
  sampling the final water result so trees and shrubs do not overwrite or float
  beside invalid water;
- validate surface-water candidates on chunk borders against deterministic
  world-coordinate samples from the neighboring column, so generation order
  cannot retain a detached one-column border fragment;
- add seeded regression tests for desert rivers, surface lakes, and small
  underground lakes in every biome family.

The tests must measure maximum water depth, unsupported-water count,
unsealed-perimeter count, and water/vegetation overlap. Configuration values
must remain runtime-readable and validated; do not hide corrective limits in
platform-specific constants.

### 3. Breaking blocks still blocks rendering with too much work in one frame

The asynchronous worker path is an improvement, but the current implementation
still needs proof that block breaking does not synchronously perform a whole
halo solve, scan too many chunks, or enqueue duplicate remeshes on the main
thread. The final design requires work to be spread across frames while still
making nearby changes converge quickly.

Implement and measure a persistent, runtime-configurable lighting scheduler:

```text
minimum nodes per frame
target nodes per frame
maximum nodes per frame
time budget in microseconds
```

When work is pending, process at least the configured minimum unless shutdown
or an unrecoverable error prevents it. Stop at the configured maximum or time
budget after the minimum has been met. Prioritize chunks near the player,
then cells near the originating edit, then older equal-priority work. Keep the
queue alive across frames; do not rebuild and restart it for each frame.

During one lighting slice, deduplicate dirty sections and schedule at most one
mesh rebuild per affected section/revision. Rebuild only the changed mesh
region when the existing Basic-module bounds/macros support it; otherwise add
the required reusable bounds helpers in Libft and prove that unchanged faces
are not regenerated. No lighting solve, world query, or full-chunk remesh may
run on the render thread for an interactive edit.

The Windows analytics build must include a repeatable workload that constantly
breaks blocks at the player, including openings from caves to the surface and
edits near chunk boundaries. Capture:

- main/render-thread time and worker light-build time;
- p50, p95, and p99 frame time;
- nodes processed per frame and queue peak;
- dirty sections/chunks coalesced and remesh regions rebuilt;
- snapshot bytes, propagation count, stale-result discards, and dropped work;
- the frame in which the edited area becomes visually correct.

An analytics capture from 2026-09-05 measured a 163.9 ms frame dominated by
`world_stream_drain` (163.3 ms), including a 71.2 ms chunk commit. Worker
captures also showed approximately 541,696 lighting cells scanned per remesh
and mesh-generation samples between roughly 68 and 114 ms. These numbers are
evidence for the remaining incremental scheduler/remesh work; they are not
evidence that the exporter thread is blocking the render thread. A matching
normal-versus-analytics interactive run is still required before attributing
any residual cost to instrumentation.

The later Windows capture must be interpreted separately: frame 120 measured
35.9 ms, with `voxel_render_software` at 35.6 ms and its nested
`software_meshes` scope at 35.1 ms. The same frame measured less than 0.4 ms
for the world-stream scopes. This points to software mesh rasterization for
that run, not chunk streaming. The old 163.9 ms result must not be used as
evidence for every analytics slowdown; retain both captures with their exact
variant and workload metadata.

The analytics renderer's detailed per-chunk timing probe is now bounded to 16
chunks on its sampled frame and is disabled when runtime instrumentation is
disabled. This prevents the diagnostic clock calls from creating an avoidable
sampled-frame spike, but it does not reduce the underlying software mesh
rasterization cost. That cost needs a matched normal/analytics render test
before any renderer optimization is selected.

The current Windows continuation completed the matched build/runtime gate:
`make -j2 automated_tests.exe` and `make -j2 analytics` both rebuilt from the
current branch, `--validate-all` passed with zero failures, and both
`ft_vox.exe --validate-renderer-publication` and
`ft_vox_analytics.exe --validate-renderer-publication` passed with
`renderer-publication: ok`. The async validator emitted a JSON metrics record
with startup frames, snapshot bytes, scanned and propagated cells, queue peak,
completed remeshes, stale-result counts, and first-visible-mesh frame.

The workload must be run against both normal and analytics executables. The
analytics launcher itself must be compiled from the current branch and its
output must identify the branch, Libft revision, configuration, and executable
variant. Do not accept an old executable merely because `make` reports a stamp
as up to date.

### Windows handoff checklist

Before implementation resumes on Windows:

1. Fetch the latest remote refs for both branches.
2. Confirm Minecraft points at the intended Libft commit.
3. Build normal and analytics variants serially from clean, current inputs.
4. Run the lighting, water, and repeated-breaking validators.
5. Run the analytics workload and preserve the CSV/JSON results.
6. Only then modify the solver, generation stages, or remesh scheduler.

Every subsequent commit should state whether it changes Libft, Minecraft, or
the submodule pointer, and the final report must include the exact Windows
commit IDs and build artifacts used for validation.

## Recommended next order

1. Re-run Valgrind and the normal build from the committed state.
2. Extend the passing cardinal/diagonal propagation validators with edit-driven
   seam convergence cases.
3. Implement the persistent budgeted node scheduler and stale-light revision.
4. Add software renderer/shadow support and entity submissions.
5. Run validators and repeated break-workload analytics.
6. Update READMEs/dependency documentation and run all final gates.

## Ownership and handoff performance findings

The active single-process path is not currently paying for client/server
replication. The replication runtime classes are present, but the game loop
does not start or pump them during ordinary local gameplay. They must not be
used as an explanation for a frame spike without a capture proving that they
are active.

The current expensive boundary is the main-thread snapshot handoff. The
rendering/world thread owns the authoritative `World`; workers receive owned
immutable snapshots and must never receive a `World *` or a live chunk pointer.
`World::update_around()` currently holds the world write lock while it drains,
captures, and queues work. That lock scope must eventually be reduced to the
short authoritative mutation/index-update phase. Snapshot capture must remain
complete before publication to a worker, so removing the lock cannot mean
sharing live chunk storage.

The lighting halo is 15 blocks wide. Before the latest optimization, every
remesh copied all blocks from up to eight initialized neighbor chunks even
though only edge slabs were consumed. The Libft `game_voxel_chunk::copy_region`
API and Minecraft snapshot capture now copy only the required edge/corner
slabs. The target chunk remains a full owned copy. This is a data-volume
optimization, not a synchronization shortcut.

The intended final boundary is:

```text
authoritative server world
    -> bounded owned edit/light delta queue
    -> client replica apply budget
    -> immutable remesh/light snapshot
    -> worker result queue
    -> bounded main-thread mesh publication
    -> renderer-owned mesh/GPU data
```

Network transport, delta encoding, hash repair, and server validation must not
pass the full `World` to a transport worker. The server side should receive a
validated command or a small snapshot request, execute world mutation on the
authoritative owner, and publish owned deltas. The client should apply block
deltas first, enqueue lighting/remesh work second, and never wait for lighting
or mesh publication before acknowledging the logical block state. Add explicit
per-stage counters for bytes copied, queue wait time, delta application time,
snapshot capture time, and mesh publication time before moving more work across
the process boundary.

### Startup scheduling correction

Chunk arrival and regeneration invalidation must not enter the same front
priority queue used for interactive player edits. During startup, every newly
published chunk can dirty its neighbours; treating all of those relights as
interactive work can fill the remesh window, starve generation, and leave the
renderer waiting on a world that is still only partially built. Arrival work is
therefore kept in the normal dirty-remesh scan, while an explicit block edit
continues to use the front priority queue and immediate submission cadence.
Visible dirty chunks are appended to the back of the priority queue rather than
using the explicit front-priority operation. This prevents a visible arrival
scan from repeatedly pushing itself ahead of a player edit or its chunk-border
neighbours. Neighbor snapshot capture is also limited to the edited target on
the immediate edit path; dirty neighbours are captured by the persistent
worker scheduler one at a time.

The persistent generation workers must also share work fairly: when both
generation and ordinary remesh requests are queued, a bounded number of
generation requests is selected before another ordinary remesh. This prevents
initial world construction from being consumed by lighting work without
removing the asynchronous lighting path. The async-generation validator now
also performs four consecutive break/place cycles on the same block while
startup generation is still active and requires the playable area to continue
converging. This exercises invalidation coalescing and repeated priority
requeueing instead of only testing one edit. It records loaded-chunk progress,
remesh queue peak, and the first visible mesh frame.
These measurements are now available in the validator output. The validator
now enforces a 1,200-frame startup budget and emits an async-worldgen-metrics
JSON line containing frames, loaded chunks, snapshot bytes, scanned/propagated
cells, light queue peak, completed remeshes, stale results, and first-visible-
mesh frame. A CI wrapper can retain this line as an artifact without parsing
human-readable diagnostics.

## Continuation checkpoint: 2026-09-08

Current branch checkpoints:

- Minecraft `agent/analytics-performance`: `98fa312787233e1894de91f3009e36f6204f680a`.
- The Minecraft `Libft` gitlink resolves to
  `33f8fdfb7fd987b4b34f78c2d047b2ca7311cfb`.
- Libft `agent/compression-analytics-cardgame-scripting` is checked out at
  `33f8fdfb7fd987b4b34f78c2d047b2ca7311cfb`.

Implemented in these checkpoints:

- Remesh meshing can query completed lighting in world coordinates. Chunk-edge
  face samples no longer wrap back into the target chunk, fixing a direct
  cause of black side faces. The packed-light vertex stride remains 16 bytes,
  with focused tests for cardinal edge samples, emissive faces, and greedy
  light boundaries.
- Surface water no longer reshapes terrain to a fixed deep bed. Runtime river
  and lake depth limits are serialized, validated, and included in the
  configuration signature. Water is emitted after terrain/caves and before
  decoration; underground water checks headroom, roof bounds, and configured
  depth. The connected-water regression's post-loop out-of-bounds index was
  removed.
- The camera validator exercises ordinary pitch clamping, ray targeting,
  deletion, receiving-face selection, collision validation, and placement.
- `--validate-block-edit-performance` records repeated edit latency, world
  drain time, scanned plus propagated light nodes, snapshot bytes, queue peak,
  dirty remesh count, stale results, and completed remeshes in JSONL.
- Analytics diagnostics use Basic's `FT_UINT64_DECIMAL_FORMAT` for portable
  Linux/Windows formatting.

The focused async-startup validator now passes at frame 210 with 13 playable
chunks. A generation-queue reservation and a startup gate prevent arrival
relights from consuming the shared queue before the playable ring exists.
The repeated-break workload also completes all 64 edits after extending its
observation window to 65,536 frames. Its latest analytics result was:

```text
frames=7190 edits=64 p50=137284us p95=162049us p99=172337us
light_nodes=343305110 snapshot_bytes=641077248 stale=252
```

The full analytics `--validate-all` run and workload still expose unresolved
performance work:

1. The aggregate run remains expensive because visible-distance and startup
   validation share the same two-worker remesh window. Priority submissions
   can return `FT_ERR_BUSY` while background work drains, even though focused
   startup now converges.
2. A 64-edit run completes, but p99 edit-to-mesh latency is approximately
   172 ms and the workload copies approximately 641 MB of snapshots. The
   current solver still scans large halo regions and publishes whole-chunk
   meshes; this is measured performance work to fix, not a reason to lower
   the workload.

The next implementation must preserve immutable snapshots and revision
guards while adding explicit playable-ring generation reservation, a
persistent per-frame lighting budget that cannot be starved, dirty-section or
bounded-region mesh coalescing, and bounded main-thread mesh publication.
Re-run the JSONL workload and full validator after each scheduler change. Do
not mark the design complete until both failures are resolved and matched
normal/analytics measurements are recorded.

### State reconciliation after the latest branch verification

The earlier Windows-oriented notes in this document are historical handoff
evidence and must not be read as proof that the current Linux checkout has
validated Windows. The current checkout was verified as follows:

- Minecraft is on `agent/analytics-performance` at
  `98fa312787233e1894de91f3009e36f6204f680a`.
- Its `Libft` gitlink resolves to
  `33f8fdfb7fd987b4b34f78c2d047b2ca7311cfb9`.
- Libft is on `agent/compression-analytics-cardgame-scripting` at that same
  commit, with no tracked changes.
- The only Minecraft working-tree item is the pre-existing untracked
  `ft_vox.rsp`; it is not part of this implementation and must not be folded
  into a feature commit.
- The redirected Linux analytics build produced a fresh
  `/tmp/ft_vox_analytics`. This confirms that the current source and
  submodule pointer link together, but it is not Windows executable
  validation.

The compact Linux aggregate log at
`/tmp/validate-all-after-startup-fix.log` reports
`validate-all: passed failures=0`. It also reports:

```text
block-edit repeated: p50=115 ms p95=137 ms p99=137 ms max=138 ms
async-worldgen: ok frame=126 initial_loaded=2 final_loaded=13
first_visible_mesh_frame=62
```

The dedicated analytics workload is the more demanding repeated-edit sample
and remains the performance gate for the unresolved scheduler work. Its latest
result is:

```text
frames=7190 edits=64
p50=137284 us p95=162049 us p99=172337 us
light_nodes=343305110 snapshot_bytes=641077248
queue_peak=392705 dirty_peak=13 stale_results=252 remesh_completed=391
```

These results are not contradictory: the aggregate validator performs a
shorter 16-sample workload, while the dedicated analytics validator performs
64 repeated edits and exposes queue accumulation and large snapshot copies.
The aggregate pass proves that the current committed state is runnable; it
does not close the incremental-lighting performance requirement.

The next Windows implementation pass must reproduce both workloads with a
freshly compiled normal executable and analytics executable, recording the
branch, Minecraft commit, Libft commit, build variant, and configuration in
each report. Compare normal and analytics runs separately before attributing
latency to instrumentation. Keep the current high p99, snapshot total, queue
peak, and stale-result count as the baseline; do not replace them with the
shorter aggregate numbers.

### Current Windows performance gates

The validators now use explicit acceptance limits in addition to their long
watchdog deadlines. The watchdog prevents a deadlock from hanging a run; the
acceptance limits fail a run that is responsive but too slow:

- repeated block edits: p95 <= 100 ms, p99 <= 250 ms, fewer than 4,000,000
  lighting nodes per completed edit, no more than 32 MiB of captured snapshots,
  and no more than 16 dirty remeshes in one frame;
- asynchronous world generation: every `update_around()` call must stay below
  100 ms and startup must reach its playable result within 240 frames;
- renderer publication: a GPU-visible block edit must be published within
  500 ms. The tighter repeated-edit percentile gate remains the CPU and
  lighting performance gate; this single-publication check includes graphics
  driver synchronization and is intentionally separate.

The boundary-edit validator now models the actual dependency graph: an edit on
the east face must update the edited chunk and the east neighbor, while the
west, north, and south neighbors must remain stable. Requiring all four
neighbors to rebuild made the test reward redundant full-halo lighting work
and falsely rejected the face-local invalidation path.

Latest normal validation evidence after these changes:

```text
validate-all: passed failures=0
block-edit repeated: p50=47 ms p95=76 ms p99=76 ms max=92 ms
renderer-publication: edit_latency_ms=355
async-worldgen: frame=31 max_update_us=14265 first_visible_mesh_frame=9
```

The dedicated workload still reports roughly 541k scanned lighting cells per
edit. That is the remaining major cost: lighting is performed off the render
thread, but each edit still rebuilds a full halo. The next optimization should
be an incremental bounded light invalidation operation, not larger worker
slices. A larger interactive slice was tested and did not improve latency; it
increased total work and was reverted.

### Interim mesh publication

Remesh requests now capture the target chunk's last usable packed-light values
alongside the immutable block snapshot and capture a matching target-plus-
neighbor light halo. The worker's geometry-only stage may use those values to
generate a lit interim mesh while the new lighting solve continues, but only
when every required halo source has a usable/current buffer. Missing or stale
neighbor light disables interim publication; it cannot publish a zero-filled
border and later replace it with the real result. The request remains pending,
so the final light-aware mesh still replaces the interim mesh and advances the
light revision. This keeps a broken/placed block visually responsive without
publishing the black unlit geometry that caused the earlier surface flicker.

This is intentionally not treated as lighting convergence: the interim mesh
may contain stale light near the edit for a short period. The final mesh and
the boundary-light validator remain responsible for proving that the light
revision eventually matches the authoritative block revision.

The previous light buffer is deliberately not required to match the newly
edited content before it can seed work. An edit necessarily makes the current
content version newer than the computed light version; rejecting that previous
buffer would force every interior edit into a full halo rebuild. The snapshot
therefore distinguishes `light_buffer_is_valid` from `light_is_current`:
incremental propagation and interim geometry may use the former, while final
publication requires the latter versions to be produced by the new result.
## Latest analytics block-edit checkpoint

The latest full analytics validation completed with zero validator failures,
but its diagnostics still show why interactive block edits can stutter. The
64-edit workload processed approximately 76.9 million light nodes and scanned
about 541,778 cells for an individual edit. It recorded 137 stale remesh
results, meaning substantial worker time was spent producing results that were
already obsolete when they reached the commit stage. The p99 edit latency was
148 ms and the first edit reached 156 ms in the full validation run.

The geometry-only publication path is now useful for responsiveness: it
publishes a mesh using the chunk's last valid lighting while the final light
solve continues asynchronously. The final light-aware result still replaces
that interim mesh before the request is cleared, so this does not weaken light
correctness.

The current block-edit commands still clear the chunk-side pending request
marker before forcing an interactive remesh submission. That preserves the
existing responsiveness behavior, but it also permits an edit burst to queue
another full capture while the earlier request is still in flight. The
scheduler then invalidates obsolete captured revisions, so the next required
optimization is a cancellation/coalescing design that can stop obsolete light
work safely and requeue exactly one newest-state request without leaving
pending ownership markers behind.

The earlier global remesh-epoch cancellation experiment was rejected during
validation because it caused real remesh timeouts and could leave requests
without a valid ownership handoff. The current implementation uses a
per-chunk cancellation generation instead. It is checked at request start and
after each bounded light slice; a canceled result releases its worker slot,
and the committer clears ownership only when the request ID still owns the
chunk. Active, queued, captured, geometry-only, and final-light supersession
must remain covered by the cancellation tests.

The bounded incremental lighting solver is now implemented for the safe
interior-edit case. Merely increasing worker slices would not reduce the
approximately 541k-cell scan and would increase contention with rendering.
The remaining performance work is a coordinated cross-chunk frontier for
border edits, plus differential coverage for source removal/addition and
skylight changes. The full solver remains the correctness fallback until that
frontier can update every affected chunk from one immutable snapshot.

The latest isolated analytics performance run passed its current gate with
p50=46.1 ms, p95=76.0 ms, p99=136.9 ms, 77.5 million light nodes, and 139
stale results. The normal debug run is more variable on this workstation and
has exceeded the 100 ms p95 gate in some runs, so both build variants still
need repeated samples before the budget can be considered stable.

### Queued remesh coalescing checkpoint

The pipeline now replaces an older queued remesh for the same chunk before it
is handed to a worker. Active requests are not forcibly canceled; their
existing revision checks remain authoritative. This avoids the unsafe
ownership handoff seen in the rejected global cancellation experiment while
removing duplicate queued full-light solves. The capture queue applies the
same same-chunk replacement rule.

After this change, the isolated analytics workload passed with p50=32.2 ms,
p95=63.0 ms, p99=140.3 ms, and 136 stale results. Analytics boundary-light
convergence and renderer-publication validation also passed. The normal
block-edit convergence validator passed with p50=45 ms, p95=62 ms, p99=62
ms, and max=63 ms. A normal debug performance run still exceeded the strict
100 ms p95 gate on this workstation (p95=161.7 ms), so the incremental-light
solver remains required rather than hiding that variance by loosening the
gate.

## Incremental lighting solver handoff

The next solver change must preserve the current full rebuild as a reference
implementation. It must not replace that path until differential tests prove
identical packed-light values for the affected chunk and every face-sharing
neighbour.

Each light request needs the source voxel and light revisions, the changed
block coordinate and old/new metadata, the change classification, an
immutable block lookup, previous packed-light boundary values, and explicit
request ownership state. The full rebuild remains the fallback for mixed
changes, unknown provenance, and frontiers that exceed their configured
maximum.

The frontier algorithm must seed the changed cell and its six neighbours,
recompute from current block metadata plus six-neighbour values, propagate
higher values, and use a separate removal/re-seeding path for lower values.
A max-only propagation queue is not sufficient because it leaves stale light
after placing an opaque block or removing an emissive source. Face crossings
read immutable neighbour borders and create neighbour work only when a packed
value changes. The frontier and visited state must survive bounded worker
slices instead of being rebuilt on every slice.

The additive fast path is valid only when the old block has no emitted light
and the edit cannot remove an existing source. All other edits use the
removal-capable path or the full rebuild fallback. This explicit classification
is required for torches, transparent blocks, and skylight shafts.

Geometry-only publication continues to use last-valid light so an edit becomes
visible quickly. Incremental light results publish only after completion and
revision validation. A new edit may supersede queued work, but active work
must either finish or return an explicit canceled result that releases its
worker slot and ownership marker exactly once; global epoch cancellation is
not sufficient.

Required differential tests cover surface openings, opaque placements, source
addition/removal, all chunk faces and corners, skylight columns, rapid edits,
queued and active supersession, multi-slice completion, stale-result rejection,
exact full-rebuild comparison, and failure cleanup. Performance reporting
must separate incremental nodes, removal nodes, frontier peak, fallback rebuild
count, stale results, and publication latency.

### Additive interior deletion checkpoint

The first bounded solver slice is now implemented for the narrow safe case of
deleting a non-emissive block strictly inside a chunk. It copies the last
valid packed-light state, seeds the changed cell and its six neighbours, and
only raises values that can be reached through the newly opened cell. The
worker then rebuilds the final mesh with the incremental light result. The
current fast paths cover non-edge interior edits when their previous light
buffer is usable. The full solver remains mandatory for edits touching a chunk
face, missing or stale dependencies, and changes that require source-aware
light removal or addition beyond the narrow incremental contract; those cases
can affect a neighbouring chunk or require light removal.

Emissive source removal and ordinary block deletion now reseed direct
skylight when the newly transparent cell has an unobstructed column to the
top of the chunk. This matters even when the old packed value contained only
block light: an opaque source can have hidden a skylight column. If the
column cannot be proven locally, the full reference solver remains the safe
fallback. The interior emissive regression verifies that placing and removing
a source restores the original packed light instead of leaving the cell
black.

The boundary validator now records the complete edit-to-convergence baseline
for the future multi-chunk frontier. The current Windows run measured 78 ms
for both border source placement and removal while using the safe full-solver
fallback.

Non-emissive deletion at a chunk edge now has a narrower optimization: the
incremental additive solver imports the opposing-neighbour light from the
immutable halo, while the affected neighbour still receives its own safe
remesh. The boundary regression verifies this path and restores the original
block afterward. Emissive placement/removal and opaque-light removal remain
on the full solver until their cross-chunk removal frontier is implemented.

The follow-up validity fix separates a usable previous light buffer from a
light buffer that is current for the newly edited content. An edit is expected
to make the latter false, so using `light_is_current()` as the incremental
solver admission test incorrectly forced ordinary interior edits through the
full rebuild. The worker now accepts a valid previous buffer as the seed for
incremental work, while final commit still requires the captured content and
light-input versions to match. Captured halo light is also rejected for
interim geometry when a required neighbor is missing or stale.

Mesh dirtiness and light-input validity are intentionally separate. A chunk
can need a new mesh because neighbouring geometry changed face visibility while
its own light buffer remains valid. Geometry-only notifications therefore do
not advance `light_input_version`; direct edits and genuine boundary-light
dependencies explicitly do. This preserves the last valid light during
remesh scheduling and prevents black interim chunks plus redundant full-light
rebuilds caused only by a mesh-dirty notification.

The implementation also corrected two lifecycle hazards in this path: the
incremental request no longer dereferences an unallocated full-solver
operation, and out-of-range frontier nodes are rejected before light storage
is accessed. Mesh light lookups outside the edited chunk fall back to the
captured neighbouring light data.

Validation on the current workstation observed seven to eight processed
frontier nodes for the repeated interior deletion workload. The normal
block-edit performance validator now measures delete latency separately from
the restore placement used to prepare the next sample; it reports p50=46.9
ms, p95=68.4 ms, and p99=218.3 ms in the latest run. The p99 remains close
enough to the strict 250 ms ceiling that repeated normal and analytics runs
are still required. This is not yet evidence that border edits or emissive
edits are solved.

An attempted optimization that admitted chunk-edge coordinates into this
target-only incremental solver was rejected by the boundary regression: after
removing a border light source, the edited chunk could remain dark while the
neighbour restored correctly. The interior-coordinate guard therefore stays
in place. A safe edge optimization must use one consistent snapshot and a
coordinated cross-chunk frontier that updates both light buffers; it must not
be replaced with a target-only incremental result.

The temporary per-edit worker diagnostic was removed after validation because
writing one line for every deletion would itself distort analytics-mode frame
times. Detailed counts must be collected through the existing counters and
exported off the gameplay path instead.

The Windows focused validation after this correction reports p50=31 ms,
p95=43 ms, p99=43 ms, and max=47 ms for the repeated edit workload. The full
validation reports zero failures, approximately 541,696 scanned cells for its
startup remesh workload, one completed remesh, and no black-border or stale
light convergence failure. This is evidence that valid prior light is now
being reused; it is not evidence that the full-halo edge fallback has been
eliminated.

The stream diagnostics now distinguish final incremental-light commits,
final full-light commits, geometry-only interim commits, and canceled
requests. These counters must be read after the relevant worker drain: the
startup validator intentionally stops after the first playable mesh so a zero
final-light count at that point is not a claim that lighting was unnecessary.
The light-convergence validator remains responsible for proving that the
playable ring eventually reaches current light versions.

### Active-remesh cancellation checkpoint

Each loaded chunk now owns a shared cancellation generation. A new edit bumps
that generation before submitting its replacement request, and the captured
generation travels with the immutable remesh request. The worker checks it at
the start of a request and after each bounded lighting slice. A superseded
request returns `FT_ERR_INVALID_STATE` and releases its remesh slot; the
result committer only clears a pending marker when the request ID still owns
that marker. This prevents an old cancellation acknowledgement from clearing
the newer edit.

This is deliberately per-chunk rather than a global pipeline epoch: unrelated
chunks continue working, while an active solve for the edited chunk stops at
the next safe slice. Queued same-chunk coalescing remains in place.

The block-edit validator now includes that burst test: six immediate
place/delete pairs are submitted without waiting between them, then the
edited region is allowed to converge once. It requires the final block state
to be air, the light version to be current, and both the pending request and
dirty-mesh flags to be clear. The current full validation passed this test,
including request-ownership cleanup.
## Versioned light validity and neighbor invalidation

Each live chunk now needs three distinct 16-bit version values. The block
content version starts at `1` and advances for every committed block or
generation mutation. The light-input version also starts at `1` and advances
when the chunk changes or a neighboring border change can affect its light.
The computed light version starts at `0`, which is an invalid/uncomputed
sentinel. A light result is publishable only when its captured content version
and light-input version still equal the live chunk values. On final light
commit, the computed light version is set to the captured content version and
the computed-input version is set to the captured input version. Geometry-only
meshes never change those values and must reuse the last valid light data.

Versions are advanced with a helper that skips zero after `0xffff`; validity
uses equality rather than ordinary signed comparisons, so rollover cannot
make an old result appear newer. The half-range rule still applies to any
future ordering API: no producer may keep an uncommitted comparison window of
`0x8000` or more versions.

When a block edit touches a chunk border, the edited chunk and every affected
cardinal neighbor are marked dirty. Neighbor invalidation advances only the
light-input version and is coalesced while an equivalent dirty request is
already pending. A remesh request captures both versions. Capture, worker
completion, and final commit all reject mismatches. This prevents a light
calculation based on an old neighbor border from replacing current lighting.

The black-flicker invariant is strict: no failed, canceled, stale, or
geometry-only result may replace the committed light buffer with a zero or
partial buffer. A stale result releases its request ownership and leaves the
last valid mesh/light pair available for the next prioritized request.

Required validation includes initial `content=1/light=0`, final convergence,
`0xffff -> 1` rollover, stale local result rejection, stale neighbor-border
result rejection, repeated edits coalescing to one current request, and
asserting that no zero-light buffer is published during geometry-only stages.

## Frontier scheduling for border edits

An edge edit does not automatically justify remeshing every neighboring
chunk. The invalidation path reads the exact opposing edge voxel at the same
Y coordinate. A known solid/opaque voxel blocks both the visible-face change
and light propagation, so that neighbor can remain untouched. Air, water,
unknown/unloaded, or unreadable state is treated as potentially affected and
keeps the conservative remesh path. This check is an optimization only; it
must never be used to skip a neighbor when its state cannot be read reliably.

The lighting solver itself follows a frontier walk: seed the changed voxel
and its six neighbors, inspect only queued nodes, and enqueue a node only when
its light or opacity changed. Full-chunk lighting remains the fallback for
initial generation, invalid/missing light state, and edits whose border
dependencies are unavailable. Tests must compare opaque-neighbor and
transparent-neighbor edits, assert the opaque neighbor receives no remesh
request, assert transparent/water neighbors do receive one, and verify that
the resulting light values still converge across the border.

## Locking rule for the frontier

The exact-neighbor tests and the edit's dirty-mark batch must not lock and
unlock the world mutex once per voxel. The caller should hold the existing
world write ownership for the authoritative edit and its bounded invalidation
batch, perform the opposing-edge reads from that protected in-memory state,
and release the lock before worker scheduling or snapshot construction. The
worker receives immutable snapshot data and performs the frontier walk without
touching the live world or reacquiring its mutex. Any future batching path
must preserve this rule: one meaningful protected state transition, then
unlocked computation and queueing.

## Runtime menu-return and edit-latency continuation: 2026-09-10

The application previously collapsed two unrelated events into the same
transition: an intentional BACK input and a failed world-stream update both
called `session.stop()` and returned to the main menu without preserving the
reason. `GameSession` and `ApplicationPhaseController` now emit a diagnostic
only when one of those transitions occurs. A stream failure reports the
returned error, the stream's last worker error, pending/retryable/failed
counts, interactive-remesh depth, and oldest remesh age. An explicit BACK
transition is reported separately. This is diagnostic-only and is compiled
out of release builds.

The edit commands now insert the edited chunk into the interactive priority
queue before submitting its remesh capture. This ensures the initial request
uses the interactive light budget rather than being captured as background
work. Repeated per-edit `stderr` tracing is sampled once per 32 voxel
revisions so analytics/debug output cannot become the measured frame-time
workload. Interaction return codes are also reported when an edit/pick/place
operation actually fails instead of being discarded.

Windows verification after this pass:

- `make -j2 automated_tests.exe` rebuilt both `ft_vox.exe` and
  `ft_vox_analytics.exe` successfully.
- `automated_tests.exe --validate-block-edit` passed, including repeated
  edits, rapid supersession, emissive-light restoration, and border edits.
- The focused workload currently measures approximately 30--47 ms for
  edit-to-remesh publication. This is improved/bounded behavior, but it is
  not yet proof of the desired sub-frame target on every machine.
- The remaining high-cost path is still the worker's whole-chunk mesh
  replacement and cross-chunk light frontier; a section-local mesh API and a
  differential border-light solver are still required before calling the
  latency objective complete.

Recoverable positive stream results no longer tear down the session. Loading
and gameplay retry those results on the next tick; only negative stream
failures transition to the main menu. The aggregate validator passed after
this change with zero failures. Its repeated-edit workload reported p50=30
ms, p95=31 ms, p99=31 ms, and max=31 ms. This confirms the recovery policy
does not bypass the mesh/light convergence checks, but a real runtime session
is still needed to capture whether a menu return is an explicit Back input or
a fatal negative stream error.

### Geometry staging correction: 2026-09-10

The remesh worker no longer waits for every captured neighbour light buffer to
be current before publishing the first replacement mesh. If the edited chunk
has a usable previous light buffer, the worker publishes geometry using the
captured previous target and halo values. The existing light remains installed
on the live chunk while the bounded incremental frontier computes the new
values. A completed result still has to pass the captured content and
light-input revision checks before replacing the live light buffer.

This interim geometry stage is restricted to interactive edit requests. A
background neighbour-arrival or ordinary full-light remesh now builds and
publishes only its completed light-aware mesh; otherwise it would perform two
whole mesh builds and two GPU-visible revisions for work that was not caused
by a player edit.

This prevents a block removal from visibly switching to a zero/partial light
buffer while an unrelated neighbour is waiting for its own lighting work. The
incremental/full diagnostic flag is also assigned after validity fallback so
completion counters describe the path actually executed.

Verification after this correction:

- `automated_tests.exe --validate-block-edit` passed, including rapid
  supersession, emissive restoration, and border edits.
- `automated_tests.exe --validate-all` passed with zero failures.
- The focused edit workload remained approximately 14--127 ms in the local
  validator, with the repeated sample p95 near 31 ms.

This is a staging correction, not completion of the cross-chunk frontier.
The current incremental solver still owns one target light buffer at a time;
face-crossing removals and source changes therefore need coordinated
neighbour requests or the reference solver. A future implementation must
update all affected chunk light buffers from one immutable revision and commit
them transactionally. It must also retain the section-local mesh work as a
separate optimization, because whole-chunk mesh replacement can remain a
visible cost even after light propagation becomes incremental.

### Startup edit lane reservation: 2026-09-10

The persistent world-generation pipeline now reserves one worker for remesh
work whenever more than one worker exists. Generation and regeneration may
use at most `worker_count - 1` active workers, while an interactive remesh can
be selected from the front-prioritized queue without waiting for every active
chunk-generation request to finish. On single-worker machines the limit
necessarily remains one; a separate dedicated worker would be required to
provide the same guarantee there.

This addresses the long delay when a player edits during initial world
generation: queue priority alone cannot preempt a generation task that is
already running. The reservation is persistent and does not create or join a
thread per edit. The aggregate validator still completed with zero failures;
the async-generation probe reported active generation work while its startup
edit was accepted and the first visible mesh arrived at frame 6.

For a non-emissive deletion on a chunk face, the opposing neighbour is now
also eligible for the additive frontier path. It seeds from the immutable
opposite-border light and only raises values through newly opened cells. The
opposing neighbour is also given the removal frontier for emissive-source
removal and opaque placement. The removal walk is now valid at an edge cell;
it clears and reconstructs the affected face cell from the neighbour's own
light field, while the source chunk performs the corresponding local walk.
This avoids the previous whole-chunk fallback for the common single-face
case. Edits with multiple interacting sources, unavailable neighbours, or
simultaneous changes in both chunks still require the coordinated
cross-chunk transaction described below; independent requests must not be
treated as proof that arbitrary multi-source propagation is solved.
## Authoritative edit classification correction: 2026-09-10

The server-authoritative block-change path was not using the same incremental
lighting classification as the local delete/place commands.  It marked the
chunk dirty and queued a default remesh, which could discard the existing
light field and schedule a full rebuild after an accepted network edit.

`World::apply_authoritative_block_change()` now captures the previous block
and light value before applying the request, classifies safe deletion and
placement cases, and passes those flags through both the edited-chunk queue
and the border-neighbour priority queue.  The fallback only promotes the
edited chunk when the immediate queue submission cannot reserve a slot; it no
longer repeats the border-priority call.

This keeps the authoritative and local edit paths consistent, but it does not
claim that every case is safe for incremental propagation.  Emissive-source
removal, opaque border changes, and any edit whose dependency crosses an
unloaded neighbour remain conservative/full-remesh cases until the coordinated
cross-chunk solver is implemented.

The authoritative path also cancels capture/solve work for the superseded
voxel revision before submitting the replacement request.  This is important
for latency: stale work must be rejected for correctness, but it should also
stop consuming the remesh worker while an accepted player edit is waiting.

Pending priority entries now also remember when incremental seeds were
coalesced from distinct edits.  A remesh request now carries the complete
coalesced seed list and applies each bounded frontier to one copied light
field.  Duplicate seeds are removed, and allocation failure falls back to the
complete solver.  This prevents rapid multi-edit bursts from producing
apparently valid but incomplete light results without forcing a full solve for
every correctly representable seed set.

The new vector ownership transfers are guarded because the surrounding queue,
capture worker, and request-builder APIs are `noexcept`.  Queue copies return
`FT_ERR_NO_MEMORY`, capture tasks move their owned seed list, and a priority
allocation failure clears the incremental mode so the request is rebuilt by
the safe complete solver rather than terminating the process.

Required regression coverage:

* apply an accepted authoritative deletion and verify it queues the same
  incremental mode as a local deletion;
* apply an accepted authoritative placement and verify the old light field is
  retained until the replacement result is committed;
* reject a stale request and verify no remesh or light invalidation is queued;
* test an authoritative edit on every four horizontal chunk borders;
* verify a failed/stale result cannot replace the current mesh or light field;
* measure edit-to-visible-mesh latency separately for local and authoritative
  edits, with the same strict percentile thresholds.

### Cross-chunk light dependency gate: 2026-09-10

Face-sharing neighbours now retain the source chunk coordinate that caused
their light input to become dirty. A neighbour remesh cannot capture a
lighting snapshot while that source has a pending remesh or while its
computed light input version is stale. This prevents a neighbour from solving
against the source chunk's pre-edit border light and then being replaced by a
second correction solve, which was the source of repeated border flicker.

The gate does not hold a mutex or spin. The neighbour remains dirty and is
retried by the normal bounded scheduler after the source result commits. In
debug and analytics builds, periodic diagnostics identify the target/source
pair and the source pending request when the gate defers work. The production
path has no logging or extra allocation.

This is still not the final cross-chunk frontier transaction: the neighbour
currently performs its own bounded solve after the source is current. The next
step is to publish a source-border frontier and consume it incrementally in
the neighbour, while retaining this dependency gate as the ordering rule.

### Border-remesh correction: 2026-09-10

The first multi-seed border implementation exposed an unsafe assumption: the
edited chunk's local light seed cannot be copied into a face-sharing neighbour.
In the neighbour request, that coordinate is an ordinary existing border cell,
not the block that changed. Treating it as the changed cell clears or raises
the wrong light value and can produce a dark/flickering border.

Border neighbours are therefore still marked dirty when their transparent edge
can observe the edit, but they are no longer promoted into the immediate
interactive queue and no longer receive the edited chunk's local seed. The
edited chunk alone receives the bounded interactive frontier. Neighbours are
processed by the normal background scheduler from a fresh snapshot, so their
border lighting converges without competing with the edit or mutating an
unrelated cell. This is intentionally a correctness/performance containment
step, not the final cross-chunk frontier algorithm.

The final implementation should replace this conservative neighbour path with
a coordinated source-to-neighbour frontier transaction. That transaction must
carry the source chunk coordinate, changed block transition, source light
revision, and target border seed; it must never reinterpret a target border
cell as the edited block. Until then, a full neighbour solve is preferable to
an incorrect incremental solve, but it must remain outside the interactive
edit budget.

Verification after the correction:

* `automated_tests.exe --validate-block-edit` passed;
* border placement/deletion convergence passed;
* border place/delete latency improved locally from approximately 62 ms to
  48/46 ms;
* repeated edit validation passed with p95 32 ms and no stale remesh result
  accepted.

### Border additive frontier re-enabled after dependency ordering: 2026-09-10

The conservative neighbour-only scheduling described above is superseded for
the safe non-emissive deletion case. Once the dependency gate confirms that
the source chunk's light is current, the face-sharing neighbour receives an
additive seed at its actual border coordinate. Its existing light field is
preserved and the frontier propagates outward from that cell; the neighbour
does not clear or reinterpret the border cell as the edited source block.

Opaque placement and emissive-source removal still use the conservative
background path because they require cross-chunk light removal and competing
source reconstruction. Those cases must not be labelled incremental until a
source-aware removal frontier is implemented.

### Source-aware cross-chunk frontier: 2026-09-11

The cross-chunk removal path is now source-aware for horizontal face edits.
The edit seed carries the source block's pre-edit packed light and explicitly
marks neighbour seeds as external-source seeds. The neighbour worker derives
the post-edit source light from the dependency-ordered snapshot, computes the
old and new contribution through the target border cell, and updates only the
removal/addition frontier. It does not treat the neighbour's border cell as
the edited block, clear the neighbour chunk, or apply the source block's
emission directly to the neighbour.

The old contribution is removed from the frontier before the new contribution
is reintroduced. This ordering is required for transitions such as an
emissive block becoming air: otherwise an equal light value can be mistaken
for an unrelated source and leave a stale block-light channel behind. Existing
equal-or-stronger values farther from the frontier are still preserved and
repropagated by the normal competing-source logic.

The dependency gate remains mandatory: a neighbour seed cannot be captured
while the source chunk has a pending remesh or stale computed light. This
prevents a correct frontier from being calculated against the pre-edit source
state and then visibly flickering when a second correction arrives.

Verification:

* `automated_tests.exe --validate-block-edit` passed after place/delete at a
  chunk edge, including exact restoration of the target and neighbour light;
* the full `automated_tests.exe --validate-all` run passed with zero failures;
* the focused Windows run measured approximately 90--93 ms for the boundary
  place/delete validation and did not produce the former packed-light value
  corruption (`15 -> 239`).

Remaining limitation: simultaneous edits on multiple faces or several
competing external sources can still require multiple bounded frontier jobs.
Those jobs must remain revision-gated and must never fall back to discarding a
valid light buffer merely to obtain a faster mesh publication.

### Worker ownership and minimum-input contract: 2026-09-11

The lighting and remesh computation must not receive `World`,
`WorldChunkStreamer`, a live `WorldChunk` pointer, a renderer object, or a
world mutex.  The worker receives only the data required to calculate its
result:

* one owned `WorldChunkSnapshot` containing the target block data, the light
  input data, and only the border/halo data required by the selected solve;
* one owned target buffer (`game_voxel_chunk` and, when required, the current
  light buffer);
* scalar chunk coordinates, revisions, cancellation values, light
  configuration, and the compact incremental seed list.

The worker returns owned light and mesh results.  It does not publish them or
look up anything through the live world.  The main/streaming boundary validates
the request and commits the result only when its revisions still match.

This distinction is important because the remesh capture thread currently
does have a separate responsibility: it obtains a short-lived read snapshot
from `World`, then releases the world lock before submitting the owned request
to the long-lived compute workers.  It must never perform lighting, mesh
generation, or callbacks while holding that lock.  The compute worker must
remain usable with a fake request in a unit test and must not depend on a live
world at all.

The minimum-data rule is therefore:

```
live World --short read/capture--> owned request --worker compute--> owned result
                                                                    |
                                             main-thread revision check/commit
```

Do not solve a missing input by passing the whole world into a worker.  Add a
small immutable field to the snapshot or seed instead.  In particular, a
cross-chunk light update should carry the source coordinate, old source light,
new source light, source/target revisions, and the affected frontier—not a
world reference.  If a future calculation needs more data, extend the owned
snapshot contract and measure its copy size rather than introducing a mutex
lookup in the hot path.

The required regression checks are:

* source inspection or a build-time architecture check must reject live-world
  references from `WorldChunkGenerationWorker`;
* a worker test must run a remesh request after the source `World` has been
  destroyed or is unavailable, proving the request is self-contained;
* instrumentation must show zero world-lock acquisitions during the compute
  portion of a remesh;
* snapshot capture duration and byte count must be recorded separately from
  worker light/mesh duration, so a slow capture is not mistaken for a slow
  lighting algorithm.

### Incremental cancellation granularity: 2026-09-11

The custom incremental removal/addition walks previously checked their
cancellation token only after the complete frontier had finished.  A newer
edit could therefore invalidate a request while the worker continued scanning
and propagating the obsolete frontier, inflating stale-result work and making
rapid edits contend with old lighting calculations.

Both propagation queues now check the request token at bounded intervals and
return `FT_ERR_INVALID_STATE` as soon as the request is superseded.  The
worker still owns only the token and the immutable request inputs; it does not
look up the live world to perform the check.  The existing persistent worker
thread is reused, and no thread is created per edit.

This is cancellation responsiveness, not yet resumable frontier execution:
the canceled frontier is discarded and the newest request rebuilds its owned
light result from the latest snapshot.  The remaining performance work is to
make the frontier itself resumable/coalesced without throwing away valid light
state, then prove that the new request does not repeat work already committed
by an earlier compatible request.

Verification:

* `make -j2 automated_tests.exe` passed;
* `automated_tests.exe --validate-block-edit` passed, including rapid
  supersession and all four horizontal border faces;
* focused output remained within the existing local gate, with repeated-edit
  p95 approximately 47 ms in this run.

The post-change aggregate Windows run also passed with zero validator
failures.  It reached 13 loaded chunks from the startup seed, reached its
first visible mesh at frame 24, and kept the maximum update call at 40,731 us.
The rapid block-edit portion measured p95 approximately 31 ms.  This confirms
that the in-loop cancellation checks do not break startup or border
convergence; the dedicated long repeated-edit workload is still required to
measure the amount of obsolete work avoided.

### Atomic surface-water candidates: 2026-09-11

The surface-fluid pass previously validated only the terrain bed and then
wrote water one block at a time.  If a solid block existed partway through the
requested water column, the loop stopped after writing the lower cells.  The
column remained marked as a valid water candidate, so later decoration could
interpret a partially filled pond as a complete one.

The generator now preflights the entire requested column before writing any
water.  Every destination must be air or already-liquid; otherwise the
candidate is rejected and no water is written in that column.  This preserves
the existing deterministic, post-terrain fluid stage and prevents partial
ponds from being exposed to vegetation placement.

Verification in the Libft submodule:

* the new atomic-column regression passed;
* all 30 `test_voxel_generation*` tests passed;
* the change does not add world access or synchronization to Minecraft's
  lighting workers.

### Resumable additive frontier checkpoint: 2026-09-11

The additive frontier now keeps its queue, cursor, seed cursor, completion
state, and processed-cell count inside the owned remesh request. An interactive
request can therefore yield at its configured node budget and be requeued on
the persistent worker without rebuilding the frontier or touching the live
world. Cancellation is checked during the bounded walk, and a superseded
request returns without publishing a partial light result.

This path is deliberately limited to interactive additive edits for now. The
visible-distance validator showed that applying the same slicing policy to
background remeshes while generation was settling accumulated partially
completed requests and delayed light convergence. Background visibility work,
mixed edits, and removal-capable edits continue to use the complete reference
solver until equivalent coalescing and starvation tests are in place.

Verification after that boundary was restored:

* `make -j2 automated_tests.exe` passed and produced normal, analytics, and
  test executables;
* `automated_tests.exe --validate-block-edit` passed, including rapid
  supersession and all four horizontal boundary faces;
* `automated_tests.exe --validate-all` passed with zero failures;
* the aggregate visible-distance check loaded 377 required chunks and reached
  light convergence without a timeout.

The minimum-data rule remains strict. The resumable worker owns only the
immutable chunk snapshot, target/light buffers, scalar revisions and
coordinates, compact light seeds, cancellation token, and its frontier queue.
It must not receive `World`, a live chunk pointer, renderer state, or a world
mutex. Only the short-lived capture stage may read the live world to construct
the snapshot; all lighting and mesh computation happens after that read lock is
released. If a future solver needs another value, add that value to the owned
snapshot or seed record and measure its copy cost rather than handing the
worker a broader object.

### Compact immutable lighting snapshot checkpoint: 2026-09-11

The remesh snapshot previously stored the target chunk twice: once in
`blocks`, and again in the full `lighting_blocks` and
`lighting_existing_light` halo arrays. The worker only needs the target data
from the first arrays; only the surrounding 15-block ring is external input.

The snapshot now stores an offset table for that ring and uses the target
`blocks`/`existing_light` arrays for center lookups. Worker readers still see
the same coordinate-based API, but the request no longer carries duplicated
center columns. This preserves immutable ownership and revision validation
while reducing the copied payload without giving the worker any live-world
access.

Verification:

* `make -j2 automated_tests.exe` passed;
* `automated_tests.exe --validate-block-edit` passed, including emissive and
  four-face border checks;
* `automated_tests.exe --validate-all` passed with zero failures;
* aggregate snapshot accounting fell from approximately 158,482,432 bytes to
  77,909,440 bytes for the same async validation workload, with the new
  accounting using the actual 32-bit and 8-bit element sizes.

This was the payload-reduction baseline before the immutable read-state
handoff below. It remains in the document as historical measurement data; the
asynchronous capture path now acquires published state handles and does not
copy mutable chunks while the world lock is held.

The new analytics baseline makes that target measurable: the latest analytics
`--validate-all --analytics-no-exporter` run recorded 34 successful captures,
`689,620,400` ns total, or approximately 20.3 ms per capture. The validator
still passed, but this cost was large enough to explain edit and streaming
stutter. The immutable read-state implementation below is the follow-up that
removes that lock/copy cost from the worker boundary.

### Immutable chunk read-state handoff checkpoint: 2026-09-11

`WorldChunk` now publishes a compact immutable read state containing only the
chunk coordinates, revisions, generation metadata, block array, and packed
light array. Generation commits, light commits, regeneration, deferred edits,
and the normal place/delete commands publish a new state before queueing
remesh work. The remesh capture task owns shared handles to the target and its
eight neighbors; the capture worker combines those handles into the bounded
lighting snapshot without acquiring `World::world_data_mutex_` or reading a
live `WorldChunk`.

This is the intended minimum-data boundary: the worker receives nine immutable
state handles plus request/revision data, while the main thread retains live
world ownership. The previous live-world capture helper remains only as a
legacy synchronous path and must not be used by the asynchronous remesh
worker.

The focused and aggregate validators pass after this handoff. A previous
failed focused run was traced to normal block commands not publishing the
updated state before queueing; both commands now publish immediately after the
block mutation. Border arrays are stored in the reader's required `y,z` and
`y,x` layouts so chunk-edge geometry is not transposed.

The remaining verification work is an explicit immutable-state equivalence
test for every border and light-halo cell. That test should compare a
live-source capture against a read-state capture before the legacy synchronous
helper is removed. The asynchronous worker path itself no longer depends on
the `World` object or its mutex.

The capture failure path is now queued as a small `{chunk, request, error}`
record. The capture worker never calls back into `World`; the main stream
update drains those records while it already owns the world write lock and
clears the pending request through the explicitly unlocked helper. Reset
discards failure records after the worker has stopped, preventing stale
request IDs from affecting a later world epoch.

Verification after this change:

* `make -j2 automated_tests.exe` passed;
* `automated_tests.exe --validate-block-edit` passed;
* `automated_tests.exe --validate-all` passed with zero failures;
* the aggregate run reached `final_loaded=13` from `initial_loaded=2` and
  reported `stale_capture=0`.

### Solid-neighbor border geometry regression: 2026-09-11

Deleting a block on a chunk boundary has two independent consequences:

* transparent/light-relevant neighbors may need light propagation or removal;
* a solid neighbor still needs a mesh update because the deleted block exposes
  a new face, even though the neighbor's lighting input did not change.

The remesh scheduler now marks every loaded face neighbor dirty for geometry.
It only marks a neighbor's light input dirty when the edge block can affect
light. A solid neighbor is submitted as a geometry-only final remesh, which
uses immutable block/light state and publishes a new mesh without discarding
or recomputing the chunk's light field.

`BlockEditValidator::validate_solid_neighbor_geometry()` now covers this exact
case: it deletes a solid edge block, requires the adjacent chunk mesh revision
to advance, verifies the adjacent solid block is preserved, and restores the
edited block. The current checks passed:

* `automated_tests.exe --validate-block-edit` exited successfully;
* `automated_tests.exe --validate-async-generation` exited successfully with
  `geometry_only=2`, `max_update_us=6167`, and `first_visible_mesh_frame=10`;
* `automated_tests.exe --validate-renderer-publication` exited successfully
  with `edit_latency_ms=337`;
* `automated_tests.exe --validate-all` passed with zero failures.

This does not yet prove every runtime lighting case is correct. The remaining
work is to add equivalent assertions for neighbor arrival/removal and for
multiple edits on the same border before the first remesh is published.

The validator now also runs `validate_rapid_border_supersession()`. It submits
four place/delete pairs on the same horizontal chunk edge without waiting for
the intermediate remeshes, then requires the edited cell and its face-sharing
neighbor to finish with current light, no pending request, and no dirty mesh.
This covers the coalescing/revision path that is most likely to reproduce a
border flicker during rapid player input. The direct `ft_vox.exe
--validate-block-edit` run passes this case.

The aggregate validator still has an intermittent visible-distance timeout on
this Windows checkout after the earlier validators have run. The failure
shows active generation workers and dirty local chunks at the convergence
deadline; the isolated `--validate-visible-distance` run passes. This remains
an open scheduler investigation, not a reason to extend or relax the
convergence timeout.

## Follow-up verification: render readiness and sky restoration

The streamed-chunk publication path now has an explicit
`WorldChunk::light_ready_for_render` state. The synchronous seed chunk sets it
only after its local light build and mesh build complete. A streamed chunk
starts not-ready and is not admitted to the GPU batch until its first
light-aware remesh has committed. A chunk that has never published a ready
light result is also prevented from being downgraded to a geometry-only
remesh. This avoids exposing the zero/invalid halo fallback as a black chunk.

Interactive edits do not clear this flag, so the last valid mesh remains
visible while the new bounded light frontier is solved. The final light-aware
commit sets it and publishes the replacement mesh atomically from the
renderer’s point of view.

Incremental removal no longer restores sky light to a hard-coded level of 15.
It preserves the snapshot’s sky channel and considers the light immediately
above the changed cell as an additional source, applying the replacement
block’s attenuation. This keeps unchanged sky values stable while still
opening a newly exposed sky column.

Current local verification after these changes:

* `ft_vox.exe --validate-block-edit` passed, including emissive restoration,
  border propagation, solid-neighbour geometry, and rapid supersession;
* `ft_vox.exe --validate-async-generation` passed;
* `ft_vox.exe --validate-visible-distance` passed with
  `required=160` and `chunks_loaded=188`;
* `ft_vox.exe --validate-renderer-publication` passed with
  `edit_latency_ms=51`.

Remaining work is still the long repeated-edit performance gate: the normal
block-edit workload passes but can show occasional 100--200 ms samples. The
next required optimization is to persist removal-frontier queues across
worker slices, just as additive propagation already does, so removal never
performs an unbounded local solve in one worker call.

## Startup baseline and streamed-light publication: 2026-09-11

The streamed generation worker already computes a chunk-local light field
before generating its mesh. The result is now explicitly marked render-ready
at that point and the readiness bit is preserved when the result is moved into
the live world slot. This means a newly generated chunk can render its valid
local-light baseline immediately; neighbour-aware border correction remains a
later refinement and must not temporarily replace the baseline with black
light.

Startup also seeds a larger warm baseline around the playable envelope. This
is still asynchronous, but it starts more nearby chunks early so the renderer
does not begin with only the center and one adjacent chunk while the rest of
the world is being generated.

The incremental removal path preserves the snapshot sky channel when removing
an emissive block. Sky-column reconstruction is used only as an additional
source for cases such as opening an opaque block; it cannot overwrite an
unchanged pre-edit sky value with an artificial full-sky value.

Verified locally after this pass:

* `make -j2 ft_vox.exe` succeeds;
* `ft_vox.exe --validate-block-edit` succeeds, including emissive restoration,
  border updates, solid-neighbour geometry, and rapid supersession;
* `ft_vox.exe --validate-async-generation` succeeds;
* `ft_vox.exe --validate-renderer-publication` succeeds.

The visible-distance validator completes the initial stream but still has a
separate intermittent recenter timeout when the worker queue is heavily
loaded. That scheduler issue remains distinct from the render-readiness fix
and should be diagnosed with candidate-state diagnostics before changing
mutex ownership or increasing the light workload.

## Resumable removal frontier: 2026-09-11

The removal side now uses request-owned frontier storage as well as the
additive side. A removal request keeps its subtraction queue, re-addition
queue, cursors, external-source replacement values, and phase state in the
immutable worker request. The worker consumes at most the configured node
budget per invocation and is requeued for another persistent worker slice when
the frontier is not finished.

This prevents a large light-removal solve from monopolizing one worker call or
discarding its valid intermediate state. The live chunk is still unchanged
until the complete light-aware result is committed; the renderer continues to
use the previous valid light/mesh pair during the solve.

The block-edit validator still passes after this change, including emissive
restoration, boundary propagation, solid-neighbour geometry, and rapid border
supersession. Async-generation and renderer-publication validators also pass.
Mixed additive/removal seed lists still use the conservative reference path;
the next frontier step is the coordinated multi-chunk removal transaction for
edits whose light influence crosses a chunk boundary.

## Stream scheduler starvation fix: 2026-09-11

The visible-distance diagnostic identified a separate scheduler defect. The
async submitter stopped submitting new generation candidates whenever any
interactive remesh entry existed. A starvation-promoted background remesh
could therefore leave hundreds of candidates absent while the pipeline had no
active or pending generation request. Interactive work is already ordered
ahead of generation by the persistent worker loop, so the global early return
was removed.

The worker loop also now initializes its selected request to `end()` rather
than the first queued request. When the generation quota is full and no
eligible remesh has reached its service point, the worker now waits instead of
silently bypassing `generation_worker_limit_` by taking the first generation
request.

Background remeshes receive service after a bounded generation batch; player
edits remain higher priority. This keeps world streaming progressing while
preventing border-light maintenance from waiting for the entire generation
queue to drain.

## Render publication gap and analytics-build output: 2026-09-11

The renderer had a publication gap during remesh upload. When a CPU mesh
revision changed, `GpuGeometryBatch` invalidated the previous GPU mesh before
checking whether the current frame still had upload capacity. A deferred upload
therefore rendered no mesh for that chunk, which appeared as a flicker or a
black chunk even though the previous light/mesh pair was still valid.

The batch now retains the last GPU publication for the same chunk coordinates
until the replacement upload succeeds. It only invalidates immediately when a
storage slot has been recycled for different chunk coordinates. The renderer
publication validator samples both identities during the transition and fails
if neither the old nor the new publication is visible.

Emitter removal was also corrected to always use the incremental removal
frontier, including cells that contain both sky and block light. Previously
those mixed cells could fall back to a full relight and expose an unnecessary
intermediate dark state.

The analytics executable had a separate performance problem: per-chunk worker
finalization, generation, and GPU-upload logs were enabled merely because
analytics was compiled in. Console I/O then competed with the render loop and
made the analytics build appear hung on Windows. These verbose traces are now
DEBUG-only; analytics retains sampled timing, slow-path, queue, and validator
diagnostics. This keeps instrumentation available without making the measured
program's progress depend on console throughput.

## Delete publication and duplicate-remesh guard: 2026-09-11

The previous publication fix covered deferred uploads for a drawable CPU mesh,
but a delete can temporarily leave the CPU-side replacement mesh without
occupied geometry while the worker is still solving the light frontier. The GPU
batch now retains the last uploaded mesh for the same chunk coordinates while
the chunk is dirty. It removes that mesh only after a final empty replacement
has been committed, so an in-progress delete cannot turn into a blank frame.
The visibility scan also keeps such a dirty chunk in the visible list so the
pending replacement remains eligible for upload.

The renderer-publication validator now performs both a place and a delete
publication cycle. For each cycle it requires either the previous complete GPU
identity or the new identity to be present on every observed frame; it fails if
there is a gap. The analytics run passed this expanded check with
`voxel_revision=3` and a 56 ms final publication latency.

The remesh scheduler also rejects generic dirty-scan submission for a chunk
already present in the priority queue. Priority entries carry the immutable
incremental-light seed and removal metadata. A second generic submission could
otherwise erase that metadata and start a full relight of the same edit,
discarding the previous light state and recreating the dark flicker. This guard
keeps the priority request as the sole owner of that edit until it is submitted
or explicitly replaced.

The remaining diagnostic gap is that `last_light_remesh_incremental` is a
single latest-result marker and can be overwritten by later recenter or
neighbour work. The block-edit validator still has an overly strict assertion
against that marker; it must be changed to correlate the observed result with
the edited content revision/request rather than treating a later full result
from another scheduling event as proof that the edit itself used a full solve.
The coordinated multi-chunk frontier transaction remains open for edits whose
removal influence crosses a face while both chunks are being edited.

## Stale remesh recovery and mixed frontier follow-up: 2026-09-11

The remaining flicker source found in the scheduler was not the light solver
itself. Orphan-marker recovery, recenter cancellation, and stale/error result
handling were creating a new seedless priority entry after an edit had already
been queued. That entry could win the next submission and request a full light
rebuild, temporarily replacing the valid light publication with an empty or
dark intermediate result.

Those paths now only clear the stale ownership marker and leave the chunk dirty.
The normal bounded scheduler chooses the replacement from the current light
versions, while an existing gameplay priority keeps its immutable incremental
seed. `queue_chunk_remesh()` also inherits matching priority metadata as a
final hand-off guard when a caller reaches it without explicitly passing the
seed list.

The worker now partitions mixed additive/removal seed lists so removal frontiers
are processed before additive propagation and preserves the request-owned light
state between worker slices. This keeps mixed edits on the incremental path
when their seed metadata is valid. A conservative full solve is still allowed
for conflicting transitions for the same cell or invalidated snapshots.

Validation after this pass:

* `--validate-renderer-publication --analytics-no-exporter` passed for both
  placement and deletion, with no old/new GPU publication gap;
* `--validate-block-edit --analytics-no-exporter` passed three consecutive
  runs, including emissive restoration, all four boundary faces, solid-neighbor
  geometry, and rapid supersession;
* repeated edit samples remained within the validator's strict budget, with
  p50 around 15--16 ms and p95/p99 around 30--32 ms in the local Windows
  analytics build.

The next unresolved lighting item is still the coordinated multi-chunk
frontier transaction: an edit at a chunk face must preserve the old valid light
publication in both chunks while the shared frontier is processed, especially
when another edit arrives before either frontier completes. The result marker
should also be correlated to the edited voxel/content revision rather than
using one mutable “latest incremental” flag.

## Regeneration light publication and revision-correlated validation: 2026-09-11

The regeneration applier had a separate authoritative-state bug. It destroyed
the existing chunk and moved the regenerated voxel data and mesh, but did not
move the regenerated light buffer. It then explicitly reset the live light
versions to invalid. A regenerated chunk could therefore contain a valid mesh
and a freshly computed worker light field while the renderer was told that its
light was invalid; the next remesh could show a dark or partially lit chunk.

Regeneration now transfers the worker-produced light buffer together with the
voxel data and mesh and publishes matching light/content versions (`1/1`) with
`light_ready_for_render` preserved. The revision validator now asserts that a
regenerated chunk has a valid, current, render-ready light buffer.

The edit validator no longer relies only on the mutable latest-result flag.
Chunks record the content and voxel revision for the most recent incremental
light commit. Placement/deletion validation requires those markers to match
the edited current revision, so later neighbour or recenter work cannot make a
wrong full rebuild appear to have validated the edit.

Verified locally in the analytics build:

* `--validate-world-revision --analytics-no-exporter` passed and confirmed the
  regenerated selected chunk retained valid/current/render-ready light;
* `--validate-block-edit --analytics-no-exporter` passed, including repeated
  edits, emissive restoration, all four boundary faces, and supersession;
* `--validate-renderer-publication --analytics-no-exporter` passed for both
  placement and deletion without a publication gap.

## Dependency orphan recovery and halo classification: 2026-09-11

Remesh dependencies can outlive the request that created them. Previously a
target could remain blocked while its dependency had no pending request, was
not dirty, and therefore could never produce the current light needed to
release the target. The scheduler now re-arms and prioritizes that dependency
when this state is observed. This is bounded to the normal priority queue and
does not make worker threads access live world state.

Snapshots now also distinguish an absent neighbour from an initialized
neighbour whose light is stale or invalid. Missing neighbours continue to use
the documented conservative boundary fallback during startup; an existing
neighbour with stale light is recorded as a blocked halo so future border
publication can wait for a valid source without incorrectly treating startup
absence as a deadlock.

The broad publication gate for incomplete halos was deliberately not enabled:
it caused a startup dependency cycle in the renderer validator. A remaining
implementation task is to use `lighting_halo_blocked` only for the affected
cardinal border frontier, not as a blanket eight-neighbour gate. That narrower
transaction is required before claiming that all runtime border flicker is
resolved.

## Cross-chunk source provenance: 2026-09-11

The next correctness gap was stale source provenance. A target remesh already
validated the target voxel/light/content revisions, but it did not validate the
source chunk whose border light was copied into the target snapshot. If the
source changed again while the target worker was running, the target could
commit a mathematically complete result based on an obsolete source border.

Border-dependent snapshots now carry the source chunk coordinates and the
captured voxel, light, content, and light-input revisions. The result committer
rejects the result before replacing either the mesh or live light when those
source revisions no longer match. This preserves the last complete target
publication and lets the current scheduler capture a new frontier.

This is provenance validation, not yet the final multi-chunk transaction: the
source and target still commit in separate operations. The remaining work is
to coalesce a face edit into one immutable source/target frontier result and
publish both mesh/light pairs together, with a differential test that edits
both sides before either worker result commits.

## Bidirectional pipeline queue reservation: 2026-09-11

The shared worker queue had a one-sided reservation: generation left room for
remesh work, but remesh submission could consume the final generation slots.
During startup this allowed border maintenance to fill the queue while a
playable chunk was still absent. Remesh submission now observes the same
reserved generation capacity, so a burst of light maintenance cannot prevent
new terrain requests from being queued.

The async-generation validator still needs a stable long-run pass under the
strict startup deadline; its current failure is a playable-area scheduling
timeout rather than a compile or light-state assertion. The next scheduler
pass should instrument the missing candidate state and distinguish queue
backpressure from worker service latency before changing the reservation size.

## Queue-capacity accounting correction: 2026-09-11

The first bidirectional-reservation implementation subtracted the remesh
reservation from both generation and remesh submissions. That double-counted
capacity: when no remesh request occupied the reserved slots, generation still
stopped below the actual queue capacity and missing stream candidates remained
`CANDIDATE_ABSENT`.

The pipeline now uses independent limits. The shared request deque is bounded
by its actual maximum, `remesh_in_flight` bounds remesh work, and the worker
loop's generation quota bounds concurrent terrain generation. The async
validator now reaches a complete playable startup area under the same edit and
remesh pressure.

This fixes startup starvation, but does not close the separate narrow
cardinal-border transaction requirement. Runtime diagnostics also now report
invalid or all-zero light baselines in the playable area so a renderer-side
black chunk can be distinguished from a generation or publication failure.

## Cross-chunk publication stability validator: 2026-09-11

The block-edit test suite now includes an edit exactly on a loaded cardinal
chunk face. While the source and receiving chunks converge, it checks that
both retain valid, non-zero light buffers; it then requires both light
revisions and mesh states to become current without pending work. This catches
the previously invisible failure mode where the final state was correct but
one chunk exposed an invalid or all-zero light buffer during the transition.

The validator passes locally. This verifies the current dependency-ordered
publication path, but it is not a substitute for the final atomic paired
frontier commit. That implementation still needs to coalesce simultaneous
source/target edits into one immutable transaction and publish both results
together when strict frame-by-frame identity is required.

The expanded analytics aggregate also passed with zero validator failures. It
reported `final_loaded=13` from the two-chunk seed, first visible mesh at
frame 7, and the new cross-chunk publication check passed alongside the
boundary, rapid-supersession, renderer-publication, and visible-distance
checks.

## Simultaneous border-edit dependency cycle: 2026-09-11

Two edits submitted on opposite sides of the same chunk face can each mark
the other chunk as a required light dependency before either result is
published. The previous scheduler then had a real cycle: both chunks were
light-invalid, neither had a pending request, and the priority queue retried
the same blocked entry indefinitely.

Dependency submission now detects this mutual-wait state and chooses the
lexicographically lower chunk coordinate as the first solver. Its dependency
gate is cleared and it computes against the conservative previous neighbor
light; the other chunk remains gated and is resubmitted after the first result
makes its source current. This preserves the last valid render publication and
converges without allowing a worker to access live world state.

The block-edit validator now performs simultaneous place and delete operations
on both sides of a face, checks that both buffers remain valid and non-zero
throughout convergence, and requires both chunks to finish current with no
pending work. The test passes locally. This is deterministic dependency-cycle
resolution, not yet the fully atomic two-result commit required by the final
handoff. The remaining transaction must retain both old publications until
the source and target frontier pair is ready, then publish both revisions
together when frame-identical publication is required.

## Preserve a valid baseline while neighbours catch up: 2026-09-11

Runtime diagnostics exposed a separate failure mode from the two-chunk cycle:
an existing chunk could have a valid published light buffer while its neighbour
was stale and had no active request. The old dependency path still invalidated
the target's light input and retried the dependency, which could leave the
target rendering black or waiting indefinitely even though its previous light
was usable.

Neighbour arrival and border-edit scheduling now distinguish these states. If
the target has an initialized, render-ready, structurally valid light buffer,
the scheduler keeps it as the visible baseline, marks only the required mesh
work, and clears the stale dependency. When the source becomes current, its
normal border notification schedules the refinement. A target without a valid
baseline still enters the dependency gate and must obtain a current source
before its first light publication.

This prevents an intermediate zero-light publication and avoids treating a
recoverable neighbour delay as a reason to discard already correct lighting.
The async-generation, block-edit, cross-chunk publication, simultaneous-border,
and aggregate validators pass with this behavior. The final paired frontier
transaction remains open for cases that require both chunks to become visible
in the same frame.

## Pending light revision without invalidating the visible baseline: 2026-09-11

Direct edits on a chunk face exposed a related versioning problem. The
neighbor needs a new light result, so its light request revision must advance,
but its already-published light values must remain drawable while the edited
source is being solved. Treating this as either a geometry-only change or a
full light-input invalidation was incorrect: the former made convergence
unobservable, while the latter could expose a black intermediate.

The scheduler now has a pending-light-remesh operation that increments the
light request revision and marks the mesh dirty without changing the current
light-input version or clearing the live light buffer. The dependency gate
waits for the source revision, and the result commit must match the pending
revision before replacing the target light. This preserves the old publication
until a valid replacement exists while retaining revision-based stale-result
rejection.

The boundary-face, cross-chunk publication, simultaneous-border, and renderer
publication validators pass with this versioning behavior. The remaining
multi-chunk transaction still needs to stage both source and target results
and publish their mesh/light pairs together when same-frame atomicity is
required.

## Deferred-edit neighbour invalidation: 2026-09-11

The base light lifecycle audit found one path that violated the baseline
preservation rule. Deferred/generated block edits called
`queue_neighbor_remeshes()`, which marked the edited chunk and all four
cardinal neighbours with `light_input_changed = true`. That advanced their
light-input versions even when only a border mesh needed reconsideration and
caused valid neighbouring light fields to be treated as discarded.

`queue_neighbor_remeshes()` now delegates to the same dependency-aware
`mark_neighbor_remeshes()` path used by normal edits. The edited chunk keeps
the light-input version already advanced by `mark_content_changed()`. A
neighbour with a valid published baseline retains that light field and gets a
border remesh/dependency request; a neighbour without a baseline is the only
case that is allowed to enter a full-light prerequisite path.

The base lifecycle is therefore:

1. Generate block data.
2. Build a chunk-local light field before the chunk becomes render-ready.
3. Build the initial mesh from that same light field.
4. Publish the paired block/light/mesh baseline.
5. For an edit, update authoritative blocks immediately and retain the old
   published light/mesh pair while the worker processes only the affected
   frontier.

## Renderer dark-frame investigation: 2026-09-11

The runtime symptom was that the world could appear completely dark even
though the CPU light validators passed. The first useful distinction is now
instrumented at the actual publication boundary: generated mesh vertices are
reported before `WorldChunk::publish_read_state()`, and the first analytics
GPU uploads report their packed-light range as well.

The generated runtime meshes contain non-zero light values. For example, the
initial generated chunks reported packed-light maxima of `240` and hundreds
or thousands of non-zero vertices. This rules out the local light build and
the generation-to-slot transfer as the source of an all-dark renderer state.

The temporary vertex-stage light interpolation experiment was removed. Each
emitted quad already writes one packed-light value to all four vertices, so
interpolating it in the vertex shader added no useful information and added a
driver-sensitive shader interface. The renderer keeps the stable flat packed
light varying and performs the existing brightness conversion in the fragment
shader.

The remaining verification is visual/GPU-side: an analytics gameplay run
must show matching non-zero `generated_mesh_light` and `upload_light` ranges,
and the shader must compile/link without diagnostics. CPU validator success
alone must not be treated as proof that the final framebuffer is lit.

## Prevent black geometry-only neighbor publications: 2026-09-11

The runtime symptom “chunks become dark as they approach the player” exposed a
specific publication hazard. A geometry-only remesh reuses the light values in
its immutable snapshot. If that snapshot did not contain a valid published
light field, its zero-filled fallback was used to build the replacement mesh.
The result could then be marked final and replace a previously drawable mesh
with a black one even though the authoritative chunk light had not changed.

Remesh workers now promote such requests to a full light build before creating
the geometry-only result. Geometry-only publication is therefore allowed only
when `existing_light_valid` is true; an invalid baseline cannot be rendered as
if it were a valid light state. The focused block-edit, border, simultaneous
border, and aggregate validators still pass after this guard.

The commit boundary also reapplies the current live light field to every
geometry-only replacement mesh immediately before ownership transfer. This is
intentional defensive synchronization: the worker owns an immutable snapshot,
while the live chunk may have received a newer light publication before the
geometry result reaches the main thread. The mesh must never publish the
snapshot's stale packed-light bytes over that newer field.
6. For a border edit, capture immutable source/target halo data and propagate
   only the relevant cardinal frontier; diagonal neighbours receive geometry
   work unless their face can receive light.
7. Publish the replacement only after the worker result passes all revision
   checks.

The focused block-edit validator and aggregate validator pass after this fix.

## Paired border publication: 2026-09-11

The result committer now briefly stages a completed source remesh when an
initialized face-sharing neighbour is explicitly waiting for that source's
light revision. If the dependent result is already completed, the source and
dependent commits are performed consecutively in the same drain pass. Each
commit still performs its normal request, content, light-input, and dependency
revision validation; staging does not bypass stale-result rejection.

The staged source is bounded to four stream frames. If the dependent result is
cancelled, superseded, or never produced, the source is committed by itself at
the deadline and the normal scheduler can re-arm the neighbour. This prevents
the atomic-publication optimization from becoming a deadlock or starvation
condition.

This is a same-publication-pass guarantee, not a general multi-chunk database
transaction: a failed or stale dependent result cannot roll back an already
committed source. The important invariant is that neither path clears a valid
published light buffer merely because a replacement is pending. The focused
block-edit and aggregate validators pass with this behavior; a future stronger
transaction can add a single immutable publication record for both chunks if
the renderer ever requires all-or-nothing visibility across a border.

## Cardinal halo admission: 2026-09-11

Snapshot capture now tracks `lighting_cardinal_halo_valid` separately from the
complete eight-neighbour `lighting_halo_valid` flag. The complete flag remains
required for a final light solve, while the interactive geometry preview only
requires the target and its four face-sharing neighbours to have usable light
baselines. A stale diagonal cannot therefore delay a visible cardinal edit or
force the renderer to discard its previous mesh.

Diagonal light data is still copied and revision-checked when it participates
in a final result; this change only narrows the early geometry-admission gate.
The full-light fallback remains responsible for rebuilding an incomplete halo,
so this does not pretend that diagonal data is current. Focused block-edit and
aggregate validation pass after the change.

## Incremental frontier continuation state: 2026-09-11

The focused temporal validator found a real worker bug that ordinary final-state
checks did not expose. An incremental removal is allowed to run over several
worker slices, but `preserve_existing` was derived only from the seed index.
While the first seed was still being processed, every continuation therefore
re-initialized its light buffer from the old snapshot. The removal frontier
could repeatedly erase the same state and eventually publish an empty result,
even though the live chunk had a valid light baseline.

Continuation now uses the worker's initialized-state flags. Only the first
slice copies the snapshot; later slices continue the same worker-owned light
buffer. A later seed may still preserve the completed result of the previous
seed even after that seed's queue has reset.

The validator was also corrected so a zero-light result is not automatically
treated as a transient bug: deleting the only emitting block can legitimately
produce a dark final field. It records temporary empty publications and rejects
only when the converged final field is non-empty, which distinguishes a real
black-frame flicker from a valid source-removal result.

Evidence from the focused run after the fix:

* `--validate-block-edit --analytics-no-exporter` exited successfully;
* boundary face matrix passed all four faces;
* interior emissive source placement/removal restored the original light;
* cross-chunk light publication and simultaneous border edits converged;
* `--validate-all --analytics-no-exporter` passed with zero failures.

The remaining runtime audit is still required: the client renderer must be
observed while moving through generated chunks and while editing ordinary,
non-emitting blocks next to an independent light source. Those scenarios must
verify that the old mesh/light pair remains visible until the affected frontier
publishes, and that no unrelated chunk is re-lit or cleared.

## GPU light interpolation for split-face black triangles: 2026-09-11

The screenshots showed black triangular halves of otherwise-lit block faces.
The CPU mesh contained packed light, but the shader passed that byte as a
`flat` integer varying. The two triangles making up one quad can therefore use
different provoking vertices and one triangle can become black.

The shader now decodes the packed sky/block value per vertex, applies the sky
darkening setting, converts it through the shared brightness curve, and passes
the resulting scalar as a smooth varying. Block ID and face remain flat because
they select discrete material data; light is spatial data and must interpolate.

This is independent of CPU light validity: the CPU publication guard still
prevents zero/stale meshes from replacing a valid mesh. The running client must
be checked with the analytics executable after rebuilding so the updated shader
files are loaded, especially at chunk borders and after block deletion.

## New-arrival cardinal light frontier: 2026-09-11

The remaining dark-chunk path was traced to ordinary chunk arrival. A newly
generated source chunk notified a face-sharing neighbour, but a neighbour that
already had a valid light buffer was only marked dirty. The scheduler then
forced that request through the geometry-only path, so the neighbour retained
its old border light forever instead of consuming the newly available source
light.

Arrival handling now has two explicit paths:

* a target without a published light baseline remains on the dependency/full
  solve path and cannot be seeded from an uninitialized buffer;
* a target with a valid baseline scans only the four shared faces, seeds cells
  whose incoming sky/block light can increase, and submits the same bounded
  incremental frontier used by block edits.

An already pending interactive request is preserved and is not overwritten by
the arrival notification. Lower incoming values fall back to the normal full
solve path because an arrival notification does not contain enough historical
source data to safely remove a stale contribution.

This prevents a valid neighbouring chunk from being republished with stale or
zero border light and removes the previous geometry-only dead end. The focused
block-edit validator and both normal/analytics builds pass after the change.

The remaining runtime acceptance test must still move the camera far enough to
stream a new chunk beside an already-lit chunk, then verify that the neighbour
keeps its old mesh until the frontier result is ready and converges without a
black frame. Analytics should show a light-aware arrival remesh, followed by a
stable non-zero `upload_light` range.

## Render-readiness guard against black replacement meshes: 2026-09-11

The renderer diagnostics exposed a second concrete failure mode: a geometry-only
result could be captured just before the live light baseline was invalidated.
That result was version-valid enough to reach the committer but contained an
all-zero packed-light mesh. Replacing the previous GPU mesh with it produced a
black frame until a later light result arrived.

Light storage validity remains separate from the old visual baseline. A light
input change leaves the previous light available so a block edit can publish
its new geometry immediately with the old lighting while the worker computes
the replacement. In addition, a geometry-only final result whose vertices are
all zero while the current mesh contains light is rejected, the old mesh is
retained, and the normal scheduler is forced onto a full worker-side light
solve. This preserves edit responsiveness without allowing a black
intermediate mesh to replace a valid publication.

The renderer-publication diagnostics now show the original non-zero upload and
no subsequent zero-light upload for that chunk. The remaining validator failure
is a separate latency-budget issue under the synthetic GPU/remesh workload;
the black replacement publication itself is prevented.

## Incremental mesh publication must use a complete light field: 2026-09-11

Incremental propagation produces a worker-owned field from the previous
snapshot plus a bounded frontier. It is not safe to treat the frontier result
as a sparse mesh-light source: untouched cells must retain their snapshot
values, and a cell that was intentionally reduced to zero must remain an
explicit zero.

The worker now reconstructs a complete temporary mesh-light field from the
snapshot and overlays the computed deltas before generating an incremental
replacement mesh. The authoritative chunk still commits the delta result as
before. This keeps the mesh and the committed light state consistent and
prevents unchanged cells from becoming black during a block-break update.

The focused acceptance test must cover a lit chunk with one removed block and
assert that an untouched lit face remains lit in the replacement mesh while
the affected frontier converges. It must also cover a border edit and verify
that the complete field uses the neighbour snapshot for out-of-chunk samples.
# 2026-09-12 analytics/screenshot follow-up

The latest analytics run and screenshots showed dark rectangular terrain faces
and short-lived black patches around exposed mountain/snow surfaces.  The
screenshots do not by themselves prove that every dark face is invalid: face
orientation shading intentionally darkens some vertical faces.  They do show
that the renderer must never publish a zero-sky sample for a cell that is
provably open to the sky.

The mesh-light sampler does not perform a repair pass.  A per-face upward scan
was evaluated as a possible safeguard, but it was rejected because it moved
authoritative lighting work into the mesh hot path and could reintroduce frame
spikes.  Meshes must consume the complete light mapping produced by the light
solver.  The mapping currently consists only of packed sky and block channels,
each clamped to 0--15.

Decorative flora is explicitly transparent and non-occluding at this stage, so
shrubs and leaves do not manufacture terrain shadows.  Actual solid terrain
and structures remain light occluders.  This keeps the current implementation
aligned with the requested basic mapping while leaving coloured light sources
for a later, separate extension.

Verification performed after the change:

* Libft analytics exporter tests passed: 22/22, including shutdown while a
  sampled runtime scope is still active.
* Libft voxel metadata, solver-to-mesh mapping, and foliage skylight tests
  passed: 18/18.
* The Minecraft analytics target rebuilt successfully.

The runtime visual proof remains outstanding: capture published mesh light
ranges while moving, editing, and crossing chunk borders, and compare normal
and analytics runs.  CPU validator success alone must not be treated as proof
that the final framebuffer is lit.

Remaining proof required before calling the screenshot issue complete:

1. Add a deterministic mesh-light test for exposed, cave, and ceiling-column
   cells, asserting sky 15 only for the exposed cases.
2. Add a runtime validator that records the minimum/maximum packed light and
   zero-light vertex count for every newly published visible mesh, including
   the chunk coordinate and source light/input versions.
3. Run that validator while walking across chunk borders and while deleting
   blocks at the border.  A light result from an older block revision must be
   rejected rather than published as a black intermediate mesh.
4. Compare a screenshot run with analytics disabled and enabled.  If only the
   analytics executable exhibits the artifact, investigate the analytics
   exporter/render scheduling separately; the lighting invariant must not be
   relaxed to hide that difference.

## Basic-lighting scope and shutdown ordering: 2026-09-12

For the current stage, light values are intentionally limited to the two
packed 0--15 channels.  Decorative flora must not act as an opaque light
occluder.  The built-in shrub and leaf metadata now keeps those blocks
transparent for face visibility and light propagation; solid terrain and
structures remain valid occluders.  A Libft metadata regression test covers
both blocks.

The analytics close path also had an unsafe ownership order in the game
session: the world was destroyed before the world analytics classification was
ended.  World activity is now ended first, then the world workers are stopped,
so no world teardown can overlap with an active world analytics state.  The
exporter wait uses a stop-or-completed-buffer predicate and shutdown wakes all
waiters, so close does not depend on a spurious condition-variable wakeup.

This does not claim the full visual lighting issue is solved.  The remaining
runtime proof still needs to capture published mesh light ranges while moving,
editing, and crossing chunk borders, and must separately test analytics
shutdown while generation and export work are active.

The Libft runtime instrumentation bridge now blocks new runtime scopes during
shutdown and waits for already-registered scopes to finish before the analytics
session can be destroyed.  This closes the worker-to-session lifetime race
that could make analytics shutdown hang or access released export buffers.

The integrated renderer-publication validator still passes after this change:
the edit latency was 96 ms and the published mesh carried a current voxel
revision.  This validates the client publication path, but is not a substitute
for the remaining visual border-light proof.

## Deterministic mesh-light matrix and publication diagnostics: 2026-09-12

The Libft lighting tests now cover three explicit mesh-input conditions:

* an exposed block whose air sample above it receives sky light 15;
* a block below a closed roof whose sky mapping remains 0;
* a block directly below the ceiling column whose sky mapping remains 0.

The test checks both the complete solver field and the packed values copied into
the generated mesh.  It intentionally does not require the opaque block cell
itself to carry sky light: the solver stores light at the sampled cell used by
each visible face.  This prevents a test from confusing an opaque block with
the exposed air beside or above it.

The GPU publication validator now emits `edit_light` and `delete_light` records
containing the chunk coordinate, voxel/mesh revisions, packed-light minimum and
maximum, and non-zero vertex count.  A current Windows run reported:

```text
edit_light   min=0 max=15 nonzero=868
delete_light min=0 max=15 nonzero=1352
```

The complete Libft suite passes 6349/6349 tests, and the renderer-publication
validator passes after the diagnostic extension.  These checks prove that the
CPU solver and publication path retain non-zero light data for the exercised
edits.  They do not replace the remaining interactive screenshot proof while
walking across streamed chunk borders or reproducing the user-reported flicker
in the analytics executable.

The same publication validator was then run with analytics export disabled in
both variants.  The normal test executable completed with edit latency 133 ms;
the analytics executable completed with edit latency 204 ms.  Both reported
`edit_light` and `delete_light` ranges with maximum sky light 15 and non-zero
vertices, and both completed the delete publication successfully.  The slower
analytics result is evidence of additional scheduling overhead in the broader
startup workload, but neither run published an all-zero replacement mesh in
this deterministic scenario.  A real interactive run is still needed to
correlate the screenshots with a particular chunk, request, and light/input
revision.

## Removal preview stability follow-up: 2026-09-12

The interactive removal path was still withholding the geometry replacement
until the bounded lighting frontier completed.  That made a block removal
depend on the light solve before the renderer could see the geometry change,
and left the runtime behavior different from the required staged contract.

Interactive removals now publish a geometry-only mesh when the immutable
snapshot has a valid complete light field and cardinal halo.  The preview is
built from that pre-edit light field and never from a partially solved worker
frontier.  The same request remains alive for the incremental light solve,
which later publishes the corrected light-aware mesh.  This keeps the previous
complete lighting visible while making the block disappear immediately.

The placement flag classification was also made explicit instead of assigning
the same light-removal flag twice.

Verification after this change:

* normal block-edit validation passed, including rapid supersession,
  emissive-source removal, four border faces, cross-chunk publication, and
  simultaneous border edits;
* normal renderer publication passed with delete publication at 52 ms;
* analytics renderer publication passed with delete publication at 67 ms;
* both publication runs reported maximum packed sky light 15 and non-zero mesh
  light vertices for the final delete mesh.

The interactive screenshot proof is still outstanding because screenshots must
be captured while walking across streamed borders and observing the actual
framebuffer.  The automated checks prove the staged CPU/GPU publication
contract for the tested workload, but do not prove every visual runtime case.

## Boundary surface publication follow-up: 2026-09-12

A boundary deletion has two independent geometry publications: the edited
chunk loses the block, while the face-sharing neighbour gains the newly exposed
face. Publishing those in the opposite order creates a short-lived hole even
when both CPU meshes are eventually correct. The render handoff now records
when an edit touches a chunk boundary and keeps the changed chunk's previous
GPU mesh until every loaded, populated face-sharing neighbour has either
completed its current mesh request and uploaded a GPU mesh matching its current
chunk and voxel revisions.

Empty neighbours do not block publication because they cannot occlude the
boundary. Interior edits do not use this barrier, so unrelated background
remesh work cannot delay ordinary block updates. The previous complete GPU
mesh remains drawable while the boundary pair converges; no empty or partially
rebuilt mesh is published as a substitute.

The acceptance test must cover both halves of this contract: the CPU neighbour
mesh must contain the exposed boundary face after a deletion, and a graphics
context test must observe that the changed chunk never has a publication gap
while its adjacent mesh transitions to the matching revision.

The graphics-context validator now covers that second half. It searches the
loaded world for a face-sharing pair with an occluding neighbour, creates a
temporary target block when the target cell is empty, waits until both baseline
meshes are uploaded, deletes the target boundary block, and checks every render
iteration. During convergence each side must retain either its previous GPU
identity or its current one; a frame where neither identity is present fails.
The test then requires both current identities and the neighbour's exposed west
face. It passed in the normal and analytics executables, including the
analytics build with export disabled.

The aggregate scheduler validation was rerun after adding this barrier. Both
normal and analytics builds passed with zero failures. The normal run expanded
from two initial chunks to thirteen and reached its first visible mesh at frame
4; the analytics run reached thirteen and its first visible mesh at frame 3.
Both completed the visible-distance, recenter, async-generation, repeated
edit, cross-chunk-light, and border-convergence checks. The analytics metrics
reported a higher maximum update slice (about 38 ms), so analytics overhead
remains a performance concern, but it did not produce a publication gap or
invalidate the border invariant.

## Neighbor-arrival face summary optimization: 2026-09-12

The streaming commit path previously rescanned every cell on each shared face
while holding the world write lock to decide whether an arriving chunk could
change its neighbour's light.  At sixteen blocks wide and 256 blocks high,
that repeated work was a measurable source of `neighbor_us` and delayed other
world operations during startup and recentering.

Each `WorldChunk` now caches four conservative summaries whenever it publishes
its immutable read state:

* the maximum sky-or-block light value on the source side of each cardinal
  face pairing;
* whether the receiving side of that pairing contains any transparent cell.

The arrival predicate is now constant time.  It schedules the target when the
source maximum is greater than one and the target face has a transparent cell.
This is intentionally conservative because attenuation has a minimum of one:
it can schedule a harmless extra light solve, but cannot suppress a possible
light contribution.  The summaries are reset with chunk state and copied or
moved with the chunk, so slot reuse cannot retain stale boundary metadata.

Verification:

* normal `--validate-all --analytics-no-exporter` passed with zero failures;
  async generation reached thirteen chunks and cross-chunk light, border
  edits, renderer publication, and visible-distance checks passed;
* analytics `--validate-all --analytics-no-exporter` passed with zero
  failures, including the same cross-chunk and renderer checks;
* analytics commit diagnostics now report `neighbor_us` at roughly 0--2 us
  for the exercised arrival commits instead of repeatedly scanning the full
  face.  The remaining analytics spikes are dominated by mesh indexing,
  chunk transfer, and recenter work, not the neighbor light predicate.

This optimization does not replace the light solver or the boundary
publication barrier.  It only prevents arrivals that cannot possibly add
light from invalidating a valid target baseline.  Runtime visual verification
of long-distance streamed borders and screenshots remains outstanding.

The final focused renderer-publication run after folding the summary update
into the existing snapshot loop also passed in both executables.  The normal
run reported `edit_latency_ms=116`; the analytics executable reported
`edit_latency_ms=184` with exporter disabled.  Both retained packed light up
to 15 and non-zero mesh-light vertices, and both completed the boundary-pair
stability check.  These values are validation evidence only; they are not a
claim that the strict interactive latency target or the whole-chunk mesh
publication cost has been eliminated.

The existing Libft `chunk_mesh_generate_*_in_bounds` and
`chunk_mesh_replace_in_bounds` APIs were audited for the next pass.  The
replacement helper currently builds a merged mesh before committing it, so a
correct section-local implementation still needs a result format that carries
section ownership and a publication path that swaps only changed sections.
Simply routing the current full result through that helper would preserve the
allocation/copy cost and could introduce partial-publication races.  This
remains an explicit follow-up rather than an unverified optimization.

## Long-run queue coalescing validation: 2026-09-13

The async-generation validator now includes a deterministic queue stress case.
It inserts 64 distinct background remesh entries, injects 10,000 repeated
notifications for those same entries, and verifies that duplicate replacement
does not grow the queue beyond 64 entries.  After advancing the stream clock
to the starvation threshold, it requires all 64 surviving entries to be
promoted.  This exercises the queue-level coalescing and starvation policy
without creating 10,000 worker jobs or changing the production queue limits.

The normal executable passed the stress case and the async-generation
validator: 14 chunks loaded, first visible mesh at frame 14, and maximum
update slice 6,624 us.  The analytics executable also passed: 13 chunks
loaded, first visible mesh at frame 18, and maximum update slice 5,633 us
with exporter disabled.  This closes the deterministic queue-level stress
gap, but it does not yet prove long-running worker fairness under real
continuous generation, nor does it replace the still-open section-local mesh
and coordinated multi-chunk frontier work.

## Conservative mesh boundaries during chunk deletion: 2026-09-13

The renderer could briefly see through the world when a face-sharing chunk was
unloaded, destroyed, or still being generated.  `ChunkNeighborMesher` treated
an absent or uninitialized neighbor as air, so it emitted boundary faces from
the remaining chunk against incomplete world state.  That publication could
race the slot replacement and expose a transient hole or transparent-looking
surface.

The mesh lookup now treats an unavailable neighbor as the built-in opaque stone
block.  This matches the existing conservative lighting lookup and ensures an
incomplete boundary cannot be interpreted as open space.  The real neighbor's
arrival still triggers the existing border remesh path, which replaces the
temporary opaque boundary with the actual neighboring block result.

This is intentionally a safety-first fallback: an outer edge whose neighbor is
not loaded is temporarily occluded rather than rendered as open sky.  The
streamer must therefore generate the configured visible neighbor ring before
considering that area fully drawable.  A focused regression should generate a
chunk without neighbors, verify that its boundary does not expose an unknown
opening, then add the neighbor and verify the shared face converges to the
correct geometry.  The existing async-generation, border-publication, and
`validate-all` runs passed after this change; visual verification of unloaded
edge behavior remains required.

The first fix covered only the live `WorldChunk` mesher.  Asynchronous remesh
workers use `WorldChunkSnapshotReader::lookup_snapshot_block`, where the
one-cell border coordinates were being handled by the wider lighting ring
before the captured border arrays were consulted.  Missing ring cells thus
still looked like air and could reintroduce the same hole in a worker result.

Snapshot lookups now resolve the immediate four cardinal border cells through
the explicit border-presence flags before reading the wider halo.  An absent
border returns opaque stone; a present border returns its captured block.  The
async validator now covers the live fallback, restoration after an air-filled
neighbor arrives, and the worker snapshot fallback.  It passed with fourteen
chunks loaded, first visible mesh at frame 16, and a 5,600 us maximum update
slice.  A real runtime capture while deleting and reusing streamed chunk slots
is still required before the visual issue can be called closed.

The first fix covered only the live `WorldChunk` mesher.  Asynchronous remesh
workers use `WorldChunkSnapshotReader::lookup_snapshot_block`, where absent
cardinal border arrays were initialized to air.  That allowed a worker result
to reintroduce the same hole after the live mesher had been corrected.

Snapshots now carry explicit presence flags for the four cardinal borders.
`lookup_border_block()` returns the same opaque stone fallback when a border is
absent, while a present border continues to return its captured block data.
The async validator checks both paths: an absent live neighbor suppresses the
boundary face, an available air neighbor restores it, and an absent snapshot
border resolves to opaque stone.  The async-generation validator passed after
this correction.  A real runtime capture while deleting and reusing streamed
chunk slots is still required before the visual issue can be called closed.

## Initial target lighting was suppressed during synchronous chunk loading: 2026-09-13

The renderer publication validator exposed a separate lighting defect: the
first uploaded center chunk could contain vertices whose packed light values
were all zero.

The cause was the world-light block callback used by
`WorldChunkLoader::build_mesh_with_neighbors()`.  Chunk voxel data is
generated before the loader marks the chunk `initialized`; that flag is
intentionally delayed until lighting and the first mesh have completed.  The
callback used the flag for both the target and its neighbors, so it treated
the already generated target as an unknown solid boundary.  Sky propagation
therefore stopped at the target itself and the initial mesh was published
dark.

The callback now permits the explicitly supplied target during this
initialization window, while still requiring neighboring chunks to be
initialized.  This preserves the conservative opaque fallback for in-flight
neighbors without suppressing the target's own sky-light calculation.

Verification:

* `make -j2 automated_tests.exe` rebuilt both normal and analytics executables.
* The renderer publication check now observes nonzero light in the initial
  center mesh and the analytics build completed the edit and border checks.
* The async-generation validator still passes, including the missing-neighbor
  snapshot regression.

The renderer validator also has a separate timing-sensitive border-pair setup
and can fail when the neighbor has not arrived before the scan, or when two
validator processes compete for the same machine.  That is distinct from the
initial all-black mesh defect fixed above and still needs validator
stabilization before it is a deterministic CI gate.

### Zero-light geometry-only publication guard: 2026-09-13

Repeated renderer traces found another black-chunk route: a geometry-only
result could be labelled final and replace a newly arrived chunk even when its
packed-light values were all zero.  The previous protection only rejected the
replacement when the old mesh was already lit, leaving chunks without an old
lit publication exposed to a black frame.

The result committer now rejects every all-zero geometry-only replacement.  It
keeps the current mesh, clears the pending marker, marks the chunk's light
ready state false, and advances the light-input version so the ordinary full
light solve is resubmitted.  Geometry-only work can therefore remove stale
geometry without ever publishing a dark replacement.

The analytics renderer-publication validator passed after this guard with a
64 ms edit publication and nonzero initial, edit, and border light fields.
The normal validator still needs repeated low-load and startup-contention
runs; one earlier normal run exceeded the 500 ms edit gate while processing a
large stale-result backlog.

The renderer validator now warms the asynchronous stream for eight bounded
frames before selecting its east-border fixture.  Center publication is
intentionally tested before the complete ring exists, but the border fixture
requires two loaded chunks; selecting it immediately after center upload was
otherwise a startup-order race rather than a product failure.  The original
GPU identity, old-publication fallback, boundary-face, lighting, and latency
assertions remain unchanged.

After the interactive-slot reservation, zero-light publication guard, and
validator warm-up, two sequential normal renderer runs passed with edit
publication latencies of 105 ms and 118 ms.  Both completed border deletion
and light checks.  Async-generation and block-edit validators also passed.

## Handoff status audit: 2026-09-13

The current Windows checkout has passed the available aggregate and focused
validation after the latest publication fixes:

* `automated_tests.exe --validate-all --analytics-no-exporter` passed with
  zero failures;
* async-generation, queue coalescing, missing-neighbor, and startup-priority
  checks passed;
* block-edit, rapid supersession, emissive, four-face boundary, and
  cross-chunk lighting checks passed;
* repeated renderer-publication checks passed with nonzero initial/edit/border
  light and recent normal edit publication between 32 ms and 118 ms;
* normal and analytics executables were rebuilt successfully.

This evidence closes the currently reproducible initial-black-mesh,
all-zero-geometry-only-publication, missing-neighbor-hole, and startup
interactive-slot failures.  It does not prove that every runtime screenshot
case is resolved; a real gameplay capture while recentering, unloading, and
editing an edge remains required.

Remaining implementation and proof items are deliberately still open:

1. Carry section-local dirty bounds through remesh requests and atomically
   replace only owned sections after the differential tests listed above.
2. Make simultaneous edits on multiple chunk faces one coordinated frontier
   transaction, retaining both old mesh/light publications until all affected
   chunks have a valid replacement.
3. Prove long-running worker fairness under continuous generation and repeated
   edits, including strict p95/p99 latency gates rather than one-shot timing.
4. Complete cross-platform sanitizer, Valgrind, graphics-context, and runtime
   recenter/unload verification.
5. Add the shared entity submission contract before implementing mob shadows;
   the current player-only GPU shadow must not be presented as full entity
   support.

These items remain explicit in the handoff because the passing validators
above cover correctness of the current complete-mesh fallback, not the
unimplemented partial-mesh or multi-chunk transaction architecture.

## Section-local mesh implementation boundary: 2026-09-13

Section-local rebuilding remains intentionally unimplemented.  The existing
Libft `*_in_bounds()` functions reduce the geometry scan, but
`chunk_mesh_replace_in_bounds()` still constructs a merged complete mesh and
repartitions its indices before committing.  Calling it with a small bounds
box would therefore improve neither the main allocation cost nor the
renderer publication cost, and could be incorrect if a face primitive crosses
the box boundary.

The safe implementation must introduce an explicit partial-mesh result:

1. The edit command records a local dirty box expanded by one voxel in every
   direction.  The expansion covers faces whose visibility changes because of
   the edited cell.  A chunk-edge edit adds the corresponding one-cell box in
   each loaded cardinal neighbor.
2. The immutable remesh request carries the box and a `partial_geometry` flag.
   Lighting-only changes, unknown neighbor state, full relights, and any box
   that touches an unresolved boundary continue to request a complete mesh.
3. The worker generates only primitives fully owned by that box.  It must
   reject a primitive that intersects the box without being wholly contained;
   silently clipping a quad would create cracks or duplicate faces.
4. The live chunk stores section/primitive ownership metadata.  The commit
   path validates request and voxel/light revisions, builds the replacement
   section off-thread, and atomically swaps the affected section list while
   retaining all untouched sections.  GPU publication receives one new mesh
   identity only after the complete section table is coherent.
5. If allocation, validation, neighbor capture, or publication fails, the old
   complete mesh remains drawable and the chunk is retried as a full remesh.

Required tests before enabling this path:

* interior edit: compare the section-local result against a clean full rebuild;
* removal and placement at every face and corner of a section;
* edits on all four chunk borders, including a missing and then arriving
  neighbor;
* water/transparent/opaque transitions and emitted-light changes;
* adjacent edits whose expanded boxes overlap and rapid supersession;
* allocation failure at every temporary-vector and section-table commit;
* index uniqueness, face ownership, occupied bounds, solid/water partitions,
  and byte-for-byte equality with a full rebuild outside the dirty region;
* renderer publication must never expose a partially swapped section table;
* performance gate comparing scan cells, allocations, commit time, and frame
  latency against the current full-mesh baseline.

Until these invariants and differential tests exist, the current complete mesh
publication is the correct fallback.  This keeps the lighting fixes above
focused on correctness and avoids trading a visible flicker/through-world
bug for a faster but invalid partial mesh.

## Geometry boundary fallback correction: 2026-09-13

The previous missing-neighbor fallback was incorrect for mesh generation.  It
returned opaque stone, which suppressed the current chunk's outward boundary
faces.  When the neighboring chunk was absent, unloading, or not yet
published, that made the renderer draw an open boundary and allowed the
camera to see through the world.

Geometry and lighting now use separate contracts:

* mesh block lookup treats an unavailable cardinal neighbor as air, causing the
  current chunk to emit a visible cap face;
* light block lookup continues to treat unavailable space as opaque stone, so
  missing data cannot create an artificial sky or block-light opening;
* once an opaque neighbor is available, the shared face is removed by the
  normal neighbor-aware remesh;
* an air neighbor deliberately leaves the face visible, because that is the
  correct exposed surface rather than a hole.

The async snapshot worker now uses `lookup_snapshot_mesh_block()` for every
mesh-generation call while retaining `lookup_snapshot_block()` for lighting
and frontier queries.  The boundary validator was corrected to test the real
contract: absent neighbor means a cap face, loaded opaque neighbor means no
shared face, and an absent snapshot mesh border resolves to air.

Verification after this correction:

* `--validate-async-generation --analytics-no-exporter` passed;
* `--validate-renderer-publication --analytics-no-exporter` passed, including
  edit and border deletion with nonzero light values;
* `--validate-all --analytics-no-exporter` passed with zero failures after the
  geometry/lighting lookup split; the visible-distance, block-edit,
  cross-chunk-light, async-generation, and boundary-face checks all remained
  green;
* the focused renderer run measured 427 ms edit publication latency, so the
  remaining latency/performance target still needs repeated low-load and
  long-run measurement even though the boundary correctness checks pass.

## Evicted-neighbor boundary invalidation: 2026-09-13

Recenter eviction has a separate boundary case from initial generation.  The
evicted source is removed before its neighbors are notified, so a notification
path that only compares a still-present source cannot repair the neighbors'
old shared-face decisions.  The source-missing branch now marks each loaded
cardinal neighbor's geometry dirty, requests a geometry-only rebuild, and keeps
its valid light versions unchanged.  The mesh worker then sees the missing
neighbor as air and emits the cap face; the light worker continues using the
opaque unknown-space fallback.

This avoids both failure modes: a neighbor cannot retain a permanent opening
after an eviction, and eviction does not trigger an unnecessary full relight.
The visible-distance/recenter validator passed after this change with 377
loaded chunks.  A long interactive camera traversal with repeated evictions is
still required for runtime proof.

## Manual architecture review: 2026-09-13

This section supersedes any earlier statement that the runtime lighting/edit
work is complete.  The user still observes intermittent full-chunk dark frames,
incorrect light, and slow placement/deletion while chunks are streaming.  The
current validators do not reproduce all of those runtime cases, so a passing
focused validator is not sufficient evidence that these issues are resolved.

No implementation change is authorized by this review section.  It is an
implementation handoff for Luna and a record of the current code-level risks.

### What the current implementation already does correctly

Initial terrain generation follows the required ownership direction.
`WorldChunkGenerationWorker::initialize_chunk_for_generation()` generates the
voxel data, builds the chunk-local light field, and creates the first
light-aware mesh on a worker.  Only the completed `WorldChunk` payload is later
transferred into a live world slot.  Generation workers consume owned requests
and do not access `World` or `World::world_data_mutex_`.

Interactive remesh workers also consume immutable snapshots.  A worker can
produce an incremental list of changed light cells, and stale results are
revision checked before publication.  These are useful foundations and should
be retained.

### Finding 1 - the global world write lock still contains whole-chunk work

`World::update_around()` acquires `world_data_mutex_` exclusively before
recenter processing and keeps it while `WorldChunkStreamer::update()` drains
results, commits meshes/light, performs recovery, updates queues, and dispatches
new work.  This means an expensive generation/remesh commit can delay a player
edit even though its calculation happened on a worker.

The direct placement/deletion calls also hold that same exclusive lock across
more than the authoritative write.  Their current critical section includes:

1. block lookup and mutation;
2. revision updates;
3. immutable read-state publication;
4. dirty-edit/history recording, which may allocate;
5. cancellation and queue manipulation;
6. neighbor invalidation and immediate remesh submission.

`publish_read_state_after_block_edit()` copies the previous full light vector
and rescans four complete vertical chunk faces while the edit still owns the
world lock.  A final light commit applies deltas and then
`publish_read_state()` walks/copies the complete 65,536-cell light field under
the same lock.  Worker-side computation therefore does not yet guarantee a
small main-thread commit.

Required correction: make the main thread the sole mutable-world owner and
reduce the exclusive commit to validation plus a bounded pointer swap or a
small prevalidated delta application.  Snapshot construction, allocation,
mesh generation, light propagation, history-record preparation, compression,
and destruction must happen before or after that critical section.

### Finding 2 - `std::shared_mutex` cannot provide the requested priority

The current world lock cannot promise that the main thread wins admission over
waiting readers or writers.  The C++ standard does not provide a main-thread
priority contract for `std::shared_mutex`.  Adding retries, yields, or more
lock calls around it would make latency less predictable.

Do not solve this with a custom spin/yield policy around the same shared world.
The intended architecture should instead avoid worker access to mutable world
state entirely:

```text
main thread owns mutable World
    |
    +-- publishes immutable chunk/frame snapshots
    +-- sends owned work packets to persistent workers
    +-- drains high-priority completed edit/light packets between frames

workers
    |
    +-- never receive World or WorldChunk pointers
    +-- never acquire world_data_mutex_
    +-- return only owned, revision-labelled results

renderer
    |
    +-- reads an immutable frame publication
    +-- never blocks a world mutation worker (there are none)
```

This gives the main thread priority by ownership and queue order rather than by
depending on unspecified mutex fairness.

### Finding 3 - light changes are sparse, but publication is still mostly whole-chunk

`IncrementalLightDelta` is a vector of coordinate/value structures.  It is not
a compressed transport format, and the result also carries a complete worker
light object and a complete replacement mesh.  The main thread calls
`voxel_light_chunk::set()` once for every delta and then republishes a complete
immutable light array.  A small frontier can therefore still incur allocation,
per-cell calls, a whole-mesh transfer, and a whole-chunk read-state copy.

Define a real worker-to-main `chunk_light_patch` format.  It must contain:

```text
chunk coordinate
base voxel/content/light-input revisions
result light version
encoding kind: sparse-runs | section-runs | complete-snapshot
sorted changed-cell payload
affected mesh-section mask/bounds
boundary propagation records for cardinal neighbors
checksum or validated payload length
```

For sparse updates, sort by linear cell index and encode the first index plus
varint index deltas and runs of identical packed-light bytes.  For dense
updates, return an immutable complete light buffer and pointer-swap it rather
than issuing 65,536 setters.  Choose the encoding on the worker using an
explicit measured size threshold.  The main thread must validate the complete
packet and all revisions before changing any live state; malformed, stale, or
partially applicable packets leave the old light and mesh untouched.

The renderer must continue using the old complete publication until the patch
and its corresponding mesh sections are committed atomically.  It must never
interpret an unavailable/in-transition light pointer as an all-zero field.

### Finding 4 - two-stage geometry/light publication can still expose mixed states

An interactive request may publish a geometry-only complete mesh and later a
second complete light-aware mesh.  This can cause two large CPU publications
and two GPU uploads for one edit.  It also creates an observable transition
between geometry built with old packed light and geometry built with the new
light field.  The fallback guards reject some all-zero results, but they do not
prove that every vertex in every published mesh belongs to one coherent
voxel/light revision.

The all-zero geometry-only guard also sets `light_ready_for_render` false and
advances the light-input version.  When no prior GPU publication is available,
that recovery path can leave a chunk absent/dark until a retry completes.  The
incremental-regression guard similarly advances the input version and starts a
full retry, which can turn one bad result into repeated stale work under load.

Required publication invariant:

```text
published_chunk = {
    voxel revision,
    light revision,
    immutable light buffer,
    immutable mesh/section table
}
```

The renderer receives either the old complete tuple or the new complete tuple,
never a mixture.  Geometry may become visible before final light convergence,
but that preview must explicitly retain the old immutable light buffer and
must not clear readiness.  A rejected result must retain the complete previous
tuple while scheduling one coalesced replacement request.

### Finding 5 - neighbor arrival can permanently suppress required light work

The arrival path currently treats this condition as evidence that an
interactive edit owns the next solve:

```text
target->pending_mesh_request_id != 0 || target->voxel_revision > 1
```

`voxel_revision > 1` remains true forever after the first edit.  Therefore a
chunk that was edited once can have all future neighbor arrivals handled as if
an interactive request were still pending, even when no such request exists.
That path marks geometry dirty without advancing/scheduling the required border
light refinement.  This is a plausible cause of chunks or surfaces remaining
partially dark after later streaming.

Replace this historical test with explicit transient ownership state: a
pending interactive request ID/generation, or a queued interactive priority
record that matches the current voxel/light-input revision.  A completed old
edit must have no influence on a later neighbor-arrival decision.

### Finding 6 - pending-request ownership has a queue-to-worker race

The capture worker removes a task from `remesh_capture_tasks_`, materializes
its immutable snapshot, and only afterward submits it to the generation
pipeline.  Orphan recovery runs concurrently on the main thread.  If recovery
looks during that gap, the request is present in neither queue and can be
declared orphaned even though the capture worker owns it.  The chunk marker is
then cleared/replaced and the valid worker result is rejected for pending-ID
mismatch.

A focused runtime validator observed this exact failure pattern before the
review: 1,064 remesh results were stale because of pending-ID mismatch, while
only five had revision mismatches and none had dependency mismatches.  Edit
publication took approximately 43 seconds in that run.  The checkout contains
an in-progress `remesh_capture_processing_request_id_` ownership marker, but it
was not rebuilt and verified before this review was requested; treat the race
as open until the deterministic test below passes.

Every request must be owned by exactly one state at all times:

```text
queued for capture
    -> actively capturing
    -> queued/active in compute pipeline
    -> completed result
    -> committed, rejected, or cancelled
```

No transition may create a moment in which normal orphan recovery sees no
owner.  Cancellation and recentering must use the same explicit state machine.

### Finding 7 - the boundary publication flag has no normal completion clear

`border_mesh_publication_pending` is set for boundary edits and eviction
repair.  The current assignments found by this review clear it during chunk
reset/destruction, but not after the current border mesh and required neighbor
meshes have been uploaded successfully.  A chunk can therefore remain on the
strict neighbor-publication gate for its entire lifetime.  Later edits may wait
behind unrelated dirty/pending neighbors even after the original boundary
transaction completed.

Give the flag a transaction/revision identity, not a permanent boolean.  Clear
that identity only after the GPU publication layer confirms that the target
and every required loaded cardinal neighbor have uploaded the matching
revision.  Missing neighbors use the explicit cap-face contract and do not
wait indefinitely.

### Required main-thread commit architecture

Luna should implement the path as a preparation/commit/publication pipeline:

```cpp
// Main thread: capture a tiny immutable edit intent at the first frame boundary.
edit_intent intent = validate_player_action_and_capture_revisions(world, input);
edit_worker_queue.push(intent);

// Persistent worker: no World access and no world mutex.
prepared_edit result;
result.block_patch = prepare_block_patch(intent);
result.light_patch = calculate_light_frontier(intent.snapshot);
result.mesh_patch = build_affected_mesh_sections(intent.snapshot,
    result.block_patch, result.light_patch);
result.encoded_light_patch = compress_light_patch(result.light_patch);
completed_edit_queue.push(std::move(result));

// Main thread, before rendering: highest-priority bounded commit phase.
while (within_edit_commit_budget() && completed_edit_queue.try_pop(result))
{
    if (!revisions_match(world, result))
    {
        coalesce_and_resubmit_latest(result.chunk);
        continue;
    }
    // No allocation, compression, full scan, worker wait, or destruction here.
    commit_block_patch(world, result.block_patch);
    commit_light_patch_or_swap(world, result.light_patch);
    publish_mesh_sections_atomically(world, result.mesh_patch);
}
publish_immutable_frame_view(world);
render(published_frame_view);
```

The player-visible block change should normally commit at the next main-thread
commit boundary.  If server authority is involved, the client may display a
prediction overlay immediately, but authoritative world storage changes only
after the accepted server result arrives.  Reconciliation must use the same
revision-labelled prepared patch path.

Do not hand the main thread a compressed blob that it must extensively parse or
allocate from.  The worker should validate/decode into a preallocated prepared
commit representation; the compressed bytes are for queue/network/storage
efficiency.  The live commit receives offsets/runs whose bounds and capacity
have already been checked.

### Strict tests required before implementation can be called fixed

The tests must reproduce actual scheduling pressure, inspect every publication
frame, and fail on transient invalid states.  Add all of the following:

1. **Startup edit priority:** continuously stream/generate chunks while placing
   and deleting blocks in a loaded visible chunk.  At least 95% of accepted
   edits must commit authoritative block storage by the next frame boundary,
   publish changed geometry within two rendered frames, and never wait behind
   ordinary generation work.  Report p50/p95/p99/max separately for storage,
   CPU mesh, light patch, and GPU publication.
2. **No-dark-frame invariant:** capture the uploaded identity and packed-light
   range every rendered frame before, during, and after interior and border
   edits.  A previously lit visible chunk may never publish an all-zero light
   mesh, clear its drawable tuple, or switch to a mesh/light revision mixture,
   even for one frame.
3. **Incremental equivalence:** for every opaque/air/transparent/emissive
   transition, compare the patched result with a clean full reference solve.
   Cover interiors, all four faces, all four corners, vertical extremes, water,
   missing neighbors, arriving neighbors, and simultaneous edits on multiple
   chunks.
4. **Bounded affected work:** an ordinary single interior edit must not clear or
   rebuild the complete light field.  Assert a strict upper bound on visited
   cells/sections and prove that all unaffected light bytes remain unchanged.
   If the frontier legitimately reaches the whole chunk, the test must record
   why instead of silently accepting every full rebuild.
5. **Capture ownership race:** pause the capture worker after it removes a task
   but before pipeline submission, run orphan recovery repeatedly, then resume.
   The pending ID must remain unchanged and the result must commit exactly once.
6. **Supersession:** pause each ownership transition, submit a newer edit to the
   same cell/chunk, and prove that only the latest revision is published, old
   work is cancelled once, and the queue remains bounded.
7. **Neighbor-arrival after completed edit:** edit a chunk, fully settle it,
   later stream each cardinal neighbor, and compare border light/geometry with
   a clean world.  This specifically detects the permanent
   `voxel_revision > 1` classification bug.
8. **Compressed patch tests:** round-trip sparse runs, dense snapshots, duplicate
   and overlapping runs, malformed lengths, out-of-range indices, truncation at
   every byte, stale base revisions, checksum failure, allocation failure, and
   transactional no-change-on-error behavior.
9. **Lock budget:** instrument world-lock wait and hold time separately.  Under
   startup generation plus edits, enforce a strict main-thread commit hold-time
   budget and fail when a commit allocates, waits for a worker, scans a complete
   chunk, performs destruction, or writes diagnostics while holding the lock.
10. **Long session:** move/recenter continuously while editing for at least ten
    minutes.  Queue sizes and stale counts must remain bounded, no chunk may
    remain light-dirty after work drains, and every visible publication must
    satisfy the no-dark-frame invariant.

Use frame counts as the primary deterministic acceptance gate and wall-clock
latency as a secondary performance gate.  Wall-clock results must still report
p50/p95/p99/max on normal and analytics builds, Windows/Linux/macOS, and both
low-core and high-core configurations.  A one-shot success or a permissive
500-1000 ms bound does not prove the user's required fraction-of-a-second
response.

### Implementation order from this review

1. Finish and deterministically verify the request-ownership state machine so
   edits cannot create stale-result storms.
2. Replace the permanent `voxel_revision > 1` arrival classification with
   current request ownership.
3. Introduce immutable double-buffered light/mesh publication and the prepared
   compressed patch format.
4. Shrink the world-lock/main-thread commit to bounded validation and swap/apply
   operations; move all preparation and cleanup outside it.
5. Add section-local mesh patches so a small edit does not publish two complete
   chunk meshes.
6. Give border publication a revision-labelled transaction and explicit GPU
   completion acknowledgement.
7. Run the complete strict test matrix above and perform runtime screenshot/
   frame-trace verification before declaring flicker, incorrect light, or edit
   latency resolved.

## Implementation pass: request ownership and arrival classification

The first two priorities from this review now have an initial implementation
and focused local evidence.  This does not close the complete lighting goal;
the later publication, lock, section-patch, and GPU-acknowledgement phases
remain open.

Implemented in this pass:

1. `WorldChunk` now records whether its pending remesh request is interactive
   and the voxel/content/light-input revisions owned by that request.  The
   marker is set only after the capture queue accepts the task and is cleared
   through one helper on completion, cancellation, stale-result recovery,
   loading, reset, and destruction.
2. Replacing a queued capture task invalidates its cancellation token before
   the old task is removed.  The new task becomes the owner only after its
   queue insertion succeeds, so queue-allocation failure cannot leave a false
   pending owner behind.
3. Neighbor-arrival classification no longer uses the historical
   `voxel_revision > 1` test.  It now requires a current interactive request
   or a queued interactive priority entry whose captured revisions still match
   the chunk.
4. The cached boundary summary lookup now translates the source-to-target
   direction into the physical source and target faces correctly.  This keeps
   the face-change fast path conservative without rescanning or suppressing a
   valid cross-chunk light update.
5. The capture worker's processing-request marker remains visible while it
   owns a task between capture-queue removal and compute-pipeline submission.
   Orphan recovery therefore cannot clear that task's chunk marker during the
   handoff.
6. GPU upload prioritization no longer treats every chunk with a historical
   `voxel_revision > 1` as permanently interactive.  It uses the current
   interactive request owner or a revision-matched interactive publication
   marker instead.

Verification performed locally:

- `make -j2 ft_vox` completed and produced both `ft_vox.exe` and the required
  `ft_vox_analytics.exe`.
- `ft_vox.exe --validate-block-edit` passed, including the new
  `completed-edit neighbor arrival scheduled` regression.
- `ft_vox.exe --validate-renderer-publication` passed after the final rebuild,
  including the initial lit upload, edit publication, border deletion, and
  neighbor GPU upload.  The measured edit upload latency was 38 ms on the
  successful diagnostic run.
- The focused block-edit validator reported p50 40 ms, p95 46 ms, and maximum
  55 ms on the final run.  Border operations remain separately visible in the
  validator output because they are more expensive than interior edits.

After replacing the historical GPU-priority check, the focused validator still
passed with p50 39 ms, p95 41 ms, and maximum 56 ms.  The renderer-publication
validator also passed with a 33 ms edit upload and a stable border-delete pair.

After packing the prepared delta records, both validators still passed.  The
latest block-edit run reported p50 27 ms, p95 42 ms, and maximum 42 ms; the
renderer-publication run reported 38 ms edit upload and a stable border-delete
pair.  This verifies that the five-byte representation preserves the light
publication behavior.

Border publication now carries the voxel revision that created the pending
transaction.  GPU gating ignores stale flags after a later unrelated edit or
slot reuse; only a flag whose revision still matches the chunk can delay a
publication.  The final GPU-acknowledged clear is still open and requires a
mutable publication-ack channel from the renderer.

The deterministic capture pause/resume race from Finding 6 is still required;
the existing processing marker closes the observed ownership gap but is not a
substitute for that controlled scheduler test.  The renderer-publication
validator now waits for a stable boundary pair and has passed its GPU-side
no-dark-frame and border-publication checks locally.

### Implementation pass: compact incremental light publication

The next priority now keeps the full worker-side light field private to the
worker.  Incremental remeshes still build a complete temporary field so the
mesh is generated from a coherent light view, but after mesh generation the
result retains only `incremental_light_deltas`: changed local coordinates and
their packed light values.  The full temporary field is released before the
result enters the commit queue.  The main-thread commit applies the validated
patch to the existing authoritative light field, so unrelated cells are not
copied or cleared.

The prepared delta record is now explicitly five bytes (`x`, little-endian
`y`, `z`, and packed light), with accessors and a size assertion.  This avoids
ABI padding in the worker-result queue without making the main thread decode a
second transport format.  Variable-length sparse-run encoding and section
patch selection remain future work for network/storage export; the internal
prepared commit representation is already bounded and allocation-free during
application.

An incremental result is accepted without a full light payload only when its
delta collection completed successfully.  An incomplete patch or a full remesh
continues to require the complete light field.  This preserves a safe fallback
for cancellation, malformed results, and missing light baselines while
reducing cross-thread ownership and transfer pressure for normal block edits.

The renderer-publication fixture was also changed to wait for a real loaded
breakable/occluding boundary pair instead of checking once after eight frames.
It now continues the bounded stream/update/render loop for up to 30 seconds and
reports ready, pending, active-generation, and remesh-queue counts on failure.
This prevents a startup scheduling race from being mistaken for a lighting
publication failure.

The remaining part of this priority is immutable publication of the mesh/light
pair.  The current pass prevents the worker's full light buffer from crossing
the result queue, but live mesh replacement and GPU acknowledgement still need
a versioned double-buffer publication transaction before this priority is
complete.  The next implementation priority remains shrinking the global
world-lock hold around streaming/commit work; that must be done with an
explicit prepare/commit split because the current renderer reads the live
chunk mesh directly.

### Implementation pass: transactional compact-light section commit

The compact incremental path now prepares every affected 16-block light
section before it changes the live chunk.  A section is cloned only when the
worker delta list touches it, the validated deltas are applied to that private
section, and the prepared section is moved into the authoritative chunk only
after all affected sections have completed successfully.  Therefore an
allocation failure while making a section non-uniform cannot leave a prefix of
the frontier in the live light field.  Unaffected sections are not copied or
cleared.

This is an intermediate transaction boundary, not the final publication
design: section preparation still occurs on the main-thread commit path and
can copy one affected section.  The next publication pass must move this
preparation into the persistent worker result (or a preallocated section-patch
pool), so the main thread performs only revision validation and no allocation,
full section copy, or decoding before the section swap.  The full immutable
mesh/light tuple and GPU acknowledgement remain required before this goal is
complete.

Verification after this pass:

- `make -j2 ft_vox` completed and produced both normal and analytics
  executables.
- `--validate-block-edit` passed, including rapid supersession, emissive
  restoration, all four boundary faces, cross-chunk publication, simultaneous
  border edits, and completed-edit neighbor arrival.
- `--validate-renderer-publication` passed on repeated runs, including the
  lit edit publication, stable border-delete pair, and neighbor GPU upload.
  One first invocation exited with a Windows heap-corruption status before
  diagnostics were emitted; five immediate reruns passed.  This must be
  reproduced under a sanitizer/debug build before the pass is treated as
  fully cleared.

Post-change verification:

- `ft_vox.exe --validate-block-edit` passed after the final rebuild.  It
  exercised incremental edits, rapid supersession, emissive restoration,
  four border directions, simultaneous border edits, cross-chunk publication,
  and completed-edit neighbor arrival.
- `ft_vox.exe --validate-renderer-publication` passed after the final rebuild.
  It observed a lit edit publication, a stable border-delete pair, and the
  neighbor GPU upload without a publication gap.  The measured edit upload
  latency was 68 ms in that run.
- The final `make -j2 ft_vox` produced both `ft_vox.exe` and
  `ft_vox_analytics.exe`.

The repeated block-edit gate remains variable under host load (the final run
reported p50 40 ms, p95 41 ms, maximum 46 ms; an earlier run reached 122 ms),
so performance work is not considered complete merely because correctness
validators pass.

## Required verification framework for lighting and edit publication

The lighting work must not be accepted from visual inspection, one successful
run, or a validator that inspects only the final settled chunk.  The failures
reported during development are transient and scheduling-sensitive: a chunk
can be black for one frame, a border face can disappear briefly, or an edit can
remain logically applied while the old GPU mesh is displayed for seconds.
Tests must therefore observe every stage and every rendered frame.

### Test harness architecture

Build one reusable fixture that owns the same components as an actual game
session:

```text
authoritative World
    -> persistent generation/light workers
    -> completed-result queues
    -> bounded main-thread commit
    -> CPU mesh publication
    -> test GPU-publication adapter
    -> frame observation record
```

The adapter must exercise the real mesh-selection, upload-budget, identity,
and neighbor-gating code.  A validator that stops after checking
`WorldChunk::mesh` does not prove that the player saw the update.  On machines
where an OpenGL context is unavailable, use a deterministic fake upload
backend that consumes the same publication API and records the exact identity
which would have been uploaded.  Keep at least one real OpenGL integration run
on every supported platform.

Each frame record must contain at least:

```cpp
struct lighting_frame_observation
{
    uint64_t frame_index;
    uint64_t edit_id;
    uint64_t voxel_revision;
    uint64_t light_revision;
    uint16_t content_version;
    uint16_t light_input_version;
    uint16_t computed_light_input_version;
    uint64_t cpu_mesh_revision;
    uint64_t uploaded_mesh_revision;
    uint64_t uploaded_voxel_revision;
    ft_bool block_state_visible_to_queries;
    ft_bool light_ready_for_render;
    ft_bool mesh_dirty;
    ft_bool remesh_pending;
    ft_bool gpu_upload_pending;
    uint8_t minimum_vertex_light;
    uint8_t maximum_vertex_light;
    uint32_t nonzero_light_vertices;
    uint32_t visible_face_count;
    uint64_t worker_queue_wait_nanoseconds;
    uint64_t worker_execution_nanoseconds;
    uint64_t main_commit_nanoseconds;
    uint64_t gpu_publication_nanoseconds;
};
```

Do not infer one stage from another.  Record separate timestamps for intent
submission, authoritative block commit, light result completion, light commit,
CPU mesh publication, GPU upload, and first rendered frame using that upload.
All records must carry edit/request and revision identities so superseded work
cannot accidentally satisfy the assertion for a newer edit.

Use a manually advanced test clock for deterministic queue and timeout tests.
Wall-clock tests remain necessary for performance, but they must be a separate
suite so a slow CI host cannot change correctness ordering.

### Latency definitions and strict acceptance gates

Measure these intervals independently:

```text
T0 intent accepted
T1 authoritative block state committed
T2 prepared light result available
T3 light patch committed
T4 matching CPU mesh published
T5 matching GPU mesh uploaded
T6 first rendered frame containing that GPU identity
```

Report at minimum:

```text
authoritative latency = T1 - T0
light latency         = T3 - T1
CPU mesh latency      = T4 - T1
GPU publication       = T5 - T4
visible latency       = T6 - T0
```

Frame counts are the primary correctness gate:

- At least 95% of accepted local edits must commit authoritative block state
  by the next main-thread commit boundary.
- At least 95% must publish changed geometry within two rendered frames after
  authoritative acceptance.
- At least 99% must become visible within four rendered frames under the
  standard startup-pressure workload.
- No accepted edit may remain invisible for more than eight rendered frames.
  Reaching this limit is a failure with a queue/revision dump, not a warning.
- Interactive edit work must overtake ordinary generation and distant remesh
  work already waiting in queues.  It may not overtake an older interactive
  edit whose ordering is required by the same chunk revision.

Wall-clock targets should be evaluated at uncapped rendering speed and at a
fixed 60 Hz test cadence:

- p50 visible latency: at most 35 ms;
- p95 visible latency: at most 75 ms;
- p99 visible latency: at most 120 ms;
- absolute maximum: at most 200 ms in the standard workload.

These are initial regression limits, not permission to spend the complete
budget.  Tighten them when stable baselines exist.  Never raise them merely to
make a regression pass.  A platform-specific adjustment requires retained
traces proving that the delay is external scheduling noise rather than queue
starvation, lock contention, excess copying, or duplicate work.

Every latency test must include a warm run and a cold/startup run.  Execute at
least 1,000 edits for a normal sample and at least 10,000 for a scheduled
stress job.  Report sample count, p50, p90, p95, p99, maximum, median absolute
deviation, missed-frame count, and the slowest edit's complete stage trace.
Average latency alone is insufficient because it hides the reported stalls.

Run normal and analytics executables separately.  The analytics build must
meet correctness and have its own latency baseline; it must not silently reuse
normal-build results.  Store executable hash, Minecraft commit, Libft commit,
compiler, optimization level, CPU count, OS, clock source, render cadence, and
test seed with every result.

### Deterministic light-correctness oracle

For each incremental operation, create two worlds from the same immutable
starting snapshot:

```text
test world       -> apply incremental light frontier and normal publication
reference world  -> apply the same block edits, then run a clean full solve
```

Drain both worlds to quiescence and compare every packed light byte in the
target chunk and every affected loaded neighbor.  Compare skylight and block
light nibbles separately as well as the packed value.  Also compare generated
mesh faces and every vertex's packed light.  A matching final checksum alone
is useful for speed but failures must identify the first coordinate, expected
value, actual value, source channel, and relevant revisions.

The oracle must not share solver state, queues, light buffers, snapshots, or
revision objects with the incremental world.  Otherwise the same bug can exist
on both sides and make the comparison pass.  Prefer the simplest complete
reference solve even if it is too expensive for gameplay.

Verify invariants during every intermediate frame, not only after quiescence:

- A previously drawable lit chunk must retain one complete drawable
  publication while replacement work is pending.
- The renderer must never treat a missing, rejected, or in-transition light
  buffer as all zero.
- A published tuple's mesh vertices must be derived from its declared voxel
  and light revisions.
- `computed_light_input_version` may equal the current input version only when
  the corresponding result was committed completely.
- Stale or malformed results must leave blocks, light, mesh, publication
  identity, and readiness unchanged.
- Unaffected light cells must remain byte-for-byte unchanged after a bounded
  local edit.
- No chunk may remain dirty or pending after queues drain unless a diagnostic
  names its currently valid owner.

### Complete lighting scenario matrix

Run each scenario for placement and deletion, for all applicable light
channels, and at every relevant spatial category:

```text
Spatial position:
  interior
  west/east/north/south face
  four horizontal corners
  section boundary y=15/16, 31/32, ...
  world bottom and world top

Block transition:
  air -> opaque
  opaque -> air
  transparent -> opaque
  opaque -> transparent
  air -> water
  water -> air
  air -> emissive
  emissive -> air
  emissive level/color change

Neighbor state:
  all cardinal neighbors loaded/current
  one neighbor missing
  neighbor generated while edit is pending
  neighbor evicted while edit is pending
  neighbor replaced in the same storage slot
  neighbor light stale
  simultaneous edits on opposite sides of one border

Scheduling state:
  idle workers
  startup generation saturated
  distant generation backlog
  remesh backlog
  result queue full/backpressured
  newer same-chunk edit supersedes active work
  camera recenter during capture, solve, commit, and upload
```

Use table-driven tests so adding a block type or boundary does not require a
new hand-written validator.  Run all four cardinal directions independently;
rotational symmetry must not be assumed because face-index translation bugs
have already occurred.

Add dedicated skylight layouts:

- open vertical column from world top to the edited cell;
- roof added and removed above a column;
- cave opening created from the side;
- overhang with lit air below it;
- transparent and water columns;
- terrain surface crossing a chunk border at different heights;
- generated chunk arriving next to a previously edited chunk.

Add dedicated block-light layouts using placeholder emissive blocks at source
levels 1, 7, and 15 and at several color encodings once colored light lands.
Test overlapping sources, source removal while another source survives,
stronger/weaker replacement, propagation across all borders, and complete
extinction after the last source is removed.

### Flicker and publication-coherence tests

The no-flicker test must retain the uploaded publication from the frame before
the edit and inspect every following frame until convergence.  Fail if any of
these occur even once:

- a populated previously lit chunk uploads a mesh with zero lit vertices;
- the renderer invalidates an old uploaded mesh solely because its replacement
  is pending;
- a chunk disappears while its coordinates still contain visible blocks;
- an adjacent face disappears before the neighboring replacement is uploaded;
- the target uses a new voxel revision with an old incompatible light revision;
- the GPU identity moves backward or skips to a stale request;
- a border transaction remains active after all matching uploads acknowledge;
- two uploads for one edit expose a geometry/light mixture between them.

Record a compact frame trace around every failure: 16 frames before the edit,
all frames during the edit, and 16 frames after convergence.  Include target
and four cardinal neighbors.  Produce a software image/hash for each frame and
retain actual screenshots for the first differing frame in CI artifacts.  A
one-frame black publication is a hard failure even if the next frame repairs
it.

The fake GPU adapter must implement the first condition directly: if the
previous uploaded mesh had nonzero lit vertices, a newer uploaded mesh with
zero lit vertices is an invalid publication unless the test scenario proves
that the complete visible geometry is intentionally unlit. This catches a
light-buffer replacement with an all-zero intermediate result instead of
waiting for the final oracle comparison.

### Bounded-work and duplicate-work tests

Instrumentation used by these tests must count work, not merely duration:

```text
captured block/light bytes
captured chunks and halo slabs
frontier nodes visited and propagated
affected light sections
affected mesh sections
full-light solves requested
full meshes generated
results cancelled, superseded, rejected, and committed
main-thread allocations
world-lock wait/hold time
CPU/GPU uploads and uploaded bytes
```

For a normal interior edit, assert that unrelated chunks receive no light job,
unaffected sections are not copied or uploaded, and at most one final
light-aware publication is produced after any explicitly allowed immediate
geometry preview.  If a frontier legitimately reaches a complete chunk or
neighbor, the result must carry a reason code and the test must verify the
reference solve requires that reach.  Never classify every whole-chunk solve
as acceptable merely because it eventually produces correct light.

Add a duplicate-work detector keyed by:

```text
(chunk coordinate, voxel revision, light-input revision, operation kind)
```

Two active jobs with the same key are a failure unless one is in an explicit
cancellation handoff state.  When an older job is superseded, it must cease
within a bounded number of worker steps and must never publish.

### Locking and main-thread ownership tests

Workers must be tested with an access sentinel that aborts a test if worker
code receives or dereferences `World`, a live `WorldChunk`, renderer state, or
`world_data_mutex_`.  Worker input must consist only of owned immutable data.

Instrument lock acquisition and release with thread ID, purpose, wait time,
hold time, and whether an allocation, full scan, I/O operation, diagnostic
write, queue wait, or object destruction occurred inside the lock.  Under the
startup-plus-edit workload:

- no worker may acquire the world lock;
- p95 main-thread world-lock wait must remain below 100 microseconds;
- p99 edit-commit lock hold must remain below 250 microseconds;
- no edit commit may wait on a condition variable or worker future;
- no logging or file output may occur while the world lock is held;
- no complete chunk scan or full mesh destruction may occur in the bounded
  commit phase.

Treat these thresholds as initial regression gates and retain histograms.  A
timeout should dump lock owner, waiters, queue ownership, active request IDs,
and all thread stacks before aborting the test.

### Fault injection and transactional guarantees

Inject failure at every allocation and queue transition used by edit/light
publication:

```text
intent enqueue
snapshot/read-state ownership
delta storage growth
prepared section/patch allocation
mesh allocation
capture queue -> active capture
capture -> compute queue
compute -> completed queue
retired mesh/result queue
GPU upload allocation/failure
```

For every injected failure, snapshot the complete authoritative and published
state before the operation.  If the API reports failure, assert that no partial
block, light, mesh, revision, readiness, pending-owner, or GPU identity change
escaped.  If the block commit is intentionally irreversible by that stage,
the API must report an accepted edit with deferred presentation rather than a
generic failure suggesting nothing happened.

Pause hooks are test-build-only and must compile out of release builds.  Add
deterministic pause/resume points before and after each ownership transition.
Use them to reproduce cancellation, orphan recovery, recentering, shutdown,
and same-chunk supersession without sleeps or timing guesses.

### Long-running, rollover, and shutdown coverage

Run a deterministic long session which walks a spiral through the world while
editing interiors and borders, loading and evicting chunks, and repeatedly
returning to prior coordinates.  Include enough operations to reuse every
chunk slot many times.  At fixed intervals compare a sample against the full
light oracle and verify queue/retired-memory sizes remain bounded.

Use a reduced-width test version of the content and light version counters to
force multiple rollovers quickly.  Verify that rollover never makes stale
light appear current and that zero remains reserved for the documented invalid
state.  Also test 64-bit request/revision comparison near wrap boundaries.

Shutdown tests must pause work in capture, propagation, result handoff, main
commit, and GPU publication.  Shutdown must cancel or drain each owner exactly
once, preserve the last complete publication until rendering stops, join
persistent workers without holding the world lock, and release all retired
payloads.  Run repeated initialize/world-open/world-close cycles under memory,
address, undefined-behavior, and thread sanitizers.

### Required test tiers and artifacts

Use the following tiers:

1. **Per-change fast tier:** compact table of interior/border edits,
   transactional failure cases, supersession, and per-frame no-dark checks.
2. **Pull-request tier:** complete transition/spatial matrix, startup pressure,
   real/fake GPU publication, normal plus analytics builds, and at least 1,000
   latency samples on each supported OS.
3. **Scheduled stress tier:** at least 10,000 edits, ten-minute streaming
   session, forced version rollover, fault-injection sweep, high/low core
   counts, and sanitizer runs.
4. **Release-candidate tier:** real GPU drivers on Windows/Linux/macOS,
   multiplayer/local-server reconciliation, suspend/resume, shutdown, and a
   retained visual frame trace for every graphics backend.

Every failed run must retain:

- the exact command, seed, configuration, commits, and executable hash;
- machine-readable latency and work-count JSONL;
- last-started test and current frame/edit/request identities;
- target plus neighbor revision histories;
- queue ownership and lock diagnostics;
- sanitizer report, stack trace, and core/minidump when available;
- the compact frame trace and first invalid rendered image.

A run is accepted only when all correctness invariants pass, latency
percentiles meet their gates, no unexplained stale/orphan work remains, and
the final test summary explicitly reports every scenario count.  Skipped tests
must name the unavailable capability and must not turn the required platform
matrix green.  The lighting implementation is not complete until this suite
passes repeatedly on normal and analytics builds without intermittent crashes,
dark frames, border holes, or multi-second edit publication.

### Harness implementation and execution contract

The first validator implementation lives in `src/validators/LightingTestHarness.*`
and is exposed through `--validate-lighting-harness`. It is deliberately a
validator-only executable path. It does not add instrumentation, worker
threads, file output, or synchronization to normal gameplay or release
rendering.

Run the focused checks after building both executable variants:

```text
make -j2 ft_vox
ft_vox.exe --validate-lighting-harness
ft_vox_analytics.exe --validate-lighting-harness
```

The validator first waits for the center chunk and its four cardinal
neighbors to have a valid, current light buffer and a settled mesh. Before
performing an edit it computes an independent full-light reference from the
captured immutable block snapshot. This startup baseline is required: a
post-edit border mismatch is not useful if the neighbor was already wrong
before the edit.

The oracle must pass world origins, not chunk coordinates, to the Libft light
solver. For a chunk `(chunk_x, chunk_z)`, the solver origin is:

```text
world_origin_x = chunk_x * GAME_VOXEL_CHUNK_WIDTH
world_origin_z = chunk_z * GAME_VOXEL_CHUNK_DEPTH
```

For each tested chunk, compare every packed light byte in all 16 x 256 x 16
cells. On a mismatch print and retain the chunk coordinate, local coordinate,
expected/actual packed value, live/snapshot block IDs, first opaque block above
the cell, voxel/light/content/input/computed revisions, and snapshot lookup
error. This distinguishes an oracle bug, a block-snapshot mismatch, direct
sky seeding defect, and incremental propagation defect.

The frame record must also retain stream-progress state: playable required,
playable drawable, and playable failed counts; deferred-edit count and cursor;
candidate, ready, pending, retryable, and failed candidate counts; and the
stream frame/progress-frame pair. These values are necessary to distinguish a
worker or candidate starvation failure from a light publication failure. The
same values must be printed in a timeout diagnostic so a zero remesh queue with
pending stream work is not misclassified as an idle, healthy pipeline.

Each edit test records the full stage sequence. `T0` is the edit start,
`T1` is the successful authoritative block query/commit, `T2` is the first
current light observation, and `T3` is the first matching mesh publication.
The current fast gate is no more than eight observed frames and 200 ms from
`T0` to `T3`. A failure must retain every frame, including valid/current,
mesh-dirty/pending state, mesh light range, visible face count, priority and
interactive queue depth, active generation count, queue peak, stale-result
categories, completed incremental/full counts, cancellation count, scanned and
propagated cell counts, and stream error.

The edit matrix must include, independently, placement and deletion at local
`x=0`, `x=15`, `z=0`, and `z=15`, followed by a full oracle comparison of the
affected neighbor. It must not accept a mesh revision alone: the mesh must be
paired with current light and the authoritative voxel revision. A retained
valid old publication is allowed while replacement work is pending, but a
zero-light replacement, stale revision pair, missing border face, or dirty
chunk with no pending/owning request is a hard failure.

The JSONL report is part of the test result, not optional logging. It contains
one frame record per observation and one edit record per attempted edit. A
superseded edit is explicitly marked with its superseding frame and is not
silently counted as an incomplete latency sample; the newer edit must still
reach the normal light/mesh/oracle gates. A failed report write is itself a
validator failure and must be reported with its return code. CI should upload
the normal report, analytics report, and
failure report as separate artifacts so analytics overhead cannot hide a
normal-build regression.

The focused harness now covers transparent and water transitions, registered
emissive transitions at source levels 1, 7, and 15, all four face borders, a corner transition and its
cardinal/diagonal neighbors, simultaneous edits on both sides of one chunk
border, per-frame 3 x 3 publication observations, and the independent
full-light oracle. The opposite-border case commits both edits before the
next stream update, then requires both chunks to publish current light and
matching meshes before either result is accepted. It also submits two same-cell edits before
the first result can publish and records the older edit as superseded; only
the newer edit is eligible for the latency gate, while both remain visible in
the report. The separate lifecycle validator covers repeated shutdown and
camera recentering. Queue backpressure/rejection and eviction/reload are
already covered by the focused and lifecycle validators. The lifecycle
validator now proves that a recentered chunk is removed from the old index,
that a missing neighbor is never treated as lit or drawable, and that the same
coordinate can be generated again after returning the camera.
The matrix also runs transactional input failures for invalid Y coordinates,
missing chunks, and missing remesh snapshots; each must leave the original
block, voxel revision, content version, light-input version, and published
light unchanged before the oracle is rerun.
It also runs a three-step same-cell supersession sequence (opaque placement,
transparent replacement, then deletion), marks both obsolete requests, and
requires only the final request to reach the current-light/current-mesh oracle
gate. This is separate from the two-request place/delete case so cancellation
and repeated replacement cannot hide behind one simple permutation.
The matrix also checks the reserved-zero rule and both sides of the 16-bit
light-input rollover, plus the signed-distance comparison at the 64-bit
request/revision wrap boundary.
The matrix also sweeps deterministic CMA allocation limits during an edit and
checks that a failed placement leaves the block and revision tuple unchanged.
If a lower-level failure mutates the block before returning an error, the
harness records that as a transactional failure and attempts cleanup only
after restoring the allocator limit; it never treats the subsequent cleanup
as proof that the original operation was atomic.
The focused harness already runs a 32-edit same-cell burst and verifies that
the final state drains through the normal oracle. The full latency tier
must then run at least 1,000 normal samples and the scheduled stress tier at
least 10,000 samples, reporting p50/p90/p95/p99/max and the slowest complete
trace rather than only an average.

### Harness implementation status: first diagnostic pass

The first local diagnostic pass exposed two separate classes of failure that
must remain visible in CI:

- The startup oracle passed after the solver was given world origins instead
  of chunk coordinates. This corrected a validator defect that only affected
  nonzero chunk coordinates.
- The focused normal harness then failed the strict first-edit gate: the edit
  remained dirty with an interactive remesh pending after nine observations.
  The retained report showed repeated stale results, so this is not accepted
  as convergence.
- The analytics harness reached the complete matrix but failed the latency
  gate with a 323 ms edit sample against the 200 ms absolute limit. Analytics
  commit traces also showed tens of milliseconds of main-thread commit time;
  analytics overhead must not be dismissed as a correctness pass.
- The existing block-edit validator independently failed emissive-delete
  region convergence with a dirty target and a pending remesh. This confirms
  that the new harness is detecting an existing scheduler/publication defect,
  rather than creating it.

The next implementation pass must use the frame JSONL to identify each stale
result category and request owner, then fix the ownership/scheduling cause.
Do not relax the eight-frame/200-ms gate, clear `mesh_dirty` in the validator,
or count a geometry-only publication as light convergence. After the fix,
rerun normal and analytics harnesses plus the existing block-edit and renderer
publication validators; retain the reports from both passing and failing
runs. Only then add the 1,000-sample latency tier and 10,000-sample scheduled
stress tier described above.

The current harness implementation additionally records the live light version
and the request-captured voxel/content/light-input versions on every frame.
Each completed edit records cumulative scanned-cell and incremental/full
completion counters at edit start and mesh publication. The acceptance check
requires an incremental light publication, at least one incremental completion,
and fewer than one complete chunk-volume of scanned cells for that edit. This
is a hard test of the intended local-frontier algorithm; a full relight or a
geometry-only result cannot silently pass as an incremental edit. The matrix
now includes a corner transition and validates both cardinal neighbours, in
addition to the four face positions and water, transparent, and emissive
transitions. If an edit times out, the report must show the request ID and all
captured versions so a pending-owner mismatch can be distinguished from slow
worker execution.

The latest local focused runs are intentionally retained as failing evidence:
the normal build completed 15 edits and timed out on emissive placement with
`light_input=21`, `computed_input=20`, and a still-owned pending request; the
analytics build reproduced the same mismatch at approximately 295 ms. Both
runs also emitted zero-light geometry-only diagnostics before the final light
result. These observations identify a production scheduling/publication
problem for the implementation owner; the validator is not permitted to
declare success merely because the block edit or an intermediate mesh became
visible.

The latest normal run also completed the harness lifecycle boundary without
heap corruption after `World::destroy()` was made idempotent. The allocator
sweep reproduced a production transaction defect: limits 1, 8, 32, and 128
returned allocation failure without changing the test cell, while the 512 and
2048 limits returned failure after the cell was observed as the new block and
its revision tuple had changed. The harness reports this as
`allocation-failure-rollback` and continues with a restored world so later
scenario results remain independent. This is a required failure result, not a
reason to skip the sweep: the edit path must either roll back completely or
return an explicit accepted/deferred result.

The focused normal report currently contains 24 edit rows and 22 scenarios
after the three source-level emissive rows were added. Version rollover,
transactional invalid-input handling, supersession permutations, opposite
border setup, and the lifecycle validator pass their harness-side assertions.
The run still correctly fails on production queue drainage, border/oracle
convergence, stale-light publication, allocation rollback, and strict latency
percentiles.

The rebuilt pull-request stress tier then completed independently in both
variants. Normal produced 1,000 edit rows, 31 oracle checkpoints, and
`p50=258 ms`, `p95=390 ms`, `p99=421 ms`, `max=461 ms`; analytics produced the
same 1,000 edit rows and checkpoints with `p50=331 ms`, `p95=531 ms`,
`p99=581 ms`, `max=636 ms`. Both reports have the same two scenario failures:
the publication invariant and the latency gate. This confirms that analytics
changes the timing distribution but does not hide the correctness failure.

The first scheduled 10,000-edit attempt was deliberately watchdog-stopped at
40 minutes after remaining responsive but producing no final report. CPU time
and retained working memory kept increasing to roughly 2,400 seconds and
600 MB. The run is therefore recorded as an unresolved scheduled-tier
resource failure. The harness now flushes progress every 100 edits and the
report header includes build variant, seed, clock source, and render distance.
The scheduled fixture now bounds retained frame observations to 8,192 and
reports both retained and dropped frame counts; edit rows and oracle
checkpoints remain complete. The next scheduled implementation step is a
rerun with that bounded trace plus a watchdog artifact, not a relaxed timeout
or a smaller correctness matrix.

### Test execution protocol and stress coverage

The validator has three deliberately separate workloads. A fast failure must
leave a small report that is easy to inspect, while long queue-pressure runs
remain opt-in so they cannot make normal gameplay or every local build slow.

The focused workload is:

~~~
ft_vox.exe --validate-lighting-harness
ft_vox_analytics.exe --validate-lighting-harness
~~~

It establishes a clean world, waits for a complete 3 x 3 chunk baseline,
compares every light cell against the independent solver, and then runs the
spatial and transition matrix. Every attempted edit is recorded even when an
earlier scenario fails. This prevents a border failure from hiding a later
corner, water, transparent, or emissive failure.

The pull-request latency workload is:

~~~
ft_vox.exe --validate-lighting-stress
ft_vox_analytics.exe --validate-lighting-stress
~~~

It performs 1,000 deterministic alternating placements and removals against
six prepared cells covering an interior, all four face borders, and a corner
of the center chunk while persistent generation and remesh workers remain
active. Every edit follows the complete publication contract:

~~~
T0  begin edit
T1  authoritative block state committed and immediately queryable
T2  current light for the new block input is observable
T3  a mesh containing that current light is published
~~~

The scheduled stress workload is intentionally heavier:

~~~
ft_vox.exe --validate-lighting-scheduled-stress
ft_vox_analytics.exe --validate-lighting-scheduled-stress
~~~

It performs 10,000 edits and belongs in scheduled CI, sanitizer builds, and
pre-release verification. Run it with low and high worker counts, with and
without competing generation work, and after camera recentering. The normal
and analytics binaries use the same fixture. Analytics may change measured
time, but must never change correctness, publication ordering, oracle results,
or ownership diagnostics.
The validator emits a progress line at least every 100 edits containing the
edit index, frame, interactive and background queue depths, active workers,
stale-result count, and cumulative scanned cells. A scheduled job that emits
no progress for a bounded watchdog interval must be captured as a hang with
the current process dump; it must not be silently allowed to consume the CI
job timeout.

The frame trace may be bounded for long runs, but correctness state may not be
bounded by that trace window. Keep the latest observation for every observed
chunk in a separate per-chunk table and evaluate every transition as it is
recorded. Persist the first invariant failure in the report header and
summary, even if the offending frame is later evicted from the diagnostic
deque. Edit rows, oracle checkpoints, and invariant failures must therefore
remain complete when frame retention drops observations.

Latency must be measured with a monotonic clock and reported per stage, not
only as one end-to-end number. Each edit record contains T1, T2, and T3
elapsed milliseconds. The summary contains attempted, complete, incomplete,
p50, p90, p95, p99, and maximum T0-to-T3 values. MAD (median absolute
deviation) is printed in diagnostics so a stable slowdown is distinguishable
from a small number of outliers. The gates are:

~~~
95% of edits: T0-to-T3 <= 75 ms
99% of edits: T0-to-T3 <= 120 ms
every edit:   T0-to-T3 <= 200 ms and <= 8 observed frames
diagnostic:   p50 <= 35 ms
~~~

An edit that reaches a mesh using an old light input is not complete. An edit
that reaches current light but never publishes a matching mesh is not
complete. A timeout, oracle mismatch, invalid publication transition, missing
incremental completion, or full-chunk scan is a hard failure.

### Detailed execution procedure for latency and light failures

Each test run must be reproducible before it is useful for performance or
correctness decisions. Record the executable hash, Libft submodule revision,
compiler/toolchain, worker count, render distance, generation budget, seed,
random seed, display/backend, sanitizer flags, and whether analytics export is
enabled. The normal and analytics binaries must receive the same seed and
scenario order. Never compare a normal run that started with a warm world to
an analytics run that started from a cold world.

For each run, execute the following phases in order:

1. **Startup baseline.** Start with an empty world and no previous report.
   Wait for the center and the complete 3 x 3 neighborhood. Capture immutable
   block snapshots and run the independent full solver for every chunk. Do
   not begin edit latency samples until every baseline chunk has a valid,
   current light buffer and a settled mesh. A baseline mismatch is a
   correctness failure, not a latency sample.
2. **Single-edit samples.** For each interior, face-border, corner,
   section-boundary, transparent, water, and emissive row, capture the 3 x 3
   baseline, issue one authoritative edit, and record T0 immediately before
   the request. Query the block again before any worker result is accepted to
   prove that T1 is authoritative state publication. Observe every frame
   until the target and all affected neighbors have current light and a mesh
   carrying the same input revision. Record T2 and T3 separately. Restore the
   original block only after the row has passed its oracle comparison.
3. **Concurrent-edit samples.** Submit edits on opposite sides of one border
   before allowing another stream update, then submit same-cell replacement
   and deletion pairs. The test must retain both request identities, identify
   which request was accepted, rejected, or superseded, and verify that only
   the final authoritative state is visible. A stale result must be rejected,
   never displayed temporarily as a dark or transparent mesh.
4. **Neighbor lifecycle samples.** Move the camera far enough to evict the
   original neighborhood, verify that the old chunk index entries are absent,
   and then return to the original coordinates. The missing state must not be
   treated as a zero-light chunk or as drawable geometry. After regeneration,
   require a fresh snapshot, fresh light solve, fresh mesh publication, and a
   complete oracle comparison. Repeat with a neighbor becoming available
   while an edit is pending.
5. **Queue-pressure samples.** Submit a deterministic burst larger than the
   interactive queue capacity while generation work is active. Record every
   accepted and rejected operation. Rejections are valid only when they use a
   documented backpressure error and do not mutate authoritative state. The
   final accepted edit must drain to a current 3 x 3 publication with no
   orphaned request, stale queue entry, or unbounded queue growth.
6. **Shutdown samples.** Repeat with work paused at capture, propagation,
   result handoff, main-thread commit, and mesh publication. Destroy the world
   at each point, join persistent workers, and verify that no worker publishes
   after destruction. Reinitialize the same object and repeat the startup
   oracle so cross-session state cannot hide a lifecycle bug.

For every frame in phases 2–5, retain the target and neighbor tuple
`(voxel_revision, content_version, light_input_version,
computed_light_input_version, mesh_revision)`, readiness flags, pending request
ID, queue owner/depth, stream progress, stale-result counters, and light work
counts. A complete sample is accepted only if all of these conditions hold:

- T1 is present and the authoritative block query returns the requested final
  state;
- T2 has `light_input_version == computed_light_input_version`, a valid light
  buffer, and an oracle-equal full light field;
- T3 has a mesh produced from that same block/light input and non-invalid
  vertex light data;
- every affected cardinal, diagonal, and replacement neighbor passes the
  same checks;
- the incremental scanned-cell delta is below one full chunk and at least one
  incremental propagation completed;
- no older request can publish after the final request, and no dirty chunk is
  left without an owning pending request.

Latency must be evaluated from the retained per-edit rows, not console output.
Sort only complete, non-superseded samples for percentile calculation. Keep
superseded, rejected, incomplete, and oracle-failed rows in separate counts so
the test cannot improve its percentile by silently discarding bad work. Report
T0-to-T1, T1-to-T2, T1-to-T3, and T0-to-T3 distributions independently. A
sample failing any correctness condition is a run failure even if its timing
is below the limit. A sample exceeding a timing gate is a run failure even if
its final light field is correct.

When a run fails, rerun the exact failing seed and scenario in isolation, then
rerun it with the same scenario while the competing generation workload is
enabled. The isolated run distinguishes an algorithm/publication defect from
starvation. The competing run distinguishes starvation from a general slow
solver. Retain both reports and compare request ownership, queue age, capture
duration, scanned cells, and main-thread commit duration. Do not add sleeps or
increase timeout gates to make a failing sample pass.

The fast tier must execute this procedure once per matrix row on every change.
The pull-request tier repeats the complete matrix and collects at least 1,000
accepted samples in both binary variants. The scheduled tier repeats it for at
least 10,000 samples, forces version rollover, injects each documented failure
point, varies worker counts, and runs under ASan, UBSan, and TSan. Each tier
must preserve the same oracle and publication checks; only the sample count,
fault schedule, and environment vary.

### Light correctness checks for every matrix row

Capture the target and all loaded neighbors before each edit. The baseline
must pass the full oracle before the row is evaluated. After convergence,
compare every 16 x 256 x 16 packed light cell in the edited chunk and every
neighbor touched by a face or corner. Invoke the solver with world
coordinates and index the result with local coordinates.

On the first mismatch retain the scenario and edit IDs, chunk and local
coordinates, expected and actual packed values, immutable snapshot and live
block IDs, all content/light/voxel/mesh revisions, request ID and captured
revisions, first opaque block in the column, and the generated/loaded/ready,
dirty, and pending state of the neighbor. The matrix must cover interior
edits, all four face boundaries, all four corners, placement and deletion,
transparent blocks, water, an emissive source, simultaneous opposite borders,
missing and newly generated neighbors, and neighbor replacement/eviction.
Testing only the edited chunk is insufficient for a border row.

The renderer-side check uses a fake GPU publication adapter in the fast and
pull-request tiers. It records frame, chunk, mesh revision, voxel revision,
light-input version, vertex light range, and index counts. It rejects a mesh
that is newer in mesh revision but older in block/light input, invalid light
data, or a publication that temporarily removes a valid neighboring face.
At least one release-candidate run per platform repeats the scenarios with
the real graphics backend and stores the first invalid image or compact frame
trace.

Publication observations are not required to be adjacent in the report. The
harness must retain the last observation independently for every chunk and
compare a later observation with that chunk's own predecessor. Neighbor
observations are intentionally interleaved with edited-chunk observations, so
an adjacent-record comparison would miss a stale mesh or a light-buffer
regression at a border. The validator must also report the observation frame,
chunk coordinate, both revision tuples, and the render-readiness flags when
this per-chunk comparison fails.

### Scheduling, ownership, and fault-injection checks

Every frame observation includes current and peak queues, interactive queue
depth, active generation count, stale-result categories, cancellation count,
scanned and propagated cells, and the last stream error. Assert that accepted
interactive work is not behind unbounded generation work, workers only use
immutable snapshots and result-owned buffers, and the main thread performs
one meaningful publication commit rather than locking once per voxel.

Inject deterministic failures around snapshot capture, block materialization,
light and frontier allocation, result handoff, queue insertion, mesh
construction, and main-thread publication. After each injected failure the
last complete publication must remain usable, committed authoritative state
must remain internally consistent, and no pending request may be left without
an owner. Repeat under ASan, UBSan, and TSan, retaining the fault point, seed,
request ID, and complete revision history.

### Failure interpretation and acceptance

A pending light-input version equal to the live input means the request was
captured for the current state but was not completed or committed within the
budget; investigate worker scheduling, priority, and main-thread commit time.
A different pending input means supersession; investigate cancellation and
ensure stale work cannot publish. A current buffer with an oracle mismatch is
a correctness error. A zero-light replacement after a valid publication is
publication atomicity failure. A neighbor-only mismatch is a boundary
dependency or capture failure. Scanned work approaching a full chunk means
the incremental frontier has regressed into a full relight.

The run is accepted only when the oracle, publication invariants, latency
distribution, incremental-work gate, queue ownership checks, and report
completeness all pass. A skipped capability must be recorded as unavailable
with a reason and must not count as a passing matrix row.

The latest validator pass also exercised the fake-GPU publication invariant.
It rejected a newer mesh during startup when the associated light input was
still stale (mesh revision 5, live input 6, computed input 5). This proves
that the publication check catches stale-light swaps before an edit is even
performed. The production scheduler/publication path must resolve this
failure; the harness must not accept it as a harmless transitional frame.

The subsequent run exposed a second independent failure: one otherwise
completed edit reported an incremental scanned-cell delta of 541,703, while
the complete 16 x 256 x 16 chunk contains only 65,536 cells. The incremental
work gate therefore rejects that edit as a full-relight regression. The
timeout diagnostic also records zero current queue depth and zero active
workers at the point where a request is still pending, which directs the
implementation investigation toward result ownership/publication state
rather than allowing the failure to be attributed only to worker load.

The latest harness expansion also passed its validator-side setup checks:
transactional invalid-input handling, the two-chunk opposite-border edit,
the three-step supersession permutation, queue-pressure drain, and eviction
reload all reached their intended assertions. The latest bounded normal
1,000-edit stress workload produced 1,000 edit rows and 31 oracle
checkpoints. It retained 8,192 frame observations and explicitly reported
21,823 dropped frame observations, proving that scheduled tracing is bounded
without dropping edit rows or oracle checkpoints. The non-evictable online
publication tracker also caught a stale mesh at frame 71 before trace
retention could hide it. It still failed the
production latency gate (`p50=227 ms`, `p95=366 ms`, `p99=413 ms`,
`max=468 ms`) and two production scenarios, including publication
invariants; those failures are retained as evidence and are not converted
into skipped or passing scenarios.

The earlier normal and analytics pull-request runs remain useful comparison
data: the normal run measured `p50=258 ms`, `p95=390 ms`, `p99=421 ms`,
`max=461 ms`, while the analytics run measured `p50=331 ms`, `p95=531 ms`,
`p99=581 ms`, `max=636 ms`. These values are not superseded for the
analytics variant until the bounded analytics run is repeated. The scheduled
10,000-edit tier is still unresolved: its first attempt was watchdog-stopped
after approximately 40 minutes, so the 1,000-edit bounded result must not be
used to claim that the 10,000-edit requirement passes.

### Lifecycle and recentering validator

The lifecycle workload is run with:

~~~
ft_vox.exe --validate-lighting-lifecycle
ft_vox_analytics.exe --validate-lighting-lifecycle
~~~

It repeats initialization and destruction three times, queues an edit before
shutdown, calls destruction a second time to verify idempotence, and checks
that no initialized chunk remains afterward. It then initializes again,
recenters the stream to chunk (2,2), waits for that chunk to become fully
ready, and compares its complete light field with the independent oracle.
This catches workers, retired snapshots, pending remesh requests, or stale
chunk-index entries crossing a world session boundary.

The current local normal-build result is five scenarios passed and zero
failures. This does not waive the focused matrix failures: lifecycle cleanup
is a separate property from correct light publication during active edits.

Startup diagnostics are written even when startup or the 3 x 3 oracle fails.
The normal and analytics binaries use separate startup-failure report paths,
and the report includes a scenario record for initialization, center
readiness, neighborhood readiness, each of the eight neighbors, and
publication invariants. This is required so a failure during early world
generation cannot leave only a stale report from a previous run.

### Mandatory verification procedure for latency, light, and publication

This section is normative. A lighting implementation is not considered
verified because a world loads, because a few screenshots look correct, or
because the final light values eventually converge. The test must prove the
complete chain from an authoritative edit through light propagation and mesh
publication, while also proving that unrelated chunks retain their previous
valid data.

#### Reproducible run setup

Every report must begin with a machine-readable environment record containing:

- executable and Libft hashes;
- compiler, linker, operating system, architecture, and sanitizer settings;
- build variant (`normal`, `debug`, or `analytics`);
- world seed, harness seed, worker count, queue capacities, render distance,
  generation budget, and frame budget;
- graphics backend and whether the fake or real publication adapter was used;
- monotonic-clock source and clock resolution;
- analytics exporter/instrumentation state;
- CPU affinity or priority overrides, if any.

The normal and analytics runs must use the same seed, edit sequence, worker
configuration, and scenario order. Reports from a warm world and a cold world
must never be compared as though they were equivalent. The harness must write
the environment header before world initialization so a crash or hang still
leaves useful evidence.

Each run uses a fresh temporary report path. The previous report is never
appended to, and a failed run must not be able to look successful because a
previous run left a complete footer behind. On normal process exit the harness
writes a final footer with pass/fail status, counts, percentile values, and
the first failure. On abnormal termination the watchdog writes an incomplete
footer containing the last phase, last edit, last frame, queue depths, and
worker states.

#### Independent light oracle

The production light solver must not be used as its own test oracle. The
harness needs a small, deterministic reference solver that operates on an
immutable block snapshot and a compact source description:

~~~
reference_light(snapshot, sources):
    initialize sky and block light arrays to zero
    seed sky sources and configured emissive sources
    push every seeded cell into a FIFO or deterministic priority queue
    while the queue is not empty:
        cell = pop()
        for each six cardinal neighbour:
            reject opaque neighbours
            candidate = attenuate(cell.value, neighbour material)
            if candidate > neighbour.value:
                neighbour.value = candidate
                push(neighbour)
    return the complete packed 0..15 fields
~~~

The reference implementation must use world coordinates and an explicit
snapshot boundary. It must not read live `World`, `WorldChunk`, renderer, or
worker state. The packed result is compared cell-for-cell for every section,
not just at sampled points. For each mismatch the report records the first
coordinate, expected and actual sky/block values, the material and opacity of
the cell and its six neighbors, the source list, the snapshot revision, the
computed revision, and the publication revision.

The oracle fixture must include:

- open-sky columns, fully enclosed columns, and transitions at every section
  boundary;
- opaque, transparent, water, and air cells;
- emissive levels 1, 7, and 15 with more than one source;
- sources next to a solid wall and sources on every chunk face and corner;
- removal and replacement of a source;
- a source or blocker immediately across every face and corner boundary;
- version values immediately before, at, and after 16-bit rollover;
- a missing neighbor, a newly published neighbor, and a neighbor being evicted.

The solver also needs a conservation check: an edit may invalidate only the
affected frontier and its dependency neighborhood. Cells outside the oracle's
dependency closure must preserve their previous light values bit-for-bit.
This catches the current class of bug where a valid chunk becomes black for a
frame because a whole light buffer was cleared before the replacement was
ready.

#### Startup and generation verification

Before editing is tested, the harness must establish a baseline in this exact
order:

1. Initialize the world with a fixed seed and record the initialization
   result.
2. Wait for the center chunk and complete 3 x 3 neighborhood, with a bounded
   watchdog. Record every frame's ready, valid, current, dirty, and pending
   state for all nine chunks.
3. Require every baseline chunk to have a valid light buffer, equal input and
   computed light versions, an oracle-equal full field, and a mesh generated
   from that same revision tuple.
4. Verify that no baseline chunk is rendered as zero-light merely because a
   neighbor or mesh is still being prepared. If a mesh is not ready, it must
   be withheld or represented by the last valid mesh; it must never be
   published as a dark/transparent replacement.
5. Confirm that generation and light work proceed in the intended spiral or
   priority order and that the main thread still accepts an edit while
   background generation is active.

The startup gate fails on the first invalid light field, stale mesh, missing
neighbor dependency, unexpected full-chunk relight, or queue starvation. A
later successful frame cannot erase an earlier invalid publication from the
report.

#### Edit-to-render latency verification

Each edit receives a unique request ID and four monotonic timestamps:

~~~
T0 = immediately before the authoritative edit request is submitted
T1 = authoritative block state is committed and queryable
T2 = current light is available and oracle-equal
T3 = a mesh containing the same block/light input is published
~~~

The harness records both wall-clock nanoseconds and frame numbers. It must not
use `sleep` as a completion signal. Each frame calls the normal bounded update
path, drains only the configured amount of work, and records the state tuple
for the target and all affected neighbors. The sample is complete only when
the tuple proves T1, T2, and T3; a console message or queue becoming empty is
not proof of completion.

For every sample report these independent durations:

- T0 to T1: authoritative commit latency;
- T1 to light availability: light scheduling/propagation latency;
- T1 to T2: light publication latency;
- T2 to T3: remesh and mesh publication latency;
- T0 to T3: user-visible end-to-end latency;
- observed frames, queue age, worker wait time, capture time, propagation
  cells, remesh time, main-thread commit time, and GPU upload time.

The default acceptance gates are strict and apply to complete, non-rejected,
non-superseded samples:

~~~
at least 95% of samples: T0-to-T3 <= 75 ms
at least 99% of samples: T0-to-T3 <= 120 ms
every sample:            T0-to-T3 <= 200 ms
every sample:            T0-to-T1 <= 8 ms
every sample:            <= 8 observed frames
diagnostic target:       p50 T0-to-T3 <= 35 ms
~~~

These are upper bounds, not suggestions. A sample that is correct but too
slow fails. A sample that is fast but has stale light, a stale mesh, a dark
intermediate publication, or an oracle mismatch also fails. Percentiles must
be calculated from retained per-edit rows after the run, not from console
output and not by dropping slow or incomplete samples. Rejected edits,
superseded edits, timeouts, oracle failures, and publication failures are
reported separately and count as run failures where the scenario requires an
accepted edit.

The harness must calculate p50, p90, p95, p99, maximum, and median absolute
deviation for every stage and for T0-to-T3. It must also print the slowest
five samples with their full revision and queue tuples. This distinguishes a
stable scheduling problem from an isolated outlier and makes it possible to
compare normal versus analytics builds without hiding analytics overhead.

#### Incremental frontier and chunk-border verification

For each block edit, construct the expected dependency closure in the test
fixture before submitting the edit. The closure includes the edited cell,
all reachable light-propagation cells, and neighboring chunk cells when the
edited cell is on a face or corner. The test must explicitly include the
case where a neighboring boundary cell is opaque; that neighbor may be
checked and then ruled out without scheduling a full neighboring chunk.

The harness records `scanned_cells`, `propagated_cells`, source removals,
source additions, and incremental/full completion counters. An edit fails if:

- the solver scans or clears a complete 16 x 256 x 16 chunk for a local edit
  whose dependency closure is smaller;
- a chunk is marked current without the required neighboring frontier being
  evaluated;
- a border edit updates one side but not the other;
- a stale border result is published after a newer edit;
- a valid old light buffer is discarded before a replacement is ready;
- a chunk becomes dark, transparent, or empty for an intermediate frame;
- the implementation locks and unlocks once per voxel rather than doing one
  bounded snapshot/commit operation.

Run the matrix separately for interior, six face directions, twelve edge
directions, and eight corners. Repeat each row with addition, removal,
replacement, water, transparent, and emissive materials. Use simultaneous
opposite-border edits and deliberately reorder worker completion so the test
proves that request IDs and captured revisions, rather than completion order,
determine what may be published.

#### Publication and renderer verification

The fast and pull-request tiers use a fake GPU publication adapter. It must
record every attempted publication, including rejected publications. A mesh
publication is valid only when:

~~~
mesh.block_revision       == current block revision used by the mesh
mesh.light_input_revision == current computed light input
mesh.light_valid          == true
mesh.vertex_light_range   is within 0..15 (or the configured color range)
mesh geometry             was built from the same immutable snapshot
~~~

The adapter rejects a newer mesh built from an older light input, a zero-light
replacement after a valid mesh, a transparent face where an opaque neighbor
requires occlusion, and any publication after world destruction. It keeps a
last-valid publication per chunk, so a failed worker result cannot erase the
only usable render state.

At least one release-candidate run on each platform repeats the same rows
with the real graphics backend. Capture the first invalid frame, a compact
publication trace, and a deterministic screenshot or framebuffer checksum.
Visual inspection is supplementary evidence; it must not replace revision
and oracle assertions.

#### Required light-test matrix

The validator must treat lighting as a state-transition system, not as a
single function test. Each row below is a separate scenario with its own
fresh world, baseline snapshot, request IDs, and report section. A row is not
covered merely because another row used the same block type.

| Area | Required cases | Required proof |
| --- | --- | --- |
| Initial generation | flat open sky, enclosed cave, overhang, vertical section boundary, missing/unready neighbor | every published chunk is oracle-equal before it becomes renderable; no zeroed replacement is published |
| Interior edits | remove, place, replace, opaque, transparent, water, emissive level 1/7/15 | only the dependency closure changes; unrelated cells retain their previous packed light values |
| Faces | edit on each of the six faces | the local chunk and the affected neighbor converge to the same boundary result |
| Edges and corners | all twelve edges and eight corners, including diagonal neighbors | each required neighbor is checked; no boundary light is lost or invented |
| Sources | add, remove, move, and replace multiple sources | source removal does not clear unrelated light; source addition does not overwrite a newer edit |
| Occlusion | opaque neighbor, transparent neighbor, water neighbor, changing opacity | face visibility and light attenuation agree with the immutable block snapshot |
| Ordering | old result completes after a newer result; light result completes after block replacement | stale results are rejected by request/revision identity |
| Rollover | block/light versions at `UINT16_MAX - 1`, `UINT16_MAX`, `0`, and `1` | modular version comparison never treats an old result as current |
| Resource pressure | full queues, allocation failures, worker cancellation, eviction/reload | the last valid light and mesh remain usable and no request is silently lost |
| Lifecycle | initialize, generate, edit, stop, destroy, reinitialize, edit again | no worker, callback, queue item, lock, or publication survives its owning world |

For each row, the report must include the input snapshot hash, expected light
hash, actual light hash, old and new block/light versions, affected-chunk
set, dependency-closure size, scanned-cell count, propagated-cell count,
publication decisions, and the reason for every rejected result. A row that
only checks a final screenshot is incomplete.

The harness must run the matrix at minimum with these repetitions:

1. one deterministic seed and fixed worker count for reproducibility;
2. at least 100 randomized edit sequences for ordinary correctness;
3. at least 1,000 edits while generation and eviction are active;
4. at least 100 true two-producer border races, with deterministic completion
   reordering;
5. one long scheduled run whose edit count is large enough to exceed the
   historical 256-edit limit by a wide margin.

The randomized generator must record its seed and produce edits from a
bounded, inspectable grammar. It must never generate an unreviewable opaque
blob of random bytes. When a failure occurs, the harness writes the shortest
prefix that reproduces it, then reruns that prefix in isolation before the
full stress run is considered meaningful.

#### Exact latency measurement and acceptance procedure

Latency is measured from the same monotonic clock for every stage. The test
must not use wall-clock time, log timestamps, sleep duration, or queue length
as a proxy for completion. A monotonic timestamp is captured at each state
transition, and a transition is accepted only after the corresponding state
and revision assertions pass.

For every accepted edit, the harness records:

~~~
request submitted       : T0
authoritative block     : T1
light result committed  : T2
mesh published          : T3
first visible frame     : T4, when a real/fake renderer can observe it
~~~

The following rules are mandatory:

- `T0` is captured immediately before enqueue/submit and before any test
  logging or diagnostic formatting that could distort the measurement.
- `T1` is captured on the main-thread commit path after the block revision is
  visible through the normal read API.
- `T2` is captured only after the target and every required boundary neighbor
  has the expected light revision and passes the independent oracle.
- `T3` is captured only after the publication adapter accepts a mesh whose
  block revision, light revision, geometry snapshot, and validity flag all
  match the committed state.
- `T4` is captured only by a render observation/checksum or the fake adapter's
  equivalent visible-frame event; a worker saying "mesh ready" is not `T4`.
- A timeout is a failed sample with an explicit missing-transition field. It
  must never be represented by an unsigned sentinel that can wrap into a
  small or negative-looking duration.

Latency must be reported separately for cold startup, active generation,
steady state, queue pressure, border edits, source edits, and analytics
variants. At least 1,000 accepted edits are required for a percentile report;
otherwise the report is marked statistically insufficient. The report must
include sample count, rejected/superseded/timeout count, p50, p90, p95, p99,
maximum, and the five slowest complete samples. Percentiles are calculated
from the individual samples, never from frame averages.

The validator must also calculate a frame-budget view. For every frame that
contains an edit, record main-thread commit time, worker time, lock wait time,
queue wait time, mesh publication time, and render time. This identifies
whether a missed latency target is caused by computation, scheduling, a lock,
or a publication barrier. A passing end-to-end number must not hide a frame
that exceeded the configured frame budget.

The test runner must execute each latency scenario in this order:

1. warm up without measuring, until the baseline 3 x 3 neighborhood is
   oracle-valid;
2. run an isolated edit with no competing generation work;
3. run the same edit while generation, lighting, remeshing, and publication
   queues are loaded;
4. repeat with the opposite-border race and forced worker reordering;
5. repeat with analytics exporter disabled, instrumentation disabled, and
   both enabled;
6. repeat on the real renderer/backend after the fake publication adapter
   passes.

The same latency thresholds apply to the normal and analytics-disabled
variants. Analytics-enabled performance is reported separately and must not
be allowed to corrupt or block the measured client path. Any sample that
requires a full-chunk relight for a local edit is a correctness and
performance failure, even if it completes inside the end-to-end threshold.

#### Light conservation and no-flicker checks

The harness must retain the previous valid light buffer and previous valid
mesh for every observed chunk. On every frame between `T1` and `T3`, it
checks that one of the following is true:

1. the old valid publication is still available; or
2. a newer publication has passed the complete block/light/oracle checks.

The following intermediate states are always failures:

- a valid chunk changes to all-zero light without an authoritative reason;
- a mesh is published from a light buffer marked invalid or from an older
  block revision;
- a boundary neighbor temporarily becomes dark while its replacement is
  being calculated;
- a transparent face is published where the immutable snapshot says it is
  occluded;
- a chunk disappears from rendering because a worker result is still pending;
- the renderer observes a result before the main-thread commit has made the
  corresponding block revision visible.

For each edit, compute a dependency closure using the reference solver's
frontier rules. Compare every cell inside the closure against the new oracle
result. Compare every cell outside the closure against the pre-edit light
buffer. This two-sided comparison is required: checking only that affected
cells became correct will not detect an accidental whole-chunk clear.

The test must sample every frame, not only the first and final frames. The
frame trace stores the chunk's old/new light hash, valid/current flags, mesh
hash, revision tuple, pending-job ID, and publication decision. A flicker is
therefore a failed transition in the trace even when the final frame is
correct.

#### Test isolation, replay, and failure triage

Every failure must be replayable from a compact artifact containing the world
seed, harness seed, scenario row, edit prefix, initial chunk snapshots,
worker count, queue capacities, fault-injection point, forced completion
order, and platform/build metadata. The replay runner must support:

- normal execution;
- one-step-per-frame execution;
- worker completion replay in recorded order;
- exporter disabled;
- instrumentation disabled;
- fake publication only;
- real publication only when the platform supports it.

Failure classification is mandatory:

- **oracle failure:** actual light differs from the independent reference;
- **revision failure:** an old result is accepted as current;
- **publication failure:** invalid, dark, transparent, or stale data becomes
  visible;
- **latency failure:** a complete valid transition exceeds a gate;
- **scheduling failure:** a required request does not progress while work is
  available;
- **lifecycle failure:** shutdown/restart leaves active work or corrupts the
  report;
- **analytics failure:** instrumentation/exporter changes behavior, blocks
  the measured path, or corrupts memory.

The first failure in each class is preserved. Later failures may be grouped,
but they must not replace the first diagnostic record. A run is accepted only
when all required rows pass, the report footer is complete, no watchdog or
sanitizer failure occurred, and the replay of every reported failure either
passes after the fix or remains an explicitly documented known failure.

#### Fault injection and lifecycle verification

Inject one deterministic failure at a time at snapshot allocation, block
snapshot copy, light queue allocation, propagation frontier allocation, result
handoff, interactive queue insertion, remesh allocation, mesh construction,
main-thread commit, and fake GPU publication. After every failure assert:

- the previous valid block/light/mesh publication remains usable;
- authoritative state is either unchanged or committed exactly once;
- no request remains pending without an owner;
- no stale result can publish later;
- queue capacities and retained report memory remain bounded;
- worker shutdown and reinitialization remain successful.

Run each injection at the first, middle, and last allocation/event in the
operation, then repeat it at version rollover and on a chunk border. The
failure schedule must be encoded in the report so a failed run can be
replayed exactly. The test must distinguish an expected injected error from a
secondary cleanup error; the latter is a failure.

#### Hang, cancellation, and core-dump procedure

Every validator process has a watchdog that observes progress markers written
by the harness, not merely process liveness. A progress marker includes phase,
edit ID, frame, last completed request, queue depths, active workers, and the
last state transition. If the marker does not advance for the configured
watchdog interval, the runner must:

1. capture stdout/stderr and the current JSONL report;
2. request a debugger/core dump of the still-running process;
3. record every thread's stack and the owner of each queue/lock;
4. send the configured abort signal only after the dump request is complete;
5. preserve the exit code, dump path, and watchdog reason in the report.

The watchdog must not silently convert a hang into a passing timeout or kill
the process before the stack capture. CI cancellation should use the same
dump-then-abort path where the platform permits it. A run with no progress
must identify whether the stall is in generation, light propagation, result
handoff, main-thread commit, renderer publication, exporter I/O, or shutdown.

Analytics builds require two additional controls. First, run the lighting
matrix with exporter disabled and with instrumentation disabled to separate a
lighting defect from analytics overhead or lifecycle corruption. Second,
repeat the exporter-enabled run and verify that analytics copies data into
worker-owned buffers without holding world/render locks. An analytics crash,
heap report, exporter queue failure, or thread-teardown failure is a separate
hard failure; it must never be recorded as a lighting pass merely because the
normal binary completed.

The current Windows investigation found a nondeterministic heap-corruption
failure in the exporter-enabled analytics variant during a worker thread's
`s_pt_rwlock_tls_state` teardown. The teardown observed freed-heap markers in
its spill buffer and failed before the harness footer was written. This must
be reproduced under a debugger and fixed or isolated before analytics timing
data is considered trustworthy. The no-exporter and no-instrumentation
variants completed the same harness without that corruption, so the test
matrix must retain all three variants until the lifecycle boundary is proven
safe.

#### Required CI and local commands

The current validator implementation writes one JSONL report per build
variant. Every edit row includes the authoritative, light, light-stage,
mesh, visible, and visible-stage durations. The `*_total_ms` values start at
the edit submission; the `*_stage_ms` values isolate the work after the
previous transition. In particular, `light_stage_ms` is T1-to-T2 and
`mesh_ms` is T2-to-T3. The summary repeats p50, p90, p95, p99, and maximum
values for each stage, plus the end-to-end distribution and the incremental
scan limit. This prevents a low mesh time from hiding an expensive light
propagation stage.

Each validator also maintains a live watchdog marker named according to the
build variant. It is rewritten periodically by the watchdog thread and
contains the last phase, frame, edit ID, stall duration, and watchdog
deadline. A timeout replaces it with a `watchdog_timeout` record and aborts
the process after the diagnostic is flushed. The JSONL report remains the
authoritative source for queue depths, revisions, publication decisions, and
the first failure; the marker is intentionally small enough to survive a
stuck or externally cancelled process.

The focused matrix is expected to fail until the production lighting path
passes its oracle, publication, latency, and incremental-scan gates. A
successful executable build or a lifecycle-only pass is therefore not proof
that lighting is correct. CI must retain the failure report and use its
scenario name, edit ID, revision tuple, stage percentile, and scanned-cell
percentile to select the smallest replay before accepting a production fix.

The following commands are the minimum verification set:

~~~
make -j2 validate-lighting-harness
make -j2 validate-lighting-stress
make -j2 validate-lighting-lifecycle
~~~

The scheduled tier is run separately so a normal build is not made to wait
for it:

~~~
make -j2 validate-lighting-scheduled-stress
~~~

For every failure, rerun the exact seed and scenario in isolation, then rerun
with generation enabled, then rerun with the analytics exporter disabled and
with instrumentation disabled. A fix is accepted only when the isolated and
competing-workload results agree and the failure does not reappear in a
repeat run. Keep the first failing report, the isolated report, and the
post-fix report as separate artifacts. Do not loosen a latency threshold,
replace a correctness assertion with a sleep, or skip a row because it is
slow or platform-sensitive.
