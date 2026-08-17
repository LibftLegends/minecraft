#version 330 core
in vec2 v_uv_tile;
in float v_shade;
flat in uint v_block_id;
flat in uint v_face;

uniform sampler2D u_atlas;
uniform int u_atlas_loaded;
uniform vec4 u_tile_uvs[384];
uniform vec3 u_fallback_colors[64];

out vec4 frag_color;

void main()
{
    uint bid = min(v_block_id, 63u);
    uint fid = min(v_face, 5u);
    if (bid == 8u)
    {
        frag_color = vec4(vec3(0.1, 0.45, 0.85) * v_shade, 0.6);
        return;
    }
    if (u_atlas_loaded == 1)
    {
        vec4 region = u_tile_uvs[bid * 6u + fid];
        vec2 uv = region.xy + fract(v_uv_tile) * region.zw;
        vec3 color = texture(u_atlas, uv).rgb;
        frag_color = vec4(color * v_shade, 1.0);
    }
    else
    {
        vec3 base = u_fallback_colors[bid];
        frag_color = vec4(base * v_shade, 1.0);
    }
}
