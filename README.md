# ft_minecraft

The repository is now the base for **`ft_minecraft`**, the follow-up project:
diverse biomes, rivers, caves with ore clusters, monsters, multiplayer, and
advanced rendering (lighting, shadows, SSAO, fog). The active task list,
owner-tagged milestones, and bonus scope are in
[MILESTONES.md](MILESTONES.md) — that is now the source of truth for
day-to-day work.

The prototype is built around Libft DUMB for window/input glue and Libft
Voxel/Game modules for chunk storage, deterministic terrain data, and mesh
generation.

## Current Focus

Building `ft_minecraft` on top of the working `ft_vox` engine. See
[MILESTONES.md](MILESTONES.md) for the full plan; in short:

- lock shared contracts (persistence format, network protocol, entity state)
  before splitting off into parallel tracks;
- Track A (bvangene): world/biome overhaul, caves & ore clusters, vegetation,
  player mechanics (sprint/fly/swim), monsters, and the gameplay bonus
  features (growing plants, crafting, bow & arrow, villages, nether portal,
  water simulation);
- Track B (rperez-t): persistence, advanced rendering (lighting, shadows,
  SSAO, fog, clouds), server-authoritative multiplayer netcode, interface
  and audio, and cross-platform packaging.

## Build

Current build command:

```sh
make
```

GNU Make 3.81 or newer is supported. GNU Make 4.0 and newer may optionally
use `--output-sync=target` for grouped parallel output.

The parent Makefile includes Libft's flattened GNU Make graph, so a parallel
build can schedule ft_vox and Libft object files through the same job pool.
Useful validation commands are:

```sh
make -j2 all
make plan
make --trace -j2 all
make -j2 tests
make test
```

The public build and test targets first perform a read-only Make planning pass
and report how many stale source targets require rebuilding. Minecraft and
Libft work are grouped separately, with Libft broken down by module. The real
build then prints concise compile, archive, and link messages while preserving
compiler diagnostics. The planning pass uses the same flattened dependency
graph as the build. Successful compiles print session-local `completed/total`
counts for the active Minecraft or Libft module, and successful Libft archives
print a session-wide archive `completed/total` count. The counters are seeded
by the plan and do not scan object directories or use repository-wide state.
The initial compile count is the number of targets Make considered stale when
the plan ran, not the number of files currently present. Archive and link work
are counted separately. Public targets do one read-only plan pass and then one
combined graph build; this small planning overhead replaces repeated per-module
stale checks and is particularly valuable on Windows. If a source or header
changes between the plan and the build, the actual Make graph remains
authoritative and the live counters are informational.

`make test` runs the complete headless validator set. It does not launch the
interactive game; use `make all` and run `./ft_vox` separately for that.

The trace for a no-op build should contain no compiler, archiver, linker, or
recursive `make -C Libft` command. Use `make clean` for native objects and
`make fclean` when the selected in-tree Libft configuration should also be
removed.
For build-system diagnostics that intentionally bypass the summary wrapper,
use `make internal-all` or `make internal-tests`.

Native ft_vox objects are stored under configuration-specific `objs_*_cfg...`
directories. Changing compiler flags, optimization, debug, coverage, LTO, or
feature-detection inputs therefore cannot reuse incompatible objects from a
previous configuration.

Submodule initialization:

```sh
make submodule_init
```

Submodule update:

```sh
make submodule_update
```

Clean build files:

```sh
make fclean
```

For the active `ft_minecraft` plan and milestones, see [MILESTONES.md](MILESTONES.md).
