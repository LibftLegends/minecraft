#ifndef WORLD_HPP
# define WORLD_HPP

# include "../../Libft/Modules/Errno/errno.hpp"
# include "../../Libft/Modules/Voxel/terrain_api.hpp"
# include "../../src/chunks/WorldChunk.hpp"
# include "../../src/chunks/WorldChunkLoader.hpp"
# include "../../src/chunks/WorldChunkStore.hpp"
# include "../../src/coordinates/WorldCoordinates.hpp"
# include "../../src/edits/WorldBlockEditor.hpp"
# include "../../src/edits/WorldEditHistory.hpp"
# include "../../src/queries/WorldBlockQuery.hpp"
# include "../../src/queries/WorldRaycaster.hpp"
# include "../../src/validators/WorldVisibilityValidator.hpp"
# include "../ft_vox.hpp"

class								WorldChunkStreamer;
class								WorldRevisionManager;

class World
{
  public:
	enum							RegenerationMode
	{
		REGEN_DECORATION_REFRESH = 0,
		REGEN_UNDERGROUND_REFRESH = 1,
		REGEN_TERRAIN_RESHAPING = 2,
		REGEN_FULL = 3
	};

	enum							ChunkRevisionState
	{
		REVISION_UNCHANGED = 0,
		REVISION_PROTECTED = 1,
		REVISION_SELECTED = 2,
		REVISION_TRANSITION = 3
	};

	struct							WorldRevision
	{
		uint32_t					identifier;
		uint32_t					stage_mask;
		RegenerationMode			mode;
		bool						pending;
		bool						regenerating;
		size_t						selected_count;
		size_t						manually_protected_count;
	};

	struct							RevisionPreviewEntry
	{
		int32_t						chunk_x;
		int32_t						chunk_z;
		ChunkRevisionState			state;
	};

	struct							RevisionChunkCoordinate
	{
		int32_t						chunk_x;
		int32_t						chunk_z;
	};

	struct							RevisionRequest
	{
		terrain_generation_config	config;
		RegenerationMode			mode;
		uint32_t					stage_mask;
		std::vector<RevisionChunkCoordinate> selected_chunks;
		std::vector<RevisionChunkCoordinate> protected_chunks;
	};

	struct							RevisionRequestResult
	{
		uint32_t					revision_identifier;
		uint32_t					stage_mask;
		int32_t						regenerated_count;
		int32_t						skipped_count;
	};

	struct							StreamDiagnostics
	{
		uint64_t					frame;
		uint64_t					progress_frame;
		size_t						candidate_count;
		size_t						ready_count;
		size_t						pending_count;
		size_t						retryable_count;
		size_t						failed_count;
		uint64_t					oldest_pending_age;
		int32_t						last_error;
	};

	WorldChunk						chunks[WorldCoordinates::CHUNK_COUNT];
	WorldChunk						*chunk_index[WorldCoordinates::CHUNK_COUNT];
	int32_t							chunk_index_center_x;
	int32_t							chunk_index_center_z;
	bool							chunk_index_valid;
	int32_t							chunk_count;
	int32_t							loaded_chunk_count;
	int32_t							center_chunk_x;
	int32_t							center_chunk_z;
	int32_t							active_render_distance;
	char							seed[128];
	terrain_generation_config		terrain_config;
	terrain_generation_context		terrain_context;
	bool							terrain_generation_started;
	uint64_t						current_tick;
	WorldEditHistory				edit_history;
	ft_uniqueptr<WorldChunkStreamer> chunk_streamer_storage_;
	ft_uniqueptr<WorldRevisionManager> revision_manager_storage_;
	WorldChunkStreamer &chunk_streamer;
	WorldRevisionManager &revision_manager;

	World();
	World(const World &other);
	~World();
	World &operator=(const World &other);

	int32_t initialize(const char *seed_value);
	int32_t initialize(const char *seed_value,
		const char *terrain_config_file_path);
	void set_terrain_config(const terrain_generation_config &config);
	const terrain_generation_config &terrain_generation_settings() const;
	int32_t load_terrain_config(const char *file_path);
	int32_t save_terrain_config(const char *file_path) const;
	void destroy();
	int32_t update_around(double camera_x, double camera_z,
		int32_t generation_budget);
	int32_t update_around(double camera_x, double camera_z,
		int32_t generation_budget, int32_t render_distance);
	int32_t stream_last_error() const;
	int32_t stream_retryable_count() const;
	StreamDiagnostics stream_diagnostics() const;
	bool validate_visible_distance(double camera_x, double camera_z, double yaw,
		int32_t required_distance) const;
	bool surface_top_at(int32_t world_x, int32_t world_z,
		double *surface_top) const;
	bool solid_block_at(int32_t world_x, int32_t world_y,
		int32_t world_z) const;
	bool block_id_at(int32_t world_x, int32_t world_y, int32_t world_z,
		uint32_t *block_id) const;
	int32_t delete_block_at(int32_t world_x, int32_t world_y, int32_t world_z);
	int32_t place_block_at(int32_t world_x, int32_t world_y, int32_t world_z,
		uint32_t block_id);
	void advance_tick();
	int32_t undo_last_edit();
	int32_t redo_last_edit();
	int32_t raycast_solid(double origin_x, double origin_y, double origin_z,
		double direction_x, double direction_y, double direction_z,
		double max_distance, int32_t *block_x, int32_t *block_y,
		int32_t *block_z) const;
	int32_t raycast_edit_target(double origin_x, double origin_y,
		double origin_z, double direction_x, double direction_y,
		double direction_z, double max_distance, int32_t *hit_block_x,
		int32_t *hit_block_y, int32_t *hit_block_z, int32_t *place_block_x,
		int32_t *place_block_y, int32_t *place_block_z,
		uint32_t *hit_block_id) const;
	const WorldChunk *find_chunk(int32_t chunk_x, int32_t chunk_z) const;
	WorldChunk *find_chunk_mutable(int32_t chunk_x, int32_t chunk_z);
	void register_chunk_index(const WorldChunk &chunk);

	int32_t begin_world_revision(const terrain_generation_config &config,
		RegenerationMode mode);
	int32_t cancel_world_revision();
	WorldRevision world_revision() const;
	int32_t select_revision_chunk(int32_t chunk_x, int32_t chunk_z,
		bool selected);
	int32_t set_chunk_protected(int32_t chunk_x, int32_t chunk_z,
		bool protected_state);
	bool is_chunk_protected(int32_t chunk_x, int32_t chunk_z) const;
	ChunkRevisionState revision_state(int32_t chunk_x, int32_t chunk_z) const;
	int32_t build_revision_preview(int32_t preview_center_x,
		int32_t preview_center_z, int32_t radius,
		std::vector<RevisionPreviewEntry> &preview) const;
	int32_t start_revision_regeneration();
	int32_t regenerate_selected_chunks(int32_t *regenerated_count,
		int32_t *skipped_count);
	int32_t apply_revision_request(const RevisionRequest &request,
		RevisionRequestResult *result);
	int32_t save_revision_metadata(const char *file_path) const;
	int32_t load_revision_metadata(const char *file_path);

  private:
	void copy_seed(const char *seed_value);
	void clear_chunk_index();
	void rebuild_chunk_index();
	int32_t seed_first_chunk_and_stream();
};

# include "../../src/world/WorldChunkStreamer.hpp"
# include "../../src/world/WorldRevisionManager.hpp"

#endif
