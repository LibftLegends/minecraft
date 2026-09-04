#version 330 core
in vec2 v_uv_tile;
in float v_shade;
flat in uint v_block_id;
flat in uint v_face;
flat in uint v_packed_light;

uniform sampler2D u_atlas;
uniform int u_atlas_loaded;
uniform vec4 u_tile_uvs[384];
uniform vec3 u_fallback_colors[64];
uniform int u_sky_darkening;

out vec4 frag_color;

const float LIGHT_BRIGHTNESS[16] = float[16](
    0.05, 0.08, 0.11, 0.15, 0.20, 0.27, 0.35, 0.44,
    0.54, 0.64, 0.73, 0.81, 0.87, 0.92, 0.97, 1.0);

void main()
{
    uint bid = min(v_block_id, 63u);
    uint fid = min(v_face, 5u);
    uint sky = v_packed_light & 15u;
    uint block = (v_packed_light >> 4u) & 15u;
    uint darkening = uint(max(u_sky_darkening, 0));
    sky = sky > darkening ? sky - darkening : 0u;
    float light = LIGHT_BRIGHTNESS[max(sky, block)];
    if (bid == 8u)
    {
        frag_color = vec4(vec3(0.1, 0.45, 0.85) * v_shade * light, 0.6);
        return;
    }
    if (u_atlas_loaded == 1)
    {
        vec4 region = u_tile_uvs[bid * 6u + fid];
        vec2 uv = region.xy + fract(v_uv_tile) * region.zw;
        vec3 color = texture(u_atlas, uv).rgb;
        frag_color = vec4(color * v_shade * light, 1.0);
    }
    else
    {
        vec3 base = u_fallback_colors[bid];
        frag_color = vec4(base * v_shade * light, 1.0);
    }
}
