#version 330 core
flat in vec4 v_bounds;
flat in vec3 v_color;
in vec2 v_pixel;
out vec4 frag_color;

void main()
{
    vec2 pixel = floor(v_pixel);
    bool border = any(lessThanEqual(pixel, v_bounds.xy))
        || any(greaterThanEqual(pixel, v_bounds.zw - 1.0));
    vec3 color = border ? vec3(20.0 / 255.0) : v_color;
    frag_color = vec4(color, max(color.r, max(color.g, color.b)));
}
