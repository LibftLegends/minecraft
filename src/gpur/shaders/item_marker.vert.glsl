#version 330 core
uniform vec4 u_marker_bounds[48];
uniform vec3 u_marker_colors[48];
uniform vec4 u_canvas_viewport;

flat out vec4 v_bounds;
flat out vec3 v_color;
out vec2 v_pixel;

const vec2 corners[6] = vec2[6](vec2(0, 0), vec2(0, 1), vec2(1, 0),
    vec2(1, 0), vec2(0, 1), vec2(1, 1));

void main()
{
    int marker = gl_VertexID / 6;
    v_bounds = u_marker_bounds[marker];
    v_color = u_marker_colors[marker];
    v_pixel = clamp(mix(v_bounds.xy, v_bounds.zw, corners[gl_VertexID % 6]),
        vec2(0), u_canvas_viewport.xy);
    vec2 ndc = v_pixel * (2.0 / u_canvas_viewport.zw) - 1.0;
    gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);
}
