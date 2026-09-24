// Radial wipe opening from the center.
#ifdef GL_ES
precision mediump float;
#endif
varying vec2 v_texCoord;
uniform sampler2D from;
uniform sampler2D to;
uniform float progress;

void main()
{
    vec2 p = v_texCoord - vec2(0.5, 0.5);
    float d = length(p);
    float r = progress * 0.70710678; // covers the far corners at progress = 1
    float blend = smoothstep(r - 0.01, r + 0.01, d);
    vec4 a = texture2D(from, v_texCoord);
    vec4 b = texture2D(to, v_texCoord);
    gl_FragColor = mix(b, a, clamp(blend, 0.0, 1.0));
}