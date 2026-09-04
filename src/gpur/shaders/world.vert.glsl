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
flat out uint v_block_id;
flat out uint v_face;
flat out uint v_packed_light;

const float SHADE[6] = float[6](0.72, 0.82, 0.72, 1.0, 0.58, 0.72);

void main()
{
    gl_Position = u_mvp * vec4(a_position + u_chunk_offset, 1.0);
    v_uv_tile = vec2(a_texcoord);
    v_shade = a_face < 6u ? SHADE[a_face] : 0.72;
    v_block_id = a_block_id;
    v_face = a_face;
    v_packed_light = a_packed_light;
}
