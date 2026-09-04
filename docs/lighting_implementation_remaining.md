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
- Block edits are coalesced into a loaded 3x3 dirty region and submitted to the
  asynchronous remesh pipeline instead of synchronously rebuilding nine
  chunks in the edit call.
- Remesh snapshots now include a 15-block horizontal lighting halo and workers
  consume immutable snapshot data. Missing streamed space is conservative
  solid space.
- CSV parsing preserves trailing/consecutive empty fields, rejects structural
  delimiters, and rolls back row metadata when the second vector append fails.
- The complete design document is promoted to
  `Libft/Docs/rendering_main_thread_optimization_design.md`.

## Still required before calling the design complete

1. Finish the runtime lighting scheduler. The current runtime configuration
   surface exists, but the configured minimum/target/maximum node counts and
   microsecond budget are not yet a true queue-level light-node scheduler.
   Replace chunk-job scanning with persistent bounded propagation queues,
   prioritize player-near edits and distance from the originating edit, and
   coalesce dirty sections before remeshing.
2. Complete halo capture for diagonal neighboring chunks. The current compact
   snapshot captures the target plus cardinal neighbors and uses conservative
   solid values for halo corners. Extend the snapshot or callback contract to
   include all eight neighboring chunks, then add seam tests for diagonal
   sources.
3. Add revision identity to lighting itself. Mesh results have stale-result
   checks, but light dirty regions need an explicit light revision and stale
   discard diagnostics so rapid edits cannot publish an older light field.
4. Make neighbor arrival/removal enqueue bounded relighting as well as face
   remeshing, and verify that temporary conservative boundaries converge after
   the neighbor is published.
5. Add full Libft lighting tests: pack/unpack, direct skylight, roof/cave
   occlusion, block falloff, multiple-source maximum, attenuation metadata,
   chunk seams, deterministic order, and edit equivalence with a clean rebuild.
6. Add Minecraft validators for edit-to-outside lighting, edit propagation
   across chunk boundaries, stale worker rejection, and the configured work
   budget. Include repeated block-breaking workloads and record p50/p95/p99
   frame time, queue peak, propagation count, snapshot bytes, and stale jobs.
7. Implement software-renderer lighting and blob shadows. The GPU path is
   wired, but the software path must prepare face/triangle shade outside its
   pixel loop and must not query the world per pixel.
8. Integrate shadows for mobs/entities when the Minecraft entity render list
   is available. The current blob submission is player-only because no entity
   draw submission path was present in the reviewed branch.
9. Add the camera look-down validator for ordinary underfoot break/place
   raycasts and verify the 89-degree limit in a real interaction run.
10. Finish the CSV test run. The broad test build was initially blocked by an
    existing `-Werror` useless-cast failure in
    `Test/Test/test_compression_stream.cpp`; that warning was corrected, but a
    full test executable still needs to be built and run.
11. Add/update module READMEs for the new public voxel-light and shadow APIs,
    and update the Libft dependency graph if required by the repository's
    documentation checks.
12. Run final matched normal/analytics builds, `make validate-all`, and a
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
2. Add diagonal halo capture and lighting seam tests.
3. Implement the persistent budgeted node scheduler and stale-light revision.
4. Add software renderer/shadow support and entity submissions.
5. Run validators and repeated break-workload analytics.
6. Update READMEs/dependency documentation and run all final gates.
