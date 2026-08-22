#include "../../src/debug/RenderDebug.hpp"

RenderDebug::RenderDebug()
    : fps(0.0), frame_ms(0.0), visible_chunks(0), loaded_chunks(0), render_distance(0),
      selected_block_id(0U), ram_mb(0U), vram_approx_mb(0U), camera_x(0.0), camera_y(0.0),
      camera_z(0.0), camera_speed(0.0), boost_speed(0.0), revision_preview_visible(false),
      revision_pending(false), revision_map_radius(0),
      revision_protected_count(0U), revision_selected_count(0U),
      revision_transition_count(0U), revision_unchanged_count(0U)
{
    seed[0] = '\0';
    biome_name[0] = '\0';
    std::fill(revision_map, revision_map + sizeof(revision_map), 0U);
}

RenderDebug::RenderDebug(const RenderDebug &other)
    : fps(0.0), frame_ms(0.0), visible_chunks(0), loaded_chunks(0), render_distance(0),
      selected_block_id(0U), ram_mb(0U), vram_approx_mb(0U), camera_x(0.0), camera_y(0.0),
      camera_z(0.0), camera_speed(0.0), boost_speed(0.0), revision_preview_visible(false),
      revision_pending(false), revision_map_radius(0),
      revision_protected_count(0U), revision_selected_count(0U),
      revision_transition_count(0U), revision_unchanged_count(0U)
{
    seed[0] = '\0';
    biome_name[0] = '\0';
    *this = other;
}

RenderDebug::~RenderDebug()
{
}

RenderDebug &RenderDebug::operator=(const RenderDebug &other)
{
    if (this != &other)
    {
        fps = other.fps;
        frame_ms = other.frame_ms;
        visible_chunks = other.visible_chunks;
        loaded_chunks = other.loaded_chunks;
        render_distance = other.render_distance;
        selected_block_id = other.selected_block_id;
        ram_mb = other.ram_mb;
        vram_approx_mb = other.vram_approx_mb;
        camera_x = other.camera_x;
        camera_y = other.camera_y;
        camera_z = other.camera_z;
        camera_speed = other.camera_speed;
        boost_speed = other.boost_speed;
        std::memcpy(seed, other.seed, sizeof(seed));
        std::memcpy(biome_name, other.biome_name, sizeof(biome_name));
        revision_preview_visible = other.revision_preview_visible;
        revision_pending = other.revision_pending;
        revision_map_radius = other.revision_map_radius;
        std::memcpy(revision_map, other.revision_map, sizeof(revision_map));
        revision_protected_count = other.revision_protected_count;
        revision_selected_count = other.revision_selected_count;
        revision_transition_count = other.revision_transition_count;
        revision_unchanged_count = other.revision_unchanged_count;
    }
    return *this;
}

void RenderDebug::clear()
{
    fps = 0.0;
    frame_ms = 0.0;
    visible_chunks = 0;
    loaded_chunks = 0;
    render_distance = 0;
    selected_block_id = 0U;
    ram_mb = 0U;
    vram_approx_mb = 0U;
    camera_x = 0.0;
    camera_y = 0.0;
    camera_z = 0.0;
    camera_speed = 0.0;
    boost_speed = 0.0;
    seed[0] = '\0';
    biome_name[0] = '\0';
    revision_preview_visible = false;
    revision_pending = false;
    revision_map_radius = 0;
    std::fill(revision_map, revision_map + sizeof(revision_map), 0U);
    revision_protected_count = 0U;
    revision_selected_count = 0U;
    revision_transition_count = 0U;
    revision_unchanged_count = 0U;
}
