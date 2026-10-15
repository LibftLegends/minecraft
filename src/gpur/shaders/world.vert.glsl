#version 330 core
layout(location = 0) in vec3 a_position;
layout(location = 1) in uvec2 a_texcoord;
layout(location = 2) in uint a_block_id;
layout(location = 3) in uint a_face;
layout(location = 4) in uint a_packed_light;

uniform mat4 u_mvp;
uniform vec3 u_chunk_offset;

out vec2 v_uv_tile;
out float v_shade;
out float v_light;
flat out uint v_block_id;
flat out uint v_face;

const float SHADE[6] = float[6](0.72, 0.82, 0.72, 1.0, 0.58, 0.72);
const float LIGHT_BRIGHTNESS[16] = float[16](
    0.05, 0.08, 0.11, 0.15, 0.20, 0.27, 0.35, 0.44,
    0.54, 0.64, 0.73, 0.81, 0.87, 0.92, 0.97, 1.0);

uniform int u_sky_darkening;

void main()
{
    gl_Position = u_mvp * vec4(a_position + u_chunk_offset, 1.0);
    v_uv_tile = vec2(a_texcoord);
    v_shade = a_face < 6u ? SHADE[a_face] : 0.72;
    v_block_id = a_block_id;
    v_face = a_face;
    uint sky = a_packed_light & 15u;
    uint block = (a_packed_light >> 4u) & 15u;
    uint darkening = uint(max(u_sky_darkening, 0));
    sky = sky > darkening ? sky - darkening : 0u;
    v_light = LIGHT_BRIGHTNESS[max(sky, block)];
}
