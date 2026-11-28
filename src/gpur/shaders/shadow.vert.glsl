#version 330 core
layout(location = 0) in vec2 a_position;

uniform mat4 u_mvp;
uniform vec2 u_shadow_center;
uniform float u_shadow_radius;
uniform float u_shadow_y;

out vec2 v_uv;

void main()
{
    v_uv = a_position * 0.5 + 0.5;
    vec2 world_xz = u_shadow_center + a_position * u_shadow_radius;
    gl_Position = u_mvp * vec4(world_xz.x, u_shadow_y, world_xz.y, 1.0);
}
