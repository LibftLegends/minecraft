#version 330 core
in vec2 v_uv_tile;
in float v_shade;
in float v_light;
flat in uint v_block_id;
flat in uint v_face;

uniform sampler2D u_atlas;
uniform int u_atlas_loaded;
uniform vec4 u_tile_uvs[384];
uniform vec3 u_fallback_colors[64];
uniform int u_sky_darkening;

out vec4 frag_color;

void main()
{
    uint bid = min(v_block_id, 63u);
    uint fid = min(v_face, 5u);
    float light = v_light;
    if (bid == 8u)
    {
        frag_color = vec4(vec3(0.1, 0.45, 0.85) * v_shade * light, 0.6);
        return;
    }
    if (u_atlas_loaded == 1)
    {
        vec4 region = u_tile_uvs[bid * 6u + fid];
        // Mesh V rises from the bottom of a face; atlas rows start at the top.
        vec2 tile_uv = vec2(fract(v_uv_tile.x), 1.0 - fract(v_uv_tile.y));
        vec2 uv = region.xy + tile_uv * region.zw;
        // Derivatives of fract() jump at each block boundary and select an
        // excessively blurry mip. Derive the footprint before wrapping.
        vec2 uv_scale = vec2(region.z, -region.w);
        vec3 color = textureGrad(u_atlas, uv,
            dFdx(v_uv_tile) * uv_scale, dFdy(v_uv_tile) * uv_scale).rgb;
        frag_color = vec4(color * v_shade * light, 1.0);
    }
    else
    {
        vec3 base = u_fallback_colors[bid];
        frag_color = vec4(base * v_shade * light, 1.0);
    }
}
