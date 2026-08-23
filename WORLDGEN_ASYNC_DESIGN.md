# Asynchronous World Generation Design

Status: implementation handoff for Luna  
Scope: Minecraft repository; Libft remains an unmodified dependency  
Primary objective: prevent chunk generation and meshing from causing main-thread frame spikes

## Summary

Move expensive chunk generation and mesh construction to a bounded worker pool. Workers must build isolated results and must never mutate the live `World`, its chunk array, neighboring chunks, or renderer-visible state. The main thread remains the sole owner of live world state and commits a limited number of completed results within a configurable time budget each frame.

This should remove the largest generation-related stalls without introducing data races. It is not safe to run the current `WorldChunkLoader::initialize_chunk()` unchanged on another thread because that function reads live neighbors and initializes the final `WorldChunk` slot directly.

## Why the current path can spike

The current streaming call chain is synchronous:

```text
GameSession::update()
  -> World::update_around()
     -> World::stream_chunks()
        -> World::try_load_chunk_at()
           -> WorldChunkLoader::initialize_chunk()
              -> terrain_generate_chunk()
              -> chunk_mesh_generate_from_chunk_with_neighbors()
           -> register_chunk_index()
           -> remesh west/east/north/south neighbors
```

Relevant code:

- `src/app/GameSession.cpp` calls `World::update_around()` from the game update path.
- `src/world/World.cpp` changes a candidate from queued to generating, calls `try_load_chunk_at()`, and does not return until generation and meshing finish.
- `src/chunks/WorldChunkLoader.cpp` generates voxel data and a mesh in the same call.
- `World::try_load_chunk_at()` remeshes as many as four loaded neighbors immediately after inserting one chunk.
- `World::regenerate_selected_chunks()` also regenerates, blends, and remeshes selected chunks synchronously.

`generation_budget` currently limits the number of chunks attempted in a frame. It does not bound the time spent on one chunk, so a budget of one can still exceed the frame budget. The code already exposes candidate states such as queued, generating, meshing, and ready, but those states currently describe sequential work on the main thread rather than an asynchronous pipeline.

World generation is a strong candidate for the observed spikes, but implementation should add timings before and after each stage so this can be confirmed rather than assumed.

## Goals

- Keep voxel generation and, where safe, mesh construction off the main thread.
- Keep all live-world publication, eviction, renderer-visible mutation, and block edits on the main thread.
- Preserve deterministic terrain for a given seed, coordinate, configuration signature, and stage mask.
- Prevent duplicate jobs for the same chunk and revision.
- Reject obsolete results after camera movement, configuration changes, regeneration, or shutdown.
- Bound queued work, memory use, worker count, and main-thread commit time.
- Support normal streaming and explicit world-revision regeneration.
- Preserve a synchronous mode for tests, diagnostics, and fallback.

## Non-goals

- Changing terrain generation algorithms or biome behavior.
- Making every `World` method generally thread-safe.
- Letting workers call rendering APIs.
- Letting background jobs edit loaded neighboring chunks.
- Creating one operating-system thread per chunk.

## Ownership model

The ownership rule is the core safety requirement.

### Main-thread-owned data

- `World::chunks`, `chunk_index`, counters, candidate states, and revision state
- loaded `WorldChunk` objects and their final voxel/mesh data
- chunk eviction and slot selection
- player block edits
- transition-boundary blending against loaded chunks
- cross-chunk feature application
- neighbor-remesh scheduling
- renderer-visible mesh revisions

### Worker-owned data

Each job owns an immutable input snapshot and a private output:

```text
ChunkGenerationRequest
  coordinate
  seed snapshot
  terrain configuration snapshot/context
  configuration signature
  generation revision
  relevance epoch
  request identifier
  requested stage mask
  operation: stream or regenerate
  optional immutable neighbor-border snapshots

ChunkGenerationResult
  matching identity fields
  generated game_voxel_chunk
  generated chunk_mesh, when mesh generation succeeded
  deferred cross-chunk block edits
  generation and mesh timings
  error code
```

No pointer in a queued request may refer to mutable `World`, `WorldChunk`, a stream candidate, or a temporary caller-owned configuration. Prefer owning values. If `terrain_generation_context` cannot be copied safely, construct one worker-local context from an owned configuration snapshot.

## Proposed pipeline

```text
Main thread                     Worker pool                    Main thread
-----------                     -----------                    -----------
select candidates
deduplicate requests
reserve bounded capacity
enqueue immutable request  -->  generate private voxel data
                                apply local-only stages
                                build mesh from snapshots
                                emit result              -->   validate identity
                                                               reserve/recheck slot
                                                               publish voxel + mesh
                                                               apply deferred edits
                                                               mark READY
                                                               queue neighbor remeshes
```

Use a fixed, bounded pool. Start with `max(1, min(4, hardware_concurrency - 1))` workers, but make it configurable. Leaving one logical core for the main/render thread is more important than maximizing generation throughput.

Libft provides `ft_thread_pool`, but it uses a FIFO queue and polls for work every millisecond. It can be used for an initial implementation if Minecraft owns priority and deduplication before submission. Do not depend on its cancellation-token overload for cancellation after submission: that overload only checks the token before enqueuing. Jobs still need cooperative stale/cancellation checks at Minecraft-controlled stage boundaries. A dedicated Minecraft queue using a condition variable is acceptable if priority, observability, or shutdown behavior cannot be implemented cleanly around `ft_thread_pool`.

## State machine

Use real asynchronous state transitions:

```text
ABSENT -> QUEUED -> GENERATING -> MESHING -> COMPLETED -> READY
              |          |          |           |
              +----------+----------+-----------+-> STALE/CANCELLED
                                     |
                                     +------------> FAILED_RETRYABLE
```

- `QUEUED`: accepted into bounded work tracking, not merely considered.
- `GENERATING`: a worker owns the request.
- `MESHING`: voxel generation completed and mesh work is active.
- `COMPLETED`: result is waiting in the completion queue.
- `READY`: the main thread validated and published it.
- `STALE/CANCELLED`: result is no longer relevant and must be destroyed without publication.
- `FAILED_RETRYABLE`: retain the current exponential frame backoff behavior.

State changes visible to `World` happen only on the main thread. Workers report events/results; they do not modify `StreamCandidate` objects.

## Request identity and stale-result rejection

Identify a request by at least:

```text
(chunk_x, chunk_z, generation_revision, relevance_epoch,
 configuration_signature, stage_mask, operation, request_id)
```

Before commit, verify all of the following:

- The world is still initialized and is the same world instance/generation.
- The result's generation revision and configuration signature are current.
- The candidate still refers to the same request identifier.
- The coordinate is still in the cache radius, unless it is an explicit revision request.
- No live chunk already occupies the coordinate with an equal or newer revision.
- A free slot still exists.

If any check fails, destroy the private result and account for it as stale. Never overwrite a newer chunk.

Increment a world-lifetime epoch during initialize, destroy, seed/config replacement, and full revision commits. Camera movement can continue to use `stream_relevance_epoch_`. Cancellation is cooperative: check an atomic epoch/token between terrain stages and before meshing. Libft exposes stage masks (`BASE_TERRAIN`, `CAVES`, `FLUIDS`, `DECORATION`, `STRUCTURES`, and `ORES`), which are suitable cancellation boundaries even if the first implementation runs all requested stages in one worker call.

## Meshing and neighbor data

Meshing should also run off-thread because it scans a complete chunk and can allocate substantial mesh data. A worker must not call the current neighbor lookup callback against live `WorldChunk` pointers.

Implement one of these safe approaches, in this order of preference:

1. At enqueue time, copy the one-block border required from each initialized neighbor into an immutable `ChunkNeighborSnapshot`. The worker meshes against these snapshots.
2. Generate an initial mesh without neighbors, publish it, and queue a snapshot-based remesh when neighbors become available.

After a chunk commits, mark its four neighbors dirty. Remesh jobs use immutable snapshots of the target chunk plus neighbor borders and return a replacement mesh. At commit, compare the target chunk's voxel revision and mesh request identifier; discard a remesh result if either changed.

Do not clear or modify a live mesh while a worker is generating its replacement. Swap/move the completed mesh into the live chunk only during commit, then destroy the old mesh on the main thread or through a separately proven-safe reclamation path.

## Cross-chunk features

`terrain_generation_config` can enable cross-chunk features through `cross_chunk_block_writer`. A worker must not configure that writer to mutate `World` or neighboring chunks.

Use a worker-local writer that appends deterministic `DeferredBlockEdit` records:

```text
world_x, world_y, world_z, block_id, source_chunk, stage, stable_sequence
```

At main-thread commit:

1. Sort edits by target coordinate, source coordinate, stage, and stable sequence.
2. Apply edits to loaded targets or retain them in a bounded per-coordinate pending-edit store.
3. Resolve collisions with a documented deterministic rule independent of worker completion order.
4. Increment target voxel revisions and enqueue remesh jobs for affected chunks/borders.

Until this buffer exists, async generation must force cross-chunk writes off for worker jobs and record that temporary limitation. It must never silently call a live-world writer from a worker.

## Main-thread commit budget

Replacing generation stalls with unbounded result commits would preserve the spike in a different location. Drain completions using both limits:

- maximum commits per frame, initially 1
- maximum commit/remesh-publication time, initially 1-2 ms

Commit should primarily validate, reserve, and move/swap already-built data. Avoid regeneration or full remeshing in this phase. Neighbor remeshes become queued jobs instead of four immediate calls.

The existing `generation_budget` can temporarily mean “maximum new requests submitted per frame,” but introduce explicit names so submission and commit limits are not confused.

## Priority and backpressure

- Prioritize the center chunk, then shortest squared distance.
- Optionally prefer chunks in the camera movement/view direction only as a secondary key.
- Explicit user-requested regeneration should have its own priority class, but must not starve nearby streaming indefinitely.
- Keep one in-flight record per coordinate and generation revision.
- Bound pending requests, active jobs, completion results, pending cross-chunk edits, and remesh jobs.
- When capacity is full, do not block the main thread. Leave the candidate absent/eligible and retry on a later frame.
- Re-evaluate queued priorities when the center chunk changes. Running work may finish, but stale results must be cheap to discard.

Suggested initial limits:

```text
worker_count:                 1 to 4, automatic by default
maximum queued generation:   worker_count * 4
maximum completed results:   worker_count * 2
maximum commits per frame:   1
commit time budget:          1.5 ms
maximum queued remesh jobs:  bounded by cache chunk count
```

Tune these from measurements, not only throughput.

## Revision regeneration

`regenerate_selected_chunks()` currently loops over every selected chunk synchronously. Convert it into a batch operation:

- Snapshot the revision config, stage mask, selected coordinates, and source voxel data where partial-stage regeneration requires it.
- Submit bounded jobs using the same request/result pipeline with operation `REGENERATE`.
- Keep the old live chunks visible while replacements are built.
- Report progress and failures without blocking `WorldRevisionServer`.
- Commit only results belonging to the active revision request.
- Perform transition blending from immutable border snapshots or as a post-generation worker stage.
- Publish the new terrain configuration/world revision only after the chosen transaction policy succeeds.

The implementation must explicitly choose and test a transaction policy. Recommended: atomic per chunk with a batch status, because retaining all results for an all-or-nothing world-sized transaction can consume excessive memory. Failed chunks retain their old data and can be retried.

## Lifecycle and shutdown

Initialization order:

1. Initialize normal world/configuration state.
2. Initialize request tracking and completion queues.
3. Start the worker pool last.

Shutdown order:

1. Stop accepting requests and increment the world-lifetime epoch.
2. Request cancellation of queued/running work.
3. Join workers; never detach them.
4. Drain and destroy uncommitted private results.
5. Destroy queues and then live world chunks.

`World::destroy()` must not destroy terrain configuration, chunks, or synchronization objects while a worker can still access a snapshot derived from them. Owned request values make this substantially easier.

## Proposed code organization

Keep orchestration in Minecraft rather than adding game-specific policy to Libft:

```text
src/world/WorldGenerationPipeline.hpp/.cpp
  lifecycle, submission, completion drain, cancellation, diagnostics

src/world/WorldGenerationRequest.hpp
  request/result identities and owned snapshots

src/chunks/ChunkNeighborSnapshot.hpp/.cpp
  immutable voxel-border capture and lookup

src/chunks/WorldChunkLoader.cpp
  add worker-safe build-private-chunk and build-private-mesh helpers

src/world/World.cpp
  candidate selection, enqueue, time-bounded commit, eviction coordination
```

Do not put implementations into the existing umbrella voxel header. Include only the individual Libft headers required by each file, in line with the current header-splitting direction.

## Configuration surface

These are Minecraft runtime/streaming settings, not terrain-generation settings:

```text
async_world_generation_enabled
world_generation_worker_count          // 0 = automatic
world_generation_max_queued
world_generation_max_completed
world_generation_submissions_per_frame
world_generation_commits_per_frame
world_generation_commit_budget_us
async_chunk_meshing_enabled
```

Keep a synchronous mode. It is useful for deterministic comparison, constrained platforms, debugging, and tests. Synchronous mode should call the same private-build and commit logic so behavior does not diverge.

## Instrumentation

Add timings and counters before changing behavior, then retain them:

- main-thread `update_around` duration
- voxel generation duration by stage and chunk
- mesh generation duration
- neighbor snapshot duration
- result commit duration
- queue depth/high-water mark
- active/idle worker count
- stale and cancelled result counts
- retry/failure counts by error code
- time from request to ready
- number and cost of neighbor remeshes
- frame-time percentiles and maximum while crossing chunk boundaries

Logging should be aggregated/rate-limited. Do not print one line per block or unconditionally print every job in release builds.

## Implementation phases

### Phase 0: prove the source of spikes

- Add stage timers around generation, meshing, neighbor remeshing, and commit-like insertion.
- Reproduce by moving continuously across chunk boundaries and by regenerating a selected area.
- Save baseline median, p95, p99, and maximum frame times.

### Phase 1: asynchronous voxel generation

- Add owned requests/results, bounded submission, deduplication, epochs, and shutdown.
- Generate private chunks on workers.
- Initially mesh and commit on the main thread only if needed to reduce implementation risk.
- Verify determinism and stale-result handling.

### Phase 2: asynchronous meshing

- Add immutable neighbor-border snapshots.
- Build replacement meshes on workers.
- Replace immediate four-neighbor remesh calls with dirty marking and queued remesh jobs.
- Add voxel and mesh revision checks before publication.

### Phase 3: cross-chunk features and regeneration

- Add deterministic deferred cross-chunk edit buffers.
- Move staged revision regeneration and transition work into the pipeline.
- Add progress/error reporting to the revision server.

### Phase 4: tuning

- Tune worker count, queue sizes, priorities, and commit budget from profiling.
- Confirm that additional workers do not starve rendering or increase allocator contention.

## Required tests

### Correctness

- Generate the same coordinates synchronously and asynchronously; compare all block IDs, generation metadata, and mesh contents/signatures.
- Repeat with multiple seeds, negative coordinates, every biome configuration, biome size constraints/overrides, and every stage mask.
- Complete jobs in deliberately shuffled orders and verify identical world output.
- Verify cross-chunk trees/structures and edit conflict resolution are deterministic.
- Verify existing player edits are not overwritten by stale generation or remesh results.
- Verify neighbor faces appear/disappear correctly as adjacent chunks commit or are evicted.

### Concurrency and lifecycle

- Move the camera rapidly so most queued results become stale.
- Change terrain config and generation revision while jobs are active.
- Destroy/reinitialize the world while jobs are queued and running.
- Fill every bounded queue and verify the main thread does not block.
- Inject allocation and task-submission failures using the Libft test-only deterministic CMA failure framework.
- Run under ThreadSanitizer on Linux and the available address/undefined-behavior sanitizers.

### Performance

- Stream continuously across chunk boundaries with synchronous and asynchronous modes.
- Record frame-time p50/p95/p99/max, generation throughput, queue depth, and stale-work ratio.
- Test 1, 2, 4, and automatic worker counts.
- Stress explicit regeneration while rendering and moving.

## Acceptance criteria

- No worker reads or mutates live `WorldChunk` storage after submission.
- No renderer-visible state is mutated off the main thread.
- No duplicate active request exists for the same coordinate/revision/operation.
- Stale results can never overwrite current chunks or edits.
- Queue-full behavior is non-blocking on the main thread.
- World destruction joins workers and leaves no queued result or dangling reference.
- Async and sync generation are deterministic equivalents.
- Neighbor remeshing no longer runs four full synchronous remeshes when a chunk is inserted.
- Profiling demonstrates a meaningful reduction in p99 and maximum frame time during streaming, without unacceptable generation latency.
- All existing world-generation, visibility, collision, edit, persistence, and revision tests continue to pass.

## Luna handoff checklist

- Start with Phase 0 instrumentation and include baseline evidence in the implementation PR.
- Preserve this document's ownership boundary even if class/file names change.
- Implement streaming first; do not combine all phases into one unreviewable change.
- Keep a synchronous feature flag until async parity and shutdown tests pass.
- Treat cross-chunk writers, neighbor lookups, eviction, block edits, and world revision changes as explicit race boundaries.
- Do not solve races by holding one world mutex during generation; that would move the stall behind a lock and still block the main thread.
- Update this document when implementation decisions differ, especially cancellation, transaction policy, and cross-chunk edit conflict resolution.
