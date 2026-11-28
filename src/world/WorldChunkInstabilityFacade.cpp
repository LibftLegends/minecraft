#include "../../src/world/World.hpp"

int32_t World::configure_chunk_instability(
	const WorldChunkInstabilityPolicy &policy) noexcept
{
	return (this->chunk_instability_store_.configure(policy));
}

bool World::is_chunk_instability_configured() const noexcept
{
	return (this->chunk_instability_store_.is_configured());
}

int32_t World::serialize_chunk_instability(
	std::vector<uint8_t> &bytes_out) const noexcept
{
	return (this->chunk_instability_store_.serialize(bytes_out));
}

int32_t World::deserialize_chunk_instability(const uint8_t *bytes,
	std::size_t byte_count) noexcept
{
	return (this->chunk_instability_store_.deserialize(bytes, byte_count));
}

int32_t World::get_chunk_instability(int32_t chunk_x, int32_t chunk_z,
	WorldChunkInstabilityRecord *record_out) const noexcept
{
	return (this->chunk_instability_store_.get(chunk_x, chunk_z, record_out));
}

int32_t World::get_chunk_instability_public_view(int32_t chunk_x,
	int32_t chunk_z, WorldChunkInstabilityPublicView *view_out) const noexcept
{
	return (this->chunk_instability_store_.get_public_view(chunk_x, chunk_z,
		view_out));
}

int32_t World::ensure_chunk_instability_record(int32_t chunk_x,
	int32_t chunk_z) noexcept
{
	return (this->chunk_instability_store_.ensure_record(chunk_x, chunk_z));
}

int32_t World::adjust_chunk_instability(uint64_t expected_record_revision,
	int32_t chunk_x, int32_t chunk_z, int32_t delta,
	WorldChunkInstabilityRecord *record_out) noexcept
{
	return (this->chunk_instability_store_.adjust(expected_record_revision,
		chunk_x, chunk_z, delta, record_out));
}

int32_t World::add_chunk_permanent_modifier(uint64_t expected_record_revision,
	int32_t chunk_x, int32_t chunk_z, uint32_t modifier_id,
	uint32_t modifier_version,
	WorldChunkInstabilityRecord *record_out) noexcept
{
	return (this->chunk_instability_store_.add_permanent_modifier(
		expected_record_revision, chunk_x, chunk_z, modifier_id,
		modifier_version, record_out));
}

int32_t World::add_chunk_feature_outcome(uint64_t expected_record_revision,
	int32_t chunk_x, int32_t chunk_z, uint32_t outcome_id,
	uint32_t outcome_version,
	WorldChunkInstabilityRecord *record_out) noexcept
{
	return (this->chunk_instability_store_.add_feature_outcome(
		expected_record_revision, chunk_x, chunk_z, outcome_id,
		outcome_version, record_out));
}

int32_t World::record_chunk_generation(uint64_t expected_record_revision,
	int32_t chunk_x, int32_t chunk_z, uint64_t generation_revision,
	uint64_t rules_hash, WorldChunkInstabilityRecord *record_out,
	const std::vector<WorldChunkInstabilityRecord::VersionedId>
		*feature_outcomes) noexcept
{
	return (this->chunk_instability_store_.record_generation(
		expected_record_revision, chunk_x, chunk_z, generation_revision,
		rules_hash, record_out, feature_outcomes));
}

int32_t World::restore_chunk_instability_record(
	uint64_t expected_current_revision,
	const WorldChunkInstabilityRecord &previous_record) noexcept
{
	return (this->chunk_instability_store_.restore_record(
		expected_current_revision, previous_record));
}

int32_t World::save_chunk_instability(const char *file_path) const noexcept
{
	return (this->chunk_instability_store_.save_file(file_path));
}

int32_t World::load_chunk_instability(const char *file_path) noexcept
{
	return (this->chunk_instability_store_.load_file(file_path));
}
