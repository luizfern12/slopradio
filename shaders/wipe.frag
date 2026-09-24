// Hard-edge wipe from left to right.
#ifdef GL_ES
precision mediump float;
#endif
varying vec2 v_texCoord;
uniform sampler2D from;
uniform sampler2D to;
uniform float progress;

void main()
{
    float edge = 1.0 - progress;
    vec2 uv = vec2(clamp(v_texCoord.x, 0.0, 1.0), v_texCoord.y);

    // smooth the wipe line a little so it doesn't alias
    float blend = smoothstep(edge - 0.01, edge + 0.01, uv.x);
    vec4 a = texture2D(from, uv);
    vec4 b = texture2D(to, uv);
    gl_FragColor = mix(a, b, clamp(blend, 0.0, 1.0));
}