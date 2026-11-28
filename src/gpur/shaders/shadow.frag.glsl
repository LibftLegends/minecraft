#version 330 core
in vec2 v_uv;
uniform float u_shadow_alpha;
out vec4 frag_color;

void main()
{
    float distance_from_center = length(v_uv * 2.0 - 1.0);
    float edge = clamp(1.0 - distance_from_center, 0.0, 1.0);
    float alpha = u_shadow_alpha * edge * edge;
    if (alpha <= 0.001)
        discard;
    frag_color = vec4(0.015, 0.012, 0.008, alpha);
}
