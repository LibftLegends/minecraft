#include "../../src/world/World.hpp"

World::World() : chunk_streamer_storage_(new WorldChunkStreamer(*this)),
	revision_manager_storage_(new WorldRevisionManager(*this)),
	chunk_streamer(*chunk_streamer_storage_.get()),
	revision_manager(*revision_manager_storage_.get())
{
	this->chunk_count = WorldCoordinates::CHUNK_COUNT;
	this->loaded_chunk_count = 0;
	this->center_chunk_x = 0;
	this->center_chunk_z = 0;
	this->chunk_index_center_x = 0;
	this->chunk_index_center_z = 0;
	this->active_render_distance = WorldCoordinates::REQUIRED_VISIBLE_DISTANCE;
	this->seed[0] = '\0';
	terrain_default_generation_config(this->terrain_config);
	this->terrain_generation_started = false;
	this->current_tick = 0U;
	this->clear_chunk_index();
}

World::World(const World &other) : chunk_streamer_storage_(new WorldChunkStreamer(*this)),
	revision_manager_storage_(new WorldRevisionManager(*this)),
	chunk_streamer(*chunk_streamer_storage_.get()),
	revision_manager(*revision_manager_storage_.get())
{
	(void)other;
}

World::~World()
{
	this->destroy();
}

World &World::operator=(const World &other)
{
	(void)other;
	return (*this);
}

void World::copy_seed(const char *seed_value)
{
	if (seed_value == nullptr)
		seed_value = "";
	std::strncpy(this->seed, seed_value, sizeof(this->seed) - 1U);
	this->seed[sizeof(this->seed) - 1U] = '\0';
}

void World::clear_chunk_index()
{
	int32_t	index;

	index = 0;
	while (index < WorldCoordinates::CHUNK_COUNT)
	{
		this->chunk_index[index] = nullptr;
		index = index + 1;
	}
	this->chunk_index_valid = false;
}

void World::rebuild_chunk_index()
{
	int32_t	index;
	int32_t	slot_x;
	int32_t	slot_z;
	int32_t	grid_x;
	int32_t	grid_z;
	int32_t	grid_width;

	this->clear_chunk_index();
	grid_width = WorldCoordinates::CACHE_CHUNK_RADIUS * 2 + 1;
	index = 0;
	while (index < this->chunk_count)
	{
		if (this->chunks[index].initialized == true)
		{
			slot_x = this->chunks[index].chunk_x - this->center_chunk_x;
			slot_z = this->chunks[index].chunk_z - this->center_chunk_z;
			if (slot_x >= -WorldCoordinates::CACHE_CHUNK_RADIUS
				&& slot_x <= WorldCoordinates::CACHE_CHUNK_RADIUS && slot_z >=
				-WorldCoordinates::CACHE_CHUNK_RADIUS
				&& slot_z <= WorldCoordinates::CACHE_CHUNK_RADIUS)
			{
				grid_x = slot_x + WorldCoordinates::CACHE_CHUNK_RADIUS;
				grid_z = slot_z + WorldCoordinates::CACHE_CHUNK_RADIUS;
				this->chunk_index[(grid_z * grid_width)
					+ grid_x] = &this->chunks[index];
			}
		}
		index = index + 1;
	}
	this->chunk_index_center_x = this->center_chunk_x;
	this->chunk_index_center_z = this->center_chunk_z;
	this->chunk_index_valid = true;
}

int32_t World::initialize(const char *seed_value)
{
	return (this->initialize(seed_value, nullptr));
}

int32_t World::seed_first_chunk_and_stream()
{
	int32_t	index;
	int32_t	error_code;
	int32_t	initial_radius;
	int32_t	initial_generated;

	index = 0;
	while (index < this->chunk_count)
	{
		this->chunks[index].reset_coordinates();
		this->chunks[index].initialized = false;
		index = index + 1;
	}
	error_code = WorldChunkLoader::initialize_chunk(&this->chunks[0], 0, 0,
			this->seed, this->chunks, this->chunk_count,
			this->terrain_context.config());
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	this->loaded_chunk_count = 1;
	this->rebuild_chunk_index();
	initial_radius = WorldCoordinates::render_distance_to_chunk_radius(this->active_render_distance);
	initial_generated = 0;
	return (this->chunk_streamer.seed_initial_stream(initial_radius, 64,
			&initial_generated));
}

int32_t World::initialize(const char *seed_value,
	const char *terrain_config_file_path)
{
	int32_t	error_code;

	this->destroy();
	this->chunk_count = WorldCoordinates::CHUNK_COUNT;
	this->loaded_chunk_count = 0;
	this->center_chunk_x = 0;
	this->center_chunk_z = 0;
	this->active_render_distance = WorldCoordinates::REQUIRED_VISIBLE_DISTANCE;
	this->clear_chunk_index();
	this->copy_seed(seed_value);
	if (terrain_config_file_path != nullptr)
	{
		error_code = this->load_terrain_config(terrain_config_file_path);
		if (error_code != FT_ERR_SUCCESS)
			return (error_code);
	}
	error_code = terrain_generation_context_initialize(this->terrain_context,
			this->terrain_config);
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	error_code = this->chunk_streamer.initialize_pipeline();
	if (error_code != FT_ERR_SUCCESS)
		return (error_code);
	this->terrain_generation_started = true;
	error_code = this->seed_first_chunk_and_stream();
	if (error_code != FT_ERR_SUCCESS)
		this->destroy();
	return (error_code);
}

void World::set_terrain_config(const terrain_generation_config &config)
{
	if (this->terrain_generation_started)
		return ;
	this->terrain_config.initialize(config);
}

const terrain_generation_config &World::terrain_generation_settings() const
{
	return (this->terrain_config);
}

int32_t World::load_terrain_config(const char *file_path)
{
	if (file_path == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	if (this->terrain_generation_started)
		return (FT_ERR_INVALID_OPERATION);
	return (terrain_generation_config_load_file(file_path,
			this->terrain_config));
}

int32_t World::save_terrain_config(const char *file_path) const
{
	const terrain_generation_config *config_to_save;

	if (file_path == nullptr)
		return (FT_ERR_INVALID_ARGUMENT);
	config_to_save = &this->terrain_config;
	if (this->terrain_generation_started)
		config_to_save = &this->terrain_context.config();
	return (terrain_generation_config_save_file(file_path, *config_to_save));
}

void World::destroy()
{
	int32_t	index;

	this->chunk_streamer.reset();
	index = 0;
	while (index < this->chunk_count)
	{
		this->chunks[index].destroy();
		index = index + 1;
	}
	this->loaded_chunk_count = 0;
	this->clear_chunk_index();
	this->revision_manager.reset();
	(void)this->terrain_context.destroy();
	this->terrain_generation_started = false;
}
