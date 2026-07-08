#version 330 core
in vec2 v_uv;
in float v_shade;
in vec3 v_world;
out vec4 o_color;
uniform sampler2D u_tex;
uniform vec2 u_light;        // ball position, GL space
uniform float u_ambient;     // base wall brightness
uniform float u_spot;        // spotlight strength added near the ball
uniform float u_spot_radius; // px
uniform vec2 u_vp_offset;    // viewport origin in window px
uniform vec2 u_vp_size;
void main() {
    vec4 c = texture(u_tex, v_uv);
    float d = distance(v_world.xy, u_light);
    float light = u_ambient + u_spot * exp(-(d * d) / (u_spot_radius * u_spot_radius));
    vec2 ndc = ((gl_FragCoord.xy - u_vp_offset) / u_vp_size) * 2.0 - 1.0;
    float vig = 1.0 - 0.35 * dot(ndc, ndc);
    o_color = vec4(c.rgb * light * vig * v_shade, 1.0);
}
