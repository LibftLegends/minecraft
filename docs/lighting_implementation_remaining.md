# Voxel lighting implementation handoff

Status: implementation paused at the user's request on 2026-09-04.

Continuation note: this document is now the handoff point for feature work.
The next implementation commits should resolve the remaining lighting, water
generation, and block-break scheduling issues described below before adding
new rendering features.

Branches:

- Minecraft: `agent/analytics-performance`
- Libft: `agent/compression-analytics-cardgame-scripting`

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

This proves storage-to-CPU-mesh publication for the validator workload. The
edit-priority upload path is compiled into both normal and analytics builds.
The separate `--validate-renderer-publication` check now exercises a real GPU
context, edits a visible block, runs the normal world/renderer handoff, and
verifies that the uploaded chunk identity and voxel revision advance after the
edit. It passed in both normal and analytics Windows builds. It remains
separate from `--validate-all` so headless CI machines do not fail the
aggregate suite.

1. Finish the runtime lighting scheduler. The persistent bounded operation and
   queue-level slicing now exist, and dirty-remesh selection now ranks the
   bounded scan window by explicit edit priority and distance from the active
   world center. Dirty sections still need stronger coalescing and distance
   from the originating edit must be carried through the queue rather than
   approximated by chunk distance. Notification-level revision coalescing is
   now in place; section-level/per-cell coalescing and starvation proof are
   still required. Verify that repeated requeueing cannot starve generation
   or a nearby player edit.
2. Verify that neighbor arrival/removal enqueue bounded relighting as well as
   face remeshing, and that temporary conservative boundaries converge after
   the neighbor is published or evicted. Arrival and eviction invalidation are
   now wired and newly published chunks use the ordinary bounded dirty-remesh
   path so startup generation cannot be starved. The bounded propagation
   scheduler and a dedicated arrival/eviction convergence validator are still
   required.
3. Complete the Libft lighting tests. Foundational coverage now includes
   pack/unpack, combined darkening, update-configuration bounds, direct
   skylight, full roof occlusion, deterministic local builds, and build stats.
   Cave opening, roof occlusion, block falloff, and multiple-source maximum
   coverage now exist. `test_voxel_lighting.cpp` is now included in the Voxel
   test group and its lighting tests pass in the global Windows test
   executable, including metadata attenuation contracts, incremental
   operation versus clean-rebuild equivalence, and a per-step configured
   slice-bound assertion and sliced-operation versus clean-rebuild equality.
   Still add broader chunk seams and cross-source deterministic-order cases.
4. Add Minecraft validators for edit-to-outside lighting, edit propagation
   across chunk boundaries, stale worker rejection, and the configured work
  budget. The block-edit validator now also waits for the loaded cardinal
  neighborhood to become clean after an edit, exposing stranded pending
  remesh requests. The repeated block-edit workload and p50/p95/p99 remesh
  timing are now present. The validator also edits a block on a chunk edge
  and requires all four loaded cardinal neighbors to publish newer, clean
  meshes and newer light revisions after both placement and deletion. Queue peak,
  propagation count,
  snapshot bytes, stale-job counts, and direct edit-to-outside light-value
  assertions still need to be added.
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
    full Windows Libft executable now passes all 6,320 tests when launched
    from `Libft\Test` (the fixture-relative working directory). Launching it
    from `Libft` produces false failures for path-dependent File/Voxel tests,
    so CI and local instructions must preserve the intended working directory.
9. Add/update module READMEs for the new public voxel-light and shadow APIs,
    and update the Libft dependency graph if required by the repository's
    documentation checks.
10. Run final matched normal/analytics builds, `make validate-all`, and a
    graphics-context runtime test. Do not claim performance success without
    before/after measurements from repeated block-breaking workloads.

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

The persistent generation workers must also share work fairly: when both
generation and ordinary remesh requests are queued, a bounded number of
generation requests is selected before another ordinary remesh. This prevents
initial world construction from being consumed by lighting work without
removing the asynchronous lighting path. The async-generation validator now
also performs a break/place edit while startup generation is still active and
requires the playable area to continue converging. It currently records the
loaded-chunk progress and frame at completion; remesh queue peak and exact
first-visible-mesh latency remain follow-up instrumentation.
