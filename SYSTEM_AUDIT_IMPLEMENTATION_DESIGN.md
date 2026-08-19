# Terrain Streaming, Biome Blending, and Allocator Performance Design

## Purpose

This document is an implementation handoff for the Luna model. It audits the current terrain-streaming, biome-transition, CMA, SCMA, and PThread mutex paths and defines how to correct or optimize them without mixing unrelated changes.

The findings are based on the repository state on 2026-08-17. The audit was followed by an implementation pass in the shared worktree; the implementation and test status are summarized below.

### Implementation status

The implementation pass now includes persistent round-robin streaming candidates with starvation credit and retry diagnostics, weighted biome sampling shared by terrain layers and tree decoration, mountain-region/ridge/valley/slope policies, allocation-free SCMA batch I/O, SCMA free-span reuse with split/coalesce/wipe handling, timed wait/notify allocator transitions, and TLS-backed lock-tracking cleanup for foreign and project-created threads. The full Libft test suite is the validation gate for these changes.

## Executive summary

The missing far-chunk behavior has a confirmed starvation path. During normal gameplay both render-distance strategies return a chunk-generation budget of zero whenever the smoothed frame time is above 12 ms. A normal 60 FPS frame is about 16.7 ms, so generation can stop permanently after the bounded startup load. Moving makes new chunks relevant, but relevance alone does not guarantee that any generation work is admitted.

Biome edges have two confirmed consistency problems. Material blending uses a probability that is zero at the biome boundary and increases toward the interior, which is the reverse of the intended neighbor-material contribution. It also selects only one neighboring biome for material blending, while height blending applies X and Z transitions sequentially. Corners therefore use different biome mixtures for shape and surface blocks and are dependent on branch/order behavior.

The allocator and mutex code is correctness-oriented but pays substantial fixed overhead on every operation. CMA and SCMA each perform transition-state atomics, thread-exit arming, and a tracked recursive-mutex operation around a globally serialized data structure. The tracked mutex then takes another global registry mutex and searches dynamic registries. SCMA batch I/O additionally allocates temporary pointer arrays while already holding its global mutex. These costs are especially visible on Windows.

Implementation priority:

1. Guarantee terrain-streaming progress.
2. Replace the biome-edge calculation with one shared weight sample used by height, materials, and decorations.
3. Remove avoidable hot-path setup and temporary allocations from CMA/SCMA.
4. Redesign lock tracking so uncontended mutex operations do not require a second global mutex.
5. Consider allocator sharding or thread-local magazines only after the lower-risk changes are measured.

## Part I: Terrain streaming audit

### Current flow

The gameplay path is:

`GameSession::tick_world()` -> `generation_budget_for_frame()` -> `World::update_around()` -> `World::stream_chunks()` -> `World::try_load_chunk_at()` -> `WorldChunkLoader::initialize_chunk()`.

Relevant files:

- `src/app/GameSession.cpp`
- `src/policy/AdaptiveRenderStrategy.cpp`
- `src/policy/FixedRenderStrategy.cpp`
- `src/world/World.cpp`
- `src/chunks/WorldChunkStore.cpp`
- `src/chunks/WorldChunkLoader.cpp`
- `src/coordinates/WorldCoordinates.cpp`

### Confirmed root cause: generation can be starved forever

Both `AdaptiveRenderStrategy::generation_budget_for_frame()` and `FixedRenderStrategy::generation_budget_for_frame()` return zero when `frame_ms > 12.0`. `GameSession` initializes its smoothed performance sample to 16.67 ms and updates it toward the real frame time. Therefore ordinary 60 FPS operation normally yields no gameplay generation budget.

Startup appears healthy because `World::initialize()` explicitly generates up to 64 chunks and `GameSession::loading_tick()` uses a fixed budget of two. Once gameplay starts, newly relevant chunks may never be generated.

This is a scheduling contract bug: generation is treated as optional spare-time work, but visible-world completeness requires eventual progress.

### Secondary streaming weaknesses

#### Distance-only ordering repeats full scans

`stream_chunks()` starts at the nearest candidate every call and scans past every loaded candidate before reaching missing chunks. As the loaded disk fills, each generated chunk requires progressively more lookup work. The chunk index makes each lookup cheap, but the repeated prefix scan is still unnecessary.

#### Work is budgeted by chunk count, not time or stage

One chunk includes terrain synthesis, mesh generation, and four neighbor remesh attempts. A count of one can have very different cost depending on terrain and neighbor state. This makes frame-time control unstable.

#### Generation and meshing are synchronous

Terrain data generation and mesh creation run in the gameplay update. A slow chunk raises frame time, which currently suppresses subsequent generation and reinforces starvation.

#### Capacity is exactly the cache square

The chunk array contains 625 entries, matching a 25 by 25 cache grid. The active stream radius is circular, so it normally fits, but failed destruction, an index inconsistency, or changing constants independently can turn `find_free_chunk_slot()` into `FT_ERR_NO_MEMORY`. The relationship is implicit and should be asserted.

#### Failure handling has no retry state

Generation errors abort `update_around()`. There is no per-candidate retry/backoff or diagnostic state, so a transient failure can repeatedly stop the same update without exposing which coordinate is blocking progress.

### Required streaming design

#### 1. Introduce a persistent chunk work queue

World state should maintain explicit candidate records keyed by chunk coordinate. Each record should have:

- coordinate;
- squared distance and optional view-direction priority;
- state: absent, queued, generating, generated-data, meshing, ready, failed-retryable, failed-terminal;
- relevance epoch;
- retry count and next retry time;
- generation/configuration revision.

When the center chunk or render distance changes, update relevance incrementally. Do not rebuild work by rescanning the entire sorted disk every frame. A priority queue may contain stale entries; validate the relevance epoch when popping rather than performing expensive arbitrary removal.

#### 2. Guarantee minimum progress

The scheduler must provide two budgets:

- a soft frame-time budget for normal work;
- a starvation deadline for required chunks.

Recommended policy:

- Permit zero generation during an individual overloaded frame.
- Accumulate generation credit over time.
- Guarantee at least one required work item after a bounded interval, such as 100-250 ms, even when frame time remains above target.
- Prioritize the nearest missing chunk and chunks in front of the camera.
- Never tie all progress to a threshold below the target frame duration. For 60 FPS, a 12 ms cutoff is too strict even before rendering cost is considered.

Use elapsed microseconds rather than only chunk count. Stop starting new work after the budget is consumed, but allow the current non-preemptible stage to finish safely.

#### 3. Separate stages

Represent generation as at least:

1. terrain data;
2. decoration/cross-chunk writes;
3. mesh build;
4. main-thread/GPU publication;
5. neighbor remesh notifications.

Terrain and CPU mesh construction may run on worker threads if the terrain context and block registry are immutable during a job. Publication into `World::chunks`, index mutation, renderer upload, and eviction must remain coordinated by the owning world thread unless those structures are redesigned for concurrency.

Do not let worker jobs write directly into reusable `WorldChunk` slots that eviction can destroy. Generate into a job-owned result and commit only if its coordinate, relevance epoch, and generation revision still match.

#### 4. Add backpressure and cancellation

Bound queued and completed-but-unpublished jobs. Cancel or discard results that are no longer relevant. Keep a small prefetch margin outside the visible radius, but distinguish it from required visible chunks so prefetch cannot delay holes inside the render radius.

#### 5. Make errors observable

Track the first failure code, coordinate, stage, retry count, queue depth, oldest required-item age, and number of ready/missing chunks. Add these to diagnostics. Retry allocation/transient errors with bounded exponential backoff; mark deterministic invalid-config failures terminal until configuration changes.

### Streaming acceptance tests

- Start with a 16.67 ms and a 25 ms synthetic frame-time stream, move continuously across chunk boundaries, and verify every chunk entering the required radius becomes ready within a bounded time.
- Hold generation budget at zero for several frames, then verify accumulated credit causes progress.
- Teleport farther than the cache diameter and verify stale queued jobs do not publish into reused slots.
- Oscillate around a chunk boundary and verify candidates are not duplicated indefinitely.
- Fill the cache, move one chunk, and verify eviction creates capacity before generation starts.
- Inject a retryable generation failure for one coordinate and verify other coordinates continue progressing.
- Change render distance while jobs are running and verify publication validates the current relevance epoch.
- Verify negative world coordinates use the same relevance and generation behavior as positive coordinates.

## Part II: Biome-edge audit

### Current model

The default selector assigns a single biome to each fixed 128 by 128 block zone using `(seed + zone_x + zone_z) % biome_count`. Height blending samples neighboring biome profiles within the fixed blend width. Surface and subsurface material blending separately asks for one transition neighbor and chooses its block with a seeded percentage.

Relevant files:

- `Libft/Modules/Voxel/voxel_data.cpp`
- `Libft/Modules/Voxel/voxel_generation.cpp`
- `Libft/Modules/Voxel/voxel.hpp`
- `Libft/Test/Test/test_voxel_generator.cpp`

### Confirmed defects

#### Material probability is oriented incorrectly

`terrain_should_use_transition_block()` derives neighbor-material probability as:

`transition_distance * 100 / blend_width`.

At the exact border, distance is zero, so the column uses zero percent neighbor material. Farther inside the current biome, neighbor material becomes more probable. This creates a reversed band instead of a natural boundary mixture.

The probability must be derived from a clearly named current-biome weight or neighbor-biome weight. At the interior edge of the blend band, current weight should approach one. At the shared boundary, both sides should derive compatible weights from the same continuous field.

#### Height and material blending do not use the same biome sample

Height blends X and then Z. Material selection uses an `if / else if` chain and picks only one edge. At corners, terrain shape can include two neighboring profiles while blocks include one. Decorations continue to use the original discrete biome. The visible result can be a smoothly shaped corner covered by an abrupt or incorrect palette.

#### The default biome map is a checker/diagonal zone function

The sum of zone coordinates produces predictable diagonal repetition and hard axis-aligned ownership boundaries. Transition noise only perturbs block choice; it does not perturb biome ownership or the height blend boundary itself.

#### Transition noise is coarse and discontinuous in intent

Noise is sampled by integer cells using floor division. This produces blocks of identical probability offset rather than a smoothly varying edge displacement. Per-block hash then creates speckling, but not a coherent irregular shoreline or biome boundary.

### Required biome design

#### 1. Define one biome sample result

Add an internal sampling concept returned for each world X/Z column:

- primary biome;
- up to three secondary biome indices;
- normalized weights summing to one;
- dominant-biome confidence;
- edge noise/displacement value;
- optional climate values such as temperature, humidity, continentalness, and erosion.

Every terrain stage must consume this same result: height, topsoil depth, surface/subsurface palette, beaches, snow, vegetation density, tree choice, and custom feature filters.

The public discrete `terrain_get_biome_index()` can return the highest-weight biome for compatibility, while generation uses the full internal sample.

#### 2. Replace square zones with a continuous ownership field

Recommended implementation is jittered Voronoi biome cells:

- Divide world space into coarse biome cells.
- Derive a deterministic jittered site and biome id for each nearby cell from seed and cell coordinate.
- Evaluate the nearest two to four sites for every column.
- Convert the distance difference between the closest sites into a blend weight.
- Add low-frequency domain-warp noise to the sample coordinate before distance evaluation.

This produces irregular, deterministic boundaries and naturally handles corners where three regions meet. An alternative climate-map implementation is acceptable, but it must still output normalized multi-biome weights and avoid fixed rectangular ownership.

#### 3. Blend scalar properties numerically

Compute surface base height, variation amplitude, ridge strength, erosion strength, topsoil depth, snow eligibility, and decoration density from weighted biome values. Sample shared world-space height noises once per column where possible, then scale/combine them using blended parameters. Do not independently sample a complete height for each biome with unrelated branch logic unless the desired effect is explicitly tested.

Clamp only after blending. This avoids flat shelves caused by clamping each biome separately.

#### 4. Blend categorical block palettes with coherent dithering

Block ids cannot be averaged. Select among weighted biome palettes using deterministic blue-noise-like or hashed threshold sampling in world space. Use a low-frequency edge displacement plus a higher-frequency dither. Surface and subsurface should use related salts so transitions look layered rather than independently speckled.

The selection probability must directly use the normalized biome weights. It must not infer orientation from distance to a particular side of a square zone.

#### 5. Make decorations weight-aware

Scale shrub/tree/feature chance by biome weight. Require a minimum dominant weight for highly biome-specific structures when necessary. Tree species should be selected from weighted candidates, while placement coordinates remain deterministic.

#### 6. Preserve chunk independence

All biome samples must depend only on world coordinate, immutable configuration, and seed. No result may depend on generation order, loaded neighbors, local chunk coordinate, or mutable random state. Adjacent chunks must produce identical samples for the same border coordinates when generated separately.

### Biome acceptance tests

- Generate neighboring chunks independently and compare height/material samples across both sides of every shared border.
- Sample a long line through each biome pair and verify weights vary continuously and sum to one.
- Verify categorical material frequencies approximate the sampled weights over a sufficiently large edge region.
- Test two-, three-, and four-cell junctions; height, material, and decoration must use the same dominant/secondary set.
- Test negative coordinates and coordinates around zero.
- Regenerate chunks in reverse order and compare byte-for-byte output.
- Visual regression maps should show biome id, weights, height, surface block, and decoration density separately.
- Bump the terrain generator version when output changes so stale generated chunks are not reused.

## Part III: Mountain-generation design

### Why the current mountains feel artificial

The current mountain contribution is a single two-dimensional value-noise sample transformed with `1 - abs(noise)` and multiplied by one global strength. Erosion is another value-noise sample that subtracts height only where its value is positive.

This creates several visible limitations:

- Ridges are repeated noise bands rather than connected mountain ranges.
- There is no low-frequency mountain-region mask, so ridge shapes can appear uniformly across every biome that permits them instead of forming a range with foothills, a core, and an exit back to lowlands.
- One scale controls the overall shape. There is no separation between continental mass, range direction, individual peaks, cliff detail, and surface roughness.
- The ridge transform produces similarly rounded crests. It does not distinguish broad old mountains from sharp young mountains.
- Current erosion is height subtraction driven by unrelated noise. It creates dents, but it does not follow slope, drainage, valleys, or ridge direction.
- Surface materials are biome-based rather than slope-based. A steep face can receive the same topsoil as a flat plateau.
- Snow is controlled primarily by height and biome permission. It does not account for slope, exposed rock, or local variation in snowline.
- The heightmap representation cannot produce overhangs, arches, or true vertical faces. That is acceptable for the first redesign, but it must be understood as a deliberate limitation.

The built-in ridge strength of eight blocks is also too small to establish a convincing large-scale silhouette by itself. Increasing it without changing the model would only amplify repetitive ridge bands and abrupt slopes.

### Required mountain shape model

Mountain generation should be a deterministic composition of independently testable fields. Every field must use world coordinates and immutable seed/configuration data so separate chunks remain seamless.

#### 1. Mountain-region mask

Generate a very low-frequency `mountain_mask` in the zero-to-one range. Domain-warp its sample coordinates with a second low-frequency field so regions are elongated and irregular instead of circular blobs.

The mask defines where a range exists:

- near zero: plains or ordinary biome terrain;
- low values: foothills;
- middle values: rising range shoulders and valleys;
- high values: mountain core and major peaks.

Biome weights should scale the mask rather than toggle ridges as a binary flag. A mountains biome may use the full mask, hills may use a reduced contribution, and plains/desert/snow biomes may still receive gentle foothills near a weighted boundary.

Use a smooth threshold with a configurable start and full-strength point. Never apply a hard `if mountain_mask > threshold` cutoff because that creates a ring-shaped terrain seam.

#### 2. Range direction and ridge network

Sample ridged multifractal noise at two or three octaves after domain warping. The low octave defines the main range direction and long crest lines. A middle octave divides those crests into individual peaks and passes. A restrained high octave adds local variation without changing the range silhouette.

Conceptually:

`ridge = weighted_sum(pow(1 - abs(noise_i), ridge_sharpness_i))`

The exponent controls crest character:

- lower exponent: broad, weathered ridges;
- higher exponent: narrow, sharp ridges.

Normalize the weighted sum before applying height. Do not let octave count implicitly increase maximum elevation.

The ridge field must be multiplied by the mountain-region mask. Ridges should fade into foothills instead of continuing visibly through lowlands.

#### 3. Base uplift, foothills, and peaks

Construct mountain height from distinct terms:

- ordinary biome/base terrain height;
- broad uplift from the mountain mask;
- foothill undulation strongest around the lower/middle mask range;
- ridge elevation strongest in the mountain core;
- sparse peak accents selected by a separate low-frequency peak field;
- valley/drainage subtraction.

Broad uplift must contribute more to the overall range silhouette than high-frequency detail. The result should still look mountainous when viewed from far away or when detail octaves are disabled.

Peak accents must be sparse and mask-aware. Avoid raising every ridge intersection into an identical spike. A deterministic peak selector can vary peak height, width, and sharpness by range cell.

#### 4. Valleys, passes, and drainage

Create valleys from a separate warped ridge/cellular field. Subtract valley depth primarily where the mountain mask is medium or high. Widen valleys at low elevations so they connect naturally to rivers and lowlands.

The same drainage field should be available to the river system. Rivers should follow valleys out of mountain regions rather than crossing ridge crests because their noise fields are unrelated.

Mountain passes should emerge where the ridge field is interrupted by the valley field. Add a minimum-pass policy only if gameplay requires traversable routes; do not flatten complete horizontal corridors.

#### 5. Erosion approximation

A full hydraulic simulation is unnecessary for runtime generation. Use a deterministic procedural approximation based on local samples:

1. Sample preliminary height at the current column and nearby world-space offsets.
2. Estimate gradient magnitude and direction.
3. Reduce sharp high-frequency detail on shallow slopes.
4. Preserve or sharpen exposed ridge crests within a configured range.
5. Cut channels where gradient direction aligns with the drainage field.
6. Deposit a small amount near the base of steep slopes to form talus and gentler foothill transitions.

This can be implemented as a second pure sampling pass; it must not read generated neighboring chunks. Sampling the mathematical height function at adjacent world coordinates keeps generation order independent.

Avoid repeated recursive height evaluation. Split the sampler into `raw_mountain_height()` and `eroded_mountain_height()`, and ensure derivative samples call only the raw function.

#### 6. Slope-aware surface layers

Estimate slope from neighboring height samples and use it alongside biome weights and elevation:

- flat/gentle slopes: biome topsoil, grass, sand, or snow-covered soil;
- moderate slopes: thinner topsoil with exposed stone patches;
- steep slopes: cliff rock palette with little or no topsoil;
- foot of steep slopes: optional gravel/scree/talus material;
- high flat shelves: snow accumulation;
- high steep faces: exposed rock with sparse snow ledges.

Material choice should use coherent world-space dithering near slope thresholds so a contour does not become a perfectly sharp material line.

The mountain rock palette should be configurable by biome. Desert mountains, snowy granite ranges, mossy hills, and volcanic terrain should share the shape model without sharing identical blocks.

#### 7. Snowline and exposure

Replace one absolute snowline decision with a snow coverage value based on:

- elevation above biome/configured snowline;
- dominant and secondary biome weights;
- slope;
- low-frequency snowline variation;
- optional exposure/aspect approximation.

Snow depth should increase gradually with elevation on suitable slopes. Steep cliffs should remain mostly exposed, while shelves and bowls retain deeper snow. This avoids a flat white horizontal stripe around every mountain.

#### 8. Caves and mountain interiors

Existing cave height limits and surface margins must use the final mountain height. Mountain caves should support entrances on slopes without carving every high peak hollow.

Add optional mountain-specific cave policy values:

- entrance probability by slope/elevation;
- tunnel bias along the range direction;
- cavern suppression near narrow peaks;
- ore distribution adjusted by mountain uplift or rock type.

Do not derive cave continuity from generated blocks. Continue using world-space fields so tunnels cross chunk borders deterministically.

### Optional later phase: three-dimensional density mountains

After the heightmap redesign is stable, selected mountain cores may use a three-dimensional density field to create overhangs, arches, cliff recesses, and more vertical faces.

This must be an additive, masked stage:

- retain the two-dimensional height model as the broad mass and fallback;
- evaluate 3D density only inside high mountain-mask regions to control cost;
- blend density terrain into the heightmap shell over a wide band;
- guarantee a solid foundation and avoid floating noise fragments unless floating terrain is explicitly desired;
- make cave density and mountain density use separate salts and a documented combination rule.

Do not begin with 3D density. Better range placement, scale hierarchy, valleys, erosion, and slope materials will provide a larger visual improvement for much lower complexity.

### Mountain configuration contract

Replace or extend the current scale/strength pair with grouped settings:

- region scale, domain-warp scale, and warp strength;
- mountain start/full mask thresholds;
- broad uplift and foothill strength;
- ridge octave count, per-octave scale/weight, and sharpness;
- peak scale, chance/density, height, and width range;
- valley scale, width, and depth;
- erosion sample distance, strength, channel strength, and talus strength;
- cliff slope thresholds and cliff/talus block palettes per biome;
- snowline, variation, slope limit, and maximum depth;
- optional 3D density enable flag, scale, threshold, and vertical range.

All values must participate in configuration validation, serialization, configuration signatures, Lua scripting exposure where appropriate, and terrain generator versioning.

Provide presets rather than forcing callers to tune dozens of independent values:

- rolling hills;
- broad old mountains;
- sharp alpine range;
- desert mesas/canyons;
- volcanic range.

Presets should populate the same underlying configuration and remain fully serializable.

### Mountain implementation sequence for Luna

1. Add diagnostic image/map output for mountain mask, raw ridges, valleys, raw height, final height, slope, and surface material.
2. Implement the mountain-region mask and broad uplift while retaining the existing ridge term behind a compatibility path.
3. Replace the single ridge term with normalized domain-warped multifractal ridges.
4. Add valleys/passes and connect the drainage field to future river placement.
5. Add the non-recursive erosion/slope sampling pass.
6. Route surface layers and snow through final height and slope.
7. Add configuration, serialization, signatures, Lua access, and presets.
8. Bump generator version and regenerate visual fixtures.
9. Consider 3D density only after the heightmap acceptance tests and performance budget pass.

### Mountain acceptance tests

- Generate a large height map and verify mountains occupy bounded regions with foothills rather than appearing uniformly everywhere.
- Verify broad silhouette remains recognizable with high-frequency octaves disabled.
- Verify ridge/valley samples and final blocks match exactly across independently generated chunk borders.
- Verify generation order and thread scheduling do not affect output.
- Verify maximum slope, minimum/maximum height, and world-height clamping for extreme valid configurations.
- Verify valleys descend toward lowlands and do not terminate as enclosed vertical pits unless a basin is intentionally selected.
- Verify topsoil depth decreases with slope and cliff materials appear on steep faces.
- Verify snow coverage changes gradually with elevation and avoids most steep faces.
- Verify biome boundaries blend mountain uplift, rock palette, vegetation, and snow consistently through the shared biome weights.
- Verify negative coordinates, zero crossings, and very distant coordinates.
- Benchmark column sampling cost with each stage enabled independently and all stages enabled.
- Produce fixed-seed visual regression images from overhead, height-map, and side-profile views.

## Part IV: CMA performance audit

### Current hot-path costs

Every top-level CMA operation can perform:

- thread-exit cleanup arming and `pthread_setspecific()`;
- an atomic load/CAS loop on the packed transition/active-count state;
- allocator-mutex pointer preparation/loading;
- a tracked recursive-mutex lock;
- another global lock inside PThread lock tracking;
- metadata protection/guard work;
- global allocator bookkeeping;
- the reverse sequence during unlock.

Small allocations use a small arena and larger allocations use free bins/pages, which are useful existing optimizations. The remaining dominant issue is global synchronization and diagnostic overhead around them.

### Low-risk CMA improvements

#### 1. Arm thread-exit cleanup once per thread

Add a thread-local `cleanup_armed` flag. Call `pthread_setspecific()` only on the first top-level allocator entry for that thread. Keep the C++ thread-local guard as the second cleanup mechanism. Measure Windows thread entry and steady-state operations separately.

#### 2. Remove repeated mutex preparation from steady state

The enable transition should fully construct and publish the allocator mutex before setting the enabled bit. A hot operation that observes enabled state should load the published pointer and lock it; it should not call the creation/CAS path. A null pointer while enabled is an invalid invariant, not a reason to attempt initialization in every allocator operation.

#### 3. Use wait/notify for transitions

Operations currently yield while transition is set, and transitions sleep for one millisecond while waiting for active operations. Replace this with the project wait-on-atomic abstraction or a condition variable/state epoch. The last exiting operation should wake transition waiters; transition completion should wake blocked operations. Keep timed and try APIs.

#### 4. Make metadata protection a build/runtime policy

If metadata protection invokes page-protection calls at operation boundaries, it is too expensive for ordinary release allocation. Keep full protection for hardened/debug modes. In release, protect only on explicit validation boundaries or leave metadata writable while preserving magic/guard checks. Benchmark this separately because it may dominate syscall-heavy platforms.

#### 5. Avoid duplicate hot-path condition checks

Snapshot immutable-per-operation state once after active-count admission. Use that snapshot for lock/unlock decisions. Transition state guarantees the mutex cannot be removed while the operation is active. Avoid re-reading packed state on unlock merely to rediscover whether this operation acquired the mutex; store that fact in the operation token.

Instead of a bare `ft_bool lock_acquired`, introduce an internal operation token containing top-level/nested status, mutex pointer, and whether metadata protection was entered. This makes cleanup exact and reduces repeated global loads.

### Higher-impact CMA design

#### Thread-local small-allocation magazines

After the low-risk work is measured, add per-thread magazines for selected small size classes. Allocate/refill a batch under the global allocator lock, then satisfy/free ordinary small blocks locally. Flush bounded batches on thread exit and during thread-safety transitions/reset.

Requirements:

- strict per-thread and process-wide cache limits;
- generation/epoch invalidation on allocator reset;
- no cached blocks from pages that can be returned to the OS;
- statistics that distinguish live allocations from cached free blocks;
- deterministic drain during tests and shutdown;
- cross-thread free either routed to a central list or placed in an atomic remote-free queue for the owning magazine.

Do not implement page sharding and magazines in the same change. Magazines are the smaller, measurable step.

### CMA tests and benchmarks

- Steady-state single-thread small allocate/free latency after warmup.
- Contended allocation with 2, 4, 8, and 16 threads.
- Cross-thread frees.
- Transition while magazines contain blocks.
- Thread exit with non-empty magazines and nested operation depth.
- Allocator reset invalidates all caches.
- Compare release, hardened, and test builds separately.
- Benchmark setup cost separately from one million steady-state operations.

## Part V: SCMA performance audit

### Current hot-path costs

SCMA is a single movable heap protected by one global recursive mutex. Every API call performs transition admission and tracked mutex locking. Allocation may run incremental compaction. Non-tail resize moves data to the heap tail. Batch read/write allocates a temporary `scma_block **` array with `std::malloc()` while holding the global SCMA lock.

### Low-risk SCMA improvements

#### 1. Remove batch temporary allocations

Keep the global lock for the whole batch. First pass: validate every request and all arithmetic without writing. Second pass: validate the handle again or retrieve it directly and perform the copies. Because the global lock prevents heap/block movement between passes, no pointer array is needed.

If avoiding duplicate validation is important, use a bounded stack buffer for small batches and a reusable internal scratch buffer for large batches, but the two-pass approach is simpler and has no allocation failure mode.

Preserve all-or-nothing validation semantics: no copy may occur until every request is validated.

#### 2. Apply the same one-time cleanup arming and operation-token design as CMA

CMA and SCMA should share an internal transition-gate implementation rather than duplicate packed-state, timeout, TLS cleanup, and polling logic. The shared component should be allocator-independent and accept create/destroy/lock/unlock callbacks or be templated internally.

#### 3. Do not prepare the mutex on each lock

As with CMA, enabled state must imply a valid published mutex. Construction belongs to the transition path.

#### 4. Move optional compaction out of ordinary allocation

An allocation should not unexpectedly compact up to 64 KiB merely because a previous non-tail free set a flag. Introduce explicit compaction debt and budget it separately. Allocation may consume a small configured debt budget only when required for capacity; otherwise compaction runs through a maintenance call or scheduler.

### Higher-impact SCMA design

#### Free-span reuse before compaction

Maintain free spans ordered by offset and optionally indexed by size class. A non-tail free securely wipes its bytes and inserts a span. Allocation uses a suitable span before appending at `used_size`. Adjacent spans coalesce. Handles remain valid because live blocks do not move.

Compaction remains available when fragmentation exceeds a threshold or a large contiguous allocation cannot be satisfied. This reduces memmove traffic and long global-lock holds.

Requirements:

- span metadata must not live inside the secure payload;
- every freed/remainder byte must be securely wiped;
- split and coalesce arithmetic must be overflow checked;
- handle generation remains unchanged by compaction and changes only on free/reuse as currently intended;
- allocation rollback must leave both span index and live list unchanged on failure.

Read concurrency should not be attempted until free-span reuse and transition overhead are stable. A movable/reallocating heap makes reader/writer locking significantly more complicated, and short reads are likely better served by reducing lock overhead first.

### SCMA tests and benchmarks

- Batch read/write sizes 0, 1, 8, 64, and 4096 with proof that no temporary allocation occurs.
- Fragmented workloads comparing bytes moved and lock hold time before/after free-span reuse.
- Secure-wipe verification for freed spans, split remainders, resize tails, compaction source ranges, and shutdown.
- Allocation failure injected at every metadata growth point with state rollback checks.
- Concurrent readers/writers and transition attempts under sustained load.
- Tail-heavy and random-free workloads reported separately.

## Part VI: PThread mutex and tracking performance audit

### Current hot-path costs

An uncontended `pt_mutex` or `pt_recursive_mutex` lock currently:

1. checks lifecycle/native state;
2. loads owner/lock atomics;
3. calls native `try_lock()`;
4. publishes owner state;
5. calls `notify_acquired()`;
6. takes the global lock-tracking registry mutex;
7. searches/updates thread and mutex-owner buffers;
8. releases the registry mutex.

Unlock performs another registry operation. Therefore a mutex intended to protect one object also serializes on a process-wide diagnostics mutex. This explains why allocator operations can effectively involve two mutexes.

### Required tracking redesign

#### 1. Keep the native mutex as the synchronization authority

Owner/depth fields support diagnostics and recursive semantics, but must not create a second correctness lock around every native operation.

#### 2. Store per-thread wait state in TLS

Each participating thread should own a stable TLS tracking record with generation and a bounded/small-vector owned set. A global registry should hold pointers/handles to active records for diagnostics and deadlock traversal. Registration and deregistration need the global registry lock; ordinary updates to the current thread record should not.

Record lifetime needs pinning or an epoch/refcount so diagnostics cannot dereference a record during thread exit.

#### 3. Make mutex owner directly queryable

Use the mutex object's atomic owner field as the owner index instead of maintaining a separate global mutex-to-owner vector. Deadlock detection can traverse:

`waiting thread record -> requested mutex -> atomic owner id/generation -> owner thread record -> waiting mutex`.

The mutex must expose an internal tracking identity with a lifetime generation so an address reused for a new mutex cannot be confused with the previous object.

#### 4. Track only the contended path where possible

For deadlock detection, publish `waiting_mutex` before a blocking native lock and clear it after acquisition/failure. Uncontended acquisition only needs to update the thread's owned set and mutex owner atomics; it should not take the global registry lock.

If complete owned-mutex snapshots are purely diagnostic, allow them to be disabled in release builds or behind a runtime diagnostics level:

- off: owner/depth correctness only;
- contention: wait graph and deadlock checks;
- full: owned-set snapshots, timestamps, and lifecycle diagnostics.

Default behavior must be explicitly chosen and documented; do not silently remove deadlock behavior.

#### 5. Avoid `try_lock()` plus `lock()` as the default contended sequence

Measure platform behavior. A fast optimistic `try_lock()` is useful only if uncontended success is common enough to offset the extra call and tracking branches. Provide benchmark variants for direct lock and try-then-lock on Windows, Linux, and macOS before choosing. The current Windows probe already contains the right primitive measurements but should feed a decision, not merely print data.

### Mutex acceptance tests and benchmarks

- Uncontended lock/unlock, recursive lock/unlock, and try-lock latency.
- Two-thread ping-pong and N-thread contention.
- Deadlock cycle detection for 2, 3, and 16 threads.
- Thread-id and mutex-address reuse with generation mismatch.
- Direct pthread, `std::thread`, project-thread, cancellation, and main-thread cleanup.
- Diagnostics snapshot concurrent with thread exit.
- Release diagnostics-off and full-tracking results reported separately.
- Windows benchmark must separate native mutex, owner atomics, TLS access, registry registration, wait tracking, and full lock wrapper.

## Shared transition-gate contract

CMA and SCMA currently duplicate almost identical state machines. Luna should extract a private shared design only after behavior-preserving tests exist.

The gate must provide:

- enabled, disabled, and transitioning lifecycle states;
- active top-level operation count;
- nested depth that does not increment the global count repeatedly;
- blocking timed transition, non-blocking try transition, and default bounded transition;
- wait/notify rather than polling/yielding;
- exact rollback when mutex create/destroy fails;
- an operation token that records the state used on entry;
- thread-exit cleanup for leaked operation depth;
- debug owner diagnostics without release hot-path allocations.

Invariant: once an operation token is admitted, the mutex/configuration it observed remains alive until that token exits. New operations cannot enter after transition ownership is acquired. Transition completion publishes the new mode before waking blocked operations.

## Implementation sequence for Luna

### Change set 1: streaming progress and observability

- Replace the zero-or-one frame policy with accumulated time credit and a bounded starvation deadline.
- Add persistent work-state diagnostics and focused streaming tests.
- Keep generation synchronous in this first change so correctness can be isolated.

### Change set 2: persistent queue and staged jobs

- Introduce relevance epochs and explicit job states.
- First keep workers disabled and drive stages synchronously through the queue.
- Then add background terrain/mesh execution with validated main-thread commit.

### Change set 3: unified biome sample

- Add the internal weighted sample and tests.
- Route height and materials through it.
- Route decorations and feature rules through it.
- Replace default square ownership with jittered/domain-warped cells.
- Build mountain regions, ridges, valleys, erosion, slope materials, and snow
  on top of the same weighted sample in the mountain sequence defined above.
- Bump generator version and add visual regression artifacts.

### Change set 4: allocator hot-path cleanup

- One-time TLS cleanup arming.
- No hot-path mutex preparation.
- Operation tokens and wait/notify transitions.
- SCMA allocation-free batch I/O.
- Benchmark each item independently.

### Change set 5: lock-tracking redesign

- TLS records and safe registry lifetime.
- Direct mutex owner identity.
- Contended-path global traversal only.
- Diagnostics levels and cross-platform benchmarks.

### Change set 6: allocator structural improvements

- SCMA free-span reuse first.
- CMA thread-local small magazines second.
- Do not combine these two changes; each needs independent corruption, leak, transition, and performance validation.

## Benchmark methodology

Do not accept an optimization based on one wall-clock run. For every platform and build type:

- one warmup plus at least ten measured samples;
- report median and p95, not only average;
- pin workload shape and operation count;
- separate initialization/transition cost from steady-state cost;
- include single-thread and contended results;
- include throughput, latency, bytes moved, peak retained memory, and lock hold time where relevant;
- compare against the parent commit artifact on the same runner class;
- treat correctness, sanitizer, leak, and deterministic-generation tests as hard gates before performance conclusions.

## Non-goals and cautions

- Do not weaken allocator transition safety to gain benchmark speed.
- Do not remove secure wiping from SCMA.
- Do not make biome output dependent on neighboring chunk availability.
- Do not publish worker-generated chunks without coordinate/revision validation.
- Do not add allocator sharding until current global invariants and statistics are documented.
- Do not optimize only the test build; stack capture and leak instrumentation must be reported separately from release behavior.
- Do not implement all recommendations in one pull request. The sequence above is designed to keep regressions attributable.

## Definition of done

The work is complete when:

- newly relevant required chunks make bounded progress at sustained 16.7 ms and 25 ms frame times;
- biome height, material, and decoration all consume the same deterministic normalized weights;
- cross-chunk terrain is byte-identical regardless of generation order;
- CMA/SCMA transitions remain safe and timed while steady-state operations avoid repeated initialization and polling work;
- SCMA batch I/O performs no per-call temporary heap allocation;
- ordinary uncontended mutex operations no longer take the global tracking registry lock;
- all lifecycle, failure-injection, cancellation, sanitizer, and leak tests pass on Windows, Linux, and macOS;
- benchmark artifacts demonstrate improvements without hidden memory-retention or tail-latency regressions.
