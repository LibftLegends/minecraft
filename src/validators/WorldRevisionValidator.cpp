#include "WorldRevisionValidator.hpp"

int WorldRevisionValidator::validate() const
{
    World world;
    terrain_generation_config config;
    int32_t regenerated;
    int32_t skipped;
    int32_t error_code = world.initialize("revision-validator");
    if (error_code != FT_ERR_SUCCESS)
        return (1);
    if (world.find_chunk_mutable(0, 0) == nullptr)
    {
        world.destroy();
        return (1);
    }
    if (world.find_chunk_mutable(0, 0)->chunk.write_block(0, 0, 0,
            TERRAIN_GENERATOR_STONE_BLOCK) != FT_ERR_SUCCESS)
    {
        world.destroy();
        return (1);
    }
    terrain_default_generation_config(config);
    int32_t begin_result = world.begin_world_revision(config, World::REGEN_TERRAIN_RESHAPING);
    int32_t edit_select_result = world.select_revision_chunk(0, 0, true);
    int32_t protect_result = world.set_chunk_protected(4, 0, true);
    int32_t select_loaded_result = world.select_revision_chunk(1, 0, true);
    int32_t select_unloaded_result = world.select_revision_chunk(1000, 1000, true);
    int32_t select_far_result = world.select_revision_chunk(1000, 1000, true);
    World::ChunkRevisionState transition_state = world.revision_state(2, 0);
    World::ChunkRevisionState protected_state = world.revision_state(4, 0);
    std::vector<World::RevisionPreviewEntry> preview;
    int32_t preview_result = world.build_revision_preview(2, 0, 4, preview);
    if (begin_result != FT_ERR_SUCCESS || edit_select_result == FT_ERR_SUCCESS
        || protect_result != FT_ERR_SUCCESS || select_loaded_result != FT_ERR_SUCCESS
        || select_unloaded_result != FT_ERR_SUCCESS
        || select_far_result != FT_ERR_SUCCESS
        || preview_result != FT_ERR_SUCCESS || preview.empty()
        || transition_state != World::REVISION_TRANSITION
        || protected_state != World::REVISION_PROTECTED)
    {
        std::fprintf(stderr, "world-revision: setup failed begin=%d edit=%d protect=%d loaded=%d unloaded=%d preview=%d transition=%d protected=%d\n",
                     begin_result, edit_select_result, protect_result,
                     select_loaded_result, select_unloaded_result,
                     preview_result, transition_state, protected_state);
        world.destroy();
        return (1);
    }
    int32_t regenerate_result = world.regenerate_selected_chunks(&regenerated, &skipped);
    if (regenerate_result != FT_ERR_SUCCESS || regenerated < 1 || skipped < 1
        || world.world_revision().pending)
    {
        std::fprintf(stderr, "world-revision: regeneration failed result=%d regenerated=%d skipped=%d pending=%d\n",
                     regenerate_result, regenerated, skipped,
                     world.world_revision().pending ? 1 : 0);
        world.destroy();
        return (1);
    }
    const char *metadata_path = "world_revision_validator.bin";
    if (world.save_revision_metadata(metadata_path) != FT_ERR_SUCCESS)
    {
        world.destroy();
        return (1);
    }
    World restored_world;
    if (restored_world.initialize("revision-validator") != FT_ERR_SUCCESS
        || restored_world.load_revision_metadata(metadata_path) != FT_ERR_SUCCESS
        || !restored_world.is_chunk_protected(4, 0))
    {
        std::remove(metadata_path);
        world.destroy();
        restored_world.destroy();
        return (1);
    }
    std::remove(metadata_path);
    restored_world.destroy();

    World::RevisionRequest request;
    terrain_default_generation_config(request.config);
    request.mode = World::REGEN_DECORATION_REFRESH;
    request.stage_mask = TERRAIN_STAGE_DECORATION;
    request.selected_chunks.push_back({1, 0});
    World::RevisionRequestResult request_result;
    if (world.apply_revision_request(request, &request_result) != FT_ERR_SUCCESS
        || request_result.regenerated_count < 1
        || request_result.stage_mask != TERRAIN_STAGE_DECORATION)
    {
        world.destroy();
        return (1);
    }
    std::printf("world-revision: ok regenerated=%d skipped=%d\n", regenerated, skipped);
    world.destroy();
    return (0);
}
