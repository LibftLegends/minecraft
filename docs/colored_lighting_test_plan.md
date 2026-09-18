# Test-only colored block lighting

This is deliberately a validation model, not a gameplay feature. The current
Minecraft light buffer is packed sky/block light and does not yet carry RGB
channels, so no new glowstone block or renderer behavior is registered.

The block-edit validator uses the existing shimmer-stone block ID as a
test-only glowstone identity and verifies the contract that a future colored
source must satisfy:

- a source has an explicit RGB color and source level;
- each traversable voxel reduces the level by one per step;
- every channel clamps at zero independently;
- the source cell keeps its exact configured color and level;
- propagation is bounded by opacity and the six-neighbor frontier;
- two sources combine by the configured rule, initially per-channel maximum;
- removing or replacing a source invalidates the old contribution and
  re-adds neighboring sources without publishing a partial buffer;
- a border source schedules only transparent/water opposing voxels and
  preserves the opaque-neighbor optimization;
- stale colored-light results are rejected with the same content/input version
  checks as ordinary light.

The first test currently covers source color `(15,10,5)` and attenuation over
four transparent cells, including independent channel clamping. The future
Libft/Voxel colored-light implementation should add integration tests using a
temporary runtime block registration rather than adding the test block to the
normal Minecraft registry. Those tests must cover red/green/blue sources,
overlapping sources, opaque barriers, source removal, chunk-border transfer,
version rollover, and deterministic repeated runs.
