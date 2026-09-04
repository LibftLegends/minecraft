# Voxel lighting implementation handoff

Status: implementation paused at the user's request on 2026-09-04.

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

## Recommended next order

1. Re-run Valgrind and the normal build from the committed state.
2. Add diagonal halo capture and lighting seam tests.
3. Implement the persistent budgeted node scheduler and stale-light revision.
4. Add software renderer/shadow support and entity submissions.
5. Run validators and repeated break-workload analytics.
6. Update READMEs/dependency documentation and run all final gates.
